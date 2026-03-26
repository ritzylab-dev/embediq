/*
 * thermostat_msg_catalog.h — Generated message catalog
 *
 * Source:    thermostat.iq
 * Namespace: com.embediq.thermostat
 * Schema:    version 1
 *
 * DO NOT EDIT — regenerate with:
 *   python3 tools/messages_iq/generate.py messages/thermostat.iq --out generated/ --output-name thermostat_msg_catalog.h
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef THERMOSTAT_MSG_CATALOG_H
#define THERMOSTAT_MSG_CATALOG_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "embediq_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --- MSG_TEMP_READING (0x1400) --- */
#define MSG_TEMP_READING  0x1400u

typedef struct {
    float  temperature_c;
} MSG_TEMP_READING_Payload_t;

_Static_assert(sizeof(MSG_TEMP_READING_Payload_t) <= EMBEDIQ_MSG_MAX_PAYLOAD,
    "MSG_TEMP_READING payload exceeds EMBEDIQ_MSG_MAX_PAYLOAD");
_Static_assert(offsetof(MSG_TEMP_READING_Payload_t, temperature_c) == 0,
    "MSG_TEMP_READING layout changed — increment schema_id");

#ifdef __cplusplus
}
#endif

#endif /* THERMOSTAT_MSG_CATALOG_H */
