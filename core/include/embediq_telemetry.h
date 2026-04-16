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

/* Metric type — value of the EmbedIQ_Telemetry_Batch_Entry_t.type field. */
typedef enum {
    EMBEDIQ_TELEMETRY_ENTRY_GAUGE     = 0u,
    EMBEDIQ_TELEMETRY_ENTRY_COUNTER   = 1u,
    EMBEDIQ_TELEMETRY_ENTRY_HISTOGRAM = 2u
} EmbedIQ_Telemetry_Entry_Type_t;

/*
 * One entry within a MSG_TELEMETRY_BATCH payload.
 *
 * Packed layout (always follows MSG_TELEMETRY_BATCH_Payload_t in the
 * message payload):
 *
 *   value_a:  gauge=avg   counter=total   histogram=mean
 *   value_b:  gauge=last  counter=0       histogram=min
 *   value_c:  gauge=max   counter=0       histogram=max
 *
 * Cast the raw payload bytes after the batch header to an array of
 * EmbedIQ_Telemetry_Batch_Entry_t[entry_count] to decode the batch.
 */
typedef struct __attribute__((packed)) {
    uint16_t metric_id;
    uint8_t  type;     /* EmbedIQ_Telemetry_Entry_Type_t */
    uint8_t  unit_id;  /* EmbedIQ_Telemetry_Unit_t */
    float    value_a;
    float    value_b;
    float    value_c;
    uint16_t count;    /* samples in this window (capped to UINT16_MAX) */
} EmbedIQ_Telemetry_Batch_Entry_t;

#define EMBEDIQ_TELEMETRY_ENTRY_SIZE  sizeof(EmbedIQ_Telemetry_Batch_Entry_t)

_Static_assert(sizeof(EmbedIQ_Telemetry_Batch_Entry_t) == 18u,
               "EmbedIQ_Telemetry_Batch_Entry_t must be 18 bytes packed "
               "(2+1+1+4+4+4+2)");

#ifdef __cplusplus
}
#endif

#endif /* EMBEDIQ_TELEMETRY_H */
