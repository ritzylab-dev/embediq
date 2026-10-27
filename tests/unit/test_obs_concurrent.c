#define _POSIX_C_SOURCE 200809L

/*
 * tests/unit/test_obs_concurrent.c — Observatory concurrency tests (Set 2 / F2)
 *
 * Proves that the Observatory is safe to call from many threads at once:
 * sequence numbers stay unique and strictly increasing, ring records are not
 * torn, .iqtrace TLV frames are never interleaved, the ISR path is lock-free
 * and drained, and the stream-HAL failure path does not re-enter the emitter.
 * Zero external test framework — stdlib + pthread only.
 *
 * Run:      ./build/tests/unit/test_obs_concurrent
 * Expected: "All 9 tests passed. (0 failed)"
 *
 * TSan:
 *   cmake -B build_tsan -DEMBEDIQ_PLATFORM=host -DEMBEDIQ_TSAN=ON
 *   cmake --build build_tsan
 *   ./build_tsan/tests/unit/test_obs_concurrent   (must produce zero TSan reports)
 *
 * test_emit_before_init_is_safe MUST run first: it checks behaviour before any
 * Observatory setup call or obs__reset() has happened in this process.
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "embediq_obs.h"
#include "embediq_config.h"
#include "embediq_osal.h"
#include "embediq_fb.h"
#include "embediq_subfn.h"
#include "embediq_bus.h"
#include "embediq_msg.h"
#include "hal_defs.h"

#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ---------------------------------------------------------------------------
 * Package-internal API (EMBEDIQ_PLATFORM_HOST)
 * ------------------------------------------------------------------------- */

extern void     obs__reset(void);
extern uint32_t obs__event_count(void);
extern uint32_t obs__ring_count(void);
extern bool     obs__ring_read(uint32_t idx, EmbedIQ_Event_t *out);
extern void     obs__set_level(uint8_t level);
extern void     fb_engine__reset(void);
/* Weak fallback so this file links before obs.c provides the hook (TDD-first
 * commit). obs.c's strong definition replaces it once implemented. */
__attribute__((weak)) uint32_t obs__isr_dropped_total(void) { return 0u; }

/* ---------------------------------------------------------------------------
 * Named test constants
 * ------------------------------------------------------------------------- */

#define OBS_T_THREADS        8u
#define OBS_T_ITERS          20000u

#define OBS_ISR_PRODUCERS    4u
#define OBS_ISR_PER_THREAD   1000u
#define OBS_ISR_LOOP_EMITS   2000u
#define OBS_ISR_EXTRA        5u      /* emits beyond the ISR ring depth (test 3) */

#define OBS_FILE_THREADS     4u
#define OBS_FILE_PER_THREAD  2000u

#define OBS_COST_EMITS       100000u
#if defined(__SANITIZE_THREAD__)
#  define OBS_UNDER_TSAN 1
#elif defined(__has_feature)
#  if __has_feature(thread_sanitizer)
#    define OBS_UNDER_TSAN 1
#  endif
#endif
#ifdef OBS_UNDER_TSAN
#  define OBS_COST_MAX_AVG_NS 20000u  /* TSan instruments every access (~10x) */
#else
#  define OBS_COST_MAX_AVG_NS 2000u
#endif

#define OBS_FAIL_ON_WRITE    3u      /* stub write() fails on this call (test 6) */
#define OBS_WATCHDOG_S       5u      /* deadlock guard for test 6 */

#define OBS_TAP_MSG_ID       0x1420u /* community range, unique to this file */
#define OBS_TAP_WAIT_MS      1000u

#define OBS_CAP_THREADS      4u
#define OBS_CAP_CYCLES       20u
#define OBS_CAP_HOLD_MS      2u
#define OBS_CAP_PATH_LEN     80u     /* "/tmp/test_obs_concurrent_NN.iqtrace" + margin */

#define TLV_HDR              4u
#define TLV_EVENT            0x0002u
#define TLV_SESSION          0x0001u
#define TLV_STREAM_END       0x0004u
#define IQTRACE_HDR          8u

#define REC_CAP              (256u * 1024u)
#define SEQ_CAP              8192u

/* ---------------------------------------------------------------------------
 * Minimal test harness — one ASSERT per test function
 * ------------------------------------------------------------------------- */

static int g_tests_run    = 0;
static int g_tests_failed = 0;

#define ASSERT(cond, msg)  do {                                                \
    g_tests_run++;                                                             \
    if (!(cond)) {                                                             \
        fprintf(stderr, "FAIL  %-56s  %s:%d  %s\n", __func__, __FILE__,      \
                __LINE__, (msg));                                              \
        g_tests_failed++;                                                      \
    } else {                                                                   \
        printf("PASS  %s\n", __func__);                                        \
    }                                                                          \
} while (0)

/* ---------------------------------------------------------------------------
 * Stream-ops stubs
 * ------------------------------------------------------------------------- */

/* Discard: accepts every write. */
static int discard_open(const char *p)            { (void)p; return 0; }
static int discard_write(const void *d, uint16_t n) { (void)d; (void)n; return 0; }
static int discard_flush(void)                    { return 0; }
static int discard_close(void)                    { return 0; }
static const embediq_obs_stream_ops_t k_discard_ops = {
    discard_open, discard_write, discard_flush, discard_close };

/* Record: appends every write to one byte buffer (the "file"). */
static uint8_t  g_rec[REC_CAP];
static uint32_t g_rec_len = 0u;
static int rec_write(const void *d, uint16_t n)
{
    if (g_rec_len + n > REC_CAP) return -1;
    memcpy(&g_rec[g_rec_len], d, n);
    g_rec_len += n;
    return 0;
}
static const embediq_obs_stream_ops_t k_record_ops = {
    discard_open, rec_write, discard_flush, discard_close };

/* Fail: the Nth write fails and reports like hal_obs_stream_posix does. */
static uint32_t g_fail_calls = 0u;
static int fail_write(const void *d, uint16_t n)
{
    (void)d; (void)n;
    g_fail_calls++;
    if (g_fail_calls == OBS_FAIL_ON_WRITE) {
        EMBEDIQ_HAL_OBS_EMIT_ERROR(EMBEDIQ_HAL_SRC_OBS_STREAM, HAL_ERR_IO);
        return HAL_ERR_IO;
    }
    return 0;
}
static const embediq_obs_stream_ops_t k_fail_ops = {
    discard_open, fail_write, discard_flush, discard_close };

/* File: real file on disk (test 9). */
static FILE *g_file = NULL;
static int file_open(const char *p)
{
    g_file = fopen(p, "wb");
    return g_file ? 0 : -1;
}
static int file_write(const void *d, uint16_t n)
{
    if (!g_file) return -1;
    return (fwrite(d, 1u, n, g_file) == n) ? 0 : -1;
}
static int file_flush(void) { return g_file ? fflush(g_file) : -1; }
static int file_close(void)
{
    if (!g_file) return 0;
    int rc = fclose(g_file);
    g_file = NULL;
    return rc;
}
static const embediq_obs_stream_ops_t k_file_ops = {
    file_open, file_write, file_flush, file_close };

/* ---------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */

static void use_file_transport(const embediq_obs_stream_ops_t *ops)
{
    obs__reset();
    embediq_obs_set_stream_ops(ops);
    embediq_obs_set_transport(EMBEDIQ_OBS_TRANSPORT_FILE);
    obs__set_level(2u);
}

static uint16_t rd_u16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

static int cmp_u32(const void *a, const void *b)
{
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return (x > y) - (x < y);
}

/* Parse g_rec as back-to-back EVENT TLVs. Returns frame count, or -1 on any
 * framing error. Fills seq[] (file order) and counts HAL_FAULT frames. */
static int parse_event_frames(uint32_t *seq, uint32_t cap, uint32_t *hal_faults)
{
    uint32_t off = 0u, n = 0u;
    if (hal_faults) *hal_faults = 0u;
    while (off < g_rec_len) {
        if (off + TLV_HDR > g_rec_len) return -1;
        uint16_t type = rd_u16(&g_rec[off]);
        uint16_t len  = rd_u16(&g_rec[off + 2u]);
        if (type != TLV_EVENT || len != sizeof(EmbedIQ_Event_t)) return -1;
        if (off + TLV_HDR + len > g_rec_len) return -1;
        EmbedIQ_Event_t e;
        memcpy(&e, &g_rec[off + TLV_HDR], sizeof(e));
        if (n < cap) seq[n] = e.sequence;
        if (hal_faults && e.event_type == EMBEDIQ_OBS_EVT_HAL_FAULT) (*hal_faults)++;
        n++;
        off += TLV_HDR + len;
    }
    return (int)n;
}

/* ===========================================================================
 * Test 8 — must run first
 * ======================================================================== */

/* ---------------------------------------------------------------------------
 * test_emit_before_init_is_safe
 * Before any setup call the ring path works; the transport write is skipped.
 * ------------------------------------------------------------------------- */

static void test_emit_before_init_is_safe(void)
{
    embediq_obs_emit(EMBEDIQ_OBS_EVT_LIFECYCLE, 0u, 0xFFu, 2u, 0u);
    embediq_obs_emit(EMBEDIQ_OBS_EVT_LIFECYCLE, 1u, 0xFFu, 2u, 0u);
    embediq_obs_emit(EMBEDIQ_OBS_EVT_LIFECYCLE, 2u, 0xFFu, 2u, 0u);
    ASSERT(obs__ring_count() == 3u, "emit before init must land in the ring");
}

/* ===========================================================================
 * Test 1
 * ======================================================================== */

static void *lifecycle_emitter(void *arg)
{
    uint32_t iters = *(const uint32_t *)arg;
    for (uint32_t i = 0u; i < iters; i++) {
        embediq_obs_emit(EMBEDIQ_OBS_EVT_LIFECYCLE, 0u, 0xFFu, 2u, 0u);
    }
    return NULL;
}

/* ---------------------------------------------------------------------------
 * test_sequence_strictly_monotonic_across_threads
 * 8 × 20,000 emits. The ring must hold consecutive, strictly increasing
 * sequence numbers ending at total-1, and every emit must be counted.
 * ------------------------------------------------------------------------- */

static void test_sequence_strictly_monotonic_across_threads(void)
{
    use_file_transport(&k_discard_ops);
    pthread_t th[OBS_T_THREADS];
    uint32_t iters = OBS_T_ITERS;
    for (uint32_t i = 0u; i < OBS_T_THREADS; i++) {
        pthread_create(&th[i], NULL, lifecycle_emitter, &iters);
    }
    for (uint32_t i = 0u; i < OBS_T_THREADS; i++) {
        pthread_join(th[i], NULL);
    }

    uint32_t total = obs__event_count();
    uint32_t n     = obs__ring_count();
    bool ok = (total >= OBS_T_THREADS * OBS_T_ITERS) && (n == EMBEDIQ_OBS_RING_DEPTH);
    EmbedIQ_Event_t prev = {0}, cur = {0};
    for (uint32_t i = 0u; ok && i < n; i++) {
        ok = obs__ring_read(i, &cur);
        if (ok && i > 0u) ok = (cur.sequence == prev.sequence + 1u);
        prev = cur;
    }
    ok = ok && (prev.sequence == total - 1u);
    printf("      total=%u ring=%u last_seq=%u\n",
           (unsigned)total, (unsigned)n, (unsigned)prev.sequence);
    ASSERT(ok, "ring sequences must be consecutive and end at total-1");
}

/* ===========================================================================
 * Test 2
 * ======================================================================== */

static void *isr_producer(void *arg)
{
    (void)arg;
    for (uint32_t i = 0u; i < OBS_ISR_PER_THREAD; i++) {
        embediq_obs_emit_from_isr(EMBEDIQ_OBS_EVT_HAL_FAULT,
                                  EMBEDIQ_HAL_SRC_OBS_STREAM, 0u, 4u, 0u);
    }
    return NULL;
}

/* ---------------------------------------------------------------------------
 * test_isr_path_no_lock_and_drained
 * 4 threads × 1,000 emit_from_isr while one thread emits normally. Every ISR
 * event is either drained to the transport or counted as an ISR-ring drop,
 * and no sequence number appears twice.
 * ------------------------------------------------------------------------- */

static uint32_t g_seq_buf[SEQ_CAP];

static void test_isr_path_no_lock_and_drained(void)
{
    use_file_transport(&k_record_ops);
    g_rec_len = 0u;

    pthread_t prod[OBS_ISR_PRODUCERS], loop;
    uint32_t loop_iters = OBS_ISR_LOOP_EMITS;
    pthread_create(&loop, NULL, lifecycle_emitter, &loop_iters);
    for (uint32_t i = 0u; i < OBS_ISR_PRODUCERS; i++) {
        pthread_create(&prod[i], NULL, isr_producer, NULL);
    }
    for (uint32_t i = 0u; i < OBS_ISR_PRODUCERS; i++) {
        pthread_join(prod[i], NULL);
    }
    pthread_join(loop, NULL);
    embediq_obs_emit(EMBEDIQ_OBS_EVT_LIFECYCLE, 0u, 0xFFu, 2u, 0u);  /* final drain */

    uint32_t hal = 0u;
    int frames = parse_event_frames(g_seq_buf, SEQ_CAP, &hal);
    uint32_t dropped = obs__isr_dropped_total();
    bool ok = frames > 0 && (uint32_t)frames <= SEQ_CAP
              && hal + dropped == OBS_ISR_PRODUCERS * OBS_ISR_PER_THREAD;
    if (ok) {
        qsort(g_seq_buf, (size_t)frames, sizeof(uint32_t), cmp_u32);
        for (int i = 1; ok && i < frames; i++) {
            ok = g_seq_buf[i] != g_seq_buf[i - 1];
        }
    }
    printf("      frames=%d hal_fault=%u isr_dropped=%u\n",
           frames, (unsigned)hal, (unsigned)dropped);
    ASSERT(ok, "all ISR events drained or counted; no duplicate sequence");
}

/* ===========================================================================
 * Test 3
 * ======================================================================== */

/* ---------------------------------------------------------------------------
 * test_isr_ring_overflow_counted
 * With no drain, DEPTH + 5 ISR emits drop 5; the next thread-path emit
 * reports them in an OVERFLOW meta-event (msg_id == 5).
 * ------------------------------------------------------------------------- */

static void test_isr_ring_overflow_counted(void)
{
    use_file_transport(&k_discard_ops);
    for (uint32_t i = 0u; i < EMBEDIQ_OBS_ISR_RING_DEPTH + OBS_ISR_EXTRA; i++) {
        embediq_obs_emit_from_isr(EMBEDIQ_OBS_EVT_HAL_FAULT,
                                  EMBEDIQ_HAL_SRC_OBS_STREAM, 0u, 4u, 0u);
    }
    embediq_obs_emit(EMBEDIQ_OBS_EVT_LIFECYCLE, 0u, 0xFFu, 2u, 0u);

    bool found = false;
    EmbedIQ_Event_t e;
    for (uint32_t i = 0u; obs__ring_read(i, &e); i++) {
        if (e.event_type == EMBEDIQ_OBS_EVT_OVERFLOW && e.msg_id == OBS_ISR_EXTRA) {
            found = true;
        }
    }
    ASSERT(found, "OVERFLOW meta-event must report the 5 dropped ISR events");
}

/* ===========================================================================
 * Test 4
 * ======================================================================== */

/* ---------------------------------------------------------------------------
 * test_file_transport_frames_not_interleaved
 * 4 threads × 2,000 emits to a recording stream: every frame is a whole
 * 14-byte EVENT TLV and sequence numbers rise strictly in file order.
 * ------------------------------------------------------------------------- */

static void test_file_transport_frames_not_interleaved(void)
{
    use_file_transport(&k_record_ops);
    g_rec_len = 0u;

    pthread_t th[OBS_FILE_THREADS];
    uint32_t iters = OBS_FILE_PER_THREAD;
    for (uint32_t i = 0u; i < OBS_FILE_THREADS; i++) {
        pthread_create(&th[i], NULL, lifecycle_emitter, &iters);
    }
    for (uint32_t i = 0u; i < OBS_FILE_THREADS; i++) {
        pthread_join(th[i], NULL);
    }

    int frames = parse_event_frames(g_seq_buf, SEQ_CAP, NULL);
    bool ok = frames == (int)(OBS_FILE_THREADS * OBS_FILE_PER_THREAD);
    for (int i = 1; ok && i < frames; i++) {
        ok = g_seq_buf[i] > g_seq_buf[i - 1];
    }
    printf("      frames=%d (expected %u)\n",
           frames, (unsigned)(OBS_FILE_THREADS * OBS_FILE_PER_THREAD));
    ASSERT(ok, "frames must be whole and in strictly increasing sequence order");
}

/* ===========================================================================
 * Test 5
 * ======================================================================== */

/* ---------------------------------------------------------------------------
 * test_critical_section_hold_time
 * 100,000 single-thread emits through the full path (ring + transport stub):
 * coarse guard that no I/O sits inside the critical section.
 * ------------------------------------------------------------------------- */

static void test_critical_section_hold_time(void)
{
    use_file_transport(&k_discard_ops);
    uint32_t t0 = embediq_osal_time_us();
    for (uint32_t i = 0u; i < OBS_COST_EMITS; i++) {
        embediq_obs_emit(EMBEDIQ_OBS_EVT_LIFECYCLE, 0u, 0xFFu, 2u, 0u);
    }
    uint32_t elapsed_us = embediq_osal_time_us() - t0;
    uint64_t avg_ns = ((uint64_t)elapsed_us * 1000u) / OBS_COST_EMITS;
    printf("      %u emits in %u us — avg %llu ns/emit (limit %u ns)\n",
           (unsigned)OBS_COST_EMITS, (unsigned)elapsed_us,
           (unsigned long long)avg_ns, (unsigned)OBS_COST_MAX_AVG_NS);
    ASSERT(avg_ns < OBS_COST_MAX_AVG_NS, "average emit cost must stay under the limit");
}

/* ===========================================================================
 * Test 6
 * ======================================================================== */

/* ---------------------------------------------------------------------------
 * test_stream_hal_write_failure_does_not_reenter
 * The stream write fails and reports via EMBEDIQ_HAL_OBS_EMIT_ERROR from
 * inside the transport. Must not deadlock (5 s alarm) and the HAL_FAULT must
 * reach the ring on the next thread-path emit.
 * ------------------------------------------------------------------------- */

static void test_stream_hal_write_failure_does_not_reenter(void)
{
    use_file_transport(&k_fail_ops);
    g_fail_calls = 0u;
    alarm(OBS_WATCHDOG_S);
    embediq_obs_emit(EMBEDIQ_OBS_EVT_LIFECYCLE, 0u, 0xFFu, 2u, 0u); /* writes 1,2 */
    embediq_obs_emit(EMBEDIQ_OBS_EVT_LIFECYCLE, 1u, 0xFFu, 2u, 0u); /* write 3 fails */
    embediq_obs_emit(EMBEDIQ_OBS_EVT_LIFECYCLE, 2u, 0xFFu, 2u, 0u); /* drains */
    alarm(0u);

    bool found = false;
    EmbedIQ_Event_t e;
    for (uint32_t i = 0u; obs__ring_read(i, &e); i++) {
        if (e.event_type == EMBEDIQ_OBS_EVT_HAL_FAULT
            && e.source_fb_id == EMBEDIQ_HAL_SRC_OBS_STREAM) {
            found = true;
        }
    }
    ASSERT(found, "HAL_FAULT from the failing stream write must reach the ring");
}

/* ===========================================================================
 * Test 7
 * ======================================================================== */

static embediq_osal_atomic32_t g_tap_runs = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);

static void tap_run_fn(EmbedIQ_FB_Handle_t fb, const void *msg,
                       void *fb_data, void *subfn_data)
{
    (void)fb; (void)msg; (void)fb_data; (void)subfn_data;
    (void)embediq_osal_atomic32_add(&g_tap_runs, 1u);
}

EMBEDIQ_SUBS(g_tap_subs, OBS_TAP_MSG_ID);

static const EmbedIQ_SubFn_Config_t g_tap_subfn = {
    .name               = "tap_subfn",
    .init_order         = 0,
    .subscriptions      = g_tap_subs,
    .subscription_count = 1,
    .run_fn             = tap_run_fn,
};

static void tap_dst_init(EmbedIQ_FB_Handle_t h, void *d)
{
    (void)d;
    embediq_subfn_register(h, &g_tap_subfn);
}

/* Boot src + dst, publish one message at `level`, let dst dispatch it, and
 * count MSG_TX / MSG_RX events for OBS_TAP_MSG_ID with the expected endpoints. */
static void run_tap_scenario(uint8_t level, uint32_t *tx, uint32_t *rx)
{
    fb_engine__reset();
    embediq_osal_atomic32_store(&g_tap_runs, 0u);

    static const EmbedIQ_FB_Config_t k_src = {
        .name = "fb_f2_src", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
    };
    static const EmbedIQ_FB_Config_t k_dst = {
        .name = "fb_f2_dst", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
        .init_fn = tap_dst_init,
        .subscriptions = g_tap_subs, .subscription_count = 1,
    };
    EmbedIQ_FB_Handle_t src = embediq_fb_register(&k_src);
    embediq_fb_register(&k_dst);
    embediq_engine_boot();
    embediq_obs_set_transport(EMBEDIQ_OBS_TRANSPORT_NULL);   /* quiet boot noise */
    obs__reset();
    obs__set_level(level);
    embediq_obs_set_stream_ops(&k_discard_ops);
    embediq_obs_set_transport(EMBEDIQ_OBS_TRANSPORT_FILE);

    uint8_t src_ep = embediq_bus_resolve_name("fb_f2_src");
    uint8_t dst_ep = embediq_bus_resolve_name("fb_f2_dst");

    EmbedIQ_Msg_t m;
    memset(&m, 0, sizeof(m));
    m.msg_id   = OBS_TAP_MSG_ID;
    m.priority = EMBEDIQ_MSG_PRIORITY_NORMAL;
    embediq_publish(src, &m);

    embediq_engine_dispatch_boot();
    uint32_t t0 = embediq_osal_time_ms();
    while (embediq_osal_atomic32_load(&g_tap_runs) == 0u
           && (embediq_osal_time_ms() - t0) < OBS_TAP_WAIT_MS) {
        embediq_osal_delay_ms(1u);
    }
    (void)embediq_engine_dispatch_shutdown();

    *tx = 0u;
    *rx = 0u;
    EmbedIQ_Event_t e;
    for (uint32_t i = 0u; obs__ring_read(i, &e); i++) {
        if (e.msg_id != OBS_TAP_MSG_ID) continue;
        if (e.event_type == EMBEDIQ_OBS_EVT_MSG_TX && e.source_fb_id == src_ep
            && e.target_fb_id == 0xFFu) (*tx)++;
        if (e.event_type == EMBEDIQ_OBS_EVT_MSG_RX && e.source_fb_id == src_ep
            && e.target_fb_id == dst_ep) (*rx)++;
    }
}

/* ---------------------------------------------------------------------------
 * test_msg_tx_rx_taps_level3
 * Level 3: exactly one MSG_TX and one MSG_RX. Levels 2 and 1: none — the taps
 * must not change the output of the levels the demo and tests run at.
 * ------------------------------------------------------------------------- */

static void test_msg_tx_rx_taps_level3(void)
{
    uint32_t tx3, rx3, tx2, rx2, tx1, rx1;
    run_tap_scenario(3u, &tx3, &rx3);
    run_tap_scenario(2u, &tx2, &rx2);
    run_tap_scenario(1u, &tx1, &rx1);
    fb_engine__reset();
    printf("      level3 tx=%u rx=%u   level2 tx=%u rx=%u   level1 tx=%u rx=%u\n",
           (unsigned)tx3, (unsigned)rx3, (unsigned)tx2, (unsigned)rx2,
           (unsigned)tx1, (unsigned)rx1);
    ASSERT(tx3 == 1u && rx3 == 1u && tx2 == 0u && rx2 == 0u && tx1 == 0u && rx1 == 0u,
           "MSG_TX/MSG_RX taps fire once at level 3 and never at levels 1-2");
}

/* ===========================================================================
 * Test 9
 * ======================================================================== */

static embediq_osal_atomic32_t g_cap_stop = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);

static void *cap_emitter(void *arg)
{
    (void)arg;
    while (embediq_osal_atomic32_load(&g_cap_stop) == 0u) {
        embediq_obs_emit(EMBEDIQ_OBS_EVT_LIFECYCLE, 0u, 0xFFu, 2u, 0u);
    }
    return NULL;
}

/* True if path is a well-formed .iqtrace: 8-byte header, then SESSION/EVENT
 * TLVs with their fixed lengths, ending exactly with STREAM_END. */
static bool iqtrace_well_formed(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    uint8_t hdr[IQTRACE_HDR];
    bool ok = fread(hdr, 1u, sizeof(hdr), f) == sizeof(hdr)
              && memcmp(hdr, "IQTR", 4u) == 0;
    bool ended = false;
    while (ok) {
        uint8_t t[TLV_HDR];
        size_t got = fread(t, 1u, sizeof(t), f);
        if (got == 0u) break;
        if (got != sizeof(t) || ended) { ok = false; break; }
        uint16_t type = rd_u16(t), len = rd_u16(&t[2]);
        if      (type == TLV_SESSION)    ok = (len == sizeof(EmbedIQ_Obs_Session_t));
        else if (type == TLV_EVENT)      ok = (len == sizeof(EmbedIQ_Event_t));
        else if (type == TLV_STREAM_END) { ok = (len == 0u); ended = true; }
        else ok = false;
        if (ok && len > 0u) {
            uint8_t buf[sizeof(EmbedIQ_Obs_Session_t)];
            ok = fread(buf, 1u, len, f) == len;
        }
    }
    fclose(f);
    return ok && ended;
}

/* ---------------------------------------------------------------------------
 * test_capture_end_while_emitting
 * 4 threads emit while main runs 20 capture_begin/capture_end cycles; every
 * file produced must be a well-formed .iqtrace.
 * ------------------------------------------------------------------------- */

static void test_capture_end_while_emitting(void)
{
    obs__reset();
    embediq_obs_set_transport(EMBEDIQ_OBS_TRANSPORT_NULL);
    embediq_obs_set_stream_ops(&k_file_ops);
    obs__set_level(2u);
    EmbedIQ_Obs_Session_t s;
    memset(&s, 0, sizeof(s));
    s.device_id = 0xF2F2F2F2u;
    embediq_obs_session_begin(&s);

    embediq_osal_atomic32_store(&g_cap_stop, 0u);
    pthread_t th[OBS_CAP_THREADS];
    for (uint32_t i = 0u; i < OBS_CAP_THREADS; i++) {
        pthread_create(&th[i], NULL, cap_emitter, NULL);
    }

    bool ok = true;
    char path[OBS_CAP_PATH_LEN];
    for (uint32_t c = 0u; c < OBS_CAP_CYCLES; c++) {
        snprintf(path, sizeof(path), "/tmp/test_obs_concurrent_%u.iqtrace", (unsigned)c);
        ok = ok && embediq_obs_capture_begin(path) == 0;
        embediq_osal_delay_ms(OBS_CAP_HOLD_MS);
        ok = ok && embediq_obs_capture_end() == 0;
    }
    embediq_osal_atomic32_store(&g_cap_stop, 1u);
    for (uint32_t i = 0u; i < OBS_CAP_THREADS; i++) {
        pthread_join(th[i], NULL);
    }
    for (uint32_t c = 0u; ok && c < OBS_CAP_CYCLES; c++) {
        snprintf(path, sizeof(path), "/tmp/test_obs_concurrent_%u.iqtrace", (unsigned)c);
        ok = iqtrace_well_formed(path);
        if (!ok) printf("      malformed: %s\n", path);
    }
    for (uint32_t c = 0u; c < OBS_CAP_CYCLES; c++) {
        snprintf(path, sizeof(path), "/tmp/test_obs_concurrent_%u.iqtrace", (unsigned)c);
        if (ok) remove(path);
    }
    ASSERT(ok, "every capture produced while emitting must be well-formed");
}

/* ---------------------------------------------------------------------------
 * Entry point
 * ------------------------------------------------------------------------- */

int main(void)
{
    test_emit_before_init_is_safe();          /* must be first */
    test_sequence_strictly_monotonic_across_threads();
    test_isr_path_no_lock_and_drained();
    test_isr_ring_overflow_counted();
    test_file_transport_frames_not_interleaved();
    test_critical_section_hold_time();
    test_stream_hal_write_failure_does_not_reenter();
    test_msg_tx_rx_taps_level3();
    test_capture_end_while_emitting();

    obs__reset();
    printf("\nAll %d tests passed. (%d failed)\n",
           g_tests_run - g_tests_failed, g_tests_failed);
    return g_tests_failed > 0 ? 1 : 0;
}
