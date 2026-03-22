/*
 * examples/thermostat/thermostat_msgs.h — Thermostat application message IDs
 *
 * Application-specific message IDs and payload types for the EmbedIQ smart
 * thermostat demo.  In production these would be generated from a
 * thermostat.iq schema file; for the Phase 1 gate demo they are hand-written
 * as the canonical example of application-level messaging.
 *
 * ID range: 0x0420–0x042F  (thermostat application namespace, AGENTS.md §3B)
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef THERMOSTAT_MSGS_H
#define THERMOSTAT_MSGS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
 * MSG_TEMP_READING (0x0420)
 *
 * Published by fb_temp_sensor every 1 second (on MSG_TIMER_1SEC).
 * Consumed by fb_temp_controller to drive the thermal FSM.
 * ------------------------------------------------------------------------- */

#define MSG_TEMP_READING  0x0420u

/**
 * Payload for MSG_TEMP_READING.
 * NOTE: In production this struct would be generated from thermostat.iq.
 */
typedef struct {
    float temperature_c;   /**< Current temperature in degrees Celsius. */
} TempReading_t;

#ifdef __cplusplus
}
#endif

#endif /* THERMOSTAT_MSGS_H */
