/*
 * examples/gateway/gateway_msgs.h — Gateway application message IDs
 *
 * Thin wrapper around the generated gateway_msg_catalog.h.
 * Provides sensor_id and unit_id constants for use across all gateway FBs.
 *
 * Do not define message IDs or payload structs here manually; edit
 * messages/gateway.iq and regenerate with:
 *   python3 tools/messages_iq/generate.py messages/gateway.iq \
 *       --out generated/ --output-name gateway_msg_catalog.h
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef GATEWAY_MSGS_H
#define GATEWAY_MSGS_H

#include "gateway_msg_catalog.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
 * Sensor IDs — used in MSG_FIELD_READING, MSG_ALERT, MSG_TELEMETRY
 * ------------------------------------------------------------------------- */

#define SENSOR_TANK_TEMP      0u   /**< Tank A temperature, deg C  */
#define SENSOR_TANK_PRESSURE  1u   /**< Tank A pressure, bar       */
#define SENSOR_PUMP_FLOW      2u   /**< Pump 1 flow rate, L/min    */
#define SENSOR_COUNT          3u

/* ---------------------------------------------------------------------------
 * Unit IDs — used in MSG_FIELD_READING.unit_id
 * ------------------------------------------------------------------------- */

#define UNIT_CELSIUS  0u
#define UNIT_BAR      1u
#define UNIT_LPM      2u

/* ---------------------------------------------------------------------------
 * Alert thresholds
 * ------------------------------------------------------------------------- */

#define THRESH_TANK_TEMP_HIGH_C     70.0f   /**< Alert when temp > 70 deg C    */
#define THRESH_TANK_PRESSURE_HIGH   8.5f    /**< Alert when pressure > 8.5 bar */
#define THRESH_PUMP_FLOW_LOW        5.0f    /**< Alert when flow < 5.0 L/min   */

#ifdef __cplusplus
}
#endif

#endif /* GATEWAY_MSGS_H */
