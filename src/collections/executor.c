//
// Copyright (c) 2026 ZettaScale Technology
//
// This program and the accompanying materials are made available under the
// terms of the Eclipse Public License 2.0 which is available at
// http://www.eclipse.org/legal/epl-2.0, or the Apache License, Version 2.0
// which is available at https://www.apache.org/licenses/LICENSE-2.0.
//
// SPDX-License-Identifier: EPL-2.0 OR Apache-2.0
//
// Contributors:
//   ZettaScale Zenoh Team, <zenoh@zettascale.tech>
//

#include "zenoh-pico/collections/executor.h"

void _z_fut_handle_clear(_z_fut_handle_t *handle) {
    if (handle == NULL) {
        return;
    }
    _z_atomic_size_store(&handle->_task_status, (size_t)_Z_FUT_STATUS_CANCELLED, _z_memory_order_release);
}

void _z_fut_handle_cancel(_z_fut_handle_t *handle) {
    size_t expected = (size_t)_Z_FUT_STATUS_PENDING;
    while (!_z_atomic_size_compare_exchange_strong(&handle->_task_status, &expected, (size_t)_Z_FUT_STATUS_CANCELLED,
                                                   _z_memory_order_release, _z_memory_order_relaxed)) {
        if (expected == (size_t)_Z_FUT_STATUS_READY || expected == (size_t)_Z_FUT_STATUS_CANCELLED) {
            // The task is already ready or cancelled, so there is nothing to do.
            return;
        }
    }
}

_z_fut_status_t _z_fut_handle_status(const _z_fut_handle_t *handle) {
    return (_z_fut_status_t)_z_atomic_size_load((_z_atomic_size_t *)&handle->_task_status, _z_memory_order_acquire);
}

_z_fut_handle_rc_t _z_fut_get_handle(_z_fut_t *fut) {
    if (_Z_RC_IS_NULL(&fut->_handle)) {
        _z_fut_handle_t *h = z_malloc(sizeof(_z_fut_handle_t));
        if (h == NULL) {
            return _z_fut_handle_rc_null();
        }
        fut->_handle = _z_fut_handle_rc_new(h);
        if (!_Z_RC_IS_NULL(&fut->_handle)) {
            _z_atomic_size_init(&fut->_handle._val->_task_status, (size_t)_Z_FUT_STATUS_PENDING);
        } else {
            z_free(h);
            return _z_fut_handle_rc_null();
        }
    }
    return _z_fut_handle_rc_clone(&fut->_handle);
}

bool _z_executor_spawn(_z_executor_t *executor, _z_fut_t *fut) {
    if (!_z_fut_deque_push_back(&executor->_tasks, fut)) {
        _z_fut_destroy(fut);
        return false;
    }
    return true;
}

_z_executor_spin_result_t _z_executor_get_next_fut(_z_executor_t *executor, _z_fut_t *task) {
    _z_executor_spin_result_t result;
    result.status = _Z_EXECUTOR_SPIN_RESULT_NO_TASKS;
    _z_timed_fut_t *timed_task_ptr = _z_timed_fut_pqueue_peek(&executor->_timed_tasks);
    if (timed_task_ptr != NULL) {
        z_clock_t now = z_clock_now();
        z_clock_t wake_up_time = executor->_epoch;
        z_clock_advance_ms(&wake_up_time, timed_task_ptr->_wake_up_time_ms);
        if (zp_clock_elapsed_ms_since(&now, &wake_up_time) > 0) {
            // The timed task is ready to execute
            _z_timed_fut_t t;
            _z_timed_fut_pqueue_pop(&executor->_timed_tasks, &t);
            if (_z_fut_deque_pop_front(&executor->_tasks, task)) {
                // We have a non-timed task to execute, we should re-enqueue the ready timed task as non-timed one and
                // execute the non-timed task first.
                _z_fut_deque_push_back(&executor->_tasks, &t._fut);
            } else {
                // No non-timed task, execute the ready timed task directly.
                _z_fut_move(task, &t._fut);
            }
            result.status = _Z_EXECUTOR_SPIN_RESULT_EXECUTED_TASK;
        } else if (_z_fut_deque_pop_front(&executor->_tasks, task)) {
            // We have a non-timed task to execute
            result.status = _Z_EXECUTOR_SPIN_RESULT_EXECUTED_TASK;
        } else {
            // No non-timed task, we should wait for the timed task to be ready.
            result.status = _Z_EXECUTOR_SPIN_RESULT_SHOULD_WAIT;
            result.next_wake_up_time = wake_up_time;
        }
    } else if (_z_fut_deque_pop_front(&executor->_tasks, task)) {
        // We have a non-timed task to execute
        result.status = _Z_EXECUTOR_SPIN_RESULT_EXECUTED_TASK;
    }
    return result;
}

_z_executor_spin_result_t _z_executor_spin(_z_executor_t *executor) {
    _z_fut_t fut;
    _z_executor_spin_result_t result;
    while (true) {  // Loop until we find non-null task to execute
        result = _z_executor_get_next_fut(executor, &fut);
        if (result.status == _Z_EXECUTOR_SPIN_RESULT_NO_TASKS ||
            result.status == _Z_EXECUTOR_SPIN_RESULT_SHOULD_WAIT) {  // No tasks to execute
            return result;
        }
        if (!_Z_RC_IS_NULL(&fut._handle)) {
            size_t expected = (size_t)_Z_FUT_STATUS_PENDING;
            while (!_z_atomic_size_compare_exchange_strong(&fut._handle._val->_task_status, &expected,
                                                           (size_t)_Z_FUT_STATUS_EXECUTING, _z_memory_order_acquire,
                                                           _z_memory_order_relaxed)) {
                if (expected == (size_t)_Z_FUT_STATUS_CANCELLED) {
                    // The task is cancelled, we can skip it.
                    _z_atomic_thread_fence(_z_memory_order_acquire);
                    _z_fut_destroy(&fut);
                    break;
                }
            }
            if (expected == (size_t)_Z_FUT_STATUS_CANCELLED) {
                continue;  // Skip to the next task.
            }
        }
        if (fut._fut_fn == NULL) {  // idle task, just skip it and check the next task.
            _z_fut_destroy(&fut);
            continue;
        }
        break;
    }
    _z_fut_fn_result_t fn_result = fut._fut_fn(fut._fut_arg, executor);

    if (!fn_result._ready) {
        // Re-enqueue the task
        if (!_Z_RC_IS_NULL(&fut._handle)) {
            _z_atomic_size_store(&fut._handle._val->_task_status, (size_t)_Z_FUT_STATUS_PENDING,
                                 _z_memory_order_release);
        }
        if (fn_result._has_wake_up_time) {
            _z_timed_fut_t timed_fut;
            _z_fut_move(&timed_fut._fut, &fut);
            timed_fut._wake_up_time_ms = zp_clock_elapsed_ms_since(&fn_result._wake_up_time, &executor->_epoch);
            if (!_z_timed_fut_pqueue_push(&executor->_timed_tasks, &timed_fut)) {
                // Failed to re-enqueue the task, we should destroy it to avoid memory leak.
                if (!_Z_RC_IS_NULL(&timed_fut._fut._handle)) {
                    _z_atomic_size_store(&timed_fut._fut._handle._val->_task_status, (size_t)_Z_FUT_STATUS_CANCELLED,
                                         _z_memory_order_release);
                }
                _z_timed_fut_destroy(&timed_fut);
                result.status = _Z_EXECUTOR_SPIN_RESULT_FAILED;
            }
        } else {
            if (!_z_fut_deque_push_back(&executor->_tasks, &fut)) {
                // Failed to re-enqueue the task, we should destroy it to avoid memory leak.
                if (!_Z_RC_IS_NULL(&fut._handle)) {
                    _z_atomic_size_store(&fut._handle._val->_task_status, (size_t)_Z_FUT_STATUS_CANCELLED,
                                         _z_memory_order_release);
                }
                _z_fut_destroy(&fut);
                result.status = _Z_EXECUTOR_SPIN_RESULT_FAILED;
            }
        }
    } else {
        if (!_Z_RC_IS_NULL(&fut._handle)) {
            _z_atomic_size_store(&fut._handle._val->_task_status, (size_t)_Z_FUT_STATUS_READY, _z_memory_order_release);
        }
        _z_fut_destroy(&fut);
    }
    return result;
}
