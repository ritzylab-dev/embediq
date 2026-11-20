#define _POSIX_C_SOURCE 200809L

/*
 * tests/unit/test_fb_watchdog.c — Unit tests for fb_watchdog
 *
 * The watchdog's public contract is exercised through embediq_wdg.h only:
 * this file declares none of the three public functions itself.
 *
 * Fixtures:
 *   manual service   wdg__init_state(): calls are accepted, no monitor runs,
 *                    and each scan is one wdg__trigger_check(). Deadlines run
 *                    on a controlled millisecond clock (wdg__set_clock).
 *   real service     wdg__reset_service() then embediq_engine_boot() with the
 *                    real fb_watchdog: readiness, the table lock and the
 *                    monitor are produced by wdg_init() itself.
 *
 * Cases:
 *   contract     status of every call on the normal path; re-registering
 *                replaces the timeout and restarts the window; NULL handle,
 *                timeout range, failed update leaves the entry as it was,
 *                not registered, unregister twice, full table, not ready
 *   time         4,294,968 ms (the value a 32-bit microsecond conversion
 *                turned into 704 µs), clock wrap, the maximum timeout, a
 *                delayed scan
 *   scan order   an entry removed after the scan selected it is still
 *                reported exactly once; nothing in phase 2 runs with the
 *                table lock held; a fault_fn that checks in does not
 *                deadlock; the missed check-in faults the monitored FB
 *   preparation  one monitor across a late boot retry; table-lock creation
 *                and monitor creation fail, then recover on a retried boot;
 *                a check-in racing preparation, judged by completed calls
 *                only (lock-order and HAL/lock-failure cases are in
 *                test_fb_watchdog_boundaries.c)
 *
 * Every wait is for a flag or a count; its time bound is only a hang guard,
 * and alarm() guards every case that runs the monitor task.
 *
 * Run:      ./build/tests/unit/test_fb_watchdog
 * Expected: "All N tests passed. (0 failed)"
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "embediq_fb.h"
#include "embediq_wdg.h"
#include "embediq_subfn.h"
#include "embediq_bus.h"
#include "embediq_msg.h"
#include "embediq_platform_msgs.h"
#include "embediq_osal.h"
#include "embediq_config.h"
#include "hal/hal_wdg.h"
#include "embediq_obs.h"

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>     /* alarm */

/* ---------------------------------------------------------------------------
 * Test-only watchdog hooks (EMBEDIQ_TEST_HOOKS, fb_watchdog.c)
 * ------------------------------------------------------------------------- */

extern void     wdg__init_state(void);
extern void     wdg__reset_service(void);
extern void     wdg__trigger_check(void);
extern uint32_t wdg__fault_count(void);
extern void     wdg__stop_monitor(void);
extern void     wdg__set_clock(uint32_t (*now_ms)(void));
extern void     wdg__set_scan_hook(void (*after_snapshot)(void));
extern void     wdg__set_capacity(uint8_t n);
extern uint32_t wdg__monitor_starts(void);

/* Test-only FB engine API */
extern void     fb_engine__reset(void);
extern EmbedIQ_FB_Handle_t fb_watchdog_register(void);
extern uint32_t fb_engine__obs_event_count(void);

/* Package-internal Observatory capture API */
extern void     obs__reset(void);
extern uint32_t obs__ring_count(void);
extern bool     obs__ring_read(uint32_t idx, EmbedIQ_Event_t *out);
extern void     obs__set_level(uint8_t level);

/* OSAL failure injection and accounting (EMBEDIQ_TEST_HOOKS) */
extern void     embediq_osal__fail_next_mutex_create(uint32_t nth);
extern void     embediq_osal__fail_next_task_create(uint32_t nth);
extern void     embediq_osal__mutex_counts(uint32_t *created, uint32_t *destroyed);

/* ---------------------------------------------------------------------------
 * Minimal test harness
 * ------------------------------------------------------------------------- */

static int g_tests_run    = 0;
static int g_tests_failed = 0;

#define ASSERT(cond, msg)  do {                                                \
    g_tests_run++;                                                             \
    if (!(cond)) {                                                             \
        fprintf(stderr, "FAIL  %-56s  %s  %s:%d\n", __func__, (msg),           \
                __FILE__, __LINE__);                                           \
        g_tests_failed++;                                                      \
    } else {                                                                   \
        printf("PASS  %s: %s\n", __func__, (msg));                             \
    }                                                                          \
} while (0)

/* ---------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */

static EmbedIQ_FB_Handle_t make_test_fb(const char *name)
{
    EmbedIQ_FB_Config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.name       = name;
    cfg.boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION;
    return embediq_fb_register(&cfg);
}

/* Controlled clock: read by monitor_scan() on the test thread only (the
 * manual fixture runs no monitor). */
static uint32_t g_now_ms;
static uint32_t fake_now(void) { return g_now_ms; }

/* Manual fixture on the controlled clock: fresh engine, service accepting
 * calls, no monitor, empty Observatory ring (ring only, no output). */
static void manual_begin(uint32_t start_ms)
{
    fb_engine__reset();
    wdg__init_state();
    wdg__set_clock(fake_now);
    g_now_ms = start_ms;
    obs__reset();
    embediq_obs_set_transport(EMBEDIQ_OBS_TRANSPORT_TCP);
}

#define ANY  (-1)

/* FAULT events with state_or_flag == state; src / msg_id / tgt matched unless ANY. */
static uint32_t fault_events(uint8_t state, int src, int msg_id, int tgt)
{
    uint32_t n = 0u;
    EmbedIQ_Event_t e;
    for (uint32_t i = 0u; obs__ring_read(i, &e); i++) {
        if (e.event_type != EMBEDIQ_OBS_EVT_FAULT || e.state_or_flag != state) continue;
        if (src != ANY && e.source_fb_id != (uint8_t)src) continue;
        if (msg_id != ANY && e.msg_id != (uint16_t)msg_id) continue;
        if (tgt != ANY && e.target_fb_id != (uint8_t)tgt) continue;
        n++;
    }
    return n;
}

/* One scan; returns the number of misses it reported. */
static uint32_t scan(void)
{
    uint32_t before = wdg__fault_count();
    wdg__trigger_check();
    return wdg__fault_count() - before;
}

/* Spin (1 ms steps) until flag != 0 or ms elapse. Returns the final flag. */
static bool wait_flag(embediq_osal_atomic32_t *flag, uint32_t ms)
{
    uint32_t t0 = embediq_osal_time_ms();
    while (embediq_osal_atomic32_load(flag) == 0u
           && (embediq_osal_time_ms() - t0) < ms) {
        embediq_osal_delay_ms(1u);
    }
    return embediq_osal_atomic32_load(flag) != 0u;
}

/* ---------------------------------------------------------------------------
 * Manual-service behaviour (real clock; unchanged cases)
 * ------------------------------------------------------------------------- */

/* Every registered FB checks in within its timeout: no fault. */
static void test_wdg_all_checkin_no_fault(void)
{
    fb_engine__reset();
    wdg__init_state();

    EmbedIQ_FB_Handle_t fb = make_test_fb("fb_all_ok");

    ASSERT(embediq_wdg_register(fb, 200u) == EMBEDIQ_OK, "register");
    ASSERT(embediq_wdg_checkin(fb) == EMBEDIQ_OK, "check-in");
    wdg__trigger_check();             /* check immediately — elapsed ≈ 0 */

    ASSERT(wdg__fault_count() == 0u,
           "no fault when FB checks in within timeout");
}

/* An FB with a 1 ms timeout that never checks in is reported. */
static void test_wdg_missed_checkin_emits_event(void)
{
    fb_engine__reset();
    wdg__init_state();

    EmbedIQ_FB_Handle_t fb = make_test_fb("fb_missed");

    ASSERT(embediq_wdg_register(fb, 1u) == EMBEDIQ_OK, "register");
    embediq_osal_delay_ms(5u);        /* wait 5 ms — timeout expires */
    wdg__trigger_check();

    ASSERT(wdg__fault_count() >= 1u,
           "fault emitted when FB misses its checkin deadline");
    ASSERT(fb_engine__obs_event_count() >= 1u,
           "observatory FAULT event emitted for missed checkin");
}

/* Three FBs; A and B check in, C does not: exactly one fault. */
static void test_wdg_multiple_fbs_all_must_checkin(void)
{
    manual_begin(1000u);

    EmbedIQ_FB_Handle_t fbA = make_test_fb("fb_a");
    EmbedIQ_FB_Handle_t fbB = make_test_fb("fb_b");
    EmbedIQ_FB_Handle_t fbC = make_test_fb("fb_c");

    (void)embediq_wdg_register(fbA, 1u);
    (void)embediq_wdg_register(fbB, 1u);
    (void)embediq_wdg_register(fbC, 1u);

    g_now_ms += 5u;              /* all three time out */
    (void)embediq_wdg_checkin(fbA);
    (void)embediq_wdg_checkin(fbB);

    ASSERT(scan() == 1u, "exactly one fault (for FB-C) when A and B check in on time");
}

/* An FB unregistered before the scan is not reported. */
static void test_wdg_unregister_removes_from_tracking(void)
{
    manual_begin(1000u);

    EmbedIQ_FB_Handle_t fb = make_test_fb("fb_unreg");

    (void)embediq_wdg_register(fb, 1u);
    g_now_ms += 5u;
    ASSERT(embediq_wdg_unregister(fb) == EMBEDIQ_OK, "unregister");
    ASSERT(scan() == 0u, "no fault for an unregistered FB even after timeout expiry");
}

/* ---------------------------------------------------------------------------
 * HAL WDG contract tests
 * ------------------------------------------------------------------------- */

static void test_hal_wdg_init_returns_ok(void)
{
    hal_wdg_deinit();
    int rc = hal_wdg_init(500u);
    ASSERT(rc == HAL_OK, "hal_wdg_init returns HAL_OK");
    hal_wdg_deinit();
}

static void test_hal_wdg_kick_returns_ok_after_init(void)
{
    hal_wdg_deinit();
    hal_wdg_init(500u);
    int rc = hal_wdg_kick();
    ASSERT(rc == HAL_OK, "hal_wdg_kick returns HAL_OK after init");
    hal_wdg_deinit();
}

static void test_hal_wdg_deinit_does_not_crash(void)
{
    hal_wdg_init(500u);
    hal_wdg_deinit();
    hal_wdg_deinit();
    ASSERT(true, "hal_wdg_deinit does not crash on repeated calls");
}

static void test_hal_wdg_kick_before_init_returns_error(void)
{
    hal_wdg_deinit();
    int rc = hal_wdg_kick();
    ASSERT(rc == HAL_ERR_INVALID,
           "hal_wdg_kick before init returns HAL_ERR_INVALID");
}

/* All registered FBs checked in: the scan kicks the HAL and emits
 * EMBEDIQ_OBS_EVT_WDG_CHECKIN in the TIMING band (driver library is built
 * at EMBEDIQ_TRACE_LEVEL=2). */
static void test_wdg_checkin_emits_timing_event(void)
{
    fb_engine__reset();
    wdg__init_state();
    hal_wdg_init(500u);

    EmbedIQ_FB_Handle_t fb = make_test_fb("fb_timing_ok");
    (void)embediq_wdg_register(fb, 200u);
    (void)embediq_wdg_checkin(fb);

    obs__reset();
    obs__set_level(2u);
    wdg__trigger_check();

    uint32_t count = obs__ring_count();
    ASSERT(count >= 1u, "all-checkin scan must emit at least one TIMING event");

    EmbedIQ_Event_t evt;
    memset(&evt, 0, sizeof(evt));
    bool found = false;
    for (uint32_t i = 0u; i < count; i++) {
        obs__ring_read(i, &evt);
        if (evt.event_type == EMBEDIQ_OBS_EVT_WDG_CHECKIN) {
            found = true;
            break;
        }
    }
    ASSERT(found, "EMBEDIQ_OBS_EVT_WDG_CHECKIN (0x50) must appear in Observatory ring");
    ASSERT(embediq_obs_event_family(EMBEDIQ_OBS_EVT_WDG_CHECKIN) ==
               EMBEDIQ_OBS_FAMILY_TIMING,
           "WDG_CHECKIN event must be in the TIMING family (band 0x50-0x5F)");

    hal_wdg_deinit();
}

/* ---------------------------------------------------------------------------
 * Contract
 * ------------------------------------------------------------------------- */

/* register / check-in / unregister return EMBEDIQ_OK on the normal path;
 * registering again replaces the timeout and restarts the window. */
static void test_calls_return_ok_and_reregister_replaces_and_restarts(void)
{
    manual_begin(1000u);
    EmbedIQ_FB_Handle_t fb = make_test_fb("contract_ok");

    ASSERT(embediq_wdg_register(fb, 100u) == EMBEDIQ_OK, "register returns OK");
    ASSERT(embediq_wdg_checkin(fb) == EMBEDIQ_OK, "check-in returns OK");

    g_now_ms = 1090u;
    ASSERT(embediq_wdg_register(fb, 50u) == EMBEDIQ_OK, "re-register returns OK");
    g_now_ms = 1140u;              /* 50 ms after the update: not more than 50 */
    ASSERT(scan() == 0u, "the window restarted at the update (old window would have expired)");
    g_now_ms = 1141u;              /* 51 ms: the old 100 ms timeout would not miss */
    ASSERT(scan() == 1u, "the replaced timeout (50 ms) is the one applied");

    ASSERT(embediq_wdg_unregister(fb) == EMBEDIQ_OK, "unregister returns OK");
    ASSERT(fault_events(EMBEDIQ_FAULT_WDG_INVALID_ARG, ANY, ANY, ANY)
           + fault_events(EMBEDIQ_FAULT_WDG_NOT_REGISTERED, ANY, ANY, ANY)
           + fault_events(EMBEDIQ_FAULT_WDG_NOT_READY, ANY, ANY, ANY) == 0u,
           "successful calls emit no watchdog fault event");
}

/* NULL handle → ERR for each call, one INVALID_ARG event each, table unchanged. */
static void test_null_handle_is_rejected_without_change(void)
{
    manual_begin(1000u);
    EmbedIQ_FB_Handle_t fb = make_test_fb("contract_null");
    (void)embediq_wdg_register(fb, 100u);
    obs__reset();

    ASSERT(embediq_wdg_register(NULL, 100u) == EMBEDIQ_ERR, "register(NULL) returns ERR");
    ASSERT(embediq_wdg_checkin(NULL) == EMBEDIQ_ERR, "checkin(NULL) returns ERR");
    ASSERT(embediq_wdg_unregister(NULL) == EMBEDIQ_ERR, "unregister(NULL) returns ERR");
    ASSERT(fault_events(EMBEDIQ_FAULT_WDG_INVALID_ARG, 0xFF, 0, ANY) == 3u,
           "one INVALID_ARG event per call, source 0xFF");

    g_now_ms = 1100u;
    ASSERT(scan() == 0u, "the existing entry keeps its deadline");
    g_now_ms = 1101u;
    ASSERT(scan() == 1u, "the existing entry is still the only one monitored");
}

/* Timeout range: 0 and MAX+1 rejected, MAX accepted; a failed update leaves
 * the entry's timeout and deadline exactly as they were. */
static void test_timeout_range_and_failed_update(void)
{
    manual_begin(1000u);
    EmbedIQ_FB_Handle_t a = make_test_fb("range_a");
    EmbedIQ_FB_Handle_t b = make_test_fb("range_b");
    uint8_t ep_b = embediq_engine_fb_ep(b);

    ASSERT(embediq_wdg_register(b, 0u) == EMBEDIQ_ERR, "timeout 0 returns ERR");
    ASSERT(embediq_wdg_register(b, EMBEDIQ_WDG_TIMEOUT_MS_MAX + 1u) == EMBEDIQ_ERR,
           "timeout MAX + 1 returns ERR");
    ASSERT(fault_events(EMBEDIQ_FAULT_WDG_INVALID_ARG, ep_b, 0, ANY) == 2u,
           "one INVALID_ARG event per rejected timeout, source = the FB");
    ASSERT(embediq_wdg_checkin(b) == EMBEDIQ_ERR, "a rejected registration adds no entry");
    ASSERT(embediq_wdg_register(b, EMBEDIQ_WDG_TIMEOUT_MS_MAX) == EMBEDIQ_OK,
           "timeout MAX returns OK");
    ASSERT(embediq_wdg_unregister(b) == EMBEDIQ_OK, "unregister");

    ASSERT(embediq_wdg_register(a, 100u) == EMBEDIQ_OK, "register a, 100 ms at t=1000");
    g_now_ms = 1060u;
    ASSERT(embediq_wdg_register(a, 0u) == EMBEDIQ_ERR, "update to 0 returns ERR");
    ASSERT(embediq_wdg_register(a, EMBEDIQ_WDG_TIMEOUT_MS_MAX + 1u) == EMBEDIQ_ERR,
           "update to MAX + 1 returns ERR");
    g_now_ms = 1100u;
    ASSERT(scan() == 0u, "deadline unchanged: 100 ms after t=1000 is not a miss");
    g_now_ms = 1101u;
    ASSERT(scan() == 1u, "timeout unchanged: 101 ms after t=1000 is a miss");
}

/* Check-in and unregister of an FB that is not registered → ERR; unregister twice. */
static void test_not_registered_and_unregister_twice(void)
{
    manual_begin(1000u);
    EmbedIQ_FB_Handle_t fb = make_test_fb("not_reg");
    uint8_t ep = embediq_engine_fb_ep(fb);

    ASSERT(embediq_wdg_checkin(fb) == EMBEDIQ_ERR, "check-in before register returns ERR");
    ASSERT(embediq_wdg_unregister(fb) == EMBEDIQ_ERR, "unregister before register returns ERR");
    ASSERT(embediq_wdg_register(fb, 100u) == EMBEDIQ_OK, "register");
    ASSERT(embediq_wdg_unregister(fb) == EMBEDIQ_OK, "first unregister returns OK");
    ASSERT(embediq_wdg_unregister(fb) == EMBEDIQ_ERR, "second unregister returns ERR");
    ASSERT(embediq_wdg_checkin(fb) == EMBEDIQ_ERR, "check-in after unregister returns ERR");
    ASSERT(fault_events(EMBEDIQ_FAULT_WDG_NOT_REGISTERED, ep, 0, ANY) == 4u,
           "one NOT_REGISTERED event per rejected call, source = the FB");
}

/* A full table rejects a new entry and keeps the existing ones. */
static void test_full_table(void)
{
    manual_begin(1000u);
    wdg__set_capacity(2u);
    EmbedIQ_FB_Handle_t a = make_test_fb("cap_a");
    EmbedIQ_FB_Handle_t b = make_test_fb("cap_b");
    EmbedIQ_FB_Handle_t c = make_test_fb("cap_c");

    ASSERT(embediq_wdg_register(a, 100u) == EMBEDIQ_OK
           && embediq_wdg_register(b, 100u) == EMBEDIQ_OK, "two FBs register");
    ASSERT(embediq_wdg_register(c, 100u) == EMBEDIQ_ERR, "a third returns ERR");
    ASSERT(fault_events(EMBEDIQ_FAULT_WDG_TABLE_FULL, embediq_engine_fb_ep(c), 0, ANY) == 1u,
           "one TABLE_FULL event, source = the FB");
    ASSERT(embediq_wdg_checkin(c) == EMBEDIQ_ERR
           && embediq_wdg_checkin(a) == EMBEDIQ_OK
           && embediq_wdg_checkin(b) == EMBEDIQ_OK,
           "table unchanged: the third is absent, the first two remain");
    ASSERT(embediq_wdg_unregister(a) == EMBEDIQ_OK, "unregister one");
    ASSERT(embediq_wdg_register(c, 100u) == EMBEDIQ_OK, "the third then registers");
    wdg__set_capacity(0u);
}

/* Before the service is ready every call → ERR NOT_READY; nothing is created. */
static void test_not_ready(void)
{
    manual_begin(1000u);
    EmbedIQ_FB_Handle_t fb = make_test_fb("not_ready");
    uint8_t ep = embediq_engine_fb_ep(fb);
    (void)embediq_wdg_register(fb, 100u);
    wdg__stop_monitor();              /* service no longer ready */
    obs__reset();

    uint32_t mc0 = 0u, md0 = 0u, mc1 = 0u, md1 = 0u;
    embediq_osal__mutex_counts(&mc0, &md0);
    ASSERT(embediq_wdg_register(fb, 100u) == EMBEDIQ_ERR
           && embediq_wdg_checkin(fb) == EMBEDIQ_ERR
           && embediq_wdg_unregister(fb) == EMBEDIQ_ERR,
           "after the service stopped, each call returns ERR");
    ASSERT(fault_events(EMBEDIQ_FAULT_WDG_NOT_READY, ep, 0, 0xFF) == 3u,
           "one NOT_READY event per call, target 0xFF");

    /* A fresh process: nothing prepared, no table lock. */
    wdg__reset_service();
    obs__reset();
    ASSERT(embediq_wdg_register(fb, 100u) == EMBEDIQ_ERR
           && embediq_wdg_checkin(fb) == EMBEDIQ_ERR
           && embediq_wdg_unregister(fb) == EMBEDIQ_ERR,
           "before any preparation, each call returns ERR (no NULL lock is taken)");
    ASSERT(fault_events(EMBEDIQ_FAULT_WDG_NOT_READY, ep, 0, 0xFF) == 3u,
           "one NOT_READY event per call before preparation");
    embediq_osal__mutex_counts(&mc1, &md1);
    ASSERT(mc1 == mc0 && wdg__monitor_starts() == 0u,
           "no call creates a lock or a monitor");
}

/* ---------------------------------------------------------------------------
 * Time (controlled clock)
 * ------------------------------------------------------------------------- */

/* 4,294,968 ms × 1000 overflows 32 bits to 704: the timeout must stay in ms. */
#define OVERFLOWING_TIMEOUT_MS  4294968u

static void test_timeout_that_overflowed_microseconds(void)
{
    manual_begin(5000u);
    EmbedIQ_FB_Handle_t fb = make_test_fb("t_overflow");
    ASSERT(embediq_wdg_register(fb, OVERFLOWING_TIMEOUT_MS) == EMBEDIQ_OK, "register");
    g_now_ms = 5000u + 1000u;
    ASSERT(scan() == 0u, "after 1,000 ms: no miss");
    g_now_ms = 5000u + OVERFLOWING_TIMEOUT_MS;
    ASSERT(scan() == 0u, "after exactly the timeout: no miss");
    g_now_ms = 5000u + OVERFLOWING_TIMEOUT_MS + 1u;
    ASSERT(scan() == 1u, "one millisecond later: one miss");
}

static void test_clock_wrap(void)
{
    uint32_t start = UINT32_MAX - 50u;
    manual_begin(start);
    EmbedIQ_FB_Handle_t fb = make_test_fb("t_wrap");
    ASSERT(embediq_wdg_register(fb, 100u) == EMBEDIQ_OK, "register 50 ms before the wrap");
    g_now_ms = start + 91u;        /* wraps to 40 */
    ASSERT(scan() == 0u, "91 ms later, past the wrap: no miss");
    g_now_ms = start + 101u;
    ASSERT(scan() == 1u, "101 ms later: a miss");
}

static void test_maximum_timeout_and_delayed_scan(void)
{
    manual_begin(7u);
    EmbedIQ_FB_Handle_t fb = make_test_fb("t_max");
    ASSERT(embediq_wdg_register(fb, EMBEDIQ_WDG_TIMEOUT_MS_MAX) == EMBEDIQ_OK, "register MAX");
    g_now_ms = 7u + EMBEDIQ_WDG_TIMEOUT_MS_MAX;
    ASSERT(scan() == 0u, "elapsed == MAX: no miss");
    g_now_ms = 7u + EMBEDIQ_WDG_TIMEOUT_MS_MAX + 1u;
    ASSERT(scan() == 1u, "elapsed == MAX + 1: a miss");
    ASSERT(embediq_wdg_unregister(fb) == EMBEDIQ_OK, "unregister");

    ASSERT(embediq_wdg_register(fb, 100u) == EMBEDIQ_OK, "register 100 ms");
    g_now_ms += 10000u;
    ASSERT(scan() == 1u, "one scan 10,000 ms late reports exactly one miss");
}

/* ---------------------------------------------------------------------------
 * Scan ordering
 * ------------------------------------------------------------------------- */

static EmbedIQ_FB_Handle_t     g_hook_fb;
static embediq_err_t           g_hook_rc;
static uint32_t                g_hook_calls;
static embediq_osal_atomic32_t g_selected_faults = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);

static void unregister_selected(void)
{
    g_hook_calls++;
    g_hook_rc = embediq_wdg_unregister(g_hook_fb);
}

static void count_fault(EmbedIQ_FB_Handle_t fb, void *fb_data, uint32_t reason)
{
    (void)fb; (void)fb_data; (void)reason;
    (void)embediq_osal_atomic32_add(&g_selected_faults, 1u);
}

/* The scan hook unregisters the FB the scan has just selected: the miss is
 * still reported exactly once, and a second scan reports nothing. */
static void test_removal_during_scan(void)
{
    alarm(20u);   /* an unregister that waited for the scan would hang */
    manual_begin(1000u);
    embediq_osal_atomic32_store(&g_selected_faults, 0u);
    static const EmbedIQ_FB_Config_t k_sel = {
        .name = "selected", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
        .fault_fn = count_fault,
    };
    EmbedIQ_FB_Handle_t fb = embediq_fb_register(&k_sel);
    ASSERT(embediq_engine_boot() == 0, "boot");
    uint8_t ep = embediq_engine_fb_ep(fb);

    ASSERT(embediq_wdg_register(fb, 100u) == EMBEDIQ_OK, "register");
    g_now_ms = 1101u;
    g_hook_fb = fb;
    g_hook_calls = 0u;
    g_hook_rc = EMBEDIQ_ERR;
    obs__reset();
    wdg__set_scan_hook(unregister_selected);
    uint32_t misses = scan();
    wdg__set_scan_hook(NULL);

    ASSERT(g_hook_calls == 1u && g_hook_rc == EMBEDIQ_OK,
           "unregister of the selected FB returns OK between selection and report");
    ASSERT(misses == 1u, "the selected miss is counted once");
    ASSERT(fault_events(0u, ep, MSG_WDG_MISSED_CHECKIN, ANY) == 1u,
           "one FAULT event for the selected miss");
    ASSERT(embediq_osal_atomic32_load(&g_selected_faults) == 1u,
           "the monitored FB's fault_fn ran once");

    g_now_ms += 1000u;
    ASSERT(scan() == 0u && embediq_osal_atomic32_load(&g_selected_faults) == 1u,
           "a second scan reports nothing");
    alarm(0u);
}

/* ---------------------------------------------------------------------------
 * Missed check-ins with the monitor task running
 *
 * These tests boot fb_watchdog through the engine, so its monitor task runs.
 * Each one stops the task before returning, and arms alarm() as a hang
 * guard: a monitor that blocks while holding its lock would otherwise hang
 * the binary.
 * ------------------------------------------------------------------------- */

#define WDG_TEST_PING_ID         0x1438u  /* community range, unique to this file */
#define MONITORED_TIMEOUT_MS     30u
#define FAULT_DEADLINE_MS        1000u
#define CHECKIN_WAIT_MAX_MS      500u
#define CHECKIN_LATENCY_MAX_US   50000u
#define HANG_GUARD_S             20u
#define MISSED_CHECKIN_REASON    1u       /* fb_watchdog.c WDG_REASON_MISSED_CHECKIN */
#define SERVICE_UNAVAILABLE_REASON 3u     /* fb_watchdog.c WDG_REASON_SERVICE_UNAVAILABLE */

/* Sink FB: subscribes to MSG_WDG_MISSED_CHECKIN, records the missed ep. */
static embediq_osal_atomic32_t g_missed_rx = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);
static embediq_osal_atomic32_t g_missed_ep = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);

static void missed_sink_run_fn(EmbedIQ_FB_Handle_t fb, const void *msg, void *fb_data,
                               void *subfn_data)
{
    (void)fb; (void)fb_data; (void)subfn_data;
    if (msg == NULL) return;
    uint8_t ep = 0u;
    memcpy(&ep, msg, sizeof(ep));
    embediq_osal_atomic32_store(&g_missed_ep, ep);
    (void)embediq_osal_atomic32_add(&g_missed_rx, 1u);
}

EMBEDIQ_SUBS(g_missed_subs, MSG_WDG_MISSED_CHECKIN);

static const EmbedIQ_SubFn_Config_t g_missed_sink_subfn = {
    .name = "missed_sink", .init_order = 0,
    .subscriptions = g_missed_subs, .subscription_count = 1,
    .run_fn = missed_sink_run_fn,
};

static void missed_sink_init(EmbedIQ_FB_Handle_t h, void *d)
{
    (void)d;
    embediq_subfn_register(h, &g_missed_sink_subfn);
}

static const EmbedIQ_FB_Config_t k_missed_sink = {
    .name = "missed_checkin_sink", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
    .init_fn = missed_sink_init,
    .subscriptions = g_missed_subs, .subscription_count = 1,
};

/* The sink's HIGH queue is full and dispatch is not running, so the monitor's
 * MSG_WDG_MISSED_CHECKIN publish blocks. A check-in from another thread —
 * which takes the watchdog lock — must still return at once. */
static embediq_osal_atomic32_t g_checkin_done = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);
static embediq_osal_atomic32_t g_checkin_us   = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);

static void *checkin_caller(void *arg)
{
    uint32_t t0 = embediq_osal_time_us();
    (void)embediq_wdg_checkin(arg);   /* returns OK: idle is still registered */
    embediq_osal_atomic32_store(&g_checkin_us, embediq_osal_time_us() - t0);
    embediq_osal_atomic32_store(&g_checkin_done, 1u);
    return NULL;
}

static void test_monitor_does_not_hold_mutex_while_publishing(void)
{
    alarm(HANG_GUARD_S);
    fb_engine__reset();
    wdg__init_state();
    embediq_osal_atomic32_store(&g_missed_rx, 0u);
    embediq_osal_atomic32_store(&g_checkin_done, 0u);
    embediq_osal_atomic32_store(&g_checkin_us, 0u);

    static const EmbedIQ_FB_Config_t k_idle = {
        .name = "idle_monitored", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
    };
    (void)embediq_fb_register(&k_missed_sink);
    EmbedIQ_FB_Handle_t idle = embediq_fb_register(&k_idle);
    EmbedIQ_FB_Handle_t wdg  = fb_watchdog_register();
    embediq_engine_boot();              /* wdg_init starts the monitor task */

    /* Fill the sink's HIGH queue; dispatch is not running, so it stays full. */
    for (uint32_t i = 0u; i < EMBEDIQ_HIGH_QUEUE_DEPTH; i++) {
        EmbedIQ_Msg_t m;
        memset(&m, 0, sizeof(m));
        m.msg_id      = MSG_WDG_MISSED_CHECKIN;
        m.priority    = EMBEDIQ_MSG_PRIORITY_HIGH;
        m.payload_len = 1u;
        embediq_publish(idle, &m);
    }

    bool registered = embediq_wdg_register(idle, MONITORED_TIMEOUT_MS) == EMBEDIQ_OK;
    uint32_t t0 = embediq_osal_time_ms();
    while (embediq_fb_get_state(idle) != EMBEDIQ_FB_STATE_FAULT
           && embediq_fb_get_state(wdg) != EMBEDIQ_FB_STATE_FAULT
           && (embediq_osal_time_ms() - t0) < FAULT_DEADLINE_MS) {
        embediq_osal_delay_ms(1u);
    }
    bool detected = embediq_fb_get_state(idle) == EMBEDIQ_FB_STATE_FAULT
                    || embediq_fb_get_state(wdg) == EMBEDIQ_FB_STATE_FAULT;

    pthread_t th;
    pthread_create(&th, NULL, checkin_caller, idle);
    bool returned = wait_flag(&g_checkin_done, CHECKIN_WAIT_MAX_MS);

    embediq_engine_dispatch_boot();     /* drain the sink: releases the publish */
    pthread_join(th, NULL);
    uint32_t latency_us = embediq_osal_atomic32_load(&g_checkin_us);
    (void)embediq_wdg_unregister(idle);
    wdg__stop_monitor();
    (void)embediq_engine_dispatch_shutdown();
    alarm(0u);

    printf("      detected=%d returned_in_%ums=%d checkin_latency=%u us\n",
           (int)detected, (unsigned)CHECKIN_WAIT_MAX_MS, (int)returned,
           (unsigned)latency_us);
    ASSERT(registered && detected && returned && latency_us < CHECKIN_LATENCY_MAX_US,
           "check-in returns promptly while the monitor's publish is blocked");
}

/* A missed check-in is the monitored FB's fault: its fault_fn runs on its own
 * dispatch thread with the missed-check-in reason, fb_watchdog stays RUNNING,
 * and MSG_WDG_MISSED_CHECKIN still reaches subscribers with the missed ep. */
static embediq_osal_atomic32_t g_ping_seen  = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);
static embediq_osal_atomic32_t g_fault_seen = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);
static pthread_t               g_mon_run_thread;
static pthread_t               g_mon_fault_thread;
static uint32_t                g_mon_fault_reason;

static void monitored_run_fn(EmbedIQ_FB_Handle_t fb, const void *msg, void *fb_data,
                             void *subfn_data)
{
    (void)fb; (void)msg; (void)fb_data; (void)subfn_data;
    g_mon_run_thread = pthread_self();
    embediq_osal_atomic32_store(&g_ping_seen, 1u);
}

/* Records the first fault only; later faults (the FB keeps missing) are ignored. */
static void monitored_fault_fn(EmbedIQ_FB_Handle_t fb, void *fb_data, uint32_t reason)
{
    (void)fb; (void)fb_data;
    if (embediq_osal_atomic32_load(&g_fault_seen) == 0u) {
        g_mon_fault_thread = pthread_self();
        g_mon_fault_reason = reason;
        embediq_osal_atomic32_store(&g_fault_seen, 1u);
    }
}

EMBEDIQ_SUBS(g_ping_subs, WDG_TEST_PING_ID);

static const EmbedIQ_SubFn_Config_t g_monitored_subfn = {
    .name = "monitored_ping", .init_order = 0,
    .subscriptions = g_ping_subs, .subscription_count = 1,
    .run_fn = monitored_run_fn,
};

static void monitored_init(EmbedIQ_FB_Handle_t h, void *d)
{
    (void)d;
    embediq_subfn_register(h, &g_monitored_subfn);
}

static void test_missed_checkin_faults_monitored_fb(void)
{
    alarm(HANG_GUARD_S);
    fb_engine__reset();
    wdg__init_state();
    embediq_osal_atomic32_store(&g_missed_rx, 0u);
    embediq_osal_atomic32_store(&g_ping_seen, 0u);
    embediq_osal_atomic32_store(&g_fault_seen, 0u);

    static const EmbedIQ_FB_Config_t k_mon = {
        .name = "monitored", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
        .init_fn = monitored_init, .fault_fn = monitored_fault_fn,
        .subscriptions = g_ping_subs, .subscription_count = 1,
    };
    EmbedIQ_FB_Handle_t mon  = embediq_fb_register(&k_mon);
    EmbedIQ_FB_Handle_t sink = embediq_fb_register(&k_missed_sink);
    EmbedIQ_FB_Handle_t wdg  = fb_watchdog_register();
    embediq_engine_boot();              /* wdg_init starts the monitor task */
    embediq_engine_dispatch_boot();

    /* Learn the monitored FB's dispatch thread through a normal message. */
    EmbedIQ_Msg_t m;
    memset(&m, 0, sizeof(m));
    m.msg_id   = WDG_TEST_PING_ID;
    m.priority = EMBEDIQ_MSG_PRIORITY_NORMAL;
    embediq_publish(sink, &m);
    bool pinged = wait_flag(&g_ping_seen, FAULT_DEADLINE_MS);

    bool registered = embediq_wdg_register(mon, MONITORED_TIMEOUT_MS) == EMBEDIQ_OK;
    bool faulted    = wait_flag(&g_fault_seen, FAULT_DEADLINE_MS);
    bool published  = wait_flag(&g_missed_rx, FAULT_DEADLINE_MS);

    bool on_own_thread = faulted && pthread_equal(g_mon_fault_thread, g_mon_run_thread);
    bool reason_ok     = faulted && g_mon_fault_reason == MISSED_CHECKIN_REASON;
    bool mon_fault     = embediq_fb_get_state(mon) == EMBEDIQ_FB_STATE_FAULT;
    bool wdg_running   = embediq_fb_get_state(wdg) == EMBEDIQ_FB_STATE_RUNNING;
    bool ep_ok         = published
                         && embediq_osal_atomic32_load(&g_missed_ep)
                            == (uint32_t)embediq_engine_fb_ep(mon);

    (void)embediq_wdg_unregister(mon);
    wdg__stop_monitor();
    (void)embediq_engine_dispatch_shutdown();
    alarm(0u);

    printf("      pinged=%d faulted=%d own_thread=%d reason_ok=%d mon_fault=%d "
           "wdg_running=%d published=%d ep_ok=%d\n",
           (int)pinged, (int)faulted, (int)on_own_thread, (int)reason_ok,
           (int)mon_fault, (int)wdg_running, (int)published, (int)ep_ok);
    ASSERT(registered && pinged && on_own_thread && reason_ok && mon_fault
           && wdg_running && ep_ok,
           "missed check-in faults the monitored FB on its own dispatch thread");
}

/* A monitored FB whose fault_fn checks in: the fault_fn runs on the monitor
 * thread (the FB has no dispatch thread), takes the watchdog lock, and must
 * neither deadlock nor be refused. */
static EmbedIQ_FB_Handle_t     g_self_fb;
static embediq_osal_atomic32_t g_self_faults = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);
static embediq_osal_atomic32_t g_self_rc_ok  = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);

static void checkin_from_fault_fn(EmbedIQ_FB_Handle_t fb, void *fb_data, uint32_t reason)
{
    (void)fb_data; (void)reason;
    if (embediq_wdg_checkin(fb) == EMBEDIQ_OK) {
        (void)embediq_osal_atomic32_add(&g_self_rc_ok, 1u);
    }
    (void)embediq_osal_atomic32_add(&g_self_faults, 1u);
}

static void test_fault_fn_that_checks_in_does_not_deadlock(void)
{
    alarm(HANG_GUARD_S);
    fb_engine__reset();
    wdg__init_state();
    embediq_osal_atomic32_store(&g_self_faults, 0u);
    embediq_osal_atomic32_store(&g_self_rc_ok, 0u);

    static const EmbedIQ_FB_Config_t k_self = {
        .name = "self_checkin", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
        .fault_fn = checkin_from_fault_fn,
    };
    g_self_fb = embediq_fb_register(&k_self);
    (void)fb_watchdog_register();
    bool booted = embediq_engine_boot() == 0;

    bool registered = embediq_wdg_register(g_self_fb, MONITORED_TIMEOUT_MS) == EMBEDIQ_OK;
    bool faulted = wait_flag(&g_self_faults, FAULT_DEADLINE_MS);
    bool rc_ok   = embediq_osal_atomic32_load(&g_self_rc_ok) >= 1u;
    /* The table lock is free again: a call from this thread returns. */
    bool unreg   = embediq_wdg_unregister(g_self_fb) == EMBEDIQ_OK;
    wdg__stop_monitor();
    alarm(0u);

    ASSERT(booted && registered && faulted && rc_ok && unreg,
           "a fault_fn that checks in returns OK and does not deadlock the monitor");
}

/* ---------------------------------------------------------------------------
 * Service preparation through the real wdg_init()
 *
 * Each case starts with wdg__reset_service(), not wdg__init_state(), so
 * readiness, the table lock and the monitor come from wdg_init() itself.
 * ------------------------------------------------------------------------- */

static const char *const k_a_deps[] = { "prep_b" };
static const char *g_b_deps[]       = { "prep_a" };   /* corrected between boots */

static const EmbedIQ_FB_Config_t k_prep_a = {
    .name = "prep_a", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
    .depends_on = k_a_deps, .depends_count = 1,
};
static const EmbedIQ_FB_Config_t k_prep_b = {
    .name = "prep_b", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
    .depends_on = g_b_deps, .depends_count = 1,
};

typedef struct {
    EmbedIQ_FB_Handle_t wdg;
    EmbedIQ_FB_Handle_t a;
    uint8_t             wdg_ep;
} prep_t;

/* Fresh engine and service; fb_watchdog plus prep_a / prep_b. With cycle,
 * the application phase fails after the watchdog initialised. */
static prep_t prep_begin(bool cycle)
{
    wdg__reset_service();
    fb_engine__reset();
    obs__reset();
    embediq_obs_set_transport(EMBEDIQ_OBS_TRANSPORT_TCP);
    g_b_deps[0] = cycle ? "prep_a" : "fb_watchdog";
    prep_t p;
    p.wdg    = fb_watchdog_register();
    p.a      = embediq_fb_register(&k_prep_a);
    (void)embediq_fb_register(&k_prep_b);
    p.wdg_ep = embediq_engine_fb_ep(p.wdg);
    return p;
}

/* A registered FB that never checks in is faulted by the running monitor. */
static bool monitored(EmbedIQ_FB_Handle_t fb)
{
    if (embediq_wdg_register(fb, MONITORED_TIMEOUT_MS) != EMBEDIQ_OK) return false;
    uint32_t t0 = embediq_osal_time_ms();
    while (embediq_fb_get_state(fb) != EMBEDIQ_FB_STATE_FAULT
           && embediq_osal_time_ms() - t0 < FAULT_DEADLINE_MS) {
        embediq_osal_delay_ms(1u);
    }
    bool ok = embediq_fb_get_state(fb) == EMBEDIQ_FB_STATE_FAULT;
    (void)embediq_wdg_unregister(fb);
    return ok;
}

/* Mutexes one boot of the fixture creates; the watchdog's table lock is the
 * last of them (no initializer after it creates one). */
static uint32_t boot_mutex_creates(bool cycle)
{
    (void)prep_begin(cycle);
    uint32_t c0 = 0u, c1 = 0u;
    embediq_osal__mutex_counts(&c0, NULL);
    (void)embediq_engine_boot();
    embediq_osal__mutex_counts(&c1, NULL);
    return c1 - c0;
}

static void test_one_monitor_across_late_boot_retry(void)
{
    alarm(HANG_GUARD_S);
    prep_t p = prep_begin(true);

    ASSERT(embediq_engine_boot() == -1, "boot fails on the dependency cycle");
    ASSERT(wdg__monitor_starts() == 1u, "the watchdog initialised: one monitor");
    g_b_deps[0] = "fb_watchdog";          /* same FBs, one dependency entry corrected */
    ASSERT(embediq_engine_boot() == 0, "the retry boot returns 0");
    ASSERT(wdg__monitor_starts() == 1u, "still exactly one monitor after the retry");
    ASSERT(fault_events(EMBEDIQ_FAULT_WDG_UNAVAILABLE, p.wdg_ep, ANY, ANY) == 0u,
           "no watchdog resource fault");
    ASSERT(monitored(p.a), "a registered FB that stops checking in is reported");
    wdg__reset_service();
    alarm(0u);
}

/* Inject the failure, boot, disarm; returns the boot result. */
typedef enum { FAIL_MUTEX, FAIL_TASK } prep_fail_t;

static int boot_with_failure(prep_fail_t kind, uint32_t nth)
{
    if (kind == FAIL_MUTEX) embediq_osal__fail_next_mutex_create(nth);
    else                    embediq_osal__fail_next_task_create(nth);
    int rc = embediq_engine_boot();
    embediq_osal__fail_next_mutex_create(0u);
    embediq_osal__fail_next_task_create(0u);
    return rc;
}

/* Common checks after a preparation failure. */
static void check_unavailable(const prep_t *p, uint16_t resource, const char *what)
{
    char msg[160];
    snprintf(msg, sizeof(msg), "%s: one UNAVAILABLE event naming the resource", what);
    ASSERT(fault_events(EMBEDIQ_FAULT_WDG_UNAVAILABLE, p->wdg_ep, resource, 0xFF) == 1u, msg);
    snprintf(msg, sizeof(msg), "%s: the watchdog FB reports its own fault", what);
    ASSERT(embediq_fb_get_state(p->wdg) == EMBEDIQ_FB_STATE_FAULT, msg);
    snprintf(msg, sizeof(msg), "%s: no monitor was started", what);
    ASSERT(wdg__monitor_starts() == 0u, msg);
    obs__reset();
    snprintf(msg, sizeof(msg), "%s: register returns ERR, NOT_READY", what);
    ASSERT(embediq_wdg_register(p->a, 100u) == EMBEDIQ_ERR
           && fault_events(EMBEDIQ_FAULT_WDG_NOT_READY, embediq_engine_fb_ep(p->a), 0, 0xFF)
              == 1u, msg);
}

static void test_mutex_creation_fails(void)
{
    alarm(HANG_GUARD_S);
    uint32_t nth = boot_mutex_creates(false);
    prep_t p = prep_begin(false);
    ASSERT(boot_with_failure(FAIL_MUTEX, nth) == 0, "boot completes");
    check_unavailable(&p, EMBEDIQ_WDG_RESOURCE_MUTEX, "table lock");
    wdg__reset_service();
    alarm(0u);
}

static void test_monitor_creation_fails(void)
{
    alarm(HANG_GUARD_S);
    prep_t p = prep_begin(false);
    ASSERT(boot_with_failure(FAIL_TASK, 1u) == 0, "boot completes");
    check_unavailable(&p, EMBEDIQ_WDG_RESOURCE_MONITOR, "monitor");
    ASSERT(hal_wdg_kick() == HAL_ERR_INVALID, "monitor: the HAL watchdog was deinitialised");
    wdg__reset_service();
    alarm(0u);
}

/* A preparation failure inside a boot that also fails late; the retried boot
 * prepares the service: ready, one monitor, register OK and monitored. */
static void recovery(prep_fail_t kind)
{
    alarm(HANG_GUARD_S);
    const char *what = kind == FAIL_MUTEX ? "table lock" : "monitor";
    uint32_t nth = kind == FAIL_MUTEX ? boot_mutex_creates(true) : 1u;
    prep_t p = prep_begin(true);
    char msg[160];

    snprintf(msg, sizeof(msg), "%s: the first boot fails on the cycle", what);
    ASSERT(boot_with_failure(kind, nth) == -1, msg);
    uint16_t resource = kind == FAIL_MUTEX ? EMBEDIQ_WDG_RESOURCE_MUTEX
                                           : EMBEDIQ_WDG_RESOURCE_MONITOR;
    snprintf(msg, sizeof(msg), "%s: the watchdog reported the failure", what);
    ASSERT(fault_events(EMBEDIQ_FAULT_WDG_UNAVAILABLE, p.wdg_ep, resource, ANY) == 1u
           && wdg__monitor_starts() == 0u, msg);

    g_b_deps[0] = "fb_watchdog";
    snprintf(msg, sizeof(msg), "%s: the retry boot returns 0", what);
    ASSERT(embediq_engine_boot() == 0, msg);
    snprintf(msg, sizeof(msg), "%s: exactly one monitor after the retry", what);
    ASSERT(wdg__monitor_starts() == 1u, msg);
    snprintf(msg, sizeof(msg), "%s: register OK and the FB is monitored", what);
    ASSERT(monitored(p.a), msg);
    wdg__reset_service();
    alarm(0u);
}

static void test_recovery_after_mutex_failure(void)   { recovery(FAIL_MUTEX); }
static void test_recovery_after_monitor_failure(void) { recovery(FAIL_TASK); }

/* A worker thread calls embediq_wdg_checkin() in a loop from before boot
 * until it is told to stop, and counts only COMPLETED calls, by status. The
 * test claims three facts, each separated from the others by a call that has
 * returned:
 *   unready      every call that completed before boot began returned ERR,
 *                and nothing was created (no monitor);
 *   ready, not   after boot returned 0, a check-in by the test thread
 *   registered   returns ERR with one NOT_REGISTERED event (the worker is
 *                parked between calls for this step, so the one event is
 *                that call's);
 *   registered   after embediq_wdg_register() has returned OK, a check-in
 *                by the test thread returns OK, and the worker completes
 *                at least one more OK call.
 * A worker call that overlaps boot or the registration is not classified:
 * it may legally return either status, depending on the table state it
 * observes once it holds the table lock. Starting first does not mean
 * locking first; the lock-order cases are in test_fb_watchdog_boundaries.c.
 * ThreadSanitizer: no report. */
static embediq_osal_atomic32_t g_race_done_err = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);
static embediq_osal_atomic32_t g_race_done_ok  = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);
static embediq_osal_atomic32_t g_race_stop     = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);
static embediq_osal_atomic32_t g_race_park     = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);
static embediq_osal_atomic32_t g_race_parked   = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);

static void *race_checkin(void *arg)
{
    while (embediq_osal_atomic32_load(&g_race_stop) == 0u) {
        if (embediq_osal_atomic32_load(&g_race_park) != 0u) {
            embediq_osal_atomic32_store(&g_race_parked, 1u);
            while (embediq_osal_atomic32_load(&g_race_park) != 0u
                   && embediq_osal_atomic32_load(&g_race_stop) == 0u) {
                embediq_osal_delay_ms(1u);
            }
            embediq_osal_atomic32_store(&g_race_parked, 0u);
            continue;
        }
        if (embediq_wdg_checkin(arg) == EMBEDIQ_OK) {
            (void)embediq_osal_atomic32_add(&g_race_done_ok, 1u);
        } else {
            (void)embediq_osal_atomic32_add(&g_race_done_err, 1u);
        }
    }
    return NULL;
}

static uint32_t race_done(void)
{
    return embediq_osal_atomic32_load(&g_race_done_err)
         + embediq_osal_atomic32_load(&g_race_done_ok);
}

/* Spin (1 ms steps) until *a >= target or ms elapse (hang guard only). */
static bool wait_at_least(embediq_osal_atomic32_t *a, uint32_t target, uint32_t ms)
{
    uint32_t t0 = embediq_osal_time_ms();
    while (embediq_osal_atomic32_load(a) < target
           && (embediq_osal_time_ms() - t0) < ms) {
        embediq_osal_delay_ms(1u);
    }
    return embediq_osal_atomic32_load(a) >= target;
}

static void test_checkin_racing_preparation(void)
{
    alarm(HANG_GUARD_S);
    prep_t p = prep_begin(false);            /* starts from wdg__reset_service() */
    uint8_t a_ep = embediq_engine_fb_ep(p.a);
    embediq_osal_atomic32_store(&g_race_done_err, 0u);
    embediq_osal_atomic32_store(&g_race_done_ok, 0u);
    embediq_osal_atomic32_store(&g_race_stop, 0u);
    embediq_osal_atomic32_store(&g_race_park, 0u);
    embediq_osal_atomic32_store(&g_race_parked, 0u);

    pthread_t th;
    pthread_create(&th, NULL, race_checkin, p.a);

    /* Unready: at least two calls completed before boot begins. */
    uint32_t t0 = embediq_osal_time_ms();
    while (race_done() < 2u && embediq_osal_time_ms() - t0 < FAULT_DEADLINE_MS) {
        embediq_osal_delay_ms(1u);
    }
    uint32_t a_err = embediq_osal_atomic32_load(&g_race_done_err);
    uint32_t a_ok  = embediq_osal_atomic32_load(&g_race_done_ok);
    uint32_t a_mon = wdg__monitor_starts();

    /* Ready, not registered: the worker is parked between calls. */
    bool booted = embediq_engine_boot() == 0;
    embediq_osal_atomic32_store(&g_race_park, 1u);
    bool parked = wait_flag(&g_race_parked, FAULT_DEADLINE_MS);
    uint32_t nr_before = fault_events(EMBEDIQ_FAULT_WDG_NOT_REGISTERED, a_ep, 0, ANY);
    embediq_err_t b_rc = embediq_wdg_checkin(p.a);
    uint32_t nr_after  = fault_events(EMBEDIQ_FAULT_WDG_NOT_REGISTERED, a_ep, 0, ANY);
    embediq_osal_atomic32_store(&g_race_park, 0u);

    /* Registered: every check below follows the register's return. */
    embediq_err_t reg_rc = embediq_wdg_register(p.a, EMBEDIQ_WDG_TIMEOUT_MS_MAX);
    uint32_t ok_after_reg = embediq_osal_atomic32_load(&g_race_done_ok);
    embediq_err_t c_rc = embediq_wdg_checkin(p.a);
    bool worker_ok = wait_at_least(&g_race_done_ok, ok_after_reg + 1u, FAULT_DEADLINE_MS);

    embediq_osal_atomic32_store(&g_race_stop, 1u);
    pthread_join(th, NULL);
    (void)embediq_wdg_unregister(p.a);
    wdg__reset_service();
    alarm(0u);

    printf("      completed before boot: err=%u ok=%u; completed in all: err=%u ok=%u\n",
           (unsigned)a_err, (unsigned)a_ok,
           (unsigned)embediq_osal_atomic32_load(&g_race_done_err),
           (unsigned)embediq_osal_atomic32_load(&g_race_done_ok));
    ASSERT(a_err + a_ok >= 2u, "unready: the worker completed calls before boot began");
    ASSERT(a_ok == 0u && a_mon == 0u,
           "unready: every call completed before boot returned ERR and created nothing");
    ASSERT(booted && parked, "boot returns 0; the worker is parked between calls");
    ASSERT(b_rc == EMBEDIQ_ERR && nr_after - nr_before == 1u,
           "ready, not registered: check-in returns ERR with one NOT_REGISTERED event");
    ASSERT(reg_rc == EMBEDIQ_OK, "register returns OK");
    ASSERT(c_rc == EMBEDIQ_OK, "after register returned: check-in returns OK");
    ASSERT(worker_ok, "after register returned: the worker completes an OK check-in");
}

/* ---------------------------------------------------------------------------
 * main
 * ------------------------------------------------------------------------- */

int main(void)
{
    /* Manual service, unchanged behaviour */
    test_wdg_all_checkin_no_fault();
    test_wdg_missed_checkin_emits_event();
    test_wdg_multiple_fbs_all_must_checkin();
    test_wdg_unregister_removes_from_tracking();

    /* HAL WDG contract tests */
    test_hal_wdg_init_returns_ok();
    test_hal_wdg_kick_returns_ok_after_init();
    test_hal_wdg_deinit_does_not_crash();
    test_hal_wdg_kick_before_init_returns_error();

    /* TIMING band */
    test_wdg_checkin_emits_timing_event();

    /* Contract */
    test_calls_return_ok_and_reregister_replaces_and_restarts();
    test_null_handle_is_rejected_without_change();
    test_timeout_range_and_failed_update();
    test_not_registered_and_unregister_twice();
    test_full_table();
    test_not_ready();

    /* Time */
    test_timeout_that_overflowed_microseconds();
    test_clock_wrap();
    test_maximum_timeout_and_delayed_scan();

    /* Scan ordering */
    test_removal_during_scan();
    test_monitor_does_not_hold_mutex_while_publishing();
    test_missed_checkin_faults_monitored_fb();
    test_fault_fn_that_checks_in_does_not_deadlock();

    /* Service preparation */
    test_one_monitor_across_late_boot_retry();
    test_mutex_creation_fails();
    test_monitor_creation_fails();
    test_recovery_after_mutex_failure();
    test_recovery_after_monitor_failure();
    test_checkin_racing_preparation();

    wdg__reset_service();
    fb_engine__reset();

    printf("\n");
    if (g_tests_failed == 0) {
        printf("All %d tests passed. (0 failed)\n", g_tests_run);
    } else {
        printf("%d of %d tests FAILED.\n", g_tests_failed, g_tests_run);
    }
    return (g_tests_failed > 0) ? 1 : 0;
}
