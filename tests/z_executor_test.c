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
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include "zenoh-pico/collections/executor.h"

#undef NDEBUG
#include <assert.h>

// ─── Helpers ────────────────────────────────────────────────────────────────

typedef struct {
    int value;
    bool destroyed;
} test_arg_t;

static bool task_fn_set_true(void *arg, void *result, _z_executor_t *executor) {
    (void)executor;
    *(bool *)result = true;
    (void)arg;
    return true;  // finished
}

static bool task_fn_increment(void *arg, void *result, _z_executor_t *executor) {
    (void)executor;
    (void)result;
    int *counter = (int *)arg;
    (*counter)++;
    return true;
}

// Task that returns false on first N calls, then true (simulates multi-step task)
typedef struct {
    int steps_remaining;
    int *call_count;
} multistep_arg_t;

static bool task_fn_multistep(void *arg, void *result, _z_executor_t *executor) {
    (void)executor;
    (void)result;
    multistep_arg_t *a = (multistep_arg_t *)arg;
    (*a->call_count)++;
    if (a->steps_remaining > 0) {
        a->steps_remaining--;
        return false;  // not finished yet
    }
    return true;
}

static void destroy_fn_set_flag(void *arg) {
    test_arg_t *a = (test_arg_t *)arg;
    a->destroyed = true;
}

// ─── Tests ───────────────────────────────────────────────────────────────────

// A newly created executor has no pending tasks: spin returns false immediately.
static void test_executor_spin_empty(void) {
    printf("Test: spin on empty executor returns false\n");
    _z_executor_t ex = _z_executor_new();
    bool has_work = _z_executor_spin(&ex);
    assert(has_work == false);
    _z_executor_destroy(&ex);
}

// spawn_and_forget runs the task and returns true.
static void test_executor_spawn_and_forget_runs_task(void) {
    printf("Test: spawn_and_forget runs task\n");
    _z_executor_t ex = _z_executor_new();
    int counter = 0;
    bool ok = _z_executor_spawn_and_forget(&ex, &counter, task_fn_increment, NULL);
    assert(ok == true);
    bool has_work = _z_executor_spin(&ex);
    assert(has_work == false);
    assert(counter == 1);
    // After task finished, next spin finds nothing.
    has_work = _z_executor_spin(&ex);
    assert(has_work == false);
    _z_executor_destroy(&ex);
}

// destroy_fn is called when a spawn_and_forget task finishes.
static void test_executor_spawn_and_forget_destroy_called(void) {
    printf("Test: spawn_and_forget calls destroy_fn after task finishes\n");
    _z_executor_t ex = _z_executor_new();
    test_arg_t arg = {.value = 0, .destroyed = false};
    int counter = 0;
    (void)counter;
    bool ok = _z_executor_spawn_and_forget(&ex, &arg, task_fn_increment, destroy_fn_set_flag);
    assert(ok == true);
    _z_executor_spin(&ex);
    assert(arg.destroyed == true);
    _z_executor_destroy(&ex);
}

// spawn returns a handle; the result is written when the task finishes.
static void test_executor_spawn_result_written(void) {
    printf("Test: spawn writes result when task finishes\n");
    _z_executor_t ex = _z_executor_new();
    bool result = false;
    _z_executor_task_handle_rc_t handle = _z_executor_spawn(&ex, NULL, task_fn_set_true, NULL, &result);
    assert(!_Z_RC_IS_NULL(&handle));
    _z_executor_spin(&ex);
    assert(result == true);
    _z_executor_task_handle_rc_drop(&handle);
    _z_executor_destroy(&ex);
}

// A task returning false is rescheduled; it must be called until it returns true.
static void test_executor_multistep_task(void) {
    printf("Test: multi-step task is re-queued until finished\n");
    _z_executor_t ex = _z_executor_new();
    int call_count = 0;
    multistep_arg_t arg = {.steps_remaining = 2, .call_count = &call_count};
    bool ok = _z_executor_spawn_and_forget(&ex, &arg, task_fn_multistep, NULL);
    assert(ok == true);

    // Step 1: returns false → still pending
    bool has_work = _z_executor_spin(&ex);
    assert(has_work == true);
    assert(call_count == 1);

    // Step 2: returns false → still pending
    has_work = _z_executor_spin(&ex);
    assert(has_work == true);
    assert(call_count == 2);

    // Step 3: returns true → finished
    has_work = _z_executor_spin(&ex);
    assert(has_work == false);
    assert(call_count == 3);

    // Now empty
    has_work = _z_executor_spin(&ex);
    assert(has_work == false);
    _z_executor_destroy(&ex);
}

// Cancelling a task via the handle prevents it from running.
static void test_executor_cancel_prevents_execution(void) {
    printf("Test: cancelled task does not execute\n");
    _z_executor_t ex = _z_executor_new();
    int counter = 0;
    bool result = false;
    _z_executor_task_handle_rc_t handle = _z_executor_spawn(&ex, &counter, task_fn_increment, NULL, &result);
    assert(!_Z_RC_IS_NULL(&handle));

    // Cancel before spinning
    _z_executor_task_handle_cancel(&handle);

    _z_executor_spin(&ex);
    assert(counter == 0);     // task body was not called
    assert(result == false);  // result was not written

    _z_executor_task_handle_rc_drop(&handle);
    _z_executor_destroy(&ex);
}

// destroy_fn is still called for a cancelled task (resource cleanup must happen).
static void test_executor_cancel_still_calls_destroy(void) {
    printf("Test: cancelled task still calls destroy_fn\n");
    _z_executor_t ex = _z_executor_new();
    test_arg_t arg = {.value = 0, .destroyed = false};
    bool result = false;
    _z_executor_task_handle_rc_t handle = _z_executor_spawn(&ex, &arg, task_fn_set_true, destroy_fn_set_flag, &result);

    _z_executor_task_handle_cancel(&handle);

    _z_executor_spin(&ex);
    assert(arg.destroyed == true);

    _z_executor_task_handle_rc_drop(&handle);
    _z_executor_destroy(&ex);
}

// Multiple tasks are all executed in a single spin loop.
static void test_executor_multiple_tasks(void) {
    printf("Test: multiple tasks all execute\n");
    _z_executor_t ex = _z_executor_new();
    int counter = 0;
    const int N = 8;
    for (int i = 0; i < N; i++) {
        bool ok = _z_executor_spawn_and_forget(&ex, &counter, task_fn_increment, NULL);
        assert(ok == true);
    }
    // Spin until drained
    while (_z_executor_spin(&ex)) {
    }
    assert(counter == N);
    _z_executor_destroy(&ex);
}

// destroy is called on all tasks still in the queue when the executor is destroyed.
static void test_executor_destroy_calls_pending_destroy_fns(void) {
    printf("Test: executor destroy calls destroy_fn on pending tasks\n");
    _z_executor_t ex = _z_executor_new();
    test_arg_t args[4];
    for (int i = 0; i < 4; i++) {
        args[i].destroyed = false;
        bool ok = _z_executor_spawn_and_forget(&ex, &args[i], task_fn_set_true, destroy_fn_set_flag);
        assert(ok == true);
    }
    // Destroy without spinning — tasks never ran, but destroy_fn must be called.
    _z_executor_destroy(&ex);
    for (int i = 0; i < 4; i++) {
        assert(args[i].destroyed == true);
    }
}

// ─── main ────────────────────────────────────────────────────────────────────

int main(void) {
    test_executor_spin_empty();
    test_executor_spawn_and_forget_runs_task();
    test_executor_spawn_and_forget_destroy_called();
    test_executor_spawn_result_written();
    test_executor_multistep_task();
    test_executor_cancel_prevents_execution();
    test_executor_cancel_still_calls_destroy();
    test_executor_multiple_tasks();
    test_executor_destroy_calls_pending_destroy_fns();
    return 0;
}