/*
 * telemetry_msg_catalog.h — Generated message catalog
 *
 * Source:    telemetry.iq
 * Namespace: com.embediq.telemetry
 * Schema:    version 1
 *
 * DO NOT EDIT — regenerate with:
 *   python3 tools/messages_iq/generate.py messages/telemetry.iq --out generated/ --output-name telemetry_msg_catalog.h
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TELEMETRY_MSG_CATALOG_H
#define TELEMETRY_MSG_CATALOG_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "embediq_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --- MSG_TELEMETRY_GAUGE (0x0640) --- */
#define MSG_TELEMETRY_GAUGE  0x0640u

typedef struct {
    uint16_t  metric_id;
    float     value;
    uint8_t   unit_id;
} MSG_TELEMETRY_GAUGE_Payload_t;

_Static_assert(sizeof(MSG_TELEMETRY_GAUGE_Payload_t) <= EMBEDIQ_MSG_MAX_PAYLOAD,
    "MSG_TELEMETRY_GAUGE payload exceeds EMBEDIQ_MSG_MAX_PAYLOAD");
_Static_assert(offsetof(MSG_TELEMETRY_GAUGE_Payload_t, metric_id) == 0,
    "MSG_TELEMETRY_GAUGE layout changed — increment schema_id");

/* --- MSG_TELEMETRY_COUNTER (0x0641) --- */
#define MSG_TELEMETRY_COUNTER  0x0641u

typedef struct {
    uint16_t  metric_id;
    uint32_t  delta;
    uint8_t   unit_id;
} MSG_TELEMETRY_COUNTER_Payload_t;

_Static_assert(sizeof(MSG_TELEMETRY_COUNTER_Payload_t) <= EMBEDIQ_MSG_MAX_PAYLOAD,
    "MSG_TELEMETRY_COUNTER payload exceeds EMBEDIQ_MSG_MAX_PAYLOAD");
_Static_assert(offsetof(MSG_TELEMETRY_COUNTER_Payload_t, metric_id) == 0,
    "MSG_TELEMETRY_COUNTER layout changed — increment schema_id");

/* --- MSG_TELEMETRY_HISTOGRAM (0x0642) --- */
#define MSG_TELEMETRY_HISTOGRAM  0x0642u

typedef struct {
    uint16_t  metric_id;
    float     observation;
    uint8_t   unit_id;
} MSG_TELEMETRY_HISTOGRAM_Payload_t;

_Static_assert(sizeof(MSG_TELEMETRY_HISTOGRAM_Payload_t) <= EMBEDIQ_MSG_MAX_PAYLOAD,
    "MSG_TELEMETRY_HISTOGRAM payload exceeds EMBEDIQ_MSG_MAX_PAYLOAD");
_Static_assert(offsetof(MSG_TELEMETRY_HISTOGRAM_Payload_t, metric_id) == 0,
    "MSG_TELEMETRY_HISTOGRAM layout changed — increment schema_id");

/* --- MSG_TELEMETRY_BATCH (0x0643) --- */
#define MSG_TELEMETRY_BATCH  0x0643u

typedef struct {
    uint32_t  window_start_s;
    uint16_t  window_dur_s;
    uint8_t   entry_count;
    uint8_t   flags;
} MSG_TELEMETRY_BATCH_Payload_t;

_Static_assert(sizeof(MSG_TELEMETRY_BATCH_Payload_t) <= EMBEDIQ_MSG_MAX_PAYLOAD,
    "MSG_TELEMETRY_BATCH payload exceeds EMBEDIQ_MSG_MAX_PAYLOAD");
_Static_assert(offsetof(MSG_TELEMETRY_BATCH_Payload_t, window_start_s) == 0,
    "MSG_TELEMETRY_BATCH layout changed — increment schema_id");

#ifdef __cplusplus
}
#endif

#endif /* TELEMETRY_MSG_CATALOG_H */
