/*
 * embediq_obs.h — Observatory contract
 *
 * Defines the fixed-layout event record and the emit/transport API for the
 * EmbedIQ Observatory — the built-in runtime observability layer.
 *
 * I-02: sizeof(EmbedIQ_Event_t) == 14 enforced by _Static_assert.
 * I-13: sequence is the canonical ordering field — never use timestamp_us
 *       for ordering or gap detection (wraps at ~71 min).
 * R-03: C11.
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef EMBEDIQ_OBS_H
#define EMBEDIQ_OBS_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
 * Observatory verbosity level fallback
 *
 * Normally set by embediq_config.h (included via embediq_msg.h).
 * This #ifndef guard provides a safe default when embediq_obs.h is included
 * before embediq_config.h, preventing a macro-redefinition warning.
 * ------------------------------------------------------------------------- */

#ifndef EMBEDIQ_OBS_LEVEL
#  ifdef EMBEDIQ_PLATFORM_HOST
#    define EMBEDIQ_OBS_LEVEL 1
#  else
#    define EMBEDIQ_OBS_LEVEL 0
#  endif
#endif

/* ---------------------------------------------------------------------------
 * Event type constants
 * ------------------------------------------------------------------------- */

#define EMBEDIQ_OBS_EVT_LIFECYCLE  0x01u  /**< FB state transition */
#define EMBEDIQ_OBS_EVT_MSG_TX     0x02u  /**< Message published */
#define EMBEDIQ_OBS_EVT_MSG_RX     0x03u  /**< Message delivered to sub-fn */
#define EMBEDIQ_OBS_EVT_FSM_TRANS  0x04u  /**< FSM state transition */
#define EMBEDIQ_OBS_EVT_FAULT      0x05u  /**< FB entered FAULT state */
#define EMBEDIQ_OBS_EVT_QUEUE_DROP 0x06u  /**< Message dropped due to queue overflow */
#define EMBEDIQ_OBS_EVT_OVERFLOW   0x07u  /**< Observatory ring buffer overflowed */

/* ---------------------------------------------------------------------------
 * Observatory event record — fixed layout, frozen at v1
 *
 * Fields:
 *   event_type    — one of EMBEDIQ_OBS_EVT_* constants above
 *   source_fb_id  — originating FB endpoint index
 *   target_fb_id  — destination FB endpoint index (0xFF = broadcast/N/A)
 *   state_or_flag — FB lifecycle state or context-specific flag
 *   msg_id        — associated message ID (0 if not message-related)
 *   timestamp_us  — INFORMATIONAL ONLY; wraps at ~71 min; do not use for
 *                   ordering or gap detection (I-13)
 *   sequence      — CANONICAL ordering field; monotonic, never resets;
 *                   use sequence delta for gap detection (I-13)
 * ------------------------------------------------------------------------- */

typedef struct __attribute__((packed)) {
    uint8_t   event_type;
    uint8_t   source_fb_id;
    uint8_t   target_fb_id;
    uint8_t   state_or_flag;
    uint16_t  msg_id;
    uint32_t  timestamp_us;  /* INFORMATIONAL — wraps ~71 min; never order by this */
    uint32_t  sequence;      /* CANONICAL ORDERING — monotonic, never resets */
} EmbedIQ_Event_t;

/* Invariant I-02: event record must be exactly 14 bytes on all targets.
 * If this assert fires, the struct layout has changed — architectural review
 * required before proceeding (CODING_RULES.md §8). */
_Static_assert(sizeof(EmbedIQ_Event_t) == 14,
    "EmbedIQ_Event_t must be exactly 14 bytes");

/* ---------------------------------------------------------------------------
 * Transport selector
 * ------------------------------------------------------------------------- */

typedef enum {
    EMBEDIQ_OBS_TRANSPORT_NULL   = 0, /**< Discard all events (production default) */
    EMBEDIQ_OBS_TRANSPORT_STDOUT = 1, /**< Print to stdout (host/debug) */
    EMBEDIQ_OBS_TRANSPORT_TCP    = 2, /**< Stream to EmbedIQ Studio via TCP */
} EmbedIQ_Obs_Transport_t;

/* ---------------------------------------------------------------------------
 * Public API — implemented in core/src/observatory/
 * ------------------------------------------------------------------------- */

/** Emit an Observatory event. No-op when EMBEDIQ_OBS_LEVEL == 0. */
void embediq_obs_emit(uint8_t event_type, uint8_t source, uint8_t target,
                      uint8_t state, uint16_t msg_id);

/** Set the active transport for all subsequent events. */
void embediq_obs_set_transport(EmbedIQ_Obs_Transport_t transport);

#ifdef __cplusplus
}
#endif

#endif /* EMBEDIQ_OBS_H */
