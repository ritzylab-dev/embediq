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
 * Event family band boundaries — used by EMBEDIQ_OBS_EVT_FAMILY() macro
 * Each family occupies a 16-slot band (0x10 stride).
 * ------------------------------------------------------------------------- */

#define EMBEDIQ_OBS_BAND_SYSTEM_START    0x10u
#define EMBEDIQ_OBS_BAND_MESSAGE_START   0x20u
#define EMBEDIQ_OBS_BAND_STATE_START     0x30u
#define EMBEDIQ_OBS_BAND_RESOURCE_START  0x40u
#define EMBEDIQ_OBS_BAND_TIMING_START    0x50u
#define EMBEDIQ_OBS_BAND_FAULT_START     0x60u
#define EMBEDIQ_OBS_BAND_FUNCTION_START  0x70u
#define EMBEDIQ_OBS_BAND_STRIDE          0x10u

/* ---------------------------------------------------------------------------
 * Event type constants — renumbered into family bands
 * ------------------------------------------------------------------------- */

/* SYSTEM family (0x10–0x1F) */
#define EMBEDIQ_OBS_EVT_OVERFLOW   0x10u  /**< Observatory ring buffer overflowed */

/* MESSAGE family (0x20–0x2F) */
#define EMBEDIQ_OBS_EVT_MSG_TX     0x20u  /**< Message published */
#define EMBEDIQ_OBS_EVT_MSG_RX     0x21u  /**< Message delivered to sub-fn */
#define EMBEDIQ_OBS_EVT_QUEUE_DROP 0x22u  /**< Message dropped: queue full */

/* STATE family (0x30–0x3F) */
#define EMBEDIQ_OBS_EVT_LIFECYCLE  0x30u  /**< FB lifecycle state transition */
#define EMBEDIQ_OBS_EVT_FSM_TRANS  0x31u  /**< FSM state transition */

/* RESOURCE family (0x40–0x4F) — reserved, not yet emitted */

/* TIMING family (0x50–0x5F) — reserved, not yet emitted */

/* FAULT family (0x60–0x6F) */
#define EMBEDIQ_OBS_EVT_FAULT      0x60u  /**< FB entered FAULT state */

/* FUNCTION family (0x70–0x7F) — reserved, not yet emitted */

/* ---------------------------------------------------------------------------
 * Family enum
 * ------------------------------------------------------------------------- */

typedef enum {
    EMBEDIQ_OBS_FAMILY_SYSTEM   = 0,
    EMBEDIQ_OBS_FAMILY_MESSAGE  = 1,
    EMBEDIQ_OBS_FAMILY_STATE    = 2,
    EMBEDIQ_OBS_FAMILY_RESOURCE = 3,
    EMBEDIQ_OBS_FAMILY_TIMING   = 4,
    EMBEDIQ_OBS_FAMILY_FAULT    = 5,
    EMBEDIQ_OBS_FAMILY_FUNCTION = 6,
    EMBEDIQ_OBS_FAMILY_UNKNOWN  = 7,
} embediq_obs_family_t;

/* Derive family from event_type band — zero runtime overhead when inlined */
static inline embediq_obs_family_t
embediq_obs_event_family(uint8_t event_type) {
    if (event_type < EMBEDIQ_OBS_BAND_MESSAGE_START)  return EMBEDIQ_OBS_FAMILY_SYSTEM;
    if (event_type < EMBEDIQ_OBS_BAND_STATE_START)    return EMBEDIQ_OBS_FAMILY_MESSAGE;
    if (event_type < EMBEDIQ_OBS_BAND_RESOURCE_START) return EMBEDIQ_OBS_FAMILY_STATE;
    if (event_type < EMBEDIQ_OBS_BAND_TIMING_START)   return EMBEDIQ_OBS_FAMILY_RESOURCE;
    if (event_type < EMBEDIQ_OBS_BAND_FAULT_START)    return EMBEDIQ_OBS_FAMILY_TIMING;
    if (event_type < EMBEDIQ_OBS_BAND_FUNCTION_START) return EMBEDIQ_OBS_FAMILY_FAULT;
    return EMBEDIQ_OBS_FAMILY_FUNCTION;
}

/* Convenience macro — expands to embediq_obs_family_t value */
#define EMBEDIQ_OBS_EVT_FAMILY(event_type)  embediq_obs_event_family(event_type)

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

/**
 * Set the Observatory verbosity level at runtime.
 *   0 — FAULT family (0x60–0x6F) + SYSTEM family (0x10–0x1F) only.
 *   1 — Level 0 + MESSAGE family (0x20–0x2F) + STATE family (0x30–0x3F).
 *   2 — All events pass.
 * No-op on MCU production builds (EMBEDIQ_OBS_LEVEL is compile-time only).
 */
void embediq_obs_set_level(uint8_t level);

#ifdef __cplusplus
}
#endif

#endif /* EMBEDIQ_OBS_H */
