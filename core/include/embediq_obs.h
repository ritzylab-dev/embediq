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
 * Compile-time-gated emit macros — one per event family.
 *
 * Usage:
 *   EMBEDIQ_OBS_EMIT_STATE(event_type, source, target, state, msg_id)
 *   EMBEDIQ_OBS_EMIT_MESSAGE(event_type, source, target, state, msg_id)
 *   etc.
 *
 * Each macro expands to embediq_obs_emit() when its family flag is enabled,
 * or to ((void)0) when disabled — allowing the compiler to eliminate dead
 * code completely in production builds.
 *
 * FAULT and SYSTEM families are unconditional — use embediq_obs_emit()
 * directly for those event types.
 * ------------------------------------------------------------------------- */

/* embediq_obs.h is included from embediq_config.h consumers but also
 * directly. Include embediq_config.h here only if not already included. */
#ifndef EMBEDIQ_CONFIG_H
#  include "embediq_config.h"
#endif

#if EMBEDIQ_TRACE_MESSAGE
#  define EMBEDIQ_OBS_EMIT_MESSAGE(evt, src, tgt, state, msg) \
     embediq_obs_emit((evt), (src), (tgt), (state), (msg))
#else
#  define EMBEDIQ_OBS_EMIT_MESSAGE(evt, src, tgt, state, msg)  ((void)0)
#endif

#if EMBEDIQ_TRACE_STATE
#  define EMBEDIQ_OBS_EMIT_STATE(evt, src, tgt, state, msg) \
     embediq_obs_emit((evt), (src), (tgt), (state), (msg))
#else
#  define EMBEDIQ_OBS_EMIT_STATE(evt, src, tgt, state, msg)    ((void)0)
#endif

#if EMBEDIQ_TRACE_RESOURCE
#  define EMBEDIQ_OBS_EMIT_RESOURCE(evt, src, tgt, state, msg) \
     embediq_obs_emit((evt), (src), (tgt), (state), (msg))
#else
#  define EMBEDIQ_OBS_EMIT_RESOURCE(evt, src, tgt, state, msg) ((void)0)
#endif

#if EMBEDIQ_TRACE_TIMING
#  define EMBEDIQ_OBS_EMIT_TIMING(evt, src, tgt, state, msg) \
     embediq_obs_emit((evt), (src), (tgt), (state), (msg))
#else
#  define EMBEDIQ_OBS_EMIT_TIMING(evt, src, tgt, state, msg)   ((void)0)
#endif

#if EMBEDIQ_TRACE_FUNCTION
#  define EMBEDIQ_OBS_EMIT_FUNCTION(evt, src, tgt, state, msg) \
     embediq_obs_emit((evt), (src), (tgt), (state), (msg))
#else
#  define EMBEDIQ_OBS_EMIT_FUNCTION(evt, src, tgt, state, msg) ((void)0)
#endif

/* FAULT and SYSTEM families are always enabled — emit directly */
#define EMBEDIQ_OBS_EMIT_FAULT(evt, src, tgt, state, msg) \
    embediq_obs_emit((evt), (src), (tgt), (state), (msg))
#define EMBEDIQ_OBS_EMIT_SYSTEM(evt, src, tgt, state, msg) \
    embediq_obs_emit((evt), (src), (tgt), (state), (msg))

/* ---------------------------------------------------------------------------
 * Platform identifier
 * Used in EmbedIQ_Obs_Session_t to identify the runtime environment.
 * ------------------------------------------------------------------------- */
typedef enum {
    EMBEDIQ_OBS_PLATFORM_POSIX      = 0, /**< Linux / macOS host build */
    EMBEDIQ_OBS_PLATFORM_FREERTOS   = 1, /**< FreeRTOS target */
    EMBEDIQ_OBS_PLATFORM_BAREMETAL  = 2, /**< Bare-metal, no RTOS */
    EMBEDIQ_OBS_PLATFORM_ZEPHYR     = 3, /**< Zephyr RTOS */
    EMBEDIQ_OBS_PLATFORM_UNKNOWN    = 0xFF,
} embediq_obs_platform_t;

/* ---------------------------------------------------------------------------
 * Session metadata — written once at the start of every .iqtrace capture.
 *
 * Caller fills all fields and passes to embediq_obs_session_begin().
 * The Observatory stores a copy and the transport (Obs-5) writes it as
 * the first TLV record in the .iqtrace stream.
 *
 * fw_version packing:  bits[31:16] = major, bits[15:8] = minor,
 *                      bits[7:0]   = patch
 * Use EMBEDIQ_OBS_FW_VERSION(maj, min, pat) to build the packed value.
 *
 * build_id: null-terminated string — recommend 8-char git short hash.
 *           Unused bytes must be zero.
 *
 * timestamp_base_us: absolute Unix time in microseconds at session start.
 *   Set to 0 if wall-clock time is unavailable (bare-metal without RTC).
 *
 * session_id: monotonic counter incremented by the caller on each power
 *   cycle or reboot (e.g. stored in NVM). Set to 0 if not tracked.
 *
 * I-14: sizeof(EmbedIQ_Obs_Session_t) == 40, enforced by _Static_assert.
 * ------------------------------------------------------------------------- */

/** Maximum length of the build_id field (including NUL terminator). */
#define EMBEDIQ_OBS_BUILD_ID_LEN  16

typedef struct {
    uint32_t device_id;          /**< Application-assigned device identifier */
    uint32_t fw_version;         /**< Packed firmware version — see above     */
    uint64_t timestamp_base_us;  /**< Unix time us at session start (0 = N/A) */
    uint32_t session_id;         /**< Monotonic per-boot session counter       */
    uint8_t  platform_id;        /**< embediq_obs_platform_t                  */
    uint8_t  trace_level;        /**< EMBEDIQ_TRACE_LEVEL at session start     */
    uint8_t  _pad[2];            /**< Reserved — must be zero                 */
    char     build_id[EMBEDIQ_OBS_BUILD_ID_LEN]; /**< Null-terminated build tag */
} EmbedIQ_Obs_Session_t;

_Static_assert(sizeof(EmbedIQ_Obs_Session_t) == 40,
    "EmbedIQ_Obs_Session_t must be exactly 40 bytes (I-14)");

/* Helper macro — build a packed fw_version value */
#define EMBEDIQ_OBS_FW_VERSION(maj, min, pat) \
    (((uint32_t)(maj) << 16) | ((uint32_t)(min) << 8) | (uint32_t)(pat))

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
    EMBEDIQ_OBS_TRANSPORT_FILE   = 3, /**< Write .iqtrace binary capture */
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

/**
 * Store the active session record.
 * Must be called once before the first event is emitted for a session.
 * The Observatory copies the struct — caller does not need to retain it.
 * Thread-safe on host builds (single-writer assumption on MCU).
 */
void embediq_obs_session_begin(const EmbedIQ_Obs_Session_t *session);

/**
 * Return a const pointer to the stored session record.
 * Returns NULL if embediq_obs_session_begin() has not been called.
 * The pointer is valid until the next call to embediq_obs_session_begin()
 * or obs__reset() (host builds only).
 */
const EmbedIQ_Obs_Session_t *embediq_obs_session_get(void);

/**
 * Begin a binary .iqtrace capture.
 * path: file path to write. If NULL, reads EMBEDIQ_OBS_PATH env var.
 * Requires embediq_obs_session_begin() to have been called first.
 * Writes file header (8 bytes) + SESSION TLV (44 bytes).
 * Sets transport to EMBEDIQ_OBS_TRANSPORT_FILE on success.
 * Returns 0 on success, -1 if no session set or file open fails.
 * No-op and returns -1 on MCU builds.
 */
int embediq_obs_capture_begin(const char *path);

/**
 * End the binary .iqtrace capture.
 * Writes STREAM_END TLV, flushes and closes the file.
 * Resets transport to EMBEDIQ_OBS_TRANSPORT_NULL.
 * Returns 0 on success, -1 on error.
 * No-op and returns -1 on MCU builds.
 */
int embediq_obs_capture_end(void);

#ifdef __cplusplus
}
#endif

#endif /* EMBEDIQ_OBS_H */
