#define _POSIX_C_SOURCE 200809L

/*
 * core/src/observatory/obs.c — EmbedIQ Observatory
 *
 * Implements embediq_obs_emit() and embediq_obs_set_transport() declared in
 * core/include/embediq_obs.h.
 *
 * RING BUFFER:
 *   Static array of EmbedIQ_Event_t, depth = EMBEDIQ_OBS_RING_DEPTH (256,
 *   power-of-2 so index masking works).  Written on every passing emit call.
 *   Oldest event overwritten on overflow (circular buffer semantics).
 *
 * SEQUENCE COUNTER:
 *   uint32_t sequence — monotonic, increments on every event that enters the
 *   ring.  Wraps are acceptable; use unsigned subtraction for gap detection.
 *   This is the canonical ordering source (Invariant I-13).
 *   NOTE: obs__reset() resets the counter for test isolation (host builds
 *   only).  In production firmware the counter never resets.
 *
 * RING BUFFER OVERFLOW:
 *   When ring is full:
 *     - Oldest entry overwritten (circular advance of head pointer).
 *     - Internal overflow counter incremented.
 *   On the NEXT call to embediq_obs_emit() when overflow counter > 0:
 *     - An EMBEDIQ_OBS_EVT_OVERFLOW event is written first (msg_id encodes
 *       the number of dropped events).
 *     - Overflow counter reset to zero.
 *     - Then the original event is written.
 *
 * TRANSPORT:
 *   EMBEDIQ_OBS_TRANSPORT_NULL   — discard; ring not written (production MCU).
 *   EMBEDIQ_OBS_TRANSPORT_STDOUT — ring written + human-readable stdout line.
 *   Default on host builds: STDOUT.
 *
 * LEVEL FILTERING (runtime, host builds):
 *   0: FAULT family (0x60–0x6F) + SYSTEM family (0x10–0x1F) only.
 *   1: Level 0 + MESSAGE family (0x20–0x2F) + STATE family (0x30–0x3F).
 *   2+: All events pass.
 *   Initialised from compile-time EMBEDIQ_OBS_LEVEL.
 *
 * MCU PRODUCTION:
 *   The entire function body is (void) + return — zero overhead.
 *   All ring/format code is inside #ifdef EMBEDIQ_PLATFORM_HOST.
 *
 * Package-internal API (EMBEDIQ_PLATFORM_HOST only):
 *   obs__reset()        — clear ring, counters, restore defaults
 *   obs__event_count()  — total events written to ring since reset
 *   obs__ring_count()   — events currently in ring
 *   obs__ring_read()    — read event at ring index (0 = oldest)
 *   obs__set_level()    — override runtime level (test use only)
 *   obs__force_overflow() — inject overflow count (test use only)
 *   obs__format_event() — format event to string (test use only)
 *
 * R-02: no malloc/free in this file.
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "embediq_obs.h"
#include "embediq_config.h"
#include "embediq_bus.h"    /* embediq_bus_resolve_id() for stdout name lookup */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#ifdef EMBEDIQ_PLATFORM_HOST
#  include <stdio.h>
#  include <stdlib.h>
#  include <time.h>
#endif

/* Stream ops — registered by platform HAL at boot. NULL = no file transport. */
static const embediq_obs_stream_ops_t *s_stream_ops = NULL;

/* ---------------------------------------------------------------------------
 * Host-only ring buffer state
 * All variables are static (no dynamic allocation — R-02).
 * ------------------------------------------------------------------------- */

#ifdef EMBEDIQ_PLATFORM_HOST

static EmbedIQ_Event_t g_ring[EMBEDIQ_OBS_RING_DEPTH];
static uint32_t g_ring_head  = 0u;  /* index of oldest stored event */
static uint32_t g_ring_count = 0u;  /* events currently in ring     */
static uint32_t g_sequence   = 0u;  /* monotonic sequence counter   */
static uint32_t g_total      = 0u;  /* total events written to ring */
static uint32_t g_overflow   = 0u;  /* drops pending overflow report */

static EmbedIQ_Obs_Transport_t g_transport = EMBEDIQ_OBS_TRANSPORT_STDOUT;
static uint8_t                 g_level     = EMBEDIQ_OBS_LEVEL;

static EmbedIQ_Obs_Session_t g_session;
static bool                  g_session_valid = false;

/* Power-of-2 mask: EMBEDIQ_OBS_RING_DEPTH must be a power of 2 (= 256). */
#define RING_MASK  ((uint32_t)(EMBEDIQ_OBS_RING_DEPTH) - 1u)

/* Local implementation constants — not in embediq_config.h (host-only details). */
#define OBS_EP_NAME_BUF    12    /* "ep_NNN" + margin */
#define OBS_LINE_BUF_SIZE  160   /* max formatted Observatory output line */

/* ---------------------------------------------------------------------------
 * Ring helpers
 * ------------------------------------------------------------------------- */

/**
 * Write evt into the ring.  If count_overflow is true and the ring was full,
 * the oldest entry is silently dropped and g_overflow is incremented.
 * If count_overflow is false (used for the overflow-recovery meta-event),
 * oldest is still dropped but g_overflow is NOT incremented — prevents the
 * overflow recovery event from re-triggering further overflow reports.
 */
static void ring_push(const EmbedIQ_Event_t *evt, bool count_overflow)
{
    if (g_ring_count == EMBEDIQ_OBS_RING_DEPTH) {
        /* Ring full — advance head, dropping the oldest entry. */
        g_ring_head = (g_ring_head + 1u) & RING_MASK;
        if (count_overflow) {
            g_overflow++;
        }
        /* g_ring_count stays EMBEDIQ_OBS_RING_DEPTH */
    } else {
        g_ring_count++;
    }
    uint32_t slot = (g_ring_head + g_ring_count - 1u) & RING_MASK;
    g_ring[slot]  = *evt;
}

/* ---------------------------------------------------------------------------
 * Level filter
 * ------------------------------------------------------------------------- */

static bool passes_level(uint8_t event_type)
{
    if (g_level >= 2u) return true;

    embediq_obs_family_t fam = embediq_obs_event_family(event_type);

    if (g_level >= 1u) {
        /* level 1: level 0 + MESSAGE + STATE families */
        return fam == EMBEDIQ_OBS_FAMILY_FAULT   ||
               fam == EMBEDIQ_OBS_FAMILY_SYSTEM  ||
               fam == EMBEDIQ_OBS_FAMILY_MESSAGE ||
               fam == EMBEDIQ_OBS_FAMILY_STATE;
    }
    /* level 0: FAULT and SYSTEM families only */
    return fam == EMBEDIQ_OBS_FAMILY_FAULT ||
           fam == EMBEDIQ_OBS_FAMILY_SYSTEM;
}

/* ---------------------------------------------------------------------------
 * Stdout formatting helpers
 * ------------------------------------------------------------------------- */

static const char *event_tag(uint8_t et)
{
    switch (et) {
        case EMBEDIQ_OBS_EVT_LIFECYCLE:  return "LC ";
        case EMBEDIQ_OBS_EVT_MSG_TX:     return "TX ";
        case EMBEDIQ_OBS_EVT_MSG_RX:     return "RX ";
        case EMBEDIQ_OBS_EVT_FSM_TRANS:  return "FSM";
        case EMBEDIQ_OBS_EVT_FAULT:      return "FLT";
        case EMBEDIQ_OBS_EVT_QUEUE_DROP: return "DRP";
        case EMBEDIQ_OBS_EVT_OVERFLOW:   return "OVF";
        default:                          return "???";
    }
}

static const char *lifecycle_state(uint8_t s)
{
    switch (s) {
        case 0: return "IDLE";
        case 1: return "INITIALISING";
        case 2: return "RUNNING";
        case 3: return "FAULT";
        case 4: return "STOPPING";
        case 5: return "STOPPED";
        default: return "?";
    }
}

/**
 * Resolve an endpoint index to its FB name.
 * Falls back to "ep_N" when not registered; uses "N/A" for 0xFF (broadcast).
 * Not thread-safe: uses a static buffer for the fallback string.
 */
static const char *fb_name(uint8_t id)
{
    if (id == 0xFFu) return "N/A";
    const char *n = embediq_bus_resolve_id(id);
    if (n) return n;
    static char fallback[OBS_EP_NAME_BUF];
    snprintf(fallback, sizeof(fallback), "ep_%u", (unsigned)id);
    return fallback;
}

static uint32_t obs_timestamp_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000000u +
                      (uint64_t)ts.tv_nsec / 1000u);
}

/**
 * Format evt into buf (NUL-terminated, buf[n-1] always \0).
 * Exposed as obs__format_event() for unit test inspection.
 *
 * Format by event type:
 *   LIFECYCLE:  [timestamp_us] LC  <fb> → N/A  <state_name>  seq=N
 *   FSM_TRANS:  [timestamp_us] FSM state_S → state_T  msg=0xXXXX  seq=N
 *   OVERFLOW:   [timestamp_us] OVF dropped=N  seq=N
 *   Others:     [timestamp_us] TAG <src_fb> → <tgt_fb>  msg=0xXXXX  seq=N
 */
static void format_event_internal(const EmbedIQ_Event_t *evt,
                                  char *buf, size_t n)
{
    if (evt->event_type == EMBEDIQ_OBS_EVT_LIFECYCLE) {
        snprintf(buf, n, "[%10u] %s %s → %s  %s  seq=%u",
                 (unsigned)evt->timestamp_us,
                 event_tag(evt->event_type),
                 fb_name(evt->source_fb_id),
                 fb_name(evt->target_fb_id),
                 lifecycle_state(evt->state_or_flag),
                 (unsigned)evt->sequence);
    } else if (evt->event_type == EMBEDIQ_OBS_EVT_FSM_TRANS) {
        /* source = old_state, target = new_state (not FB endpoint IDs) */
        snprintf(buf, n, "[%10u] %s state_%u → state_%u  msg=0x%04x  seq=%u",
                 (unsigned)evt->timestamp_us,
                 event_tag(evt->event_type),
                 (unsigned)evt->source_fb_id,
                 (unsigned)evt->target_fb_id,
                 (unsigned)evt->msg_id,
                 (unsigned)evt->sequence);
    } else if (evt->event_type == EMBEDIQ_OBS_EVT_OVERFLOW) {
        snprintf(buf, n, "[%10u] %s dropped=%u  seq=%u",
                 (unsigned)evt->timestamp_us,
                 event_tag(evt->event_type),
                 (unsigned)evt->msg_id,   /* overflow count packed here */
                 (unsigned)evt->sequence);
    } else {
        snprintf(buf, n, "[%10u] %s %s → %s  msg=0x%04x  seq=%u",
                 (unsigned)evt->timestamp_us,
                 event_tag(evt->event_type),
                 fb_name(evt->source_fb_id),
                 fb_name(evt->target_fb_id),
                 (unsigned)evt->msg_id,
                 (unsigned)evt->sequence);
    }
}

/* ---------------------------------------------------------------------------
 * TLV writer — used by capture_begin/end and the FILE transport emit path
 * ------------------------------------------------------------------------- */

static int write_tlv(uint16_t type, uint16_t length, const void *payload)
{
    if (!s_stream_ops || !s_stream_ops->write) return -1;
    uint8_t hdr[4];
    hdr[0] = (uint8_t)(type   & 0xFFu);
    hdr[1] = (uint8_t)(type   >> 8u);
    hdr[2] = (uint8_t)(length & 0xFFu);
    hdr[3] = (uint8_t)(length >> 8u);
    if (s_stream_ops->write(hdr, 4u) != 0) return -1;
    if (length > 0u && payload != NULL) {
        if (s_stream_ops->write(payload, length) != 0)
            return -1;
    }
    return 0;
}

#endif /* EMBEDIQ_PLATFORM_HOST */

/* ---------------------------------------------------------------------------
 * Public: embediq_obs_emit()
 * ------------------------------------------------------------------------- */

void embediq_obs_emit(uint8_t event_type, uint8_t source, uint8_t target,
                      uint8_t state, uint16_t msg_id)
{
#ifndef EMBEDIQ_PLATFORM_HOST
    /*
     * MCU production build: complete no-op.
     * Dead-code elimination removes all Observatory overhead at LEVEL 0.
     */
    (void)event_type; (void)source; (void)target;
    (void)state;      (void)msg_id;
    return;

#else /* EMBEDIQ_PLATFORM_HOST */

    /* NULL transport: discard everything, including ring writes. */
    if (g_transport == EMBEDIQ_OBS_TRANSPORT_NULL) return;

    /* Level filter: skip events below the configured verbosity. */
    if (!passes_level(event_type)) return;

    /* ------------------------------------------------------------------
     * Overflow recovery: before writing the incoming event, first emit a
     * meta-event reporting how many events were dropped.  The meta-event
     * uses ring_push(..., false) so it does NOT re-trigger overflow
     * counting if the ring is still full.
     * ------------------------------------------------------------------ */
    if (g_overflow > 0u) {
        uint32_t dropped = g_overflow;
        g_overflow = 0u;   /* reset before push to avoid re-entry issues */

        EmbedIQ_Event_t ovf = {
            .event_type    = EMBEDIQ_OBS_EVT_OVERFLOW,
            .source_fb_id  = 0xFFu,
            .target_fb_id  = 0xFFu,
            .state_or_flag = 0u,
            .msg_id        = (uint16_t)dropped,   /* pack drop count here */
            .timestamp_us  = obs_timestamp_us(),
            .sequence      = g_sequence++,
        };
        ring_push(&ovf, false);
        g_total++;

        if (g_transport == EMBEDIQ_OBS_TRANSPORT_STDOUT) {
            char line[OBS_LINE_BUF_SIZE];
            format_event_internal(&ovf, line, sizeof(line));
            printf("%s\n", line);
        }
    }

    /* Build the incoming event record. */
    EmbedIQ_Event_t evt = {
        .event_type    = event_type,
        .source_fb_id  = source,
        .target_fb_id  = target,
        .state_or_flag = state,
        .msg_id        = msg_id,
        .timestamp_us  = obs_timestamp_us(),
        .sequence      = g_sequence++,
    };

    ring_push(&evt, true);   /* may increment g_overflow if ring full */
    g_total++;

    if (g_transport == EMBEDIQ_OBS_TRANSPORT_STDOUT) {
        char line[OBS_LINE_BUF_SIZE];
        format_event_internal(&evt, line, sizeof(line));
        printf("%s\n", line);
    }

    if (g_transport == EMBEDIQ_OBS_TRANSPORT_FILE) {
        write_tlv(0x0002u, (uint16_t)sizeof(EmbedIQ_Event_t), &evt);
    }

#endif /* EMBEDIQ_PLATFORM_HOST */
}

/* ---------------------------------------------------------------------------
 * Public: embediq_obs_set_transport()
 * ------------------------------------------------------------------------- */

void embediq_obs_set_transport(EmbedIQ_Obs_Transport_t transport)
{
#ifdef EMBEDIQ_PLATFORM_HOST
    g_transport = transport;
#else
    (void)transport;
#endif
}

/* ---------------------------------------------------------------------------
 * Public: embediq_obs_set_level()
 * ------------------------------------------------------------------------- */

void embediq_obs_set_level(uint8_t level)
{
#ifdef EMBEDIQ_PLATFORM_HOST
    g_level = level;
#else
    (void)level;
#endif
}

/* ---------------------------------------------------------------------------
 * Public: embediq_obs_session_begin() / embediq_obs_session_get()
 * ------------------------------------------------------------------------- */

void embediq_obs_session_begin(const EmbedIQ_Obs_Session_t *session)
{
#ifdef EMBEDIQ_PLATFORM_HOST
    g_session       = *session;
    g_session_valid = true;
#else
    (void)session;
#endif
}

const EmbedIQ_Obs_Session_t *embediq_obs_session_get(void)
{
#ifdef EMBEDIQ_PLATFORM_HOST
    return g_session_valid ? &g_session : NULL;
#else
    return NULL;
#endif
}

/* ---------------------------------------------------------------------------
 * Public: embediq_obs_capture_begin() / embediq_obs_capture_end()
 * ------------------------------------------------------------------------- */

void embediq_obs_set_stream_ops(const embediq_obs_stream_ops_t *ops)
{
    s_stream_ops = ops;
}

int embediq_obs_capture_begin(const char *path)
{
#ifndef EMBEDIQ_PLATFORM_HOST
    (void)path;
    return -1;
#else
    if (!s_stream_ops || !s_stream_ops->open) return -1;

    const char *p = path ? path : getenv("EMBEDIQ_OBS_PATH");
    if (!p) p = "/tmp/embediq_capture.iqtrace";

    const EmbedIQ_Obs_Session_t *s = embediq_obs_session_get();
    if (!s) return -1;

    if (s_stream_ops->open(p) != 0) return -1;

    /* 8-byte file header: IQTR + version 1 LE + reserved */
    static const uint8_t hdr[] = {
        0x49u, 0x51u, 0x54u, 0x52u,
        0x01u, 0x00u,
        0x00u, 0x00u
    };
    if (s_stream_ops->write(hdr, (uint16_t)sizeof(hdr)) != 0) {
        s_stream_ops->close();
        return -1;
    }

    /* SESSION TLV */
    if (write_tlv(0x0001u, (uint16_t)sizeof(*s), s) != 0) {
        s_stream_ops->close();
        return -1;
    }

    if (s_stream_ops->flush) s_stream_ops->flush();
    g_transport = EMBEDIQ_OBS_TRANSPORT_FILE;
    return 0;
#endif
}

int embediq_obs_capture_end(void)
{
#ifndef EMBEDIQ_PLATFORM_HOST
    return -1;
#else
    int ret = write_tlv(0x0004u, 0u, NULL);  /* STREAM_END */
    if (s_stream_ops) {
        if (s_stream_ops->flush) s_stream_ops->flush();
        if (s_stream_ops->close) s_stream_ops->close();
    }
    g_transport = EMBEDIQ_OBS_TRANSPORT_NULL;
    return ret;
#endif
}

/* ---------------------------------------------------------------------------
 * Package-internal + test-only API (EMBEDIQ_PLATFORM_HOST)
 * ------------------------------------------------------------------------- */

#ifdef EMBEDIQ_PLATFORM_HOST

/**
 * Reset all Observatory state.
 * Called by fb_engine__reset() to provide clean isolation between test cases.
 * NOTE: in production firmware the sequence counter never resets.
 */
void obs__reset(void)
{
    if (s_stream_ops && s_stream_ops->close) s_stream_ops->close();
    memset(g_ring, 0, sizeof(g_ring));
    g_ring_head  = 0u;
    g_ring_count = 0u;
    g_sequence   = 0u;
    g_total      = 0u;
    g_overflow   = 0u;
    g_transport  = EMBEDIQ_OBS_TRANSPORT_STDOUT;
    g_level      = EMBEDIQ_OBS_LEVEL;
    memset(&g_session, 0, sizeof(g_session));
    g_session_valid = false;
}

/** Total events written to the ring since last reset. */
uint32_t obs__event_count(void)
{
    return g_total;
}

/** Number of events currently held in the ring buffer. */
uint32_t obs__ring_count(void)
{
    return g_ring_count;
}

/**
 * Read the event at position idx (0 = oldest event in ring).
 * Returns true on success, false if idx >= g_ring_count.
 */
bool obs__ring_read(uint32_t idx, EmbedIQ_Event_t *out)
{
    if (!out || idx >= g_ring_count) return false;
    *out = g_ring[(g_ring_head + idx) & RING_MASK];
    return true;
}

/** Override the runtime verbosity level (test use only). */
void obs__set_level(uint8_t level)
{
    g_level = level;
}

/**
 * Inject an overflow count — simulates the ring having been full and
 * dropping 'count' events, without needing to fill the 256-slot ring.
 * On the next embediq_obs_emit() call the overflow recovery event fires.
 */
void obs__force_overflow(uint32_t count)
{
    g_overflow = count;
}

/**
 * Format evt into buf using the human-readable Observatory line format.
 * Used by test_stdout_transport_format_contains_seq to inspect the format
 * without stdout redirection.
 */
void obs__format_event(const EmbedIQ_Event_t *evt, char *buf, size_t n)
{
    format_event_internal(evt, buf, n);
}

/* ---------------------------------------------------------------------------
 * ISR-safe emit — host implementation
 *
 * On the host (POSIX, single-writer), ISR context does not exist.
 * Delegate directly to embediq_obs_emit(). MCU implementation is Phase 2
 * (XOBS-1 gate) — the undefined symbol on MCU targets is intentional until
 * the ring buffer backend is implemented.
 * ------------------------------------------------------------------------- */
void embediq_obs_emit_from_isr(uint8_t event_type, uint8_t source,
                                uint8_t target, uint8_t state,
                                uint16_t msg_id)
{
    embediq_obs_emit(event_type, source, target, state, msg_id);
}

#endif /* EMBEDIQ_PLATFORM_HOST */
