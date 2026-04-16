/*
 * embediq_telemetry.h — Telemetry application facade
 *
 * Hand-authored companion to the generated telemetry_msg_catalog.h.
 * Pulls in the generated message IDs and payload structs, and adds the
 * EmbedIQ_Telemetry_Unit_t enum referenced by the unit_id fields of
 * MSG_TELEMETRY_GAUGE / COUNTER / HISTOGRAM.
 *
 * Pattern: mirrors examples/thermostat/thermostat_msgs.h — generator output
 * stays pure (drift-check safe); any non-generated declaration that needs
 * to ship with the schema lives here.
 *
 * Application FBs include this one header to get both the message
 * contract and the unit enum:
 *
 *   #include "embediq_telemetry.h"
 *
 * Do not define message IDs or payload structs here manually — edit
 * messages/telemetry.iq and regenerate.
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef EMBEDIQ_TELEMETRY_H
#define EMBEDIQ_TELEMETRY_H

#include "telemetry_msg_catalog.h"

#ifdef __cplusplus
extern "C" {
#endif

/* EmbedIQ telemetry unit identifiers — resolved to strings by fb_cloud_mqtt */
typedef enum {
    EMBEDIQ_UNIT_PERCENT  = 0u,
    EMBEDIQ_UNIT_CELSIUS  = 1u,
    EMBEDIQ_UNIT_RPM      = 2u,
    EMBEDIQ_UNIT_G        = 3u,
    EMBEDIQ_UNIT_DB       = 4u,
    EMBEDIQ_UNIT_KW       = 5u,
    EMBEDIQ_UNIT_BAR      = 6u,
    EMBEDIQ_UNIT_MS       = 7u,
    EMBEDIQ_UNIT_COUNT    = 8u,
    EMBEDIQ_UNIT_BYTES    = 9u,
    EMBEDIQ_UNIT_MV       = 10u,
    EMBEDIQ_UNIT_AMPERE   = 11u,
    EMBEDIQ_UNIT_MPM      = 12u,
    EMBEDIQ_UNIT_LPM      = 13u,
    EMBEDIQ_UNIT_UNKNOWN  = 255u
} EmbedIQ_Telemetry_Unit_t;

#ifdef __cplusplus
}
#endif

#endif /* EMBEDIQ_TELEMETRY_H */
