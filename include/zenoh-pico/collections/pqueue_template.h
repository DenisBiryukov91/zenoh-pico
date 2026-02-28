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

// user needs to define the following macros before including this file:
// _ZP_PQUEUE_TEMPLATE_ELEM_TYPE: the type of the elements in the priority queue
// _ZP_PQUEUE_TEMPLATE_NAME: the name of the priority queue type to generate (without the _t suffix)
// _ZP_PQUEUE_TEMPLATE_SIZE: the maximum size of the priority queue (optional, default is 16)
// _ZP_PQUEUE_TEMPLATE_ELEM_DESTROY_FN_NAME: the name of the function to destroy an element (optional, default is a
// no-op) _ZP_PQUEUE_TEMPLATE_ELEM_MOVE_FN_NAME: the name of the function to move an element (optional, default is
// element-wise copy + destroy source) _ZP_PQUEUE_TEMPLATE_ELEM_CMP_FN_NAME: the name of the comparison function
// (elem_a, elem_b) -> int
//   should return <0 if a has higher priority than b, 0 if equal, >0 if b has higher priority than a
//   (i.e. min-priority queue by default: smallest element is at the top)

#include <stdbool.h>
#include <stddef.h>

#include "zenoh-pico/collections/cat.h"

#ifndef _ZP_PQUEUE_TEMPLATE_ELEM_TYPE
#error "_ZP_PQUEUE_TEMPLATE_ELEM_TYPE must be defined before including pqueue_template.h"
#define _ZP_PQUEUE_TEMPLATE_ELEM_TYPE int
#endif
#ifndef _ZP_PQUEUE_TEMPLATE_SIZE
#define _ZP_PQUEUE_TEMPLATE_SIZE 16
#endif
#ifndef _ZP_PQUEUE_TEMPLATE_NAME
#define _ZP_PQUEUE_TEMPLATE_NAME _ZP_CAT(_ZP_CAT(_ZP_PQUEUE_TEMPLATE_ELEM_TYPE, pqueue), _ZP_PQUEUE_TEMPLATE_SIZE)
#endif
#ifndef _ZP_PQUEUE_TEMPLATE_ELEM_CMP_FN_NAME
#error "_ZP_PQUEUE_TEMPLATE_ELEM_CMP_FN_NAME must be defined before including pqueue_template.h"
#endif
#ifndef _ZP_PQUEUE_TEMPLATE_ELEM_DESTROY_FN_NAME
#define _ZP_PQUEUE_TEMPLATE_ELEM_DESTROY_FN_NAME(x) (void)(x)
#endif
#ifndef _ZP_PQUEUE_TEMPLATE_ELEM_MOVE_FN_NAME
#define _ZP_PQUEUE_TEMPLATE_ELEM_MOVE_FN_NAME(dst, src) \
    *(dst) = *(src);                                    \
    _ZP_PQUEUE_TEMPLATE_ELEM_DESTROY_FN_NAME(src);
#endif

#define _ZP_PQUEUE_TEMPLATE_TYPE _ZP_CAT(_ZP_PQUEUE_TEMPLATE_NAME, t)
typedef struct _ZP_PQUEUE_TEMPLATE_TYPE {
    _ZP_PQUEUE_TEMPLATE_ELEM_TYPE _buffer[_ZP_PQUEUE_TEMPLATE_SIZE];
    size_t _size;
} _ZP_PQUEUE_TEMPLATE_TYPE;

static inline _ZP_PQUEUE_TEMPLATE_TYPE _ZP_CAT(_ZP_PQUEUE_TEMPLATE_NAME, new)(void) {
    _ZP_PQUEUE_TEMPLATE_TYPE h = {0};
    return h;
}
static inline void _ZP_CAT(_ZP_PQUEUE_TEMPLATE_NAME, destroy)(_ZP_PQUEUE_TEMPLATE_TYPE *h) {
    for (size_t i = 0; i < h->_size; i++) {
        _ZP_PQUEUE_TEMPLATE_ELEM_DESTROY_FN_NAME(&h->_buffer[i]);
    }
    h->_size = 0;
}
static inline size_t _ZP_CAT(_ZP_PQUEUE_TEMPLATE_NAME, size)(const _ZP_PQUEUE_TEMPLATE_TYPE *h) { return h->_size; }
static inline bool _ZP_CAT(_ZP_PQUEUE_TEMPLATE_NAME, is_empty)(const _ZP_PQUEUE_TEMPLATE_TYPE *h) {
    return h->_size == 0;
}
static inline _ZP_PQUEUE_TEMPLATE_ELEM_TYPE *_ZP_CAT(_ZP_PQUEUE_TEMPLATE_NAME, peek)(_ZP_PQUEUE_TEMPLATE_TYPE *h) {
    if (h->_size == 0) {
        return NULL;
    }
    return &h->_buffer[0];
}
static inline void _ZP_CAT(_ZP_PQUEUE_TEMPLATE_NAME, sift_up)(_ZP_PQUEUE_TEMPLATE_TYPE *h, size_t i) {
    while (i > 0) {
        size_t parent = (i - 1) / 2;
        if (_ZP_PQUEUE_TEMPLATE_ELEM_CMP_FN_NAME(&h->_buffer[i], &h->_buffer[parent]) < 0) {
            _ZP_PQUEUE_TEMPLATE_ELEM_TYPE tmp;
            _ZP_PQUEUE_TEMPLATE_ELEM_MOVE_FN_NAME(&tmp, &h->_buffer[parent]);
            _ZP_PQUEUE_TEMPLATE_ELEM_MOVE_FN_NAME(&h->_buffer[parent], &h->_buffer[i]);
            _ZP_PQUEUE_TEMPLATE_ELEM_MOVE_FN_NAME(&h->_buffer[i], &tmp);
            i = parent;
        } else {
            break;
        }
    }
}
static inline void _ZP_CAT(_ZP_PQUEUE_TEMPLATE_NAME, sift_down)(_ZP_PQUEUE_TEMPLATE_TYPE *h, size_t i) {
    while (true) {
        size_t left = 2 * i + 1;
        size_t right = 2 * i + 2;
        size_t best = i;
        if (left < h->_size && _ZP_PQUEUE_TEMPLATE_ELEM_CMP_FN_NAME(&h->_buffer[left], &h->_buffer[best]) < 0) {
            best = left;
        }
        if (right < h->_size && _ZP_PQUEUE_TEMPLATE_ELEM_CMP_FN_NAME(&h->_buffer[right], &h->_buffer[best]) < 0) {
            best = right;
        }
        if (best == i) {
            break;
        }
        _ZP_PQUEUE_TEMPLATE_ELEM_TYPE tmp;
        _ZP_PQUEUE_TEMPLATE_ELEM_MOVE_FN_NAME(&tmp, &h->_buffer[i]);
        _ZP_PQUEUE_TEMPLATE_ELEM_MOVE_FN_NAME(&h->_buffer[i], &h->_buffer[best]);
        _ZP_PQUEUE_TEMPLATE_ELEM_MOVE_FN_NAME(&h->_buffer[best], &tmp);
        i = best;
    }
}
static inline bool _ZP_CAT(_ZP_PQUEUE_TEMPLATE_NAME, push)(_ZP_PQUEUE_TEMPLATE_TYPE *h,
                                                           _ZP_PQUEUE_TEMPLATE_ELEM_TYPE *elem) {
    if (h->_size == _ZP_PQUEUE_TEMPLATE_SIZE) {
        return false;
    }
    _ZP_PQUEUE_TEMPLATE_ELEM_MOVE_FN_NAME(&h->_buffer[h->_size], elem);
    _ZP_CAT(_ZP_PQUEUE_TEMPLATE_NAME, sift_up)(h, h->_size);
    h->_size++;
    return true;
}
static inline bool _ZP_CAT(_ZP_PQUEUE_TEMPLATE_NAME, pop)(_ZP_PQUEUE_TEMPLATE_TYPE *h,
                                                          _ZP_PQUEUE_TEMPLATE_ELEM_TYPE *out) {
    if (h->_size == 0) {
        return false;
    }
    _ZP_PQUEUE_TEMPLATE_ELEM_MOVE_FN_NAME(out, &h->_buffer[0]);
    h->_size--;
    if (h->_size > 0) {
        _ZP_PQUEUE_TEMPLATE_ELEM_MOVE_FN_NAME(&h->_buffer[0], &h->_buffer[h->_size]);
        _ZP_CAT(_ZP_PQUEUE_TEMPLATE_NAME, sift_down)(h, 0);
    }
    return true;
}

#undef _ZP_PQUEUE_TEMPLATE_ELEM_TYPE
#undef _ZP_PQUEUE_TEMPLATE_NAME
#undef _ZP_PQUEUE_TEMPLATE_SIZE
#undef _ZP_PQUEUE_TEMPLATE_ELEM_DESTROY_FN_NAME
#undef _ZP_PQUEUE_TEMPLATE_ELEM_MOVE_FN_NAME
#undef _ZP_PQUEUE_TEMPLATE_ELEM_CMP_FN_NAME
#undef _ZP_PQUEUE_TEMPLATE_TYPE