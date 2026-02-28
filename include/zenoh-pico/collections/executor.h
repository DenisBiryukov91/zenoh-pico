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

typedef enum _z_executor_task_status_t {
    _Z_EXECUTOR_TASK_STATUS_PENDING = 0,
    _Z_EXECUTOR_TASK_STATUS_READY = 1,
    _Z_EXECUTOR_TASK_STATUS_CANCELLED = 2,
    _Z_EXECUTOR_TASK_STATUS_EXECUTING = 3,
} _z_executor_task_status_t;

typedef struct _z_executor_task_handle_t {
    void *result;
    _z_atomic_size_t _task_status;
} _z_executor_task_handle_t;

void _z_executor_task_handle_clear(_z_executor_task_handle_t *handle);
_Z_REFCOUNT_DEFINE_NO_FROM_VAL(_z_executor_task_handle, _z_executor_task_handle)
void _z_executor_task_handle_cancel(const _z_executor_task_handle_rc_t *handle);
_z_executor_task_status_t _z_executor_task_status(const _z_executor_task_handle_rc_t *handle);

typedef struct _z_executor_t _z_executor_t;
typedef bool (*_z_executor_task_fn_t)(void *arg, void *result, _z_executor_t *executor);
typedef void (*_z_executor_task_destroy_fn_t)(void *arg);

typedef struct _z_executor_task_t {
    void *_task_arg;
    _z_executor_task_handle_rc_t _handle;
    _z_executor_task_fn_t _task_fn;
    _z_executor_task_destroy_fn_t _destroy_fn;
} _z_executor_task_t;

static inline void _z_executor_task_destroy(_z_executor_task_t *task) {
    if (task->_destroy_fn != NULL) {
        task->_destroy_fn(task->_task_arg);
    }
    _z_executor_task_handle_rc_drop(&task->_handle);
}

static inline void _z_executor_task_move(_z_executor_task_t *dst, _z_executor_task_t *src) {
    dst->_task_arg = src->_task_arg;
    dst->_handle = src->_handle;
    dst->_task_fn = src->_task_fn;
    dst->_destroy_fn = src->_destroy_fn;

    // Clear source
    src->_task_arg = NULL;
    src->_handle = _z_executor_task_handle_rc_null();
    src->_task_fn = NULL;
    src->_destroy_fn = NULL;
}

#define _ZP_DEQUE_TEMPLATE_ELEM_TYPE _z_executor_task_t
#define _ZP_DEQUE_TEMPLATE_NAME _z_executor_task_deque
#define _ZP_DEQUE_TEMPLATE_ELEM_DESTROY_FN_NAME _z_executor_task_destroy
#define _ZP_DEQUE_TEMPLATE_ELEM_MOVE_FN_NAME _z_executor_task_move
#define _ZP_DEQUE_TEMPLATE_SIZE 16
#include "zenoh-pico/collections/deque_template.h"

typedef struct _z_executor_t {
    _z_executor_task_deque_t _tasks;
} _z_executor_t;

static inline _z_executor_t _z_executor_new(void) {
    _z_executor_t executor;
    executor._tasks = _z_executor_task_deque_new();
    return executor;
}
static inline void _z_executor_destroy(_z_executor_t *executor) { _z_executor_task_deque_destroy(&executor->_tasks); }

_z_executor_task_handle_rc_t _z_executor_spawn(_z_executor_t *executor, void *task_arg, _z_executor_task_fn_t task_fn,
                                               _z_executor_task_destroy_fn_t destroy_fn, void *task_result);
bool _z_executor_spawn_and_forget(_z_executor_t *executor, void *task_arg, _z_executor_task_fn_t task_fn,
                                  _z_executor_task_destroy_fn_t destroy_fn);
bool _z_executor_spin(_z_executor_t *executor);

typedef struct _z_timer_executor_task_status_t {
    bool _finished;
    z_clock_t _wake_up_time;
} _z_timer_executor_task_status_t;

typedef struct _z_timer_executor_t _z_timer_executor_t;
typedef _z_timer_executor_task_status_t (*_z_timer_executor_task_fn_t)(void *arg, void *result,
                                                                       _z_timer_executor_t *executor);

typedef struct _z_timer_executor_task_t {
    void *_task_arg;
    _z_executor_task_handle_rc_t _handle;
    _z_timer_executor_task_fn_t _task_fn;
    _z_executor_task_destroy_fn_t _destroy_fn;
    unsigned long _wake_up_time_ms;
} _z_timer_executor_task_t;

static inline void _z_timer_executor_task_destroy(_z_timer_executor_task_t *task) {
    if (task->_destroy_fn != NULL) {
        task->_destroy_fn(task->_task_arg);
    }
    _z_executor_task_handle_rc_drop(&task->_handle);
}

static inline void _z_timer_executor_task_move(_z_timer_executor_task_t *dst, _z_timer_executor_task_t *src) {
    dst->_task_arg = src->_task_arg;
    dst->_handle = src->_handle;
    dst->_task_fn = src->_task_fn;
    dst->_destroy_fn = src->_destroy_fn;
    dst->_wake_up_time_ms = src->_wake_up_time_ms;

    // Clear source
    src->_task_arg = NULL;
    src->_handle = _z_executor_task_handle_rc_null();
    src->_task_fn = NULL;
    src->_destroy_fn = NULL;
    src->_wake_up_time_ms = 0;
}

static inline int _z_timer_executor_task_cmp(const _z_timer_executor_task_t *a, const _z_timer_executor_task_t *b) {
    if (a->_wake_up_time_ms < b->_wake_up_time_ms) {
        return -1;
    } else if (a->_wake_up_time_ms > b->_wake_up_time_ms) {
        return 1;
    } else {
        return 0;
    }
}
#define _ZP_PQUEUE_TEMPLATE_ELEM_TYPE _z_timer_executor_task_t
#define _ZP_PQUEUE_TEMPLATE_NAME _z_timer_executor_task_pqueue
#define _ZP_PQUEUE_TEMPLATE_ELEM_DESTROY_FN_NAME _z_timer_executor_task_destroy
#define _ZP_PQUEUE_TEMPLATE_ELEM_MOVE_FN_NAME _z_timer_executor_task_move
#define _ZP_PQUEUE_TEMPLATE_ELEM_CMP_FN_NAME _z_timer_executor_task_cmp
#define _ZP_PQUEUE_TEMPLATE_SIZE 16
#include "zenoh-pico/collections/pqueue_template.h"

typedef struct _z_timer_executor_t {
    _z_timer_executor_task_pqueue_t _tasks;
    z_clock_t _epoch;
} _z_timer_executor_t;

static inline _z_timer_executor_t _z_timer_executor_new(void) {
    _z_timer_executor_t executor;
    executor._tasks = _z_timer_executor_task_pqueue_new();
    executor._epoch = z_clock_now();
    return executor;
}
static inline void _z_timer_executor_destroy(_z_timer_executor_t *executor) {
    _z_timer_executor_task_pqueue_destroy(&executor->_tasks);
}

typedef struct _z_timer_executor_spin_result_t {
    bool has_pending_tasks;
    z_clock_t _wake_up_time;
} _z_timer_executor_spin_result_t;

_z_executor_task_handle_rc_t _z_timer_executor_spawn(_z_timer_executor_t *executor, void *task_arg,
                                                     _z_timer_executor_task_fn_t task_fn,
                                                     _z_executor_task_destroy_fn_t destroy_fn, void *task_result);
bool _z_timer_executor_spawn_and_forget(_z_timer_executor_t *executor, void *task_arg,
                                        _z_timer_executor_task_fn_t task_fn, _z_executor_task_destroy_fn_t destroy_fn);
_z_timer_executor_spin_result_t _z_timer_executor_spin(_z_timer_executor_t *executor);

#ifdef __cplusplus
}
#endif

#endif