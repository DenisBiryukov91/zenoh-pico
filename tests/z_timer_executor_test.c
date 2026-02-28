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
// filepath: /Users/denisbiryukov/repositories/work/zenoh-pico/tests/z_timer_executor_test.c

#include <stddef.h>
#include <stdio.h>

#include "zenoh-pico/collections/executor.h"
#include "zenoh-pico/system/platform.h"

#undef NDEBUG
#include <assert.h>

// ─── Helpers ────────────────────────────────────────────────────────────────

typedef struct {
    int call_count;
    bool destroyed;
    unsigned long reschedule_delay_ms;  // 0 means finish immediately
} timer_test_arg_t;

// Finishes immediately, increments call_count
static _z_timer_executor_task_status_t task_fn_finish_immediately(void *arg, void *result,
                                                                  _z_timer_executor_t *executor) {
    (void)executor;
    (void)result;
    timer_test_arg_t *a = (timer_test_arg_t *)arg;
    a->call_count++;
    z_clock_t wake = z_clock_now();
    z_clock_advance_ms(&wake, a->reschedule_delay_ms);
    return (_z_timer_executor_task_status_t){._finished = true, ._wake_up_time = wake};
}

// Reschedules once with a given delay, finishes on second call
static _z_timer_executor_task_status_t task_fn_reschedule_once(void *arg, void *result, _z_timer_executor_t *executor) {
    (void)executor;
    (void)result;
    timer_test_arg_t *a = (timer_test_arg_t *)arg;
    a->call_count++;
    z_clock_t wake = z_clock_now();
    // advance wake_up_time by reschedule_delay_ms
    z_clock_advance_ms(&wake, a->reschedule_delay_ms);
    if (a->call_count == 1) {
        return (_z_timer_executor_task_status_t){
            ._finished = false,
            ._wake_up_time = wake,
        };
    }
    return (_z_timer_executor_task_status_t){._finished = true, ._wake_up_time = wake};
}

// Writes true to result on finish
static _z_timer_executor_task_status_t task_fn_write_result(void *arg, void *result, _z_timer_executor_t *executor) {
    (void)executor;
    (void)arg;
    *(bool *)result = true;
    return (_z_timer_executor_task_status_t){._finished = true, ._wake_up_time = z_clock_now()};
}

static void destroy_fn(void *arg) {
    timer_test_arg_t *a = (timer_test_arg_t *)arg;
    a->destroyed = true;
}

// ─── Tests ───────────────────────────────────────────────────────────────────

// A newly created timer executor has no pending tasks.
static void test_timer_executor_spin_empty(void) {
    printf("Test: spin on empty timer executor returns no pending tasks\n");
    _z_timer_executor_t ex = _z_timer_executor_new();
    _z_timer_executor_spin_result_t res = _z_timer_executor_spin(&ex);
    assert(res.has_pending_tasks == false);
    _z_timer_executor_destroy(&ex);
}

// spawn_and_forget: task runs and destroy_fn is called.
static void test_timer_executor_spawn_and_forget_runs_task(void) {
    printf("Test: spawn_and_forget runs task and calls destroy_fn\n");
    _z_timer_executor_t ex = _z_timer_executor_new();
    timer_test_arg_t arg = {.call_count = 0, .destroyed = false, .reschedule_delay_ms = 1000};

    bool ok = _z_timer_executor_spawn_and_forget(&ex, &arg, task_fn_finish_immediately, destroy_fn);
    assert(ok == true);

    _z_timer_executor_spin_result_t res = _z_timer_executor_spin(&ex);
    assert(res.has_pending_tasks == false);
    assert(arg.call_count == 1);
    assert(arg.destroyed == true);

    // After task finished, no more pending tasks
    res = _z_timer_executor_spin(&ex);
    assert(res.has_pending_tasks == false);
    _z_timer_executor_destroy(&ex);
}

// spawn: result is written when task finishes.
static void test_timer_executor_spawn_result_written(void) {
    printf("Test: spawn writes result when task finishes\n");
    _z_timer_executor_t ex = _z_timer_executor_new();
    bool result = false;
    _z_executor_task_handle_rc_t handle = _z_timer_executor_spawn(&ex, NULL, task_fn_write_result, NULL, &result);
    assert(!_Z_RC_IS_NULL(&handle));

    _z_timer_executor_spin(&ex);
    assert(result == true);

    _z_executor_task_handle_rc_drop(&handle);
    _z_timer_executor_destroy(&ex);
}

// A task that reschedules itself is re-queued and eventually finishes.
static void test_timer_executor_task_reschedules(void) {
    printf("Test: rescheduled task is re-queued and eventually finishes\n");
    _z_timer_executor_t ex = _z_timer_executor_new();
    timer_test_arg_t arg = {.call_count = 0, .destroyed = false, .reschedule_delay_ms = 1000};

    bool ok = _z_timer_executor_spawn_and_forget(&ex, &arg, task_fn_reschedule_once, destroy_fn);
    assert(ok == true);

    // First spin: task runs, reschedules → still pending
    _z_timer_executor_spin_result_t res = _z_timer_executor_spin(&ex);
    assert(arg.call_count == 1);
    assert(res.has_pending_tasks == true);
    assert(arg.destroyed == false);

    // Second spin: task sleeps
    res = _z_timer_executor_spin(&ex);
    assert(res.has_pending_tasks == true);  // still pending because destroy_fn not called yet
    assert(arg.call_count == 1);
    z_sleep_ms(500);

    // Third spin: task sleeps some more
    res = _z_timer_executor_spin(&ex);
    assert(res.has_pending_tasks == true);  // still pending because destroy_fn not called yet
    assert(arg.call_count == 1);
    z_sleep_ms(600);

    // Fourth spin: task runs again, finishes
    res = _z_timer_executor_spin(&ex);
    assert(res.has_pending_tasks == false);  // still pending because destroy_fn not called yet
    assert(arg.call_count == 2);
    assert(arg.destroyed == true);

    // Now empty
    res = _z_timer_executor_spin(&ex);
    assert(res.has_pending_tasks == false);
    _z_timer_executor_destroy(&ex);
}

// Cancelling via handle prevents task body from running.
static void test_timer_executor_cancel_prevents_execution(void) {
    printf("Test: cancelled task does not execute\n");
    _z_timer_executor_t ex = _z_timer_executor_new();
    timer_test_arg_t arg = {.call_count = 0, .destroyed = false};
    bool result = false;

    _z_executor_task_handle_rc_t handle =
        _z_timer_executor_spawn(&ex, &arg, task_fn_finish_immediately, destroy_fn, &result);
    assert(!_Z_RC_IS_NULL(&handle));

    _z_executor_task_handle_cancel(&handle);

    _z_timer_executor_spin(&ex);
    assert(arg.call_count == 0);    // body never called
    assert(result == false);        // result not written
    assert(arg.destroyed == true);  // destroy_fn still called

    _z_executor_task_handle_rc_drop(&handle);
    _z_timer_executor_destroy(&ex);
}

// destroy_fn called on all pending tasks when executor is destroyed.
static void test_timer_executor_destroy_calls_pending_destroy_fns(void) {
    printf("Test: executor destroy calls destroy_fn on all pending tasks\n");
    _z_timer_executor_t ex = _z_timer_executor_new();
    timer_test_arg_t args[4];
    for (int i = 0; i < 4; i++) {
        args[i].call_count = 0;
        args[i].destroyed = false;
        bool ok = _z_timer_executor_spawn_and_forget(&ex, &args[i], task_fn_finish_immediately, destroy_fn);
        assert(ok == true);
    }
    // Destroy without spinning — tasks never ran
    _z_timer_executor_destroy(&ex);
    for (int i = 0; i < 4; i++) {
        assert(args[i].call_count == 0);
        assert(args[i].destroyed == true);
    }
}

// Multiple tasks: all execute; heap ordering is respected (earliest wake_up_time first).
static void test_timer_executor_multiple_tasks_order(void) {
    printf("Test: multiple tasks all execute\n");
    _z_timer_executor_t ex = _z_timer_executor_new();
    const int N = 6;
    timer_test_arg_t args[6];
    for (int i = 0; i < N; i++) {
        args[i].call_count = 0;
        args[i].destroyed = false;
        args[i].reschedule_delay_ms = i * 500;  // variable wake_up_time to test ordering
        bool ok = _z_timer_executor_spawn_and_forget(&ex, &args[i], task_fn_reschedule_once, destroy_fn);
        assert(ok == true);
    }
    for (int i = 0; i < N; i++) {
        // run first step of tasks which fires without delay
        _z_timer_executor_spin_result_t res = _z_timer_executor_spin(&ex);
    }
    for (int i = 0; i < N; i++) {
        z_sleep_ms(300);
        // run second step of half the tasks which fire with delay ≤ 300ms, while the other half are still pending
        _z_timer_executor_spin_result_t res = _z_timer_executor_spin(&ex);
    }

    for (int i = 0; i < N; i++) {
        if (i <= N / 2) {
            assert(args[i].call_count == 2);
            assert(args[i].destroyed == true);
        } else {
            assert(args[i].call_count == 1);
            assert(args[i].destroyed == false);
        }
    }

    // Drain
    _z_timer_executor_spin_result_t res;
    size_t spins = 0;
    do {
        res = _z_timer_executor_spin(&ex);
        z_sleep_ms(300);
        spins++;
    } while (res.has_pending_tasks);
    assert(spins > 0);  // at least one spin needed to finish remaining tasks
    assert(spins < 10);

    for (int i = 0; i < N; i++) {
        assert(args[i].call_count == 2);
        assert(args[i].destroyed == true);
    }
    _z_timer_executor_destroy(&ex);
}

// ─── main ────────────────────────────────────────────────────────────────────

int main(void) {
    test_timer_executor_spin_empty();
    test_timer_executor_spawn_and_forget_runs_task();
    test_timer_executor_spawn_result_written();
    test_timer_executor_task_reschedules();
    test_timer_executor_cancel_prevents_execution();
    test_timer_executor_destroy_calls_pending_destroy_fns();
    test_timer_executor_multiple_tasks_order();
    return 0;
}