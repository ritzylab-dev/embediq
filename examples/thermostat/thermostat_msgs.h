/*
 * examples/thermostat/thermostat_msgs.h — Thermostat application message IDs
 *
 * Thin wrapper around the generated thermostat_msg_catalog.h.
 * MSG_TEMP_READING and MSG_TEMP_READING_Payload_t are defined there; this
 * header re-exports them and provides the TempReading_t backward-compatible
 * alias used throughout the thermostat FB implementation.
 *
 * Do not define message IDs or payload structs here manually; edit
 * messages/thermostat.iq and regenerate with:
 *   python3 tools/messages_iq/generate.py messages/thermostat.iq \
 *       --out generated/ --output-name thermostat_msg_catalog.h
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef THERMOSTAT_MSGS_H
#define THERMOSTAT_MSGS_H

#include "thermostat_msg_catalog.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Backward-compatible alias.
 * All existing code using TempReading_t continues to compile unchanged.
 */
typedef MSG_TEMP_READING_Payload_t TempReading_t;

#ifdef __cplusplus
}
#endif

#endif /* THERMOSTAT_MSGS_H */
