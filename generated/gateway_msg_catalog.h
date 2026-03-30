/*
 * gateway_msg_catalog.h — Generated message catalog
 *
 * Source:    gateway.iq
 * Namespace: com.embediq.gateway
 * Schema:    version 1
 *
 * DO NOT EDIT — regenerate with:
 *   python3 tools/messages_iq/generate.py messages/gateway.iq --out generated/ --output-name gateway_msg_catalog.h
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef GATEWAY_MSG_CATALOG_H
#define GATEWAY_MSG_CATALOG_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "embediq_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --- MSG_FIELD_READING (0x1410) --- */
#define MSG_FIELD_READING  0x1410u

typedef struct {
    uint8_t  sensor_id;
    uint8_t  unit_id;
    float    raw_value;
} MSG_FIELD_READING_Payload_t;

_Static_assert(sizeof(MSG_FIELD_READING_Payload_t) <= EMBEDIQ_MSG_MAX_PAYLOAD,
    "MSG_FIELD_READING payload exceeds EMBEDIQ_MSG_MAX_PAYLOAD");
_Static_assert(offsetof(MSG_FIELD_READING_Payload_t, sensor_id) == 0,
    "MSG_FIELD_READING layout changed — increment schema_id");

/* --- MSG_ALERT (0x1411) --- */
#define MSG_ALERT  0x1411u

typedef struct {
    uint8_t  sensor_id;
    bool     active;
    float    value;
    float    threshold;
} MSG_ALERT_Payload_t;

_Static_assert(sizeof(MSG_ALERT_Payload_t) <= EMBEDIQ_MSG_MAX_PAYLOAD,
    "MSG_ALERT payload exceeds EMBEDIQ_MSG_MAX_PAYLOAD");
_Static_assert(offsetof(MSG_ALERT_Payload_t, sensor_id) == 0,
    "MSG_ALERT layout changed — increment schema_id");

/* --- MSG_TELEMETRY (0x1412) --- */
#define MSG_TELEMETRY  0x1412u

typedef struct {
    uint8_t   sensor_id;
    uint16_t  seq_num;
    float     value;
} MSG_TELEMETRY_Payload_t;

_Static_assert(sizeof(MSG_TELEMETRY_Payload_t) <= EMBEDIQ_MSG_MAX_PAYLOAD,
    "MSG_TELEMETRY payload exceeds EMBEDIQ_MSG_MAX_PAYLOAD");
_Static_assert(offsetof(MSG_TELEMETRY_Payload_t, sensor_id) == 0,
    "MSG_TELEMETRY layout changed — increment schema_id");

#ifdef __cplusplus
}
#endif

#endif /* GATEWAY_MSG_CATALOG_H */
