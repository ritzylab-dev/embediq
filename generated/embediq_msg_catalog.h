/*
 * embediq_msg_catalog.h — Generated message catalog
 *
 * Source:    core.iq
 * Namespace: com.embediq.core
 * Schema:    version 1
 *
 * DO NOT EDIT — regenerate with:
 *   python3 tools/messages_iq/generate.py messages/core.iq --out generated/ --output-name embediq_msg_catalog.h
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef EMBEDIQ_MSG_CATALOG_H
#define EMBEDIQ_MSG_CATALOG_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "embediq_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --- MSG_FB_STATE_CHANGE (0x0001) --- */
#define MSG_FB_STATE_CHANGE  0x0001u

typedef struct {
    uint8_t  fb_id;
    uint8_t  new_state;
    uint8_t  prev_state;
    uint8_t  reason;
} MSG_FB_STATE_CHANGE_Payload_t;

_Static_assert(sizeof(MSG_FB_STATE_CHANGE_Payload_t) <= EMBEDIQ_MSG_MAX_PAYLOAD,
    "MSG_FB_STATE_CHANGE payload exceeds EMBEDIQ_MSG_MAX_PAYLOAD");
_Static_assert(offsetof(MSG_FB_STATE_CHANGE_Payload_t, fb_id) == 0,
    "MSG_FB_STATE_CHANGE layout changed — increment schema_id");

/* --- MSG_FB_FAULT (0x0002) --- */
#define MSG_FB_FAULT  0x0002u

typedef struct {
    uint8_t   fb_id;
    uint32_t  fault_code;
    uint8_t   _reserved;
} MSG_FB_FAULT_Payload_t;

_Static_assert(sizeof(MSG_FB_FAULT_Payload_t) <= EMBEDIQ_MSG_MAX_PAYLOAD,
    "MSG_FB_FAULT payload exceeds EMBEDIQ_MSG_MAX_PAYLOAD");
_Static_assert(offsetof(MSG_FB_FAULT_Payload_t, fb_id) == 0,
    "MSG_FB_FAULT layout changed — increment schema_id");

/* --- MSG_SYS_OTA_REQUEST (0x0003) --- */
#define MSG_SYS_OTA_REQUEST  0x0003u

typedef struct {
    uint8_t  reason;
} MSG_SYS_OTA_REQUEST_Payload_t;

_Static_assert(sizeof(MSG_SYS_OTA_REQUEST_Payload_t) <= EMBEDIQ_MSG_MAX_PAYLOAD,
    "MSG_SYS_OTA_REQUEST payload exceeds EMBEDIQ_MSG_MAX_PAYLOAD");
_Static_assert(offsetof(MSG_SYS_OTA_REQUEST_Payload_t, reason) == 0,
    "MSG_SYS_OTA_REQUEST layout changed — increment schema_id");

/* --- MSG_SYS_OTA_READY (0x0004) --- */
#define MSG_SYS_OTA_READY  0x0004u

typedef struct {
    uint8_t  fb_id;
} MSG_SYS_OTA_READY_Payload_t;

_Static_assert(sizeof(MSG_SYS_OTA_READY_Payload_t) <= EMBEDIQ_MSG_MAX_PAYLOAD,
    "MSG_SYS_OTA_READY payload exceeds EMBEDIQ_MSG_MAX_PAYLOAD");
_Static_assert(offsetof(MSG_SYS_OTA_READY_Payload_t, fb_id) == 0,
    "MSG_SYS_OTA_READY layout changed — increment schema_id");

/* --- MSG_SYS_SHUTDOWN (0x0005) --- */
#define MSG_SYS_SHUTDOWN  0x0005u

typedef struct {
    uint8_t  reason;
} MSG_SYS_SHUTDOWN_Payload_t;

_Static_assert(sizeof(MSG_SYS_SHUTDOWN_Payload_t) <= EMBEDIQ_MSG_MAX_PAYLOAD,
    "MSG_SYS_SHUTDOWN payload exceeds EMBEDIQ_MSG_MAX_PAYLOAD");
_Static_assert(offsetof(MSG_SYS_SHUTDOWN_Payload_t, reason) == 0,
    "MSG_SYS_SHUTDOWN layout changed — increment schema_id");

/* --- MSG_SYSTEM_READY (0x0020) --- */
#define MSG_SYSTEM_READY  0x0020u

typedef struct {
    uint8_t  boot_phase;
} MSG_SYSTEM_READY_Payload_t;

_Static_assert(sizeof(MSG_SYSTEM_READY_Payload_t) <= EMBEDIQ_MSG_MAX_PAYLOAD,
    "MSG_SYSTEM_READY payload exceeds EMBEDIQ_MSG_MAX_PAYLOAD");
_Static_assert(offsetof(MSG_SYSTEM_READY_Payload_t, boot_phase) == 0,
    "MSG_SYSTEM_READY layout changed — increment schema_id");

#ifdef __cplusplus
}
#endif

#endif /* EMBEDIQ_MSG_CATALOG_H */
