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
// filepath: /Users/denisbiryukov/repositories/work/zenoh-pico/tests/z_background_executor_test.c

#include <stddef.h>
#include <stdio.h>

#include "zenoh-pico/collections/background_executor.h"
#include "zenoh-pico/system/platform.h"

#undef NDEBUG
#include <assert.h>

#if Z_FEATURE_MULTI_THREAD == 1

// ─── Helpers ────────────────────────────────────────────────────────────────

typedef struct {
    _z_mutex_t mutex;
    _z_condvar_t condvar;
    int call_count;
    bool destroyed;
    bool finished;
    size_t reschedule_delay_ms;  // for task that reschedules once
} shared_arg_t;

static void shared_arg_init(shared_arg_t *a) {
    _z_mutex_init(&a->mutex);
    _z_condvar_init(&a->condvar);
    a->call_count = 0;
    a->destroyed = false;
    a->finished = false;
    a->reschedule_delay_ms = 0;
}

static void shared_arg_clear(shared_arg_t *a) {
    _z_condvar_drop(&a->condvar);
    _z_mutex_drop(&a->mutex);
}

// Wait until call_count reaches expected, with a generous timeout
static void shared_arg_wait_call_count(shared_arg_t *a, int expected) {
    _z_mutex_lock(&a->mutex);
    while (a->call_count < expected) {
        _z_condvar_wait(&a->condvar, &a->mutex);
    }
    _z_mutex_unlock(&a->mutex);
}

static void shared_arg_wait_destroyed(shared_arg_t *a) {
    _z_mutex_lock(&a->mutex);
    while (!a->destroyed) {
        _z_condvar_wait(&a->condvar, &a->mutex);
    }
    _z_mutex_unlock(&a->mutex);
}

// Task that finishes immediately
static _z_timer_executor_task_status_t task_fn_finish(void *arg, void *result, _z_timer_executor_t *ex) {
    (void)result;
    (void)ex;
    shared_arg_t *a = (shared_arg_t *)arg;
    _z_mutex_lock(&a->mutex);
    a->call_count++;
    a->finished = true;
    _z_condvar_signal_all(&a->condvar);
    _z_mutex_unlock(&a->mutex);
    return (_z_timer_executor_task_status_t){._finished = true, ._wake_up_time = z_clock_now()};
}

// Task that reschedules once then finishes
static _z_timer_executor_task_status_t task_fn_reschedule_once(void *arg, void *result, _z_timer_executor_t *ex) {
    (void)result;
    (void)ex;
    shared_arg_t *a = (shared_arg_t *)arg;
    _z_mutex_lock(&a->mutex);
    a->call_count++;
    bool should_finish = (a->call_count >= 2);
    _z_condvar_signal_all(&a->condvar);
    _z_mutex_unlock(&a->mutex);
    z_clock_t wake = z_clock_now();
    z_clock_advance_ms(&wake, a->reschedule_delay_ms);
    return (_z_timer_executor_task_status_t){._finished = should_finish, ._wake_up_time = wake};
}

// Task that writes true to result
static _z_timer_executor_task_status_t task_fn_write_result(void *arg, void *result, _z_timer_executor_t *ex) {
    (void)ex;
    shared_arg_t *a = (shared_arg_t *)arg;
    *(bool *)result = true;
    _z_mutex_lock(&a->mutex);
    a->call_count++;
    _z_condvar_signal_all(&a->condvar);
    _z_mutex_unlock(&a->mutex);
    return (_z_timer_executor_task_status_t){._finished = true, ._wake_up_time = z_clock_now()};
}

static void destroy_fn(void *arg) {
    shared_arg_t *a = (shared_arg_t *)arg;
    _z_mutex_lock(&a->mutex);
    a->destroyed = true;
    _z_condvar_signal_all(&a->condvar);
    _z_mutex_unlock(&a->mutex);
}

// ─── Tests ───────────────────────────────────────────────────────────────────

// Create and immediately destroy — no tasks spawned.
static void test_background_executor_new_destroy(void) {
    printf("Test: new and destroy with no tasks\n");
    _z_background_executor_t be;
    assert(_z_background_executor_new(&be) == _Z_RES_OK);
    assert(_z_background_executor_destroy(&be) == _Z_RES_OK);
}

// spawn_and_forget: task body runs and destroy_fn is called.
static void test_background_executor_spawn_and_forget(size_t idle_time_ms) {
    printf("Test: spawn_and_forget runs task and calls destroy_fn, idle_time_ms=%zu\n", idle_time_ms);
    _z_background_executor_t be;
    assert(_z_background_executor_new(&be) == _Z_RES_OK);

    shared_arg_t arg;
    shared_arg_init(&arg);

    if (idle_time_ms > 0) {
        z_sleep_ms(idle_time_ms);
    }
    assert(_z_background_executor_spawn_and_forget(&be, &arg, task_fn_finish, destroy_fn) == _Z_RES_OK);

    shared_arg_wait_call_count(&arg, 1);
    assert(arg.call_count == 1);

    shared_arg_wait_destroyed(&arg);
    assert(arg.destroyed == true);

    assert(_z_background_executor_destroy(&be) == _Z_RES_OK);
    shared_arg_clear(&arg);
}

// spawn: result pointer is written when task finishes.
static void test_background_executor_spawn_result_written(void) {
    printf("Test: spawn writes result when task finishes\n");
    _z_background_executor_t be;
    assert(_z_background_executor_new(&be) == _Z_RES_OK);

    shared_arg_t arg;
    shared_arg_init(&arg);
    bool result = false;

    _z_background_executor_spawn_result_t sr =
        _z_background_executor_spawn(&be, &arg, task_fn_write_result, destroy_fn, &result);
    assert(sr.result == _Z_RES_OK);
    assert(!_Z_RC_IS_NULL(&sr.handle));

    shared_arg_wait_call_count(&arg, 1);
    assert(result == true);

    shared_arg_wait_destroyed(&arg);

    _z_executor_task_handle_rc_drop(&sr.handle);
    assert(_z_background_executor_destroy(&be) == _Z_RES_OK);
    shared_arg_clear(&arg);
}

// A rescheduled task is re-queued until it finishes.
static void test_background_executor_task_reschedules(size_t reschedule_delay_ms) {
    printf("Test: rescheduled task runs until finished, reschedule_delay_ms=%zu\n", reschedule_delay_ms);
    _z_background_executor_t be;
    assert(_z_background_executor_new(&be) == _Z_RES_OK);

    shared_arg_t arg;
    shared_arg_init(&arg);
    arg.reschedule_delay_ms = reschedule_delay_ms;

    assert(_z_background_executor_spawn_and_forget(&be, &arg, task_fn_reschedule_once, destroy_fn) == _Z_RES_OK);

    // Must be called exactly twice: once rescheduled, once finished
    shared_arg_wait_call_count(&arg, 2);
    assert(arg.call_count == 2);

    shared_arg_wait_destroyed(&arg);
    assert(arg.destroyed == true);

    assert(_z_background_executor_destroy(&be) == _Z_RES_OK);
    shared_arg_clear(&arg);
}

// Cancel via handle: task body does not run but destroy_fn is called.
static void test_background_executor_cancel_prevents_execution(void) {
    printf("Test: cancelled task does not execute; destroy_fn still called\n");
    _z_background_executor_t be;
    assert(_z_background_executor_new(&be) == _Z_RES_OK);

    shared_arg_t arg;
    shared_arg_init(&arg);
    bool result = false;

    // Suspend the executor so we can cancel before the task runs
    assert(_z_background_executor_suspend(&be) == _Z_RES_OK);

    _z_background_executor_spawn_result_t sr =
        _z_background_executor_spawn(&be, &arg, task_fn_finish, destroy_fn, &result);
    assert(sr.result == _Z_RES_OK);

    // Cancel while executor is suspended
    _z_executor_task_handle_cancel(&sr.handle);

    assert(_z_background_executor_resume(&be) == _Z_RES_OK);

    // destroy_fn must still be called even though task was cancelled
    shared_arg_wait_destroyed(&arg);
    assert(arg.destroyed == true);
    assert(arg.call_count == 0);  // body never ran
    assert(result == false);      // result not written

    _z_executor_task_handle_rc_drop(&sr.handle);
    assert(_z_background_executor_destroy(&be) == _Z_RES_OK);
    shared_arg_clear(&arg);
}

// Multiple concurrent tasks all complete.
static void test_background_executor_multiple_tasks(void) {
    printf("Test: multiple tasks all complete\n");
    _z_background_executor_t be;
    assert(_z_background_executor_new(&be) == _Z_RES_OK);

    const int N = 8;
    shared_arg_t args[8];
    for (int i = 0; i < N; i++) {
        shared_arg_init(&args[i]);
        assert(_z_background_executor_spawn_and_forget(&be, &args[i], task_fn_finish, destroy_fn) == _Z_RES_OK);
    }

    for (int i = 0; i < N; i++) {
        shared_arg_wait_call_count(&args[i], 1);
        shared_arg_wait_destroyed(&args[i]);
        assert(args[i].call_count == 1);
        assert(args[i].destroyed == true);
    }

    assert(_z_background_executor_destroy(&be) == _Z_RES_OK);
    for (int i = 0; i < N; i++) {
        shared_arg_clear(&args[i]);
    }
}

// destroy while tasks are pending: destroy_fn must be called for each.
static void test_background_executor_destroy_calls_pending_destroy_fns(void) {
    printf("Test: destroy calls destroy_fn on pending tasks\n");
    _z_background_executor_t be;
    assert(_z_background_executor_new(&be) == _Z_RES_OK);

    const int N = 4;
    shared_arg_t args[4];

    // Suspend so tasks queue up without running
    assert(_z_background_executor_suspend(&be) == _Z_RES_OK);
    for (int i = 0; i < N; i++) {
        shared_arg_init(&args[i]);
        assert(_z_background_executor_spawn_and_forget(&be, &args[i], task_fn_finish, destroy_fn) == _Z_RES_OK);
    }
    assert(_z_background_executor_resume(&be) == _Z_RES_OK);

    // Destroy immediately — some tasks may not have run yet
    assert(_z_background_executor_destroy(&be) == _Z_RES_OK);

    // All destroy_fns must have been called by now
    for (int i = 0; i < N; i++) {
        assert(args[i].destroyed == true);
        shared_arg_clear(&args[i]);
    }
}

// suspend/resume: tasks do not run while suspended.
static void test_background_executor_suspend_resume(void) {
    printf("Test: tasks do not run while executor is suspended\n");
    _z_background_executor_t be;
    assert(_z_background_executor_new(&be) == _Z_RES_OK);

    shared_arg_t arg;
    shared_arg_init(&arg);

    assert(_z_background_executor_suspend(&be) == _Z_RES_OK);
    assert(_z_background_executor_spawn_and_forget(&be, &arg, task_fn_finish, destroy_fn) == _Z_RES_OK);

    // Give the background thread a chance to (incorrectly) run the task
    z_sleep_ms(500);
    assert(arg.call_count == 0);  // must not have run yet

    assert(_z_background_executor_resume(&be) == _Z_RES_OK);

    shared_arg_wait_call_count(&arg, 1);
    assert(arg.call_count == 1);  // now it ran

    shared_arg_wait_destroyed(&arg);
    assert(_z_background_executor_destroy(&be) == _Z_RES_OK);
    shared_arg_clear(&arg);
}

// ─── main ────────────────────────────────────────────────────────────────────

int main(void) {
    test_background_executor_new_destroy();
    test_background_executor_spawn_and_forget(0);
    test_background_executor_spawn_and_forget(500);
    test_background_executor_spawn_result_written();
    test_background_executor_task_reschedules(0);
    test_background_executor_task_reschedules(500);
    test_background_executor_cancel_prevents_execution();
    test_background_executor_multiple_tasks();
    test_background_executor_destroy_calls_pending_destroy_fns();
    test_background_executor_suspend_resume();
    printf("All background executor tests passed.\n");
    return 0;
}

#else

int main(void) {
    printf("Skipping background executor tests (Z_FEATURE_MULTI_THREAD disabled)\n");
    return 0;
}

#endif