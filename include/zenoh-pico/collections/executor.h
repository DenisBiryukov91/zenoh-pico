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

#ifndef ZENOH_PICO_COLLECTIONS_EXECUTOR_H
#define ZENOH_PICO_COLLECTIONS_EXECUTOR_H

#include <stddef.h>

#include "zenoh-pico/collections/atomic.h"
#include "zenoh-pico/collections/refcount.h"
#include "zenoh-pico/system/platform.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum _z_fut_status_t {
    _Z_FUT_STATUS_PENDING = 0,
    _Z_FUT_STATUS_READY = 1,
    _Z_FUT_STATUS_CANCELLED = 2,
    _Z_FUT_STATUS_EXECUTING = 3,
} _z_fut_status_t;

typedef struct _z_fut_handle_t {
    _z_atomic_size_t _task_status;
} _z_fut_handle_t;

void _z_fut_handle_clear(_z_fut_handle_t *handle);
_Z_REFCOUNT_DEFINE_NO_FROM_VAL(_z_fut_handle, _z_fut_handle)
void _z_fut_handle_cancel(_z_fut_handle_t *handle);
_z_fut_status_t _z_fut_handle_status(const _z_fut_handle_t *handle);

typedef struct _z_fut_fn_result_t {
    bool _ready;
    bool _has_wake_up_time;
    z_clock_t _wake_up_time;
} _z_fut_fn_result_t;

typedef struct _z_executor_t _z_executor_t;
typedef _z_fut_fn_result_t (*_z_fut_fn_t)(void *arg, _z_executor_t *executor);
typedef void (*_z_fut_destroy_fn_t)(void *arg);

typedef struct _z_fut_t {
    void *_fut_arg;
    _z_fut_fn_t _fut_fn;
    _z_fut_destroy_fn_t _destroy_fn;
    _z_fut_handle_rc_t _handle;
} _z_fut_t;

_z_fut_handle_rc_t _z_fut_get_handle(_z_fut_t *fut);

static inline void _z_fut_destroy(_z_fut_t *fut) {
    if (fut->_destroy_fn != NULL) {
        fut->_destroy_fn(fut->_fut_arg);
    }
    _z_fut_handle_rc_drop(&fut->_handle);
}

static inline void _z_fut_move(_z_fut_t *dst, _z_fut_t *src) {
    dst->_fut_arg = src->_fut_arg;
    dst->_handle = src->_handle;
    dst->_fut_fn = src->_fut_fn;
    dst->_destroy_fn = src->_destroy_fn;

    // Clear source
    src->_fut_arg = NULL;
    src->_handle = _z_fut_handle_rc_null();
    src->_fut_fn = NULL;
    src->_destroy_fn = NULL;
}
typedef struct _z_timed_fut_t {
    _z_fut_t _fut;
    unsigned long _wake_up_time_ms;
} _z_timed_fut_t;

static inline void _z_timed_fut_destroy(_z_timed_fut_t *fut) { _z_fut_destroy(&fut->_fut); }

static inline void _z_timed_fut_move(_z_timed_fut_t *dst, _z_timed_fut_t *src) {
    _z_fut_move(&dst->_fut, &src->_fut);
    dst->_wake_up_time_ms = src->_wake_up_time_ms;
}

static inline int _z_timed_fut_cmp(const _z_timed_fut_t *a, const _z_timed_fut_t *b) {
    if (a->_wake_up_time_ms < b->_wake_up_time_ms) {
        return -1;
    } else if (a->_wake_up_time_ms > b->_wake_up_time_ms) {
        return 1;
    } else {
        return 0;
    }
}

static inline void _z_fut_new(_z_fut_t *fut, void *arg, _z_fut_fn_t fut_fn, _z_fut_destroy_fn_t destroy_fn) {
    fut->_fut_arg = arg;
    fut->_fut_fn = fut_fn;
    fut->_destroy_fn = destroy_fn;
    fut->_handle = _z_fut_handle_rc_null();
}

static inline void _z_fut_null(_z_fut_t *fut) { _z_fut_new(fut, NULL, NULL, NULL); }

static inline bool _z_fut_is_null(const _z_fut_t *fut) { return fut->_fut_fn == NULL && _Z_RC_IS_NULL(&fut->_handle); }

#define _ZP_DEQUE_TEMPLATE_ELEM_TYPE _z_fut_t
#define _ZP_DEQUE_TEMPLATE_NAME _z_fut_deque
#define _ZP_DEQUE_TEMPLATE_ELEM_DESTROY_FN_NAME _z_fut_destroy
#define _ZP_DEQUE_TEMPLATE_ELEM_MOVE_FN_NAME _z_fut_move
#define _ZP_DEQUE_TEMPLATE_SIZE 16
#include "zenoh-pico/collections/deque_template.h"

#define _ZP_PQUEUE_TEMPLATE_ELEM_TYPE _z_timed_fut_t
#define _ZP_PQUEUE_TEMPLATE_NAME _z_timed_fut_pqueue
#define _ZP_PQUEUE_TEMPLATE_ELEM_DESTROY_FN_NAME _z_timed_fut_destroy
#define _ZP_PQUEUE_TEMPLATE_ELEM_MOVE_FN_NAME _z_timed_fut_move
#define _ZP_PQUEUE_TEMPLATE_ELEM_CMP_FN_NAME _z_timed_fut_cmp
#define _ZP_PQUEUE_TEMPLATE_SIZE 16
#include "zenoh-pico/collections/pqueue_template.h"

typedef struct _z_executor_t {
    _z_fut_deque_t _tasks;
    _z_timed_fut_pqueue_t _timed_tasks;
    z_clock_t _epoch;
} _z_executor_t;

static inline _z_executor_t _z_executor_new(void) {
    _z_executor_t executor;
    executor._tasks = _z_fut_deque_new();
    executor._timed_tasks = _z_timed_fut_pqueue_new();
    executor._epoch = z_clock_now();
    return executor;
}
static inline void _z_executor_destroy(_z_executor_t *executor) {
    _z_fut_deque_destroy(&executor->_tasks);
    _z_timed_fut_pqueue_destroy(&executor->_timed_tasks);
}

// Spawn a new future to be executed.
// The executor takes the ownership of the future, and will destroy the future (by calling the destroy_fn) when the
// future is finished or cancelled. The caller can use the future handle to cancel the future or check its status.
bool _z_executor_spawn(_z_executor_t *executor, _z_fut_t *fut);

typedef enum _z_executor_spin_result_status_t {
    _Z_EXECUTOR_SPIN_RESULT_NO_TASKS,
    _Z_EXECUTOR_SPIN_RESULT_EXECUTED_TASK,
    _Z_EXECUTOR_SPIN_RESULT_SHOULD_WAIT,
    _Z_EXECUTOR_SPIN_RESULT_FAILED,
} _z_executor_spin_result_status_t;
typedef struct _z_executor_spin_result_t {
    _z_executor_spin_result_status_t status;
    z_clock_t next_wake_up_time;
} _z_executor_spin_result_t;

_z_executor_spin_result_t _z_executor_spin(_z_executor_t *executor);

#ifdef __cplusplus
}
#endif

#endif
