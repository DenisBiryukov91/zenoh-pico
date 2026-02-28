#include "zenoh-pico/collections/background_executor.h"

#if Z_FEATURE_MULTI_THREAD == 1

typedef struct _z_background_executor_inner_t {
    _z_timer_executor_t _executor;
    _z_mutex_t _mutex;
    _z_condvar_t _condvar;
    _z_atomic_size_t _waiters;
    bool _stop_requested;
    bool _running;
} _z_background_executor_inner_t;

z_result_t _z_background_executor_inner_new(_z_background_executor_inner_t *be) {
    _Z_RETURN_IF_ERR(_z_mutex_init(&be->_mutex));
    _Z_CLEAN_RETURN_IF_ERR(_z_condvar_init(&be->_condvar), _z_mutex_drop(&be->_mutex));
    be->_executor = _z_timer_executor_new();
    _z_atomic_size_init(&be->_waiters, 0);
    be->_stop_requested = false;
    be->_running = false;
    return _Z_RES_OK;
}

z_result_t _z_background_executor_inner_suspend_and_lock(_z_background_executor_inner_t *be) {
    _z_atomic_size_fetch_add(&be->_waiters, 1, _z_memory_order_acq_rel);
    return _z_mutex_lock(&be->_mutex);
}

z_result_t _z_background_executor_inner_suspend(_z_background_executor_inner_t *be) {
    _Z_RETURN_IF_ERR(_z_background_executor_inner_suspend_and_lock(be));
    return _z_mutex_unlock(&be->_mutex);
}

z_result_t _z_background_executor_inner_unlock_and_resume(_z_background_executor_inner_t *be) {
    _z_atomic_size_fetch_sub(&be->_waiters, 1, _z_memory_order_acq_rel);
    _Z_CLEAN_RETURN_IF_ERR(_z_condvar_signal_all(&be->_condvar), _z_mutex_unlock(&be->_mutex));
    return _z_mutex_unlock(&be->_mutex);
}

z_result_t _z_background_executor_inner_resume(_z_background_executor_inner_t *be) {
    _Z_RETURN_IF_ERR(_z_mutex_lock(&be->_mutex));
    return _z_background_executor_inner_unlock_and_resume(be);
}

z_result_t _z_background_executor_inner_run_forever(_z_background_executor_inner_t *be) {
    _Z_RETURN_IF_ERR(_z_mutex_lock(&be->_mutex));
    be->_running = true;
    while (!be->_stop_requested) {
        while (_z_atomic_size_load(&be->_waiters, _z_memory_order_acquire) >
               0) {  // if there are waiters, sleep until they are resumed
            _Z_CLEAN_RETURN_IF_ERR(_z_condvar_wait(&be->_condvar, &be->_mutex), _z_mutex_unlock(&be->_mutex));
        }
        _z_timer_executor_spin_result_t res = _z_timer_executor_spin(&be->_executor);
        if (!res.has_pending_tasks) {  // no pending tasks, sleep until next task is added
            _Z_CLEAN_RETURN_IF_ERR(_z_condvar_wait(&be->_condvar, &be->_mutex), _z_mutex_unlock(&be->_mutex));
        } else {
            z_clock_t now = z_clock_now();
            if (zp_clock_elapsed_ms_since(&res._wake_up_time, &now) > 1) {  // sleep until next task is ready
                z_result_t wait_result = _z_condvar_wait_until(&be->_condvar, &be->_mutex, &res._wake_up_time);
                if (wait_result != Z_ETIMEDOUT && wait_result != _Z_RES_OK) {
                    return _z_mutex_unlock(&be->_mutex);
                }
            }
        }
    }
    be->_running = false;
    _z_condvar_signal_all(&be->_condvar);  // wake up all waiters so that they can see the stop requested flag and exit
    return _z_mutex_unlock(&be->_mutex);
}

z_result_t _z_background_executor_inner_signal_stop(_z_background_executor_inner_t *be) {
    _Z_RETURN_IF_ERR(_z_background_executor_inner_suspend_and_lock(be));
    be->_stop_requested = true;
    return _z_background_executor_inner_unlock_and_resume(be);
}

z_result_t _z_background_executor_inner_stop(_z_background_executor_inner_t *be) {
    _Z_RETURN_IF_ERR(_z_background_executor_inner_suspend_and_lock(be));
    be->_stop_requested = true;
    _z_atomic_size_fetch_sub(&be->_waiters, 1, _z_memory_order_acq_rel);
    while (be->_running) {
        _Z_CLEAN_RETURN_IF_ERR(_z_condvar_wait(&be->_condvar, &be->_mutex), _z_mutex_unlock(&be->_mutex));
    }
    return _z_mutex_unlock(&be->_mutex);
}

_z_background_executor_spawn_result_t _z_background_executor_inner_spawn(_z_background_executor_inner_t *be,
                                                                         void *task_arg,
                                                                         _z_timer_executor_task_fn_t task_fn,
                                                                         _z_executor_task_destroy_fn_t destroy_fn,
                                                                         void *result) {
    _z_background_executor_spawn_result_t spawn_result = {0};
    spawn_result.result = _z_background_executor_inner_suspend_and_lock(be);
    if (spawn_result.result != _Z_RES_OK) {
        return spawn_result;
    }
    spawn_result.handle = _z_timer_executor_spawn(&be->_executor, task_arg, task_fn, destroy_fn, result);
    if (_Z_RC_IS_NULL(&spawn_result.handle)) {
        spawn_result.result = _Z_ERR_SYSTEM_OUT_OF_MEMORY;
    }
    _z_background_executor_inner_unlock_and_resume(be);
    return spawn_result;
}

z_result_t _z_background_executor_inner_spawn_and_forget(_z_background_executor_inner_t *be, void *task_arg,
                                                         _z_timer_executor_task_fn_t task_fn,
                                                         _z_executor_task_destroy_fn_t destroy_fn) {
    _Z_RETURN_IF_ERR(_z_background_executor_inner_suspend_and_lock(be));
    bool ok = _z_timer_executor_spawn_and_forget(&be->_executor, task_arg, task_fn, destroy_fn);
    _Z_RETURN_IF_ERR(_z_background_executor_inner_unlock_and_resume(be));
    return ok ? _Z_RES_OK : _Z_ERR_SYSTEM_OUT_OF_MEMORY;
}

void _z_background_executor_inner_clear(_z_background_executor_inner_t *be) {
    _z_timer_executor_destroy(&be->_executor);
    _z_condvar_drop(&be->_condvar);
    _z_mutex_drop(&be->_mutex);
}

void *_z_background_executor_task_fn(void *arg) {
    _z_background_executor_inner_t *be = (_z_background_executor_inner_t *)arg;
    _z_background_executor_inner_run_forever(be);
    return NULL;
}

z_result_t _z_background_executor_new(_z_background_executor_t *be) {
    be->_inner = _z_background_executor_inner_rc_null();
    _z_background_executor_inner_t *inner =
        (_z_background_executor_inner_t *)z_malloc(sizeof(_z_background_executor_inner_t));
    if (!inner) {
        return _Z_ERR_SYSTEM_OUT_OF_MEMORY;
    }
    if (_z_background_executor_inner_new(inner) != _Z_RES_OK) {
        z_free(inner);
        return _Z_ERR_SYSTEM_OUT_OF_MEMORY;
    }
    be->_inner = _z_background_executor_inner_rc_new(inner);
    if (_Z_RC_IS_NULL(&be->_inner)) {
        _z_background_executor_inner_clear(inner);
        z_free(inner);
        return _Z_ERR_SYSTEM_OUT_OF_MEMORY;
    }

    z_result_t ret = _z_task_init(&be->_task, NULL, _z_background_executor_task_fn, inner);
    if (ret != _Z_RES_OK) {
        _z_background_executor_inner_rc_drop(&be->_inner);
    }
    return ret;
}

_z_background_executor_spawn_result_t _z_background_executor_spawn(_z_background_executor_t *be, void *task_arg,
                                                                   _z_timer_executor_task_fn_t task_fn,
                                                                   _z_executor_task_destroy_fn_t destroy_fn,
                                                                   void *result) {
    _z_background_executor_spawn_result_t spawn_result = {0};
    if (_Z_RC_IS_NULL(&be->_inner)) {
        spawn_result.result = _Z_ERR_INVALID;
        return spawn_result;
    }

    spawn_result = _z_background_executor_inner_spawn(_Z_RC_IN_VAL(&be->_inner), task_arg, task_fn, destroy_fn, result);
    return spawn_result;
}

z_result_t _z_background_executor_spawn_and_forget(_z_background_executor_t *be, void *task_arg,
                                                   _z_timer_executor_task_fn_t task_fn,
                                                   _z_executor_task_destroy_fn_t destroy_fn) {
    if (_Z_RC_IS_NULL(&be->_inner)) {
        return _Z_ERR_INVALID;
    }
    return _z_background_executor_inner_spawn_and_forget(_Z_RC_IN_VAL(&be->_inner), task_arg, task_fn, destroy_fn);
}

z_result_t _z_background_executor_suspend(_z_background_executor_t *be) {
    if (_Z_RC_IS_NULL(&be->_inner)) {
        return _Z_ERR_INVALID;
    }
    return _z_background_executor_inner_suspend(_Z_RC_IN_VAL(&be->_inner));
}

z_result_t _z_background_executor_resume(_z_background_executor_t *be) {
    if (_Z_RC_IS_NULL(&be->_inner)) {
        return _Z_ERR_INVALID;
    }
    return _z_background_executor_inner_resume(_Z_RC_IN_VAL(&be->_inner));
}

z_result_t _z_background_executor_destroy(_z_background_executor_t *be) {
    if (_Z_RC_IS_NULL(&be->_inner)) {
        return _Z_RES_OK;
    }
    _z_background_executor_inner_t *inner = _Z_RC_IN_VAL(&be->_inner);
    if (inner == NULL) {
        return _Z_RES_OK;
    }
    _Z_RETURN_IF_ERR(_z_background_executor_inner_signal_stop(inner));
    _z_task_join(&be->_task);
    _z_background_executor_inner_rc_drop(&be->_inner);
    return _Z_RES_OK;
}

#endif