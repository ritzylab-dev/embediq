/*
 * embediq_msg.h — Message envelope contract
 *
 * Defines the fixed-size message envelope that every Functional Block
 * sends and receives. Layout is frozen at v1 — any change requires an
 * architectural review (CODING_RULES.md §8).
 *
 * I-03: offsetof(EmbedIQ_Msg_t, payload) == 20 enforced by _Static_assert.
 * R-03: C11. Fixed-width types from <stdint.h> only.
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef EMBEDIQ_MSG_H
#define EMBEDIQ_MSG_H

#include <stdint.h>
#include <stddef.h>
#include "embediq_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
 * Message priority — selects which per-FB queue receives the message
 * ------------------------------------------------------------------------- */

typedef enum {
    EMBEDIQ_MSG_PRIORITY_HIGH   = 0, /**< Blocks sender when queue full. Rare. */
    EMBEDIQ_MSG_PRIORITY_NORMAL = 1, /**< Drops oldest on overflow. Observable. */
    EMBEDIQ_MSG_PRIORITY_LOW    = 2  /**< Drops newest on overflow. Observable. */
} EmbedIQ_Msg_Priority_t;

/* ---------------------------------------------------------------------------
 * Message envelope — fixed layout, frozen at v1
 *
 * Fields:
 *   msg_id              — identifies message type (messages.iq is authoritative)
 *   source_endpoint_id  — sender's registered endpoint index
 *   target_endpoint_id  — recipient endpoint index; 0xFF = broadcast
 *   schema_id           — payload schema version (incremented on any change)
 *   payload_len         — actual bytes used in payload[]; <= EMBEDIQ_MSG_MAX_PAYLOAD
 *   correlation_id      — request/response pairing; 0 = fire-and-forget
 *   timestamp_us        — INFORMATIONAL ONLY; wraps at ~71 min (AGENTS.md §8)
 *   priority            — EmbedIQ_Msg_Priority_t cast to uint8_t
 *   _reserved[3]        — padding to align payload at byte 20; must be zero
 *   payload[]           — fixed-size body; schema defined in messages.iq
 *
 * Do NOT hand-write payload struct types — payload structs must be generated
 * from messages.iq (Invariant I-04, Forbidden pattern in CODING_RULES.md §5).
 * ------------------------------------------------------------------------- */

typedef struct __attribute__((packed)) {
    uint16_t  msg_id;
    uint8_t   source_endpoint_id;
    uint8_t   target_endpoint_id;           /* 0xFF = broadcast */
    uint16_t  schema_id;
    uint16_t  payload_len;
    uint32_t  correlation_id;
    uint32_t  timestamp_us;                 /* INFORMATIONAL ONLY — wraps ~71 min */
    uint8_t   priority;
    uint8_t   _reserved[3];                 /* must be zeroed by sender */
    uint8_t   payload[EMBEDIQ_MSG_MAX_PAYLOAD];
} EmbedIQ_Msg_t;

/* Invariant I-03: payload field must start at byte 20.
 * If this assert fires, the struct layout has changed — architectural review
 * required before proceeding (CODING_RULES.md §8). */
_Static_assert(offsetof(EmbedIQ_Msg_t, payload) == 20,
    "EmbedIQ_Msg_t header must be exactly 20 bytes");

#ifdef __cplusplus
}
#endif

#endif /* EMBEDIQ_MSG_H */
