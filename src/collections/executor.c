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

void _z_executor_task_handle_clear(_z_executor_task_handle_t *handle) {
    if (handle == NULL) {
        return;
    }
    handle->result = NULL;
    _z_atomic_size_store(&handle->_task_status, (size_t)_Z_EXECUTOR_TASK_STATUS_CANCELLED, _z_memory_order_release);
}

void _z_executor_task_handle_cancel(const _z_executor_task_handle_rc_t *handle) {
    if (_Z_RC_IS_NULL(handle)) {
        return;
    }
    size_t expected = (size_t)_Z_EXECUTOR_TASK_STATUS_PENDING;
    while (!_z_atomic_size_compare_exchange_strong(&handle->_val->_task_status, &expected,
                                                   (size_t)_Z_EXECUTOR_TASK_STATUS_CANCELLED, _z_memory_order_release,
                                                   _z_memory_order_relaxed)) {
        if (expected == (size_t)_Z_EXECUTOR_TASK_STATUS_READY ||
            expected == (size_t)_Z_EXECUTOR_TASK_STATUS_CANCELLED) {
            // The task is already ready or cancelled, so there is nothing to do.
            return;
        }
    }
}

_z_executor_task_status_t _z_executor_task_status(const _z_executor_task_handle_rc_t *handle) {
    if (_Z_RC_IS_NULL(handle)) {
        return _Z_EXECUTOR_TASK_STATUS_CANCELLED;
    }
    return (_z_executor_task_status_t)_z_atomic_size_load((_z_atomic_size_t *)&handle->_val->_task_status,
                                                          _z_memory_order_acquire);
}

_z_executor_task_handle_rc_t _z_executor_spawn(_z_executor_t *executor, void *task_arg, _z_executor_task_fn_t task_fn,
                                               _z_executor_task_destroy_fn_t destroy_fn, void *result) {
    _z_executor_task_t task;
    task._task_arg = task_arg;
    task._task_fn = task_fn;
    task._destroy_fn = destroy_fn;
    task._handle = _z_executor_task_handle_rc_null();
    _z_executor_task_handle_t *h = z_malloc(sizeof(_z_executor_task_handle_t));
    if (h == NULL) {
        _z_executor_task_destroy(&task);
        return _z_executor_task_handle_rc_null();
    }
    _z_executor_task_handle_rc_t handle = _z_executor_task_handle_rc_new(h);
    if (handle._val != NULL) {
        handle._val->result = result;
        _z_atomic_size_init(&handle._val->_task_status, (size_t)_Z_EXECUTOR_TASK_STATUS_PENDING);
        task._handle = _z_executor_task_handle_rc_clone(&handle);
        if (!_z_executor_task_deque_push_back(&executor->_tasks, &task)) {
            _z_executor_task_destroy(&task);
            _z_executor_task_handle_rc_drop(&handle);
        }
    } else {
        _z_executor_task_destroy(&task);
        z_free(h);
    }
    return handle;
}

bool _z_executor_spawn_and_forget(_z_executor_t *executor, void *task_arg, _z_executor_task_fn_t task_fn,
                                  _z_executor_task_destroy_fn_t destroy_fn) {
    _z_executor_task_t task;
    task._task_arg = task_arg;
    task._task_fn = task_fn;
    task._destroy_fn = destroy_fn;
    task._handle = _z_executor_task_handle_rc_null();
    if (!_z_executor_task_deque_push_back(&executor->_tasks, &task)) {
        _z_executor_task_destroy(&task);
        return false;
    }
    return true;
}

bool _z_executor_spin(_z_executor_t *executor) {
    _z_executor_task_t task;
    if (!_z_executor_task_deque_pop_front(&executor->_tasks, &task)) {
        return false;
    }
    size_t expected = (size_t)_Z_EXECUTOR_TASK_STATUS_PENDING;
    if (!_Z_RC_IS_NULL(&task._handle)) {
        while (!_z_atomic_size_compare_exchange_strong(&task._handle._val->_task_status, &expected,
                                                       (size_t)_Z_EXECUTOR_TASK_STATUS_EXECUTING,
                                                       _z_memory_order_acquire, _z_memory_order_relaxed)) {
            if (expected == (size_t)_Z_EXECUTOR_TASK_STATUS_CANCELLED) {
                // The task is cancelled, we can skip it.
                _z_executor_task_destroy(&task);
                return _z_executor_spin(executor);
            }
        }
    }

    if (!task._task_fn(task._task_arg, _Z_RC_IS_NULL(&task._handle) ? NULL : task._handle._val->result, executor)) {
        // Re-enqueue the task
        if (!_Z_RC_IS_NULL(&task._handle)) {
            _z_atomic_size_store(&task._handle._val->_task_status, (size_t)_Z_EXECUTOR_TASK_STATUS_PENDING,
                                 _z_memory_order_release);
        }
        _z_executor_task_deque_push_back(&executor->_tasks, &task);
        return true;
    } else {
        if (!_Z_RC_IS_NULL(&task._handle)) {
            _z_atomic_size_store(&task._handle._val->_task_status, (size_t)_Z_EXECUTOR_TASK_STATUS_READY,
                                 _z_memory_order_release);
        }
        _z_executor_task_destroy(&task);
        return _z_executor_task_deque_size(&executor->_tasks) > 0;
    }
}

_z_executor_task_handle_rc_t _z_timer_executor_spawn(_z_timer_executor_t *executor, void *task_arg,
                                                     _z_timer_executor_task_fn_t task_fn,
                                                     _z_executor_task_destroy_fn_t destroy_fn, void *result) {
    _z_timer_executor_task_t task;
    task._task_arg = task_arg;
    task._task_fn = task_fn;
    task._destroy_fn = destroy_fn;
    task._wake_up_time_ms = z_clock_elapsed_ms(&executor->_epoch);
    task._handle = _z_executor_task_handle_rc_null();
    _z_executor_task_handle_t *h = z_malloc(sizeof(_z_executor_task_handle_t));
    if (h == NULL) {
        _z_timer_executor_task_destroy(&task);
        return _z_executor_task_handle_rc_null();
    }
    _z_executor_task_handle_rc_t handle = _z_executor_task_handle_rc_new(h);
    if (handle._val != NULL) {
        handle._val->result = result;
        _z_atomic_size_init(&handle._val->_task_status, (size_t)_Z_EXECUTOR_TASK_STATUS_PENDING);
        task._handle = _z_executor_task_handle_rc_clone(&handle);
        if (!_z_timer_executor_task_pqueue_push(&executor->_tasks, &task)) {
            _z_timer_executor_task_destroy(&task);
            _z_executor_task_handle_rc_drop(&handle);
        }
    } else {
        _z_timer_executor_task_destroy(&task);
        z_free(h);
    }
    return handle;
}

bool _z_timer_executor_spawn_and_forget(_z_timer_executor_t *executor, void *task_arg,
                                        _z_timer_executor_task_fn_t task_fn, _z_executor_task_destroy_fn_t destroy_fn) {
    _z_timer_executor_task_t task;
    task._task_arg = task_arg;
    task._task_fn = task_fn;
    task._destroy_fn = destroy_fn;
    task._wake_up_time_ms = z_clock_elapsed_ms(&executor->_epoch);
    task._handle = _z_executor_task_handle_rc_null();
    if (!_z_timer_executor_task_pqueue_push(&executor->_tasks, &task)) {
        _z_timer_executor_task_destroy(&task);
        return false;
    }
    return true;
}

_z_timer_executor_spin_result_t _z_timer_executor_next_task_status_inner(_z_timer_executor_t *executor) {
    _z_timer_executor_spin_result_t result = {0};
    _z_timer_executor_task_t *task_ptr = _z_timer_executor_task_pqueue_peek(&executor->_tasks);
    result._wake_up_time = executor->_epoch;
    if (task_ptr == NULL) {
        return result;
    }
    result.has_pending_tasks = true;
    z_clock_advance_ms(&result._wake_up_time, task_ptr->_wake_up_time_ms);
    return result;
}

_z_timer_executor_spin_result_t _z_timer_executor_spin(_z_timer_executor_t *executor) {
    _z_timer_executor_spin_result_t result = _z_timer_executor_next_task_status_inner(executor);
    z_clock_t now = z_clock_now();
    if (!result.has_pending_tasks || zp_clock_elapsed_ms_since(&result._wake_up_time, &now) > 0) {
        return result;
    }
    _z_timer_executor_task_t task;
    _z_timer_executor_task_pqueue_pop(&executor->_tasks, &task);
    size_t expected = (size_t)_Z_EXECUTOR_TASK_STATUS_PENDING;
    if (!_Z_RC_IS_NULL(&task._handle)) {
        while (!_z_atomic_size_compare_exchange_strong(&task._handle._val->_task_status, &expected,
                                                       (size_t)_Z_EXECUTOR_TASK_STATUS_EXECUTING,
                                                       _z_memory_order_acquire, _z_memory_order_relaxed)) {
            if (expected == (size_t)_Z_EXECUTOR_TASK_STATUS_CANCELLED) {
                // The task is cancelled, we can skip it.
                _z_timer_executor_task_destroy(&task);
                return _z_timer_executor_spin(executor);
            }
        }
    }
    _z_timer_executor_task_status_t status =
        task._task_fn(task._task_arg, _Z_RC_IS_NULL(&task._handle) ? NULL : task._handle._val->result, executor);
    if (!status._finished) {
        // Re-enqueue the task
        task._wake_up_time_ms = zp_clock_elapsed_ms_since(&status._wake_up_time, &executor->_epoch);
        if (!_Z_RC_IS_NULL(&task._handle)) {
            _z_atomic_size_store(&task._handle._val->_task_status, (size_t)_Z_EXECUTOR_TASK_STATUS_PENDING,
                                 _z_memory_order_release);
        }
        _z_timer_executor_task_pqueue_push(&executor->_tasks, &task);
        return _z_timer_executor_next_task_status_inner(executor);
    } else {
        if (!_Z_RC_IS_NULL(&task._handle)) {
            _z_atomic_size_store(&task._handle._val->_task_status, (size_t)_Z_EXECUTOR_TASK_STATUS_READY,
                                 _z_memory_order_release);
        }
        _z_timer_executor_task_destroy(&task);
        return _z_timer_executor_next_task_status_inner(executor);
    }
}
