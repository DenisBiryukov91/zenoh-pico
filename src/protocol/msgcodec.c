//
// Copyright (c) 2022 ZettaScale Technology
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

#include "zenoh-pico/protocol/msgcodec.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "zenoh-pico/api/constants.h"
#include "zenoh-pico/collections/bytes.h"
#include "zenoh-pico/link/endpoint.h"
#include "zenoh-pico/protocol/core.h"
#include "zenoh-pico/protocol/ext.h"
#include "zenoh-pico/protocol/extcodec.h"
#include "zenoh-pico/protocol/iobuf.h"
#include "zenoh-pico/protocol/keyexpr.h"
#include "zenoh-pico/protocol/msg.h"
#include "zenoh-pico/utils/logging.h"
#include "zenoh-pico/utils/result.h"

/*=============================*/
/*           Fields            */
/*=============================*/
/*------------------ Payload field ------------------*/
int8_t _z_payload_encode(_z_wbuf_t *wbf, const _z_payload_t *pld) {
    int8_t ret = _Z_RES_OK;
    _Z_DEBUG("Encoding _PAYLOAD\n");
    ret |= _z_bytes_encode(wbf, pld);

    return ret;
}

int8_t _z_payload_decode_na(_z_payload_t *pld, _z_zbuf_t *zbf) {
    _Z_DEBUG("Decoding _PAYLOAD\n");
    return _z_bytes_decode(pld, zbf);
}

int8_t _z_payload_decode(_z_payload_t *pld, _z_zbuf_t *zbf) { return _z_payload_decode_na(pld, zbf); }

int8_t _z_id_encode_as_zbytes(_z_wbuf_t *wbf, const _z_id_t *id) {
    int8_t ret = _Z_RES_OK;
    uint8_t len = _z_id_len(*id);

    if (id->id[len] != 0) {
        ret |= _z_wbuf_write(wbf, len);
        ret |= _z_wbuf_write_bytes(wbf, id->id, 0, len);
    } else {
        _Z_DEBUG("Attempted to encode invalid ID 0");
        ret = _Z_ERR_MESSAGE_ZENOH_UNKNOWN;
    }
    return ret;
}

/// Decodes a `zid` from the zbf, returning a negative value in case of error.
///
/// Note that while `_z_id_t` has an error state (full 0s), this function doesn't
/// guarantee that this state will be set in case of errors.
int8_t _z_id_decode_as_zbytes(_z_id_t *id, _z_zbuf_t *zbf) {
    int8_t ret = _Z_RES_OK;
    uint8_t len = _z_zbuf_read(zbf);
    _z_zbuf_read_bytes(zbf, id->id, 0, len);
    memset(id->id + len, 0, 16 - len);
    return ret;
}

/*------------------ Timestamp Field ------------------*/
int8_t _z_timestamp_encode(_z_wbuf_t *wbf, const _z_timestamp_t *ts) {
    int8_t ret = _Z_RES_OK;
    _Z_DEBUG("Encoding _TIMESTAMP\n");

    _Z_EC(_z_uint64_encode(wbf, ts->time))
    ret |= _z_id_encode_as_zbytes(wbf, &ts->id);

    return ret;
}

int8_t _z_timestamp_decode_na(_z_timestamp_t *ts, _z_zbuf_t *zbf) {
    _Z_DEBUG("Decoding _TIMESTAMP\n");
    int8_t ret = _Z_RES_OK;

    ret |= _z_uint64_decode(&ts->time, zbf);
    ret |= _z_id_decode_as_zbytes(&ts->id, zbf);

    return ret;
}

int8_t _z_timestamp_decode(_z_timestamp_t *ts, _z_zbuf_t *zbf) { return _z_timestamp_decode_na(ts, zbf); }

/*------------------ SubMode Field ------------------*/
int8_t _z_subinfo_encode(_z_wbuf_t *wbf, const _z_subinfo_t *fld) {
    int8_t ret = _Z_RES_OK;
    _Z_DEBUG("Encoding _SUB_MODE\n");

    uint8_t header = fld->mode;
    if ((fld->period.origin != 0) || (fld->period.period != 0) || (fld->period.duration != 0)) {
        _Z_SET_FLAG(header, _Z_FLAG_Z_P);
        _Z_EC(_z_wbuf_write(wbf, header))
        ret |= _z_period_encode(wbf, &fld->period);
    } else {
        _Z_EC(_z_wbuf_write(wbf, header))
    }

    return ret;
}

int8_t _z_subinfo_decode_na(_z_subinfo_t *si, _z_zbuf_t *zbf, uint8_t header) {
    _Z_DEBUG("Decoding _SUB_MODE\n");
    int8_t ret = _Z_RES_OK;

    // Decode the header
    if (_Z_HAS_FLAG(header, _Z_FLAG_Z_R) == true) {
        si->reliability = Z_RELIABILITY_RELIABLE;
    } else {
        si->reliability = Z_RELIABILITY_BEST_EFFORT;
    }

    uint8_t h_subifo;
    ret |= _z_uint8_decode(&h_subifo, zbf);
    si->mode = _Z_MID(h_subifo);
    if (_Z_HAS_FLAG(h_subifo, _Z_FLAG_Z_P) == true) {
        ret |= _z_period_decode(&si->period, zbf);
    } else {
        si->period = (_z_period_t){.origin = 0, .period = 0, .duration = 0};
    }

    return ret;
}

int8_t _z_subinfo_decode(_z_subinfo_t *si, _z_zbuf_t *zbf, uint8_t header) {
    return _z_subinfo_decode_na(si, zbf, header);
}

/*------------------ ResKey Field ------------------*/
int8_t _z_keyexpr_encode(_z_wbuf_t *wbf, _Bool has_suffix, const _z_keyexpr_t *fld) {
    int8_t ret = _Z_RES_OK;
    _Z_DEBUG("Encoding _RESKEY\n");

    _Z_EC(_z_zint_encode(wbf, fld->_id))
    if (has_suffix == true) {
        _Z_EC(_z_str_encode(wbf, fld->_suffix))
    }

    return ret;
}

int8_t _z_keyexpr_decode_na(_z_keyexpr_t *ke, _z_zbuf_t *zbf, _Bool has_suffix) {
    _Z_DEBUG("Decoding _RESKEY\n");
    int8_t ret = _Z_RES_OK;

    ret |= _z_zint_decode(&ke->_id, zbf);
    if (has_suffix == true) {
        char *str = NULL;
        ret |= _z_str_decode(&str, zbf);
        if (ret == _Z_RES_OK) {
            ke->_suffix = str;
        } else {
            ke->_suffix = NULL;
        }
    } else {
        ke->_suffix = NULL;
    }

    return ret;
}

int8_t _z_keyexpr_decode(_z_keyexpr_t *ke, _z_zbuf_t *zbf, uint8_t header) {
    return _z_keyexpr_decode_na(ke, zbf, header);
}

/*------------------ Locators Field ------------------*/
int8_t _z_locators_encode(_z_wbuf_t *wbf, const _z_locator_array_t *la) {
    int8_t ret = _Z_RES_OK;
    _Z_DEBUG("Encoding _LOCATORS\n");
    _Z_EC(_z_zint_encode(wbf, la->_len))
    for (size_t i = 0; i < la->_len; i++) {
        char *s = _z_locator_to_str(&la->_val[i]);
        _Z_EC(_z_str_encode(wbf, s))
        z_free(s);
    }

    return ret;
}

int8_t _z_locators_decode_na(_z_locator_array_t *a_loc, _z_zbuf_t *zbf) {
    _Z_DEBUG("Decoding _LOCATORS\n");
    int8_t ret = _Z_RES_OK;

    _z_zint_t len = 0;  // Number of elements in the array
    ret |= _z_zint_decode(&len, zbf);
    if (ret == _Z_RES_OK) {
        *a_loc = _z_locator_array_make(len);

        // Decode the elements
        for (size_t i = 0; i < len; i++) {
            char *str = NULL;
            ret |= _z_str_decode(&str, zbf);
            if (ret == _Z_RES_OK) {
                _z_locator_init(&a_loc->_val[i]);
                ret |= _z_locator_from_str(&a_loc->_val[i], str);
                z_free(str);
            } else {
                a_loc->_len = i;
            }
        }
    } else {
        *a_loc = _z_locator_array_make(0);
    }

    return ret;
}

int8_t _z_locators_decode(_z_locator_array_t *a_loc, _z_zbuf_t *zbf) { return _z_locators_decode_na(a_loc, zbf); }

/*=============================*/
/*        Zenoh Messages       */
/*=============================*/
/*------------------ Resource Declaration ------------------*/
int8_t _z_res_decl_encode(_z_wbuf_t *wbf, uint8_t header, const _z_res_decl_t *dcl) {
    int8_t ret = _Z_RES_OK;
    _Z_DEBUG("Encoding _Z_DECL_RESOURCE\n");

    _Z_EC(_z_zint_encode(wbf, dcl->_id))
    ret |= _z_keyexpr_encode(wbf, _Z_HAS_FLAG(header, _Z_FLAG_Z_K), &dcl->_key);

    return ret;
}

int8_t _z_res_decl_decode_na(_z_res_decl_t *dcl, _z_zbuf_t *zbf, uint8_t header) {
    _Z_DEBUG("Decoding _Z_DECL_RESOURCE\n");
    int8_t ret = _Z_RES_OK;

    ret |= _z_zint_decode(&dcl->_id, zbf);
    ret |= _z_keyexpr_decode(&dcl->_key, zbf, _Z_HAS_FLAG(header, _Z_FLAG_Z_K));

    return ret;
}

int8_t _z_res_decl_decode(_z_res_decl_t *dcl, _z_zbuf_t *zbf, uint8_t header) {
    return _z_res_decl_decode_na(dcl, zbf, header);
}

/*------------------ Publisher Declaration ------------------*/
int8_t _z_pub_decl_encode(_z_wbuf_t *wbf, uint8_t header, const _z_pub_decl_t *dcl) {
    int8_t ret = _Z_RES_OK;
    _Z_DEBUG("Encoding _Z_DECL_PUBLISHER\n");

    ret |= _z_keyexpr_encode(wbf, _Z_HAS_FLAG(header, _Z_FLAG_Z_K), &dcl->_key);

    return ret;
}

int8_t _z_pub_decl_decode_na(_z_pub_decl_t *dcl, _z_zbuf_t *zbf, uint8_t header) {
    _Z_DEBUG("Decoding _Z_DECL_PUBLISHER\n");
    int8_t ret = _Z_RES_OK;

    ret |= _z_keyexpr_decode(&dcl->_key, zbf, _Z_HAS_FLAG(header, _Z_FLAG_Z_K));

    return ret;
}

int8_t _z_pub_decl_decode(_z_pub_decl_t *dcl, _z_zbuf_t *zbf, uint8_t header) {
    return _z_pub_decl_decode_na(dcl, zbf, header);
}

/*------------------ Subscriber Declaration ------------------*/
int8_t _z_sub_decl_encode(_z_wbuf_t *wbf, uint8_t header, const _z_sub_decl_t *dcl) {
    int8_t ret = _Z_RES_OK;
    _Z_DEBUG("Encoding _Z_DECL_SUBSCRIBER\n");

    _Z_EC(_z_keyexpr_encode(wbf, _Z_HAS_FLAG(header, _Z_FLAG_Z_K), &dcl->_key))
    if (_Z_HAS_FLAG(header, _Z_FLAG_Z_S) == true) {
        ret |= _z_subinfo_encode(wbf, &dcl->_subinfo);
    }

    return ret;
}

int8_t _z_sub_decl_decode_na(_z_sub_decl_t *dcl, _z_zbuf_t *zbf, uint8_t header) {
    _Z_DEBUG("Decoding _Z_DECL_SUBSCRIBER\n");
    int8_t ret = _Z_RES_OK;

    ret |= _z_keyexpr_decode(&dcl->_key, zbf, _Z_HAS_FLAG(header, _Z_FLAG_Z_K));
    if (_Z_HAS_FLAG(header, _Z_FLAG_Z_S) == true) {
        ret |= _z_subinfo_decode(&dcl->_subinfo, zbf, header);
    } else {
        dcl->_subinfo.mode = Z_SUBMODE_PUSH;  // Default subscription mode is non-periodic PUSH
        dcl->_subinfo.period = (_z_period_t){.origin = 0, .period = 0, .duration = 0};
        if (_Z_HAS_FLAG(header, _Z_FLAG_Z_R) == true) {
            dcl->_subinfo.reliability = Z_RELIABILITY_RELIABLE;
        } else {
            dcl->_subinfo.reliability = Z_RELIABILITY_BEST_EFFORT;
        }
    }

    return ret;
}

int8_t _z_sub_decl_decode(_z_sub_decl_t *dcl, _z_zbuf_t *zbf, uint8_t header) {
    return _z_sub_decl_decode_na(dcl, zbf, header);
}

/*------------------ Queryable Declaration ------------------*/
int8_t _z_qle_decl_encode(_z_wbuf_t *wbf, uint8_t header, const _z_qle_decl_t *dcl) {
    int8_t ret = _Z_RES_OK;
    _Z_DEBUG("Encoding _Z_DECL_QUERYABLE\n");

    _Z_EC(_z_keyexpr_encode(wbf, _Z_HAS_FLAG(header, _Z_FLAG_Z_K), &dcl->_key));

    if (_Z_HAS_FLAG(header, _Z_FLAG_Z_Q) == true) {
        _Z_EC(_z_zint_encode(wbf, dcl->_complete));
        _Z_EC(_z_zint_encode(wbf, dcl->_distance));
    }

    return ret;
}

int8_t _z_qle_decl_decode_na(_z_qle_decl_t *dcl, _z_zbuf_t *zbf, uint8_t header) {
    _Z_DEBUG("Decoding _Z_DECL_QUERYABLE\n");
    int8_t ret = _Z_RES_OK;

    ret |= _z_keyexpr_decode(&dcl->_key, zbf, _Z_HAS_FLAG(header, _Z_FLAG_Z_K));
    if (_Z_HAS_FLAG(header, _Z_FLAG_Z_Q) == true) {
        ret |= _z_zint_decode(&dcl->_complete, zbf);
        ret |= _z_zint_decode(&dcl->_distance, zbf);
    } else {
        dcl->_complete = 0;
        dcl->_distance = 0;
    }

    return ret;
}

int8_t _z_qle_decl_decode(_z_qle_decl_t *dcl, _z_zbuf_t *zbf, uint8_t header) {
    return _z_qle_decl_decode_na(dcl, zbf, header);
}

/*------------------ Forget Resource Declaration ------------------*/
int8_t _z_forget_res_decl_encode(_z_wbuf_t *wbf, const _z_forget_res_decl_t *dcl) {
    int8_t ret = _Z_RES_OK;
    _Z_DEBUG("Encoding _Z_DECL_FORGET_RESOURCE\n");

    ret |= _z_zint_encode(wbf, dcl->_rid);

    return ret;
}

int8_t _z_forget_res_decl_decode_na(_z_forget_res_decl_t *dcl, _z_zbuf_t *zbf) {
    _Z_DEBUG("Decoding _Z_DECL_FORGET_RESOURCE\n");
    int8_t ret = _Z_RES_OK;

    ret |= _z_zint_decode(&dcl->_rid, zbf);

    return ret;
}

int8_t _z_forget_res_decl_decode(_z_forget_res_decl_t *dcl, _z_zbuf_t *zbf) {
    return _z_forget_res_decl_decode_na(dcl, zbf);
}

/*------------------ Forget Publisher Declaration ------------------*/
int8_t _z_forget_pub_decl_encode(_z_wbuf_t *wbf, uint8_t header, const _z_forget_pub_decl_t *dcl) {
    int8_t ret = _Z_RES_OK;
    _Z_DEBUG("Encoding _Z_DECL_FORGET_PUBLISHER\n");

    ret |= _z_keyexpr_encode(wbf, _Z_HAS_FLAG(header, _Z_FLAG_Z_K), &dcl->_key);

    return ret;
}

int8_t _z_forget_pub_decl_decode_na(_z_forget_pub_decl_t *dcl, _z_zbuf_t *zbf, uint8_t header) {
    _Z_DEBUG("Decoding _Z_DECL_FORGET_PUBLISHER\n");
    int8_t ret = _Z_RES_OK;

    ret |= _z_keyexpr_decode(&dcl->_key, zbf, _Z_HAS_FLAG(header, _Z_FLAG_Z_K));

    return ret;
}

int8_t _z_forget_pub_decl_decode(_z_forget_pub_decl_t *dcl, _z_zbuf_t *zbf, uint8_t header) {
    return _z_forget_pub_decl_decode_na(dcl, zbf, header);
}

/*------------------ Forget Subscriber Declaration ------------------*/
int8_t _z_forget_sub_decl_encode(_z_wbuf_t *wbf, uint8_t header, const _z_forget_sub_decl_t *dcl) {
    _Z_DEBUG("Encoding _Z_DECL_FORGET_PUBLISHER\n");
    int8_t ret = _Z_RES_OK;

    ret |= _z_keyexpr_encode(wbf, _Z_HAS_FLAG(header, _Z_FLAG_Z_K), &dcl->_key);

    return ret;
}

int8_t _z_forget_sub_decl_decode_na(_z_forget_sub_decl_t *dcl, _z_zbuf_t *zbf, uint8_t header) {
    _Z_DEBUG("Decoding _Z_DECL_FORGET_PUBLISHER\n");
    int8_t ret = _Z_RES_OK;

    ret |= _z_keyexpr_decode(&dcl->_key, zbf, _Z_HAS_FLAG(header, _Z_FLAG_Z_K));

    return ret;
}

int8_t _z_forget_sub_decl_decode(_z_forget_sub_decl_t *dcl, _z_zbuf_t *zbf, uint8_t header) {
    return _z_forget_sub_decl_decode_na(dcl, zbf, header);
}

/*------------------ Forget Queryable Declaration ------------------*/
int8_t _z_forget_qle_decl_encode(_z_wbuf_t *wbf, uint8_t header, const _z_forget_qle_decl_t *dcl) {
    int8_t ret = _Z_RES_OK;
    _Z_DEBUG("Encoding _Z_DECL_FORGET_QUERYABLE\n");

    ret |= _z_keyexpr_encode(wbf, _Z_HAS_FLAG(header, _Z_FLAG_Z_K), &dcl->_key);

    return ret;
}

int8_t _z_forget_qle_decl_decode_na(_z_forget_qle_decl_t *dcl, _z_zbuf_t *zbf, uint8_t header) {
    _Z_DEBUG("Decoding _Z_DECL_FORGET_QUERYABLE\n");
    int8_t ret = _Z_RES_OK;

    ret |= _z_keyexpr_decode(&dcl->_key, zbf, _Z_HAS_FLAG(header, _Z_FLAG_Z_K));

    return ret;
}

int8_t _z_forget_qle_decl_decode(_z_forget_qle_decl_t *dcl, _z_zbuf_t *zbf, uint8_t header) {
    return _z_forget_qle_decl_decode_na(dcl, zbf, header);
}

/*------------------ Declaration Field ------------------*/
int8_t _z_declaration_encode(_z_wbuf_t *wbf, const _z_declaration_t *dcl) {
    int8_t ret = _Z_RES_OK;

    _Z_EC(_z_wbuf_write(wbf, dcl->_header))
    uint8_t did = _Z_MID(dcl->_header);
    switch (did) {
        case _Z_DECL_RESOURCE: {
            ret |= _z_res_decl_encode(wbf, dcl->_header, &dcl->_body._res);
        } break;

        case _Z_DECL_PUBLISHER: {
            ret |= _z_pub_decl_encode(wbf, dcl->_header, &dcl->_body._pub);
        } break;

        case _Z_DECL_SUBSCRIBER: {
            ret |= _z_sub_decl_encode(wbf, dcl->_header, &dcl->_body._sub);
        } break;

        case _Z_DECL_QUERYABLE: {
            ret |= _z_qle_decl_encode(wbf, dcl->_header, &dcl->_body._qle);
        } break;

        case _Z_DECL_FORGET_RESOURCE: {
            ret |= _z_forget_res_decl_encode(wbf, &dcl->_body._forget_res);
        } break;

        case _Z_DECL_FORGET_PUBLISHER: {
            ret |= _z_forget_pub_decl_encode(wbf, dcl->_header, &dcl->_body._forget_pub);
        } break;

        case _Z_DECL_FORGET_SUBSCRIBER: {
            ret |= _z_forget_sub_decl_encode(wbf, dcl->_header, &dcl->_body._forget_sub);
        } break;

        case _Z_DECL_FORGET_QUERYABLE: {
            ret |= _z_forget_qle_decl_encode(wbf, dcl->_header, &dcl->_body._forget_qle);
        } break;

        default: {
            _Z_DEBUG("WARNING: Trying to encode declaration with unknown ID(%d)\n", did);
            ret |= _Z_ERR_MESSAGE_SERIALIZATION_FAILED;
        } break;
    }

    return ret;
}

int8_t _z_declaration_decode_na(_z_declaration_t *decl, _z_zbuf_t *zbf) {
    int8_t ret = _Z_RES_OK;

    ret |= _z_uint8_decode(&decl->_header, zbf);
    if (ret == _Z_RES_OK) {
        uint8_t mid = _Z_MID(decl->_header);

        switch (mid) {
            case _Z_DECL_RESOURCE: {
                ret |= _z_res_decl_decode(&decl->_body._res, zbf, decl->_header);
            } break;

            case _Z_DECL_PUBLISHER: {
                ret |= _z_pub_decl_decode(&decl->_body._pub, zbf, decl->_header);
            } break;

            case _Z_DECL_SUBSCRIBER: {
                ret |= _z_sub_decl_decode(&decl->_body._sub, zbf, decl->_header);
            } break;

            case _Z_DECL_QUERYABLE: {
                ret |= _z_qle_decl_decode(&decl->_body._qle, zbf, decl->_header);
            } break;

            case _Z_DECL_FORGET_RESOURCE: {
                ret |= _z_forget_res_decl_decode(&decl->_body._forget_res, zbf);
            } break;

            case _Z_DECL_FORGET_PUBLISHER: {
                ret |= _z_forget_pub_decl_decode(&decl->_body._forget_pub, zbf, decl->_header);
            } break;

            case _Z_DECL_FORGET_SUBSCRIBER: {
                ret |= _z_forget_sub_decl_decode(&decl->_body._forget_sub, zbf, decl->_header);
            } break;

            case _Z_DECL_FORGET_QUERYABLE: {
                ret |= _z_forget_qle_decl_decode(&decl->_body._forget_qle, zbf, decl->_header);
            } break;

            default: {
                _Z_DEBUG("WARNING: Trying to decode declaration with unknown ID(%d)\n", mid);
                ret |= _Z_ERR_MESSAGE_DESERIALIZATION_FAILED;
            } break;
        }
    }

    return ret;
}

int8_t _z_declaration_decode(_z_declaration_t *decl, _z_zbuf_t *zbf) { return _z_declaration_decode_na(decl, zbf); }

/*------------------ Data Info Field ------------------*/
int8_t _z_data_info_encode(_z_wbuf_t *wbf, const _z_data_info_t *fld) {
    int8_t ret = _Z_RES_OK;
    _Z_DEBUG("Encoding _Z_DATA_INFO\n");

    // Encode the flags
    // WARNING: we do not support sliced content in zenoh-pico.
    //          Disable the SLICED flag to be on the safe side.
    _Z_EC(_z_zint_encode(wbf, fld->_flags & ~_Z_DATA_INFO_SLICED))

    if (_Z_HAS_FLAG(fld->_flags, _Z_DATA_INFO_KIND) == true) {
        _Z_EC(_z_uint8_encode(wbf, fld->_kind))
    }
    if (_Z_HAS_FLAG(fld->_flags, _Z_DATA_INFO_ENC) == true) {
        _Z_EC(_z_encoding_prefix_encode(wbf, fld->_encoding.prefix))
        _Z_EC(_z_bytes_encode(wbf, &fld->_encoding.suffix))
    }
    if (_Z_HAS_FLAG(fld->_flags, _Z_DATA_INFO_TSTAMP) == true) {
        _Z_EC(_z_timestamp_encode(wbf, &fld->_tstamp))
    }
    if (_Z_HAS_FLAG(fld->_flags, _Z_DATA_INFO_SRC_ID) == true) {
        _Z_EC(_z_bytes_encode(wbf, &fld->_source_id))
    }
    if (_Z_HAS_FLAG(fld->_flags, _Z_DATA_INFO_SRC_SN) == true) {
        _Z_EC(_z_zint_encode(wbf, fld->_source_sn))
    }

    return ret;
}

int8_t _z_data_info_decode_na(_z_data_info_t *di, _z_zbuf_t *zbf) {
    _Z_DEBUG("Decoding _Z_DATA_INFO\n");
    int8_t ret = _Z_RES_OK;

    ret |= _z_zint_decode(&di->_flags, zbf);
    // WARNING: we do not support sliced content in zenoh-pico.
    //          Return error in case the payload is sliced.
    if ((ret == _Z_RES_OK) && _Z_HAS_FLAG(di->_flags, _Z_DATA_INFO_SLICED) == false) {
        if (_Z_HAS_FLAG(di->_flags, _Z_DATA_INFO_KIND) == true) {
            ret |= _z_uint8_decode(&di->_kind, zbf);
        } else {
            di->_kind = Z_SAMPLE_KIND_PUT;
        }

        if (_Z_HAS_FLAG(di->_flags, _Z_DATA_INFO_ENC) == true) {
            ret |= _z_encoding_prefix_decode(&di->_encoding.prefix, zbf);
            ret |= _z_bytes_decode(&di->_encoding.suffix, zbf);
        } else {
            di->_encoding.prefix = Z_ENCODING_PREFIX_EMPTY;
            di->_encoding.suffix = _z_bytes_empty();
        }

        if (_Z_HAS_FLAG(di->_flags, _Z_DATA_INFO_TSTAMP) == true) {
            ret |= _z_timestamp_decode(&di->_tstamp, zbf);
        } else {
            _z_timestamp_reset(&di->_tstamp);
        }

        if (_Z_HAS_FLAG(di->_flags, _Z_DATA_INFO_SRC_ID) == true) {
            ret |= _z_bytes_decode(&di->_source_id, zbf);
        } else {
            di->_source_id = _z_bytes_empty();
        }

        if (_Z_HAS_FLAG(di->_flags, _Z_DATA_INFO_SRC_SN) == true) {
            ret |= _z_zint_decode(&di->_source_sn, zbf);
        } else {
            di->_source_sn = 0;
        }
    }

    return ret;
}

int8_t _z_data_info_decode(_z_data_info_t *di, _z_zbuf_t *zbf) { return _z_data_info_decode_na(di, zbf); }

/*------------------ Data Message ------------------*/
int8_t _z_data_encode(_z_wbuf_t *wbf, uint8_t header, const _z_msg_data_t *msg) {
    int8_t ret = _Z_RES_OK;
    _Z_DEBUG("Encoding _Z_MID_Z_DATA\n");

    _Z_EC(_z_keyexpr_encode(wbf, _Z_HAS_FLAG(header, _Z_FLAG_Z_K), &msg->_key))

    if (_Z_HAS_FLAG(header, _Z_FLAG_Z_I) == true) {
        _Z_EC(_z_data_info_encode(wbf, &msg->_info))
    }
    _Z_EC(_z_payload_encode(wbf, &msg->_payload))

    return ret;
}

int8_t _z_data_decode_na(_z_msg_data_t *msg, _z_zbuf_t *zbf, uint8_t header) {
    _Z_DEBUG("Decoding _Z_MID_Z_DATA\n");
    int8_t ret = _Z_RES_OK;

    ret |= _z_keyexpr_decode(&msg->_key, zbf, _Z_HAS_FLAG(header, _Z_FLAG_Z_K));
    if (_Z_HAS_FLAG(header, _Z_FLAG_Z_I) == true) {
        ret |= _z_data_info_decode(&msg->_info, zbf);
    } else {
        msg->_info._flags = 0;
        msg->_info._kind = Z_SAMPLE_KIND_PUT;
        msg->_info._encoding.prefix = Z_ENCODING_PREFIX_EMPTY;
        msg->_info._encoding.suffix = _z_bytes_empty();
        msg->_info._source_id = _z_bytes_empty();
        msg->_info._source_sn = 0;
        _z_timestamp_reset(&msg->_info._tstamp);
    }
    ret |= _z_payload_decode(&msg->_payload, zbf);

    return ret;
}

int8_t _z_data_decode(_z_msg_data_t *msg, _z_zbuf_t *zbf, uint8_t header) {
    return _z_data_decode_na(msg, zbf, header);
}

/*------------------ Pull Message ------------------*/
int8_t _z_pull_encode(_z_wbuf_t *wbf, uint8_t header, const _z_msg_pull_t *msg) {
    int8_t ret = _Z_RES_OK;
    _Z_DEBUG("Encoding _Z_MID_Z_PULL\n");

    _Z_EC(_z_keyexpr_encode(wbf, _Z_HAS_FLAG(header, _Z_FLAG_Z_K), &msg->_key))

    _Z_EC(_z_zint_encode(wbf, msg->_pull_id))

    if (_Z_HAS_FLAG(header, _Z_FLAG_Z_N) == true) {
        _Z_EC(_z_zint_encode(wbf, msg->_max_samples))
    }

    return ret;
}

int8_t _z_pull_decode_na(_z_msg_pull_t *msg, _z_zbuf_t *zbf, uint8_t header) {
    _Z_DEBUG("Decoding _Z_MID_Z_PULL\n");
    int8_t ret = _Z_RES_OK;

    ret |= _z_keyexpr_decode(&msg->_key, zbf, _Z_HAS_FLAG(header, _Z_FLAG_Z_K));
    ret |= _z_zint_decode(&msg->_pull_id, zbf);
    if (_Z_HAS_FLAG(header, _Z_FLAG_Z_N) == true) {
        ret |= _z_zint_decode(&msg->_max_samples, zbf);
    } else {
        msg->_max_samples = 1;  // FIXME: confirm default value
    }

    return ret;
}

int8_t _z_pull_decode(_z_msg_pull_t *msg, _z_zbuf_t *zbf, uint8_t header) {
    return _z_pull_decode_na(msg, zbf, header);
}

/*------------------ Query Message ------------------*/
int8_t _z_query_encode(_z_wbuf_t *wbf, uint8_t header, const _z_msg_query_t *msg) {
    int8_t ret = _Z_RES_OK;
    _Z_DEBUG("Encoding _Z_MID_Z_QUERY\n");

    _Z_EC(_z_keyexpr_encode(wbf, _Z_HAS_FLAG(header, _Z_FLAG_Z_K), &msg->_key))

    _Z_EC(_z_str_encode(wbf, msg->_parameters))

    _Z_EC(_z_zint_encode(wbf, msg->_qid))

    if (_Z_HAS_FLAG(header, _Z_FLAG_Z_T) == true) {
        _Z_EC(_z_query_target_encode(wbf, msg->_target))
    }
    ret |= _z_consolidation_mode_encode(wbf, msg->_consolidation);

    if (_Z_HAS_FLAG(header, _Z_FLAG_Z_B) == true) {
        _Z_EC(_z_data_info_encode(wbf, &msg->_info))
        _Z_EC(_z_payload_encode(wbf, &msg->_payload))
    }

    return ret;
}

int8_t _z_query_decode_na(_z_msg_query_t *msg, _z_zbuf_t *zbf, uint8_t header) {
    _Z_DEBUG("Decoding _Z_MID_Z_QUERY\n");
    int8_t ret = _Z_RES_OK;

    ret |= _z_keyexpr_decode(&msg->_key, zbf, _Z_HAS_FLAG(header, _Z_FLAG_Z_K));
    ret |= _z_str_decode(&msg->_parameters, zbf);
    ret |= _z_zint_decode(&msg->_qid, zbf);
    if (_Z_HAS_FLAG(header, _Z_FLAG_Z_T) == true) {
        ret |= _z_query_target_decode(&msg->_target, zbf);
    } else {
        msg->_target = Z_QUERY_TARGET_BEST_MATCHING;
    }
    ret |= _z_consolidation_mode_decode(&msg->_consolidation, zbf);
    if (_Z_HAS_FLAG(header, _Z_FLAG_Z_I) == true) {
        ret |= _z_data_info_decode(&msg->_info, zbf);
        ret |= _z_payload_decode(&msg->_payload, zbf);
    } else {
        msg->_info._flags = 0;
        msg->_info._kind = Z_SAMPLE_KIND_PUT;
        msg->_info._encoding.prefix = Z_ENCODING_PREFIX_EMPTY;
        msg->_info._encoding.suffix = _z_bytes_empty();
        msg->_info._source_id = _z_bytes_empty();
        msg->_info._source_sn = 0;
        _z_timestamp_reset(&msg->_info._tstamp);
        msg->_payload = _z_bytes_empty();
    }

    return ret;
}

int8_t _z_query_decode(_z_msg_query_t *msg, _z_zbuf_t *zbf, uint8_t header) {
    return _z_query_decode_na(msg, zbf, header);
}

/*------------------ Zenoh Message ------------------*/
int8_t _z_zenoh_message_encode(_z_wbuf_t *wbf, const _z_zenoh_message_t *msg) {
    int8_t ret = _Z_RES_OK;

    _Z_EC(_z_wbuf_write(wbf, msg->_header))

    uint8_t mid = _Z_MID(msg->_header);
    switch (mid) {
        case _Z_MID_Z_DATA: {
            ret |= _z_data_encode(wbf, msg->_header, &msg->_body._data);
        } break;

        case _Z_MID_Z_QUERY: {
            ret |= _z_query_encode(wbf, msg->_header, &msg->_body._query);
        } break;

        case _Z_MID_Z_PULL: {
            ret |= _z_pull_encode(wbf, msg->_header, &msg->_body._pull);
        } break;

        case _Z_MID_Z_UNIT: {
            // Do nothing. Unit messages have no body
        } break;

        default: {
            _Z_DEBUG("WARNING: Trying to encode message with unknown ID(%d)\n", mid);
            ret |= _Z_ERR_MESSAGE_ZENOH_UNKNOWN;
        } break;
    }

    return ret;
}

int8_t _z_zenoh_message_decode_na(_z_zenoh_message_t *msg, _z_zbuf_t *zbf) {
    int8_t ret = _Z_RES_OK;

    _Bool is_last = false;
    do {
        ret |= _z_uint8_decode(&msg->_header, zbf);
        if (ret == _Z_RES_OK) {
            uint8_t mid = _Z_MID(msg->_header);
            switch (mid) {
                case _Z_MID_Z_DATA: {
                    ret |= _z_data_decode(&msg->_body._data, zbf, msg->_header);
                    is_last = true;
                } break;

                case _Z_MID_Z_QUERY: {
                    ret |= _z_query_decode(&msg->_body._query, zbf, msg->_header);
                    is_last = true;
                } break;

                case _Z_MID_Z_PULL: {
                    ret |= _z_pull_decode(&msg->_body._pull, zbf, msg->_header);
                    is_last = true;
                } break;

                case _Z_MID_Z_UNIT: {
                    // Do nothing. Unit messages have no body.
                    is_last = true;
                } break;

                case _Z_MID_Z_LINK_STATE_LIST: {
                    _Z_DEBUG("WARNING: Link state not supported in zenoh-pico\n");
                    is_last = true;
                } break;

                default: {
                    _Z_DEBUG("WARNING: Trying to decode zenoh message with unknown ID(%d)\n", mid);
                    ret |= _Z_ERR_MESSAGE_ZENOH_UNKNOWN;
                } break;
            }
        } else {
            msg->_header = 0xFF;
        }
    } while ((ret == _Z_RES_OK) && (is_last == false));

    return ret;
}

int8_t _z_zenoh_message_decode(_z_zenoh_message_t *msg, _z_zbuf_t *zbf) { return _z_zenoh_message_decode_na(msg, zbf); }

/*------------------ Declaration Message ------------------*/
int8_t _z_declare_encode(_z_wbuf_t *wbf, const _z_n_msg_declare_t *msg) {
    int8_t ret = _Z_RES_OK;
    _Z_DEBUG("Encoding _Z_MID_N_DECLARE\n");

    _Z_EC(_z_declaration_encode(wbf, &msg->_declaration));

    return ret;
}

int8_t _z_declare_decode_na(_z_n_msg_declare_t *msg, _z_zbuf_t *zbf) {
    _Z_DEBUG("Decoding _Z_MID_N_DECLARE\n");
    int8_t ret = _Z_RES_OK;

    ret |= _z_declaration_decode_na(&msg->_declaration, zbf);
    if (ret != _Z_RES_OK) {
        // TODO[protocol]: Add empty declaration
    }

    return ret;
}

int8_t _z_declare_decode(_z_n_msg_declare_t *msg, _z_zbuf_t *zbf) { return _z_declare_decode_na(msg, zbf); }

/*------------------ Push Body Field ------------------*/
int8_t _z_push_body_encode(_z_wbuf_t *wbf, const _z_push_body_t *pshb) {
    (void)(wbf);
    (void)(pshb);
    int8_t ret = _Z_RES_OK;

    return ret;
}

int8_t _z_push_body_decode_na(_z_push_body_t *pshb, _z_zbuf_t *zbf) {
    (void)(zbf);
    (void)(pshb);
    int8_t ret = _Z_RES_OK;

    return ret;
}

int8_t _z_push_body_decode(_z_push_body_t *pshb, _z_zbuf_t *zbf) { return _z_push_body_decode_na(pshb, zbf); }

/*------------------ Push Message ------------------*/
int8_t _z_push_encode(_z_wbuf_t *wbf, uint8_t header, const _z_n_msg_push_t *msg) {
    int8_t ret = _Z_RES_OK;
    _Z_DEBUG("Encoding _Z_MID_N_PUSH\n");

    _Z_EC(_z_keyexpr_encode(wbf, _Z_HAS_FLAG(header, _Z_FLAG_N_PUSH_N), &msg->_key));
    _Z_EC(_z_push_body_encode(wbf, &msg->_body));

    return ret;
}

int8_t _z_push_decode_na(_z_n_msg_push_t *msg, _z_zbuf_t *zbf, uint8_t header) {
    _Z_DEBUG("Decoding _Z_MID_N_PUSH\n");
    int8_t ret = _Z_RES_OK;

    ret |= _z_keyexpr_decode(&msg->_key, zbf, _Z_HAS_FLAG(header, _Z_FLAG_N_PUSH_N));
    ret |= _z_push_body_decode(&msg->_body, zbf);

    return ret;
}

int8_t _z_push_decode(_z_n_msg_push_t *msg, _z_zbuf_t *zbf, uint8_t header) {
    return _z_push_decode_na(msg, zbf, header);
}

/*------------------ Request Body Field ------------------*/
int8_t _z_request_body_encode(_z_wbuf_t *wbf, const _z_request_body_t *reqb) {
    (void)(wbf);
    (void)(reqb);
    int8_t ret = _Z_RES_OK;

    return ret;
}

int8_t _z_request_body_decode_na(_z_request_body_t *reqb, _z_zbuf_t *zbf) {
    (void)(zbf);
    (void)(reqb);
    int8_t ret = _Z_RES_OK;

    return ret;
}

int8_t _z_request_body_decode(_z_request_body_t *reqb, _z_zbuf_t *zbf) { return _z_request_body_decode_na(reqb, zbf); }

/*------------------ Request Message ------------------*/
int8_t _z_request_encode(_z_wbuf_t *wbf, uint8_t header, const _z_n_msg_request_t *msg) {
    int8_t ret = _Z_RES_OK;
    _Z_DEBUG("Encoding _Z_MID_N_REQUEST\n");

    _Z_EC(_z_zint_encode(wbf, msg->_rid));
    _Z_EC(_z_keyexpr_encode(wbf, _Z_HAS_FLAG(header, _Z_FLAG_N_REQUEST_N), &msg->_key));
    _Z_EC(_z_request_body_encode(wbf, &msg->_body));

    return ret;
}

int8_t _z_request_decode_na(_z_n_msg_request_t *msg, _z_zbuf_t *zbf, uint8_t header) {
    _Z_DEBUG("Decoding _Z_MID_N_REQUEST\n");
    int8_t ret = _Z_RES_OK;

    ret |= _z_zint_decode(&msg->_rid, zbf);
    ret |= _z_keyexpr_decode(&msg->_key, zbf, _Z_HAS_FLAG(header, _Z_FLAG_N_REQUEST_N));
    ret |= _z_request_body_decode(&msg->_body, zbf);

    return ret;
}

int8_t _z_request_decode(_z_n_msg_request_t *msg, _z_zbuf_t *zbf, uint8_t header) {
    return _z_request_decode_na(msg, zbf, header);
}

/*------------------ Response Body Field ------------------*/
int8_t _z_response_body_encode(_z_wbuf_t *wbf, const _z_response_body_t *rspb) {
    (void)(wbf);
    (void)(rspb);
    int8_t ret = _Z_RES_OK;

    return ret;
}

int8_t _z_response_body_decode_na(_z_response_body_t *rspb, _z_zbuf_t *zbf) {
    (void)(zbf);
    (void)(rspb);
    int8_t ret = _Z_RES_OK;

    return ret;
}

int8_t _z_response_body_decode(_z_response_body_t *rspb, _z_zbuf_t *zbf) {
    return _z_response_body_decode_na(rspb, zbf);
}

/*------------------ Response Message ------------------*/
int8_t _z_response_encode(_z_wbuf_t *wbf, uint8_t header, const _z_n_msg_response_t *msg) {
    int8_t ret = _Z_RES_OK;
    _Z_DEBUG("Encoding _Z_MID_N_RESPONSE\n");

    _Z_EC(_z_zint_encode(wbf, msg->_rid));
    _Z_EC(_z_keyexpr_encode(wbf, _Z_HAS_FLAG(header, _Z_FLAG_N_RESPONSE_N), &msg->_key));
    _Z_EC(_z_response_body_encode(wbf, &msg->_body));

    return ret;
}

int8_t _z_response_decode_na(_z_n_msg_response_t *msg, _z_zbuf_t *zbf, uint8_t header) {
    _Z_DEBUG("Decoding _Z_MID_N_RESPONSE\n");
    int8_t ret = _Z_RES_OK;

    ret |= _z_zint_decode(&msg->_rid, zbf);
    ret |= _z_keyexpr_decode(&msg->_key, zbf, _Z_HAS_FLAG(header, _Z_FLAG_N_RESPONSE_N));
    ret |= _z_response_body_decode(&msg->_body, zbf);

    return ret;
}

int8_t _z_response_decode(_z_n_msg_response_t *msg, _z_zbuf_t *zbf, uint8_t header) {
    return _z_response_decode_na(msg, zbf, header);
}

/*------------------ Response Final Message ------------------*/
int8_t _z_response_final_encode(_z_wbuf_t *wbf, uint8_t header, const _z_n_msg_response_final_t *msg) {
    (void)(header);
    int8_t ret = _Z_RES_OK;
    _Z_DEBUG("Encoding _Z_MID_N_RESPONSE\n");

    _Z_EC(_z_zint_encode(wbf, msg->_rid));

    return ret;
}

int8_t _z_response_final_decode_na(_z_n_msg_response_final_t *msg, _z_zbuf_t *zbf, uint8_t header) {
    (void)(header);
    _Z_DEBUG("Decoding _Z_MID_N_RESPONSE\n");
    int8_t ret = _Z_RES_OK;

    ret |= _z_zint_decode(&msg->_rid, zbf);

    return ret;
}

int8_t _z_response_final_decode(_z_n_msg_response_final_t *msg, _z_zbuf_t *zbf, uint8_t header) {
    return _z_response_final_decode_na(msg, zbf, header);
}

/*------------------ Network Message ------------------*/
int8_t _z_network_message_encode(_z_wbuf_t *wbf, const _z_network_message_t *msg) {
    int8_t ret = _Z_RES_OK;

    uint8_t header = msg->_header;
    if (_z_msg_ext_vec_is_empty(&msg->_extensions) == false) {
        header |= _Z_FLAG_N_Z;
    }

    _Z_EC(_z_wbuf_write(wbf, header))
    switch (_Z_MID(msg->_header)) {
        case _Z_MID_N_DECLARE: {
            ret |= _z_declare_encode(wbf, &msg->_body._declare);
        } break;

        default: {
            _Z_DEBUG("WARNING: Trying to encode network message with unknown ID(%d)\n", _Z_MID(msg->_header));
            ret |= _Z_ERR_MESSAGE_TRANSPORT_UNKNOWN;
        } break;
    }

    return ret;
}

int8_t _z_network_message_decode_na(_z_network_message_t *msg, _z_zbuf_t *zbf) {
    int8_t ret = _Z_RES_OK;

    ret |= _z_uint8_decode(&msg->_header, zbf);  // Decode the header
    if (ret == _Z_RES_OK) {
        uint8_t mid = _Z_MID(msg->_header);
        switch (mid) {
            case _Z_MID_N_DECLARE: {
                ret |= _z_declare_decode(&msg->_body._declare, zbf);
            } break;

            default: {
                _Z_DEBUG("WARNING: Trying to decode session message with unknown ID(%d)\n", mid);
                ret |= _Z_ERR_MESSAGE_TRANSPORT_UNKNOWN;
            } break;
        }
    }

    return ret;
}

int8_t _z_network_message_decode(_z_network_message_t *n_msg, _z_zbuf_t *zbf) {
    return _z_network_message_decode_na(n_msg, zbf);
}

/*=============================*/
/*       Transport Messages    */
/*=============================*/
/*------------------ Join Message ------------------*/
int8_t _z_join_encode(_z_wbuf_t *wbf, uint8_t header, const _z_t_msg_join_t *msg) {
    int8_t ret = _Z_RES_OK;
    _Z_DEBUG("Encoding _Z_MID_T_JOIN\n");

    _Z_EC(_z_wbuf_write(wbf, msg->_version))

    uint8_t cbyte = 0;
    cbyte |= (msg->_whatami & 0x03);
    uint8_t zidlen = _z_id_len(msg->_zid);
    cbyte |= ((zidlen - 1) & 0x0F) << 4;
    _Z_EC(_z_uint8_encode(wbf, cbyte))
    _Z_EC(_z_wbuf_write_bytes(wbf, msg->_zid.id, 0, zidlen))

    if (_Z_HAS_FLAG(header, _Z_FLAG_T_JOIN_S) == true) {
        cbyte = 0;
        cbyte |= (msg->_seq_num_res & 0x03);
        cbyte |= ((msg->_req_id_res & 0x03) << 2);
        _Z_EC(_z_uint8_encode(wbf, cbyte))
        _Z_EC(_z_uint16_encode(wbf, msg->_batch_size))
    }

    if (_Z_HAS_FLAG(header, _Z_FLAG_T_JOIN_T) == true) {
        _Z_EC(_z_zint_encode(wbf, msg->_lease / 1000))
    } else {
        _Z_EC(_z_zint_encode(wbf, msg->_lease))
    }

    if (msg->_next_sn._is_qos) {
        if (_Z_HAS_FLAG(header, _Z_FLAG_T_Z) == true) {
            _Z_EC(_z_uint8_encode(wbf, 0x51))  // QOS-ext: (enc=zbuf)(mandatory=true)(id=1)
            for (uint8_t i = 0; i < Z_PRIORITIES_NUM; i++) {
                _Z_EC(_z_zint_encode(wbf, msg->_next_sn._val._qos[i]._reliable))
                _Z_EC(_z_zint_encode(wbf, msg->_next_sn._val._qos[i]._best_effort))
            }
        } else {
            _Z_DEBUG("Attempted to serialize QoS-SN extension, but the header extension flag was unset");
            ret |= _Z_ERR_MESSAGE_SERIALIZATION_FAILED;
        }
    } else {
        _Z_EC(_z_zint_encode(wbf, msg->_next_sn._val._plain._reliable))
        _Z_EC(_z_zint_encode(wbf, msg->_next_sn._val._plain._best_effort))
    }

    return ret;
}

int8_t _z_join_decode_ext(_z_msg_ext_t *extension, void *ctx) {
    int8_t ret = _Z_RES_OK;
    _z_t_msg_join_t *msg = (_z_t_msg_join_t *)ctx;
    if (_Z_EXT_FULL_ID(extension->_header) == 0x51) {  // QOS: (enc=zbuf)(mandatory=true)(id=1)
        msg->_next_sn._is_qos = true;
        _z_zbuf_t zbf = _z_zbytes_as_zbuf(extension->_body._zbuf._val);
        for (int i = 0; (ret == _Z_RES_OK) && (i < Z_PRIORITIES_NUM); ++i) {
            ret |= _z_zint_decode(&msg->_next_sn._val._plain._reliable, &zbf);
            ret |= _z_zint_decode(&msg->_next_sn._val._plain._best_effort, &zbf);
        }
    } else if (_Z_MSG_EXT_IS_MANDATORY(extension->_header)) {
        ret = _Z_ERR_MESSAGE_EXTENSION_MANDATORY_AND_UNKNOWN;
    }
    return ret;
}

int8_t _z_join_decode_na(_z_t_msg_join_t *msg, _z_zbuf_t *zbf, uint8_t header) {
    _Z_DEBUG("Decoding _Z_MID_T_JOIN\n");
    int8_t ret = _Z_RES_OK;

    ret |= _z_uint8_decode(&msg->_version, zbf);

    uint8_t cbyte = 0;
    ret |= _z_uint8_decode(&cbyte, zbf);
    msg->_whatami = cbyte & 0x03;

    uint8_t zidlen = ((cbyte & 0xF0) >> 4) + 1;
    msg->_zid = _z_id_empty();
    if (ret == _Z_RES_OK) {
        _z_zbuf_read_bytes(zbf, msg->_zid.id, 0, zidlen);
    }

    if (_Z_HAS_FLAG(header, _Z_FLAG_T_JOIN_S) == true) {
        cbyte = 0;
        ret |= _z_uint8_decode(&cbyte, zbf);
        msg->_seq_num_res = (cbyte & 0x03);
        msg->_req_id_res = ((cbyte >> 2) & 0x03);
        ret |= _z_uint16_decode(&msg->_batch_size, zbf);
    } else {
        msg->_seq_num_res = _Z_DEFAULT_RESOLUTION_SIZE;
        msg->_req_id_res = _Z_DEFAULT_RESOLUTION_SIZE;
        msg->_batch_size = _Z_DEFAULT_BATCH_SIZE;
    }

    ret |= _z_zint_decode(&msg->_lease, zbf);
    if (_Z_HAS_FLAG(header, _Z_FLAG_T_JOIN_T) == true) {
        msg->_lease = msg->_lease * 1000;
    }

    msg->_next_sn._is_qos = false;
    ret |= _z_zint_decode(&msg->_next_sn._val._plain._reliable, zbf);
    ret |= _z_zint_decode(&msg->_next_sn._val._plain._best_effort, zbf);

    if (_Z_HAS_FLAG(header, _Z_FLAG_T_Z)) {
        ret |= _z_msg_ext_decode_iter(zbf, _z_join_decode_ext, msg);
    }

    return ret;
}

int8_t _z_join_decode(_z_t_msg_join_t *msg, _z_zbuf_t *zbf, uint8_t header) {
    return _z_join_decode_na(msg, zbf, header);
}

/*------------------ Init Message ------------------*/
int8_t _z_init_encode(_z_wbuf_t *wbf, uint8_t header, const _z_t_msg_init_t *msg) {
    _Z_DEBUG("Encoding _Z_MID_T_INIT\n");
    int8_t ret = _Z_RES_OK;

    _Z_EC(_z_wbuf_write(wbf, msg->_version))

    uint8_t cbyte = 0;
    cbyte |= (msg->_whatami & 0x03);
    uint8_t zidlen = _z_id_len(msg->_zid);
    cbyte |= ((zidlen - 1) & 0x0F) << 4;  // TODO[protocol]: check if ZID > 0 && <= 16
    _Z_EC(_z_uint8_encode(wbf, cbyte))
    _Z_EC(_z_wbuf_write_bytes(wbf, msg->_zid.id, 0, zidlen))

    if (_Z_HAS_FLAG(header, _Z_FLAG_T_INIT_S) == true) {
        cbyte = 0;
        cbyte |= (msg->_seq_num_res & 0x03);
        cbyte |= ((msg->_req_id_res & 0x03) << 2);
        _Z_EC(_z_uint8_encode(wbf, cbyte))
        _Z_EC(_z_uint16_encode(wbf, msg->_batch_size))
    }

    if (_Z_HAS_FLAG(header, _Z_FLAG_T_INIT_A) == true) {
        _Z_EC(_z_bytes_encode(wbf, &msg->_cookie))
    }

    return ret;
}

int8_t _z_init_decode_na(_z_t_msg_init_t *msg, _z_zbuf_t *zbf, uint8_t header) {
    _Z_DEBUG("Decoding _Z_MID_T_INIT\n");
    int8_t ret = _Z_RES_OK;

    ret |= _z_uint8_decode(&msg->_version, zbf);

    uint8_t cbyte = 0;
    ret |= _z_uint8_decode(&cbyte, zbf);
    msg->_zid = _z_id_empty();

    if (ret == _Z_RES_OK) {
        msg->_whatami = cbyte & 0x03;
        uint8_t zidlen = ((cbyte & 0xF0) >> 4) + 1;
        _z_zbuf_read_bytes(zbf, msg->_zid.id, 0, zidlen);
    }

    if ((ret == _Z_RES_OK) && (_Z_HAS_FLAG(header, _Z_FLAG_T_INIT_S) == true)) {
        cbyte = 0;
        ret |= _z_uint8_decode(&cbyte, zbf);
        msg->_seq_num_res = (cbyte & 0x03);
        msg->_req_id_res = ((cbyte >> 2) & 0x03);
        ret |= _z_uint16_decode(&msg->_batch_size, zbf);
    } else {
        msg->_seq_num_res = _Z_DEFAULT_RESOLUTION_SIZE;
        msg->_req_id_res = _Z_DEFAULT_RESOLUTION_SIZE;
        msg->_batch_size = _Z_DEFAULT_BATCH_SIZE;
    }

    if ((ret == _Z_RES_OK) && (_Z_HAS_FLAG(header, _Z_FLAG_T_INIT_A) == true)) {
        ret |= _z_bytes_decode(&msg->_cookie, zbf);
        if (ret != _Z_RES_OK) {
            msg->_cookie = _z_bytes_empty();
        }
    } else {
        msg->_cookie = _z_bytes_empty();
    }

    if ((ret == _Z_RES_OK) && (_Z_HAS_FLAG(header, _Z_FLAG_T_Z) == true)) {
        ret |= _z_msg_ext_skip_non_mandatories(zbf);
    }

    return ret;
}

int8_t _z_init_decode(_z_t_msg_init_t *msg, _z_zbuf_t *zbf, uint8_t header) {
    return _z_init_decode_na(msg, zbf, header);
}

/*------------------ Open Message ------------------*/
int8_t _z_open_encode(_z_wbuf_t *wbf, uint8_t header, const _z_t_msg_open_t *msg) {
    int8_t ret = _Z_RES_OK;
    _Z_DEBUG("Encoding _Z_MID_T_OPEN\n");

    if (_Z_HAS_FLAG(header, _Z_FLAG_T_OPEN_T) == true) {
        _Z_EC(_z_zint_encode(wbf, msg->_lease / 1000))
    } else {
        _Z_EC(_z_zint_encode(wbf, msg->_lease))
    }

    _Z_EC(_z_zint_encode(wbf, msg->_initial_sn))

    if (_Z_HAS_FLAG(header, _Z_FLAG_T_OPEN_A) == false) {
        _Z_EC(_z_bytes_encode(wbf, &msg->_cookie))
    }

    return ret;
}

int8_t _z_open_decode_na(_z_t_msg_open_t *msg, _z_zbuf_t *zbf, uint8_t header) {
    _Z_DEBUG("Decoding _Z_MID_T_OPEN\n");
    int8_t ret = _Z_RES_OK;

    ret |= _z_zint_decode(&msg->_lease, zbf);
    if ((ret == _Z_RES_OK) && (_Z_HAS_FLAG(header, _Z_FLAG_T_OPEN_T) == true)) {
        msg->_lease = msg->_lease * 1000;
    }

    ret |= _z_zint_decode(&msg->_initial_sn, zbf);

    if ((ret == _Z_RES_OK) && (_Z_HAS_FLAG(header, _Z_FLAG_T_OPEN_A) == false)) {
        ret |= _z_bytes_decode(&msg->_cookie, zbf);
        if (ret != _Z_RES_OK) {
            msg->_cookie = _z_bytes_empty();
        }
    } else {
        msg->_cookie = _z_bytes_empty();
    }
    if ((ret == _Z_RES_OK) && (_Z_HAS_FLAG(header, _Z_FLAG_T_Z) == true)) {
        ret |= _z_msg_ext_skip_non_mandatories(zbf);
    }

    return ret;
}

int8_t _z_open_decode(_z_t_msg_open_t *msg, _z_zbuf_t *zbf, uint8_t header) {
    return _z_open_decode_na(msg, zbf, header);
}

/*------------------ Close Message ------------------*/
int8_t _z_close_encode(_z_wbuf_t *wbf, uint8_t header, const _z_t_msg_close_t *msg) {
    (void)(header);
    int8_t ret = _Z_RES_OK;
    _Z_DEBUG("Encoding _Z_MID_T_CLOSE\n");

    ret |= _z_wbuf_write(wbf, msg->_reason);

    return ret;
}

int8_t _z_close_decode_na(_z_t_msg_close_t *msg, _z_zbuf_t *zbf, uint8_t header) {
    (void)(header);
    int8_t ret = _Z_RES_OK;
    _Z_DEBUG("Decoding _Z_MID_T_CLOSE\n");

    ret |= _z_uint8_decode(&msg->_reason, zbf);

    return ret;
}

int8_t _z_close_decode(_z_t_msg_close_t *msg, _z_zbuf_t *zbf, uint8_t header) {
    return _z_close_decode_na(msg, zbf, header);
}

/*------------------ Keep Alive Message ------------------*/
int8_t _z_keep_alive_encode(_z_wbuf_t *wbf, uint8_t header, const _z_t_msg_keep_alive_t *msg) {
    (void)(wbf);
    (void)(header);
    (void)(msg);

    int8_t ret = _Z_RES_OK;
    _Z_DEBUG("Encoding _Z_MID_T_KEEP_ALIVE\n");

    return ret;
}

int8_t _z_keep_alive_decode_na(_z_t_msg_keep_alive_t *msg, _z_zbuf_t *zbf, uint8_t header) {
    (void)(msg);
    (void)(zbf);
    (void)(header);

    int8_t ret = _Z_RES_OK;
    _Z_DEBUG("Decoding _Z_MID_T_KEEP_ALIVE\n");

    ret |= _z_msg_ext_skip_non_mandatories(zbf);

    return ret;
}

int8_t _z_keep_alive_decode(_z_t_msg_keep_alive_t *msg, _z_zbuf_t *zbf, uint8_t header) {
    return _z_keep_alive_decode_na(msg, zbf, header);
}

/*------------------ Frame Message ------------------*/

int8_t _z_frame_encode(_z_wbuf_t *wbf, uint8_t header, const _z_t_msg_frame_t *msg) {
    int8_t ret = _Z_RES_OK;

    _Z_EC(_z_zint_encode(wbf, msg->_sn))

    if (_Z_HAS_FLAG(header, _Z_FLAG_T_Z)) {
        ret = _Z_ERR_MESSAGE_SERIALIZATION_FAILED;
    }
    if (ret == _Z_RES_OK) {
        size_t len = _z_network_message_vec_len(&msg->_messages);
        for (size_t i = 0; i < len; i++) {
            _Z_EC(_z_network_message_encode(wbf, _z_network_message_vec_get(&msg->_messages, i)))
        }
    }

    return ret;
}

int8_t _z_frame_decode(_z_t_msg_frame_t *msg, _z_zbuf_t *zbf, uint8_t header) {
    int8_t ret = _Z_RES_OK;

    ret |= _z_zint_decode(&msg->_sn, zbf);
    if ((ret == _Z_RES_OK) && (_Z_HAS_FLAG(header, _Z_FLAG_T_Z) == true)) {
        ret |= _z_msg_ext_skip_non_mandatories(zbf);
    }
    if (ret == _Z_RES_OK) {
        msg->_messages = _z_network_message_vec_make(_ZENOH_PICO_FRAME_MESSAGES_VEC_SIZE);
        while (_z_zbuf_len(zbf) > 0) {
            // Mark the reading position of the iobfer
            size_t r_pos = _z_zbuf_get_rpos(zbf);
            _z_network_message_t *nm = (_z_network_message_t *)z_malloc(sizeof(_z_network_message_t));
            ret |= _z_network_message_decode(nm, zbf);
            if (ret == _Z_RES_OK) {
                _z_network_message_vec_append(&msg->_messages, nm);
            } else {
                _z_n_msg_free(&nm);

                _z_zbuf_set_rpos(zbf, r_pos);  // Restore the reading position of the iobfer

                // FIXME: Check for the return error, since not all of them means a decoding error
                //        in this particular case. As of now, we roll-back the reading position
                //        and return to the Zenoh transport-level decoder.
                //        https://github.com/eclipse-zenoh/zenoh-pico/pull/132#discussion_r1045593602
                if ((ret & _Z_ERR_MESSAGE_ZENOH_UNKNOWN) == _Z_ERR_MESSAGE_ZENOH_UNKNOWN) {
                    ret = _Z_RES_OK;
                }
                break;
            }
        }
    }
    return ret;
}

/*------------------ Fragment Message ------------------*/
int8_t _z_fragment_encode(_z_wbuf_t *wbf, uint8_t header, const _z_t_msg_fragment_t *msg) {
    int8_t ret = _Z_RES_OK;
    _Z_DEBUG("Encoding _Z_TRANSPORT_FRAGMENT\n");
    _Z_EC(_z_zint_encode(wbf, msg->_sn))
    if (_Z_HAS_FLAG(header, _Z_FLAG_T_Z)) {
        ret = _Z_ERR_MESSAGE_SERIALIZATION_FAILED;
    }
    if (ret == _Z_RES_OK) {
        _Z_EC(_z_bytes_encode(wbf, &msg->_payload))
    }

    return ret;
}

int8_t _z_fragment_decode(_z_t_msg_fragment_t *msg, _z_zbuf_t *zbf, uint8_t header) {
    int8_t ret = _Z_RES_OK;
    _Z_DEBUG("Decoding _Z_TRANSPORT_FRAGMENT\n");
    ret |= _z_zint_decode(&msg->_sn, zbf);

    if ((ret == _Z_RES_OK) && (_Z_HAS_FLAG(header, _Z_FLAG_T_Z) == true)) {
        ret |= _z_msg_ext_skip_non_mandatories(zbf);
    }

    ret |= _z_bytes_decode(&msg->_payload, zbf);

    return ret;
}

/*------------------ Transport Extensions Message ------------------*/
int8_t _z_extensions_encode(_z_wbuf_t *wbf, uint8_t header, const _z_msg_ext_vec_t *v_ext) {
    (void)(header);
    int8_t ret = _Z_RES_OK;

    _Z_DEBUG("Encoding _Z_TRANSPORT_EXTENSIONS\n");
    if (_Z_HAS_FLAG(header, _Z_FLAG_T_Z) == true) {
        ret |= _z_msg_ext_vec_encode(wbf, v_ext);
    }

    return ret;
}

int8_t _z_extensions_decode_na(_z_msg_ext_vec_t *v_ext, _z_zbuf_t *zbf, uint8_t header) {
    (void)(header);
    int8_t ret = _Z_RES_OK;

    _Z_DEBUG("Decoding _Z_TRANSPORT_EXTENSIONS\n");
    if (_Z_HAS_FLAG(header, _Z_FLAG_T_Z) == true) {
        ret |= _z_msg_ext_vec_decode(v_ext, zbf);
    } else {
        _z_msg_ext_vec_reset(v_ext);
    }

    return ret;
}

int8_t _z_extensions_decode(_z_msg_ext_vec_t *v_ext, _z_zbuf_t *zbf, uint8_t header) {
    return _z_extensions_decode_na(v_ext, zbf, header);
}

/*------------------ Transport Message ------------------*/
int8_t _z_transport_message_encode(_z_wbuf_t *wbf, const _z_transport_message_t *msg) {
    int8_t ret = _Z_RES_OK;

    // Encode the decorators if present

    uint8_t header = msg->_header;

    _Z_EC(_z_wbuf_write(wbf, header))
    switch (_Z_MID(msg->_header)) {
        case _Z_MID_T_FRAME: {
            ret |= _z_frame_encode(wbf, msg->_header, &msg->_body._frame);
        } break;

        case _Z_MID_T_FRAGMENT: {
            ret |= _z_fragment_encode(wbf, msg->_header, &msg->_body._fragment);
        } break;

        case _Z_MID_T_KEEP_ALIVE: {
            ret |= _z_keep_alive_encode(wbf, msg->_header, &msg->_body._keep_alive);
        } break;

        case _Z_MID_T_JOIN: {
            ret |= _z_join_encode(wbf, msg->_header, &msg->_body._join);
        } break;

        case _Z_MID_T_INIT: {
            ret |= _z_init_encode(wbf, msg->_header, &msg->_body._init);
        } break;

        case _Z_MID_T_OPEN: {
            ret |= _z_open_encode(wbf, msg->_header, &msg->_body._open);
        } break;

        case _Z_MID_T_CLOSE: {
            ret |= _z_close_encode(wbf, msg->_header, &msg->_body._close);
        } break;

        default: {
            _Z_DEBUG("WARNING: Trying to encode session message with unknown ID(%d)\n", _Z_MID(msg->_header));
            ret |= _Z_ERR_MESSAGE_TRANSPORT_UNKNOWN;
        } break;
    }

    return ret;
}

int8_t _z_transport_message_decode_na(_z_transport_message_t *msg, _z_zbuf_t *zbf) {
    int8_t ret = _Z_RES_OK;

    ret |= _z_uint8_decode(&msg->_header, zbf);  // Decode the header
    if (ret == _Z_RES_OK) {
        uint8_t mid = _Z_MID(msg->_header);
        switch (mid) {
            case _Z_MID_T_FRAME: {
                ret |= _z_frame_decode(&msg->_body._frame, zbf, msg->_header);
            } break;

            case _Z_MID_T_FRAGMENT: {
                ret |= _z_fragment_decode(&msg->_body._fragment, zbf, msg->_header);
            } break;

            case _Z_MID_T_KEEP_ALIVE: {
                ret |= _z_keep_alive_decode(&msg->_body._keep_alive, zbf, msg->_header);
            } break;

            case _Z_MID_T_JOIN: {
                ret |= _z_join_decode(&msg->_body._join, zbf, msg->_header);
            } break;

            case _Z_MID_T_INIT: {
                ret |= _z_init_decode(&msg->_body._init, zbf, msg->_header);
            } break;

            case _Z_MID_T_OPEN: {
                ret |= _z_open_decode(&msg->_body._open, zbf, msg->_header);
            } break;

            case _Z_MID_T_CLOSE: {
                ret |= _z_close_decode(&msg->_body._close, zbf, msg->_header);
            } break;

            default: {
                _Z_DEBUG("WARNING: Trying to decode session message with unknown ID(%d)\n", mid);
                ret |= _Z_ERR_MESSAGE_TRANSPORT_UNKNOWN;
            } break;
        }
    } else {
        msg->_header = 0xFF;
    }

    return ret;
}

int8_t _z_transport_message_decode(_z_transport_message_t *t_msg, _z_zbuf_t *zbf) {
    return _z_transport_message_decode_na(t_msg, zbf);
}

/*=============================*/
/*       Scouting Messages     */
/*=============================*/
/*------------------ Scout Message ------------------*/
int8_t _z_scout_encode(_z_wbuf_t *wbf, uint8_t header, const _z_s_msg_scout_t *msg) {
    int8_t ret = _Z_RES_OK;
    (void)(header);
    _Z_DEBUG("Encoding _Z_MID_SCOUT\n");

    _Z_EC(_z_uint8_encode(wbf, msg->_version))

    uint8_t cbyte = 0;
    cbyte |= (msg->_what & 0x07);
    uint8_t zid_len = _z_id_len(msg->_zid);
    if (zid_len > 0) {
        _Z_SET_FLAG(cbyte, _Z_FLAG_T_SCOUT_I);
        cbyte |= ((zid_len - 1) & 0x0F) << 4;
    }
    _Z_EC(_z_uint8_encode(wbf, cbyte))

    ret |= _z_wbuf_write_bytes(wbf, msg->_zid.id, 0, zid_len);

    return ret;
}

int8_t _z_scout_decode_na(_z_s_msg_scout_t *msg, _z_zbuf_t *zbf, uint8_t header) {
    int8_t ret = _Z_RES_OK;
    (void)(header);
    _Z_DEBUG("Decoding _Z_MID_SCOUT\n");

    ret |= _z_uint8_decode(&msg->_version, zbf);

    uint8_t cbyte = 0;
    ret |= _z_uint8_decode(&cbyte, zbf);
    msg->_what = cbyte & 0x07;
    msg->_zid = _z_id_empty();
    if ((ret == _Z_RES_OK) && (_Z_HAS_FLAG(cbyte, _Z_FLAG_T_SCOUT_I) == true)) {
        uint8_t zidlen = ((cbyte & 0xF0) >> 4) + 1;
        _z_zbuf_read_bytes(zbf, msg->_zid.id, 0, zidlen);
    }

    return ret;
}

int8_t _z_scout_decode(_z_s_msg_scout_t *msg, _z_zbuf_t *zbf, uint8_t header) {
    return _z_scout_decode_na(msg, zbf, header);
}

/*------------------ Hello Message ------------------*/
int8_t _z_hello_encode(_z_wbuf_t *wbf, uint8_t header, const _z_s_msg_hello_t *msg) {
    int8_t ret = _Z_RES_OK;
    _Z_DEBUG("Encoding _Z_MID_HELLO\n");

    _Z_EC(_z_uint8_encode(wbf, msg->_version))
    uint8_t zidlen = _z_id_len(msg->_zid);
    uint8_t cbyte = 0;
    cbyte |= (msg->_whatami & 0x03);
    cbyte |= ((zidlen - 1) & 0x0F) << 4;
    _Z_EC(_z_uint8_encode(wbf, cbyte))
    _Z_EC(_z_bytes_val_encode(wbf, &(_z_bytes_t){.start = msg->_zid.id, .len = zidlen, ._is_alloc = false}));

    if (_Z_HAS_FLAG(header, _Z_FLAG_T_HELLO_L) == true) {
        _Z_EC(_z_locators_encode(wbf, &msg->_locators))
    }

    return ret;
}

int8_t _z_hello_decode_na(_z_s_msg_hello_t *msg, _z_zbuf_t *zbf, uint8_t header) {
    _Z_DEBUG("Decoding _Z_MID_HELLO\n");
    int8_t ret = _Z_RES_OK;

    ret |= _z_uint8_decode(&msg->_version, zbf);

    uint8_t cbyte = 0;
    ret |= _z_uint8_decode(&cbyte, zbf);
    msg->_whatami = cbyte & 0x03;
    uint8_t zidlen = ((cbyte & 0xF0) >> 4) + 1;

    if (ret == _Z_RES_OK) {
        msg->_zid = _z_id_empty();
        _z_zbuf_read_bytes(zbf, msg->_zid.id, 0, zidlen);
    } else {
        msg->_zid = _z_id_empty();
    }

    if ((ret == _Z_RES_OK) && (_Z_HAS_FLAG(header, _Z_FLAG_T_HELLO_L) == true)) {
        ret |= _z_locators_decode(&msg->_locators, zbf);
        if (ret != _Z_RES_OK) {
            msg->_locators = _z_locator_array_empty();
        }
    } else {
        msg->_locators = _z_locator_array_empty();
    }

    return ret;
}

int8_t _z_hello_decode(_z_s_msg_hello_t *msg, _z_zbuf_t *zbf, uint8_t header) {
    return _z_hello_decode_na(msg, zbf, header);
}

int8_t _z_scouting_message_encode(_z_wbuf_t *wbf, const _z_scouting_message_t *msg) {
    int8_t ret = _Z_RES_OK;

    // Encode the decorators if present

    uint8_t header = msg->_header;
    // size_t n_ext = _z_msg_ext_vec_len(&msg->_extensions);
    // if (n_ext > 0) {
    //     header |= _Z_FLAG_T_Z;
    // }

    _Z_EC(_z_wbuf_write(wbf, header))
    switch (_Z_MID(msg->_header)) {
        case _Z_MID_SCOUT: {
            ret |= _z_scout_encode(wbf, msg->_header, &msg->_body._scout);
        } break;

        case _Z_MID_HELLO: {
            ret |= _z_hello_encode(wbf, msg->_header, &msg->_body._hello);
        } break;

        default: {
            _Z_DEBUG("WARNING: Trying to encode session message with unknown ID(%d)\n", _Z_MID(msg->_header));
            ret |= _Z_ERR_MESSAGE_TRANSPORT_UNKNOWN;
        } break;
    }

    // ret |= _z_msg_ext_vec_encode(wbf, &msg->_extensions);

    return ret;
}
int8_t _z_scouting_message_decode_na(_z_scouting_message_t *msg, _z_zbuf_t *zbf) {
    int8_t ret = _Z_RES_OK;

    _Bool is_last = false;

    do {
        ret |= _z_uint8_decode(&msg->_header, zbf);  // Decode the header
        if (ret == _Z_RES_OK) {
            uint8_t mid = _Z_MID(msg->_header);
            switch (mid) {
                case _Z_MID_SCOUT: {
                    ret |= _z_scout_decode(&msg->_body._scout, zbf, msg->_header);
                    is_last = true;
                } break;

                case _Z_MID_HELLO: {
                    ret |= _z_hello_decode(&msg->_body._hello, zbf, msg->_header);
                    is_last = true;
                } break;

                default: {
                    _Z_DEBUG("WARNING: Trying to decode session message with unknown ID(%d)\n", mid);
                    ret |= _Z_ERR_MESSAGE_TRANSPORT_UNKNOWN;
                    is_last = true;
                } break;
            }
        } else {
            msg->_header = 0xFF;
        }
    } while ((ret == _Z_RES_OK) && (is_last == false));

    if ((ret == _Z_RES_OK) && (msg->_header & _Z_MSG_EXT_FLAG_Z) != 0) {
        ret |= _z_msg_ext_skip_non_mandatories(zbf);
    }

    return ret;
}

int8_t _z_scouting_message_decode(_z_scouting_message_t *s_msg, _z_zbuf_t *zbf) {
    return _z_scouting_message_decode_na(s_msg, zbf);
}