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
 * THREAD SAFETY (Set 2 / F2) — three layers, never nested the other way:
 *   1. s_transport_mutex (OSAL mutex) serialises a whole thread-path emit
 *      across threads, so .iqtrace / stdout order == sequence order for
 *      thread-path events and TLV frames are never interleaved.
 *   2. The OSAL critical section guards ONLY the ring, its counters, the
 *      overflow count and the sequence assignment. No I/O inside it.
 *   3. g_sequence, g_transport and g_level are OSAL atomics.
 *   embediq_obs_emit_from_isr() takes none of the locks: it pushes into a
 *   lock-free multi-producer ISR ring (EMBEDIQ_OBS_ISR_RING_DEPTH), drained
 *   into the main ring — and written to the transport — by the next
 *   thread-path emit or capture_end(). A stream-HAL write failure that
 *   reports through EMBEDIQ_HAL_OBS_EMIT_ERROR (→ emit_from_isr) therefore
 *   never re-enters the transport it was called from (T1-5).
 *   Events emitted before embediq_obs_init() (any setup call or engine boot)
 *   are recorded in the ring but not written to a transport.
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
#include "embediq_osal.h"   /* critical section, atomics, transport mutex */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#ifdef EMBEDIQ_PLATFORM_HOST
#  include <stdio.h>
#  include <stdlib.h>
#  include <time.h>
#endif

/* Stream ops — registered by platform HAL at boot. NULL = no file transport.
 * Written under s_transport_mutex (host). */
static const embediq_obs_stream_ops_t *s_stream_ops = NULL;

/* ---------------------------------------------------------------------------
 * Host-only ring buffer state
 * All variables are static (no dynamic allocation — R-02).
 * ------------------------------------------------------------------------- */

#ifdef EMBEDIQ_PLATFORM_HOST

/* Main ring — every field below is accessed only inside the OSAL critical
 * section. */
static EmbedIQ_Event_t g_ring[EMBEDIQ_OBS_RING_DEPTH];
static uint32_t g_ring_head  = 0u;  /* index of oldest stored event */
static uint32_t g_ring_count = 0u;  /* events currently in ring     */
static uint32_t g_total      = 0u;  /* total events written to ring */
static uint32_t g_overflow   = 0u;  /* drops pending overflow report */

/* Shared by the thread path and the ISR path. */
static embediq_osal_atomic32_t g_sequence  = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);
static embediq_osal_atomic32_t g_transport =
    EMBEDIQ_OSAL_ATOMIC32_INIT((uint32_t)EMBEDIQ_OBS_TRANSPORT_STDOUT);
static embediq_osal_atomic32_t g_level     =
    EMBEDIQ_OSAL_ATOMIC32_INIT((uint32_t)EMBEDIQ_OBS_LEVEL);

/* Serialises transport I/O and setup calls. Created by embediq_obs_init(). */
static EmbedIQ_Mutex_t *s_transport_mutex = NULL;

static EmbedIQ_Obs_Session_t g_session;
static bool                  g_session_valid = false;

/* ISR ring — lock-free multi-producer; drained under the critical section.
 * head = next slot to reserve, tail = next slot to drain (both monotonic,
 * masked on use); ready[slot] publishes a fully written record. */
static EmbedIQ_Event_t         g_isr_ring[EMBEDIQ_OBS_ISR_RING_DEPTH];
static embediq_osal_atomic32_t g_isr_ready[EMBEDIQ_OBS_ISR_RING_DEPTH];
static embediq_osal_atomic32_t g_isr_head          = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);
static embediq_osal_atomic32_t g_isr_tail          = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);
static embediq_osal_atomic32_t g_isr_dropped       = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);
static embediq_osal_atomic32_t g_isr_dropped_total = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);

/* Power-of-2 mask: EMBEDIQ_OBS_RING_DEPTH must be a power of 2 (= 256). */
#define RING_MASK  ((uint32_t)(EMBEDIQ_OBS_RING_DEPTH) - 1u)
#define ISR_MASK   ((uint32_t)(EMBEDIQ_OBS_ISR_RING_DEPTH) - 1u)

_Static_assert((EMBEDIQ_OBS_ISR_RING_DEPTH & (EMBEDIQ_OBS_ISR_RING_DEPTH - 1)) == 0,
               "EMBEDIQ_OBS_ISR_RING_DEPTH must be a power of 2");

/* Local implementation constants — not in embediq_config.h (host-only details). */
#define OBS_EP_NAME_BUF    12    /* "ep_NNN" + margin */
#define OBS_LINE_BUF_SIZE  160   /* max formatted Observatory output line */

/* ---------------------------------------------------------------------------
 * Ring helpers
 * ------------------------------------------------------------------------- */

/**
 * Write evt into the ring. Caller holds the critical section.
 * If count_overflow is true and the ring was full,
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
    uint32_t level = embediq_osal_atomic32_load(&g_level);
    if (level >= 2u) return true;

    embediq_obs_family_t fam = embediq_obs_event_family(event_type);

    if (level >= 1u) {
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

/* ---------------------------------------------------------------------------
 * Concurrency helpers
 * ------------------------------------------------------------------------- */

static EmbedIQ_Obs_Transport_t transport_get(void)
{
    return (EmbedIQ_Obs_Transport_t)embediq_osal_atomic32_load(&g_transport);
}

static void transport_set(EmbedIQ_Obs_Transport_t t)
{
    embediq_osal_atomic32_store(&g_transport, (uint32_t)t);
}

/* Next canonical sequence number — shared by both paths, lock-free. */
static uint32_t next_sequence(void)
{
    return embediq_osal_atomic32_add(&g_sequence, 1u) - 1u;
}

static void transport_lock(void)
{
    if (s_transport_mutex) {
        (void)embediq_osal_mutex_lock(s_transport_mutex, UINT32_MAX);
    }
}

static void transport_unlock(void)
{
    if (s_transport_mutex) embediq_osal_mutex_unlock(s_transport_mutex);
}

/**
 * Move every published ISR-ring record into the main ring (oldest first) and
 * fold pending ISR drops into g_overflow. Caller holds the critical section.
 * Copies up to cap drained records into out (out may be NULL). Bounded to one
 * ring's worth per call so a producer storm cannot extend the hold time.
 * Returns the number of records copied to out.
 */
static uint32_t drain_isr_ring_locked(EmbedIQ_Event_t *out, uint32_t cap)
{
    uint32_t copied = 0u;
    uint32_t tail   = embediq_osal_atomic32_load(&g_isr_tail);
    for (uint32_t i = 0u; i < EMBEDIQ_OBS_ISR_RING_DEPTH; i++) {
        if (tail == embediq_osal_atomic32_load(&g_isr_head)) break;
        uint32_t slot = tail & ISR_MASK;
        if (embediq_osal_atomic32_load(&g_isr_ready[slot]) == 0u) break; /* reserved, not yet written */
        EmbedIQ_Event_t e = g_isr_ring[slot];
        embediq_osal_atomic32_store(&g_isr_ready[slot], 0u);
        tail++;
        embediq_osal_atomic32_store(&g_isr_tail, tail);
        ring_push(&e, true);
        g_total++;
        if (out && copied < cap) out[copied++] = e;
    }
    uint32_t dropped = embediq_osal_atomic32_load(&g_isr_dropped);
    if (dropped > 0u) {
        (void)embediq_osal_atomic32_sub(&g_isr_dropped, dropped);
        g_overflow += dropped;
    }
    return copied;
}

/**
 * Write one event to the current transport. Caller holds s_transport_mutex.
 * OVERFLOW meta-events are written like any other event, so a sequence gap
 * in a .iqtrace file is always accounted for by an OVERFLOW record.
 */
static void transport_write_locked(const EmbedIQ_Event_t *evt)
{
    EmbedIQ_Obs_Transport_t t = transport_get();
    if (t == EMBEDIQ_OBS_TRANSPORT_STDOUT) {
        char line[OBS_LINE_BUF_SIZE];
        format_event_internal(evt, line, sizeof(line));
        printf("%s\n", line);
    } else if (t == EMBEDIQ_OBS_TRANSPORT_FILE) {
        (void)write_tlv(0x0002u, (uint16_t)sizeof(EmbedIQ_Event_t), evt);
    }
}

#endif /* EMBEDIQ_PLATFORM_HOST */

/* ---------------------------------------------------------------------------
 * Public: embediq_obs_init()
 * ------------------------------------------------------------------------- */

void embediq_obs_init(void)
{
#ifdef EMBEDIQ_PLATFORM_HOST
    if (s_transport_mutex) return;   /* idempotent */
    s_transport_mutex = embediq_osal_mutex_create();
#endif
}

/* ---------------------------------------------------------------------------
 * Public: embediq_obs_level_at_least()
 * ------------------------------------------------------------------------- */

bool embediq_obs_level_at_least(uint8_t lvl)
{
#ifdef EMBEDIQ_PLATFORM_HOST
    return embediq_osal_atomic32_load(&g_level) >= (uint32_t)lvl;
#else
    (void)lvl;
    return false;   /* emit is a no-op on MCU production builds */
#endif
}

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
    if (transport_get() == EMBEDIQ_OBS_TRANSPORT_NULL) return;

    /* Level filter: skip events below the configured verbosity. */
    if (!passes_level(event_type)) return;

    uint32_t        ts = obs_timestamp_us();
    EmbedIQ_Event_t drained[EMBEDIQ_OBS_ISR_RING_DEPTH];
    uint32_t        n_drained;
    bool            have_ovf = false;
    EmbedIQ_Event_t ovf;
    EmbedIQ_Event_t evt;

    transport_lock();

    /* ---- critical section: ring + counters + sequence only ---- */
    embediq_osal_critical_enter();

    n_drained = drain_isr_ring_locked(drained, EMBEDIQ_OBS_ISR_RING_DEPTH);

    /* Overflow recovery: report dropped events before the incoming event.
     * ring_push(..., false) so the meta-event cannot re-trigger counting. */
    if (g_overflow > 0u) {
        ovf = (EmbedIQ_Event_t){
            .event_type    = EMBEDIQ_OBS_EVT_OVERFLOW,
            .source_fb_id  = 0xFFu,
            .target_fb_id  = 0xFFu,
            .state_or_flag = 0u,
            .msg_id        = (uint16_t)g_overflow,   /* pack drop count here */
            .timestamp_us  = ts,
            .sequence      = next_sequence(),
        };
        g_overflow = 0u;
        ring_push(&ovf, false);
        g_total++;
        have_ovf = true;
    }

    evt = (EmbedIQ_Event_t){
        .event_type    = event_type,
        .source_fb_id  = source,
        .target_fb_id  = target,
        .state_or_flag = state,
        .msg_id        = msg_id,
        .timestamp_us  = ts,
        .sequence      = next_sequence(),
    };
    ring_push(&evt, true);   /* may increment g_overflow if ring full */
    g_total++;

    embediq_osal_critical_exit();
    /* ---- end critical section ---- */

    /* Transport I/O — outside the critical section, under the transport
     * mutex. Skipped before embediq_obs_init() (no mutex yet). */
    if (s_transport_mutex) {
        for (uint32_t i = 0u; i < n_drained; i++) {
            transport_write_locked(&drained[i]);
        }
        if (have_ovf) transport_write_locked(&ovf);
        transport_write_locked(&evt);
    }

    transport_unlock();

#endif /* EMBEDIQ_PLATFORM_HOST */
}

/* ---------------------------------------------------------------------------
 * Public: embediq_obs_set_transport()
 * ------------------------------------------------------------------------- */

void embediq_obs_set_transport(EmbedIQ_Obs_Transport_t transport)
{
#ifdef EMBEDIQ_PLATFORM_HOST
    embediq_obs_init();
    transport_lock();
    transport_set(transport);
    transport_unlock();
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
    transport_lock();
    embediq_osal_atomic32_store(&g_level, (uint32_t)level);
    transport_unlock();
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
    embediq_obs_init();
    transport_lock();
    g_session       = *session;
    g_session_valid = true;
    transport_unlock();
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
#ifdef EMBEDIQ_PLATFORM_HOST
    embediq_obs_init();
    transport_lock();
    s_stream_ops = ops;
    transport_unlock();
#else
    s_stream_ops = ops;
#endif
}

int embediq_obs_capture_begin(const char *path)
{
#ifndef EMBEDIQ_PLATFORM_HOST
    (void)path;
    return -1;
#else
    embediq_obs_init();
    transport_lock();
    int ret = -1;

    const char *p = path ? path : getenv("EMBEDIQ_OBS_PATH");
    if (!p) p = "/tmp/embediq_capture.iqtrace";

    /* 8-byte file header: IQTR + version 1 LE + reserved */
    static const uint8_t hdr[] = {
        0x49u, 0x51u, 0x54u, 0x52u,
        0x01u, 0x00u,
        0x00u, 0x00u
    };

    const EmbedIQ_Obs_Session_t *s = g_session_valid ? &g_session : NULL;
    if (s && s_stream_ops && s_stream_ops->open && s_stream_ops->open(p) == 0) {
        if (s_stream_ops->write(hdr, (uint16_t)sizeof(hdr)) == 0
            && write_tlv(0x0001u, (uint16_t)sizeof(*s), s) == 0) {   /* SESSION TLV */
            if (s_stream_ops->flush) s_stream_ops->flush();
            transport_set(EMBEDIQ_OBS_TRANSPORT_FILE);
            ret = 0;
        } else {
            s_stream_ops->close();
        }
    }

    transport_unlock();
    return ret;
#endif
}

int embediq_obs_capture_end(void)
{
#ifndef EMBEDIQ_PLATFORM_HOST
    return -1;
#else
    embediq_obs_init();
    transport_lock();   /* no emitter can be mid-write while the stream closes */

    /* Flush pending ISR-path events into the ring and the capture first. */
    EmbedIQ_Event_t drained[EMBEDIQ_OBS_ISR_RING_DEPTH];
    embediq_osal_critical_enter();
    uint32_t n_drained = drain_isr_ring_locked(drained, EMBEDIQ_OBS_ISR_RING_DEPTH);
    embediq_osal_critical_exit();
    for (uint32_t i = 0u; i < n_drained; i++) {
        transport_write_locked(&drained[i]);
    }

    int ret = write_tlv(0x0004u, 0u, NULL);  /* STREAM_END */
    if (s_stream_ops) {
        if (s_stream_ops->flush) s_stream_ops->flush();
        if (s_stream_ops->close) s_stream_ops->close();
    }
    transport_set(EMBEDIQ_OBS_TRANSPORT_NULL);
    transport_unlock();
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
    embediq_obs_init();
    transport_lock();
    if (s_stream_ops && s_stream_ops->close) s_stream_ops->close();

    embediq_osal_critical_enter();
    memset(g_ring, 0, sizeof(g_ring));
    g_ring_head  = 0u;
    g_ring_count = 0u;
    g_total      = 0u;
    g_overflow   = 0u;
    embediq_osal_critical_exit();

    embediq_osal_atomic32_store(&g_sequence, 0u);
    for (uint32_t i = 0u; i < EMBEDIQ_OBS_ISR_RING_DEPTH; i++) {
        embediq_osal_atomic32_store(&g_isr_ready[i], 0u);
    }
    embediq_osal_atomic32_store(&g_isr_head, 0u);
    embediq_osal_atomic32_store(&g_isr_tail, 0u);
    embediq_osal_atomic32_store(&g_isr_dropped, 0u);
    embediq_osal_atomic32_store(&g_isr_dropped_total, 0u);
    transport_set(EMBEDIQ_OBS_TRANSPORT_STDOUT);
    embediq_osal_atomic32_store(&g_level, (uint32_t)EMBEDIQ_OBS_LEVEL);
    memset(&g_session, 0, sizeof(g_session));
    g_session_valid = false;
    transport_unlock();
}

/** Total events written to the ring since last reset. */
uint32_t obs__event_count(void)
{
    embediq_osal_critical_enter();
    (void)drain_isr_ring_locked(NULL, 0u);
    uint32_t n = g_total;
    embediq_osal_critical_exit();
    return n;
}

/** Number of events currently held in the ring buffer. */
uint32_t obs__ring_count(void)
{
    embediq_osal_critical_enter();
    (void)drain_isr_ring_locked(NULL, 0u);
    uint32_t n = g_ring_count;
    embediq_osal_critical_exit();
    return n;
}

/**
 * Read the event at position idx (0 = oldest event in ring).
 * Returns true on success, false if idx >= g_ring_count.
 */
bool obs__ring_read(uint32_t idx, EmbedIQ_Event_t *out)
{
    if (!out) return false;
    embediq_osal_critical_enter();
    (void)drain_isr_ring_locked(NULL, 0u);
    bool ok = idx < g_ring_count;
    if (ok) *out = g_ring[(g_ring_head + idx) & RING_MASK];
    embediq_osal_critical_exit();
    return ok;
}

/** Override the runtime verbosity level (test use only). */
void obs__set_level(uint8_t level)
{
    embediq_osal_atomic32_store(&g_level, (uint32_t)level);
}

/**
 * Inject an overflow count — simulates the ring having been full and
 * dropping 'count' events, without needing to fill the 256-slot ring.
 * On the next embediq_obs_emit() call the overflow recovery event fires.
 */
void obs__force_overflow(uint32_t count)
{
    embediq_osal_critical_enter();
    g_overflow = count;
    embediq_osal_critical_exit();
}

/** Cumulative ISR-ring drops since the last obs__reset() (test use only). */
uint32_t obs__isr_dropped_total(void)
{
    return embediq_osal_atomic32_load(&g_isr_dropped_total);
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
 * ISR-safe emit — host implementation (host form of the Phase-2 MCU design)
 *
 * Lock-free multi-producer push into g_isr_ring: reserve a slot by CAS on
 * g_isr_head, write the record, publish it through g_isr_ready[slot]. No
 * mutex, no critical section, no transport, no printf. A full ring counts
 * the drop and returns; the drop is reported in the next OVERFLOW event.
 * Drained by the next thread-path emit, capture_end(), or a test reader.
 * ------------------------------------------------------------------------- */
void embediq_obs_emit_from_isr(uint8_t event_type, uint8_t source,
                                uint8_t target, uint8_t state,
                                uint16_t msg_id)
{
    if (transport_get() == EMBEDIQ_OBS_TRANSPORT_NULL) return;
    if (!passes_level(event_type)) return;

    uint32_t head;
    for (;;) {
        head = embediq_osal_atomic32_load(&g_isr_head);
        uint32_t tail = embediq_osal_atomic32_load(&g_isr_tail);
        if (head - tail >= (uint32_t)EMBEDIQ_OBS_ISR_RING_DEPTH) {
            (void)embediq_osal_atomic32_add(&g_isr_dropped, 1u);
            (void)embediq_osal_atomic32_add(&g_isr_dropped_total, 1u);
            return;
        }
        if (embediq_osal_atomic32_cas(&g_isr_head, head, head + 1u)) break;
    }

    uint32_t slot = head & ISR_MASK;
    g_isr_ring[slot] = (EmbedIQ_Event_t){
        .event_type    = event_type,
        .source_fb_id  = source,
        .target_fb_id  = target,
        .state_or_flag = state,
        .msg_id        = msg_id,
        .timestamp_us  = obs_timestamp_us(),
        .sequence      = next_sequence(),
    };
    embediq_osal_atomic32_store(&g_isr_ready[slot], 1u);
}

#endif /* EMBEDIQ_PLATFORM_HOST */
