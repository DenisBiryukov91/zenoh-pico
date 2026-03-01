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

#ifndef ZENOH_PICO_COLLECTIONS_BACKGROUND_EXECUTOR_H
#define ZENOH_PICO_COLLECTIONS_BACKGROUND_EXECUTOR_H

#include "zenoh-pico/config.h"
#if Z_FEATURE_MULTI_THREAD == 1
#include <stddef.h>

#include "zenoh-pico/collections/executor.h"
#include "zenoh-pico/collections/refcount.h"
#include "zenoh-pico/system/platform.h"

typedef struct _z_background_executor_inner_t _z_background_executor_inner_t;
void _z_background_executor_inner_clear(_z_background_executor_inner_t *be);

_Z_REFCOUNT_DEFINE_NO_FROM_VAL(_z_background_executor_inner, _z_background_executor_inner)

typedef struct _z_background_executor_t {
    _z_background_executor_inner_rc_t _inner;
    _z_task_t _task;
} _z_background_executor_t;

z_result_t _z_background_executor_new(_z_background_executor_t *be);
// Returns a handle to the spawned task and a result indicating whether the spawn was successful.
// If the spawn was successful, the caller can use the handle to cancel the task or check its status.
// If the spawn failed, the caller should not use the handle.
// The background executor takes ownership of the future and will destroy it when the task is executed or cancelled or
// in case of spawn failure.
z_result_t _z_background_executor_spawn(_z_background_executor_t *be, _z_fut_t *fut);
z_result_t _z_background_executor_suspend(_z_background_executor_t *be);
z_result_t _z_background_executor_resume(_z_background_executor_t *be);
z_result_t _z_background_executor_destroy(_z_background_executor_t *be);

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __cplusplus
}
#endif
#endif /* Z_FEATURE_MULTI_THREAD */
#endif
