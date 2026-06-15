/*
 * provisioning_msg_catalog.h — Generated message catalog
 *
 * Source:    provisioning.iq
 * Namespace: com.embediq.provisioning
 * Schema:    version 1
 *
 * DO NOT EDIT — regenerate with:
 *   python3 tools/messages_iq/generate.py messages/provisioning.iq --out generated/ --output-name provisioning_msg_catalog.h
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef PROVISIONING_MSG_CATALOG_H
#define PROVISIONING_MSG_CATALOG_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "embediq_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --- MSG_PROV_IDENTITY_READY (0x0672) --- */
#define MSG_PROV_IDENTITY_READY  0x0672u

typedef struct {
    uint8_t  status;
} MSG_PROV_IDENTITY_READY_Payload_t;

_Static_assert(sizeof(MSG_PROV_IDENTITY_READY_Payload_t) <= EMBEDIQ_MSG_MAX_PAYLOAD,
    "MSG_PROV_IDENTITY_READY payload exceeds EMBEDIQ_MSG_MAX_PAYLOAD");
_Static_assert(offsetof(MSG_PROV_IDENTITY_READY_Payload_t, status) == 0,
    "MSG_PROV_IDENTITY_READY layout changed — increment schema_id");

/* --- MSG_PROV_CONFIG_RECEIVED (0x0673) --- */
#define MSG_PROV_CONFIG_RECEIVED  0x0673u

typedef struct {
    uint8_t  _reserved;
} MSG_PROV_CONFIG_RECEIVED_Payload_t;

_Static_assert(sizeof(MSG_PROV_CONFIG_RECEIVED_Payload_t) <= EMBEDIQ_MSG_MAX_PAYLOAD,
    "MSG_PROV_CONFIG_RECEIVED payload exceeds EMBEDIQ_MSG_MAX_PAYLOAD");
_Static_assert(offsetof(MSG_PROV_CONFIG_RECEIVED_Payload_t, _reserved) == 0,
    "MSG_PROV_CONFIG_RECEIVED layout changed — increment schema_id");

/* --- MSG_PROV_FACTORY_RESET (0x0674) --- */
#define MSG_PROV_FACTORY_RESET  0x0674u

typedef struct {
    uint8_t  reason;
} MSG_PROV_FACTORY_RESET_Payload_t;

_Static_assert(sizeof(MSG_PROV_FACTORY_RESET_Payload_t) <= EMBEDIQ_MSG_MAX_PAYLOAD,
    "MSG_PROV_FACTORY_RESET payload exceeds EMBEDIQ_MSG_MAX_PAYLOAD");
_Static_assert(offsetof(MSG_PROV_FACTORY_RESET_Payload_t, reason) == 0,
    "MSG_PROV_FACTORY_RESET layout changed — increment schema_id");

#ifdef __cplusplus
}
#endif

#endif /* PROVISIONING_MSG_CATALOG_H */
