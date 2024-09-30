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

#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <zenoh-pico.h>
#include <zenoh-pico/system/platform.h>

typedef struct kv_pair_t {
    const char *key;
    const char *value;
} kv_pair_t;

#if Z_FEATURE_PUBLICATION == 1

int main(int argc, char **argv) {
    const char *keyexpr = "demo/example/zenoh-pico-pub";
    char *const default_value = "Pub from Pico!";
    char *value = default_value;
    const char *mode = "client";
    char *clocator = NULL;
    char *llocator = NULL;
    int n = 2147483647;  // max int value by default

    int opt;
    while ((opt = getopt(argc, argv, "k:v:e:m:l:n:")) != -1) {
        switch (opt) {
            case 'k':
                keyexpr = optarg;
                break;
            case 'v':
                value = optarg;
                break;
            case 'e':
                clocator = optarg;
                break;
            case 'm':
                mode = optarg;
                break;
            case 'l':
                llocator = optarg;
                break;
            case 'n':
                n = atoi(optarg);
                break;
            case '?':
                if (optopt == 'k' || optopt == 'v' || optopt == 'e' || optopt == 'm' || optopt == 'l' ||
                    optopt == 'n') {
                    fprintf(stderr, "Option -%c requires an argument.\n", optopt);
                } else {
                    fprintf(stderr, "Unknown option `-%c'.\n", optopt);
                }
                return 1;
            default:
                return -1;
        }
    }

    z_owned_config_t config;
    z_config_default(&config);
    zp_config_insert(z_loan_mut(config), Z_CONFIG_MODE_KEY, mode);
    if (clocator != NULL) {
        zp_config_insert(z_loan_mut(config), Z_CONFIG_CONNECT_KEY, clocator);
    }
    if (llocator != NULL) {
        zp_config_insert(z_loan_mut(config), Z_CONFIG_LISTEN_KEY, llocator);
    }

    printf("Opening session...\n");
    z_owned_session_t s;
    if (z_open(&s, z_move(config), NULL) < 0) {
        printf("Unable to open session!\n");
        return -1;
    }

    // Start read and lease tasks for zenoh-pico
    if (zp_start_read_task(z_loan_mut(s), NULL) < 0 || zp_start_lease_task(z_loan_mut(s), NULL) < 0) {
        printf("Unable to start read and lease tasks\n");
        z_close(z_move(s), NULL);
        return -1;
    }
    // Wait for joins in peer mode
    if (strcmp(mode, "peer") == 0) {
        printf("Waiting for joins...\n");
        sleep(3);
    }
    // Declare publisher
    printf("Declaring publisher for '%s'...\n", keyexpr);
    z_view_keyexpr_t ke;
    z_view_keyexpr_from_str(&ke, keyexpr);
    z_owned_publisher_t pub;
    if (z_declare_publisher(&pub, z_loan(s), z_loan(ke), NULL) < 0) {
        printf("Unable to declare publisher for key expression!\n");
        return -1;
    }

    z_publisher_put_options_t options;
    z_publisher_put_options_default(&options);

#if defined(Z_FEATURE_UNSTABLE_API)
    // Allocate attachment
    kv_pair_t kvs[2];
    kvs[0] = (kv_pair_t){.key = "source", .value = "C"};
#endif
    z_owned_bytes_t attachment;
    z_bytes_empty(&attachment);

    // Create encoding
    z_owned_encoding_t encoding;

    // Create timestamp
    z_timestamp_t ts;
    z_timestamp_new(&ts, z_loan(s));

    // Publish data
    printf("Press CTRL-C to quit...\n");
    char buf[256];
    for (int idx = 0; idx < n; ++idx) {
        z_sleep_s(1);
        sprintf(buf, "[%4d] %s", idx, value);
        printf("Putting Data ('%s': '%s')...\n", keyexpr, buf);

        // Create payload
        z_owned_bytes_t payload;
        z_bytes_copy_from_str(&payload, buf);
#if defined(Z_FEATURE_UNSTABLE_API)
        // Add attachment value
        char buf_ind[16];
        sprintf(buf_ind, "%d", idx);
        kvs[1] = (kv_pair_t){.key = "index", .value = buf_ind};
        ze_owned_serializer_t serializer;
        ze_serializer_empty(&serializer);
        ze_serializer_serialize_sequence_begin(z_loan_mut(serializer), 2);
        for (size_t i = 0; i < 2; ++i) {
            ze_serializer_serialize_str(z_loan_mut(serializer), kvs[i].key);
            ze_serializer_serialize_str(z_loan_mut(serializer), kvs[i].value);
        }
        ze_serializer_serialize_sequence_end(z_loan_mut(serializer));
        ze_serializer_finish(z_move(serializer), &attachment);
        options.attachment = z_move(attachment);
#endif
        // Add encoding value
        z_encoding_from_str(&encoding, "zenoh/string;utf8");
        options.encoding = z_move(encoding);

        // Add timestamp
        options.timestamp = &ts;

        z_publisher_put(z_loan(pub), z_move(payload), &options);
    }
    // Clean up
    z_undeclare_publisher(z_move(pub));
    z_close(z_move(s), NULL);
    return 0;
}
#else
int main(void) {
    printf("ERROR: Zenoh pico was compiled without Z_FEATURE_PUBLICATION but this example requires it.\n");
    return -2;
}
#endif
