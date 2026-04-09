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
 * source_fb_id namespace (uint8_t)
 *
 * Disambiguation contract: source_fb_id < 0xC0 identifies an FB endpoint
 * instance (index into the endpoint table). source_fb_id >= 0xC0 identifies
 * a non-FB layer source. Decoders MUST branch on this boundary.
 *
 * 0x00–0xBF  FB endpoint indices         (192 slots; EMBEDIQ_MAX_ENDPOINTS = 64)
 * 0xC0–0xCF  OSAL resource type IDs      (16 slots)
 * 0xD0–0xDF  HAL peripheral source IDs   (16 slots)
 * 0xE0–0xEF  Library source IDs          (16 slots — named constraint)
 * 0xF0–0xFF  Reserve
 *
 * See: ARCHITECTURE.md — Phase 2 MCU Observatory, source_fb_id section
 * ------------------------------------------------------------------------- */

/* OSAL resource type IDs (0xC0–0xCF) */
#define EMBEDIQ_OSAL_SRC_TASKS    0xC0u  /**< OSAL task pool */
#define EMBEDIQ_OSAL_SRC_QUEUES   0xC1u  /**< OSAL queue pool */
#define EMBEDIQ_OSAL_SRC_MUTEXES  0xC2u  /**< OSAL mutex pool */
#define EMBEDIQ_OSAL_SRC_SIGNALS  0xC3u  /**< OSAL signal pool */
#define EMBEDIQ_OSAL_SRC_SEMS     0xC4u  /**< OSAL semaphore pool */
/* 0xC5–0xCF reserved for future OSAL resource types */

/* HAL peripheral source IDs (0xD0–0xDF) */
#define EMBEDIQ_HAL_SRC_UART      0xD0u
#define EMBEDIQ_HAL_SRC_I2C       0xD1u
#define EMBEDIQ_HAL_SRC_SPI       0xD2u
#define EMBEDIQ_HAL_SRC_GPIO      0xD3u
#define EMBEDIQ_HAL_SRC_FLASH     0xD4u
#define EMBEDIQ_HAL_SRC_TIMER     0xD5u
#define EMBEDIQ_HAL_SRC_WDG       0xD6u
/* 0xD7–0xDF reserved for additional HAL peripherals */

/* Library source IDs (0xE0–0xEF) — assigned by embediq_platform_lib_declare() */
#define EMBEDIQ_LIB_SRC_BASE      0xE0u  /**< First library source slot */
#define EMBEDIQ_LIB_SRC_MAX       0xEFu  /**< Last library source slot */
/* Maximum 16 instrumented library sources per firmware build (named constraint).
 * Slot 17 causes embediq_platform_lib_declare() to return EMBEDIQ_ERR. */
/* 0xF0–0xFF reserved */

/* ---------------------------------------------------------------------------
 * Event type constants — organised by family band
 *
 * Decision A  — existing constants (0x10, 0x20–0x22, 0x30–0x31, 0x60): FROZEN.
 *               These map directly to EmbedIQ_Event_t.event_type.
 *               Do NOT renumber. Do NOT remove. Binary compatibility.
 *
 * Decision B  — 14 new constants across SYSTEM, FAULT, and FUNCTION bands.
 *               Additive only — no existing constant is changed.
 *
 * Decision J  — 4 AI Phase-1 constants in SYSTEM band (0x17–0x1A).
 *               CON-05: AI block wins 0x17–0x1A; system-ops shifted to 0x1B–0x1D.
 *
 * Phase-3 AI family band (0x80–0x8F): reserved by comment — see bottom of section.
 * ------------------------------------------------------------------------- */

/* SYSTEM family (0x10–0x1F) ----------------------------------------------- */

/* Existing — FROZEN */
#define EMBEDIQ_OBS_EVT_OVERFLOW            0x10u /**< Observatory ring buffer overflowed.
                                                        An overflow event IS an audit gap record — by design. */
/* New — Decision B: system lifecycle */
#define EMBEDIQ_OBS_EVT_BOOT                0x11u /**< Boot event.
                                                        state_or_flag: 0=normal, 1=watchdog, 2=power-on, 3=SW-reset */
#define EMBEDIQ_OBS_EVT_SHUTDOWN            0x12u /**< Controlled, clean shutdown */
#define EMBEDIQ_OBS_EVT_WATCHDOG_RST        0x13u /**< Unclean watchdog reset.
                                                        Distinct from SHUTDOWN — diagnostic coverage evidence
                                                        for IEC 61508 / ISO 26262. */
#define EMBEDIQ_OBS_EVT_HEARTBEAT           0x14u /**< Periodic liveness heartbeat.
                                                        Diagnostic coverage (IEC 61508 Table A.12). */
#define EMBEDIQ_OBS_EVT_OTA_UPDATE          0x15u /**< OTA firmware update.
                                                        fw_version in session header captures before/after (R156). */
#define EMBEDIQ_OBS_EVT_CONFIG_CHANGE       0x16u /**< Configuration parameter changed.
                                                        Audit trail for NERC CIP-010 / IEC 62443. */

/* New — Decision J: AI Phase-1 block (CON-05: AI wins 0x17–0x1A) */
#define EMBEDIQ_OBS_EVT_AI_INFERENCE_START  0x17u /**< AI model inference begins.
                                                        EU AI Act Art.12 logging requirement. */
#define EMBEDIQ_OBS_EVT_AI_INFERENCE_END    0x18u /**< AI model inference completes.
                                                        Pair with AI_INFERENCE_START for latency measurement. */
#define EMBEDIQ_OBS_EVT_AI_MODEL_LOAD       0x19u /**< AI model loaded into memory.
                                                        session_id links all subsequent inferences to this load. */
#define EMBEDIQ_OBS_EVT_AI_CONFIDENCE_THRESHOLD 0x1Au /**< AI confidence dropped below threshold.
                                                        EU AI Act Art.13 explainability event.
                                                        state_or_flag: threshold value (0–255 = 0–100%). */

/* New — Decision B: system operations (shifted from 0x17–0x1D per CON-05) */
#define EMBEDIQ_OBS_EVT_MAINTENANCE_MODE    0x1Bu /**< Device entered maintenance mode */
#define EMBEDIQ_OBS_EVT_FW_START            0x1Cu /**< Firmware execution start (post-bootloader) */
#define EMBEDIQ_OBS_EVT_CONFIG_LOAD         0x1Du /**< Configuration loaded from persistent storage */

/* 0x1E–0x1F: reserved — future SYSTEM band expansion */

/* MESSAGE family (0x20–0x2F) ---------------------------------------------- */

/* Existing — FROZEN */
#define EMBEDIQ_OBS_EVT_MSG_TX              0x20u /**< Message published to bus */
#define EMBEDIQ_OBS_EVT_MSG_RX              0x21u /**< Message delivered to subscriber FB */
#define EMBEDIQ_OBS_EVT_QUEUE_DROP          0x22u /**< Message dropped — queue full */

/* STATE family (0x30–0x3F) ------------------------------------------------ */

/* Existing — FROZEN */
#define EMBEDIQ_OBS_EVT_LIFECYCLE           0x30u /**< FB lifecycle state transition */
#define EMBEDIQ_OBS_EVT_FSM_TRANS           0x31u /**< FSM state machine transition */

/* RESOURCE family (0x40–0x4F) — resource acquisition and lifecycle */
#define EMBEDIQ_OBS_EVT_LIB_INIT    0x40u  /**< Library global init completed (once per boot per library) */
#define EMBEDIQ_OBS_EVT_LIB_DEINIT  0x41u  /**< Library global deinit called at shutdown */
/* 0x42–0x4F reserved for Phase 2 OSAL and HAL resource events (XOBS-1, XOBS-2) */

/* TIMING family (0x50–0x5F) — reserved, not yet emitted */

/* FAULT family (0x60–0x6F) ------------------------------------------------ */

/* Existing — FROZEN */
#define EMBEDIQ_OBS_EVT_FAULT               0x60u /**< FB entered FAULT state */

/* New — Decision B: security and fault lifecycle */
#define EMBEDIQ_OBS_EVT_SECURITY_INCIDENT   0x61u /**< Security incident detected.
                                                        IEC 62443 / R155 CSMS / EU CRA / PCI DSS. */
#define EMBEDIQ_OBS_EVT_AUTH_EVENT          0x62u /**< Authentication event (login/logout/fail).
                                                        PCI DSS Req.10 / IEC 62443 / ISO 27001. */
#define EMBEDIQ_OBS_EVT_FAULT_CLEARED       0x63u /**< Previously reported fault cleared.
                                                        Closes the fault lifecycle. */
/* 0x64–0x65 reserved for future library fault events */
#define EMBEDIQ_OBS_EVT_OSAL_FAULT  0x66u  /**< OSAL resource fault (XOBS-1).
                                                 source_fb_id: EMBEDIQ_OSAL_SRC_* (tasks/queues/mutexes/signals).
                                                 state_or_flag: 0=task_create_fail  1=mutex_timeout
                                                                2=queue_send_full   3=signal_timeout
                                                                4=stack_overflow.
                                                 msg_id: resource instance index (0 on POSIX). */
/* 0x67: EMBEDIQ_OBS_EVT_HAL_FAULT — reserved, activated in XOBS-2 (before ESP32 HAL) */
/* 0x68–0x6F reserved */

/* FUNCTION family (0x70–0x7F) --------------------------------------------- */

/* New — Decision B: certification test markers */
#define EMBEDIQ_OBS_EVT_TEST_START          0x70u /**< Certification test case start.
                                                        DO-178C / EN 50128 test evidence in audit trail.
                                                        msg_id = test case ID. */
#define EMBEDIQ_OBS_EVT_TEST_END            0x71u /**< Certification test case end.
                                                        state_or_flag: 0=pass, 1=fail, 2=inconclusive. */

/* 0x72–0x7F: reserved — future FUNCTION band expansion */

/*
 * AI EVENT FAMILY BAND — PHASE 3 RESERVATION
 * -------------------------------------------
 * 0x80–0x8F: reserved for the AI event family (Phase 3).
 *
 * DO NOT allocate constants in this range before the Phase 3 AI event
 * taxonomy specification is published. Reservation prevents third-party
 * squatting on this range.
 *
 * Planned Phase-3 constants (taxonomy TBD):
 *   AI_POLICY_ALLOW, AI_POLICY_BLOCK, AI_MODEL_UPDATE,
 *   AI_DRIFT_DETECTED, AI_EXPLAINABILITY_LOG, and others.
 *
 * Prerequisite: ≥2 production deployments using Phase-1 AI constants
 * (0x17–0x1A) before Phase-3 band is finalised.
 *
 * See: docs/architecture/AI_FIRST_ARCHITECTURE.md
 *      ROADMAP.md — Phase 3: Full AI Event Family Band
 */

/* ---------------------------------------------------------------------------
 * Family enum
 *
 * NOTE: Enum and callback typedefs use lowercase_t naming intentionally —
 * this is the established convention for non-struct types in EmbedIQ Core.
 * Struct types use EmbedIQ_PascalCase_t. Do not rename.
 * ------------------------------------------------------------------------- */

typedef enum {
    EMBEDIQ_OBS_FAMILY_SYSTEM   = 0,
    EMBEDIQ_OBS_FAMILY_MESSAGE  = 1,
    EMBEDIQ_OBS_FAMILY_STATE    = 2,
    EMBEDIQ_OBS_FAMILY_RESOURCE = 3,
    EMBEDIQ_OBS_FAMILY_TIMING   = 4,
    EMBEDIQ_OBS_FAMILY_FAULT    = 5,
    EMBEDIQ_OBS_FAMILY_FUNCTION = 6,
    EMBEDIQ_OBS_FAMILY_AI       = 7,  /**< Phase 3 reserved band (0x80–0x8F) */
    EMBEDIQ_OBS_FAMILY_VENDOR   = 8,  /**< Community/vendor extension (0x90–0xFF) */
    /* NOTE: EMBEDIQ_OBS_FAMILY_UNKNOWN integer value is 9 as of this release.
     * It was 7 in v0.1.x. Named-constant usage is safe across all versions.
     * Any code comparing against the raw integer 7 to mean UNKNOWN must be updated. */
    EMBEDIQ_OBS_FAMILY_UNKNOWN  = 9,
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
    if (event_type >= 0x90u) return EMBEDIQ_OBS_FAMILY_VENDOR;
    if (event_type >= 0x80u) return EMBEDIQ_OBS_FAMILY_AI;
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
    EMBEDIQ_OBS_TRANSPORT_TCP    = 2, /**< Stream to remote viewer via TCP */
    EMBEDIQ_OBS_TRANSPORT_FILE   = 3, /**< Write .iqtrace binary capture */
} EmbedIQ_Obs_Transport_t;

/* ---------------------------------------------------------------------------
 * Stream ops table — pluggable binary transport for .iqtrace file capture
 *
 * The Observatory core calls these function pointers for file I/O.
 * HAL implementations register an ops table at boot via
 * embediq_obs_set_stream_ops(). If no ops are registered, file capture
 * is silently disabled (capture_begin returns -1).
 *
 * This abstraction removes the Layer 1 Core → HAL dependency: obs.c
 * never includes any hal/ header.
 * ------------------------------------------------------------------------- */

typedef struct {
    int  (*open)(const char *path);          /**< Open/create a binary stream */
    int  (*write)(const void *data, uint16_t len); /**< Append bytes */
    int  (*flush)(void);                     /**< Flush buffered bytes */
    int  (*close)(void);                     /**< Close the stream */
} embediq_obs_stream_ops_t;

/**
 * Register the binary stream transport implementation.
 * Called once at platform init (before any capture_begin call).
 * The Observatory stores the pointer — ops must remain valid for process lifetime.
 */
void embediq_obs_set_stream_ops(const embediq_obs_stream_ops_t *ops);

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

/**
 * embediq_obs_emit_from_isr() — ISR-safe event emission. Declared void.
 *
 * Fire-and-forget: the ISR does not wait for emission to complete and must
 * not attempt overflow handling. Overflow detection occurs exclusively in
 * the observer task.
 *
 * On host (EMBEDIQ_PLATFORM_HOST): delegates to embediq_obs_emit().
 * On MCU (Phase 2): uses ISR-reentrant ring write (XOBS-1 gate).
 *
 * The _from_isr suffix ensures ISR call sites are grep-auditable.
 */
void embediq_obs_emit_from_isr(uint8_t event_type, uint8_t source,
                                uint8_t target, uint8_t state,
                                uint16_t msg_id);

#ifdef __cplusplus
}
#endif

#endif /* EMBEDIQ_OBS_H */
