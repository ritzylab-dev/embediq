#define _POSIX_C_SOURCE 200809L

/*
 * tests/unit/test_fb_watchdog.c — Unit tests for fb_watchdog
 *
 * Tests the health-token watchdog: registration, checkin, missed-checkin
 * fault detection, unregistration, and multi-FB isolation.
 *
 * All tests use wdg__init_state() + wdg__trigger_check() to avoid depending
 * on real 100 ms timer intervals.  Faults are detected via either the
 * Observatory event count or wdg__fault_count().
 *
 * Run:      ./build/tests/unit/test_fb_watchdog
 * Expected: "All 14 tests passed. (0 failed)"
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "embediq_fb.h"
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
 * Package-internal watchdog API
 * ------------------------------------------------------------------------- */

extern void     wdg__init_state(void);
extern void     wdg__trigger_check(void);
extern uint32_t wdg__fault_count(void);
extern void     wdg__stop_monitor(void);

/* Public watchdog API (declared in fb_watchdog.c) */
extern void embediq_wdg_register(void *fb, uint32_t timeout_ms);
extern void embediq_wdg_checkin(void *fb);
extern void embediq_wdg_unregister(void *fb);

/* Test-only FB engine API */
extern void     fb_engine__reset(void);
extern EmbedIQ_FB_Handle_t fb_watchdog_register(void);
extern uint32_t fb_engine__obs_event_count(void);

/* Package-internal Observatory capture API (EMBEDIQ_PLATFORM_HOST only) */
extern void     obs__reset(void);
extern uint32_t obs__ring_count(void);
extern bool     obs__ring_read(uint32_t idx, EmbedIQ_Event_t *out);
extern void     obs__set_level(uint8_t level);

/* ---------------------------------------------------------------------------
 * Minimal test harness
 * ------------------------------------------------------------------------- */

static int g_tests_run    = 0;
static int g_tests_failed = 0;

#define ASSERT(cond, msg)  do {                                                \
    g_tests_run++;                                                             \
    if (!(cond)) {                                                             \
        fprintf(stderr, "FAIL  %-56s  %s:%d\n", __func__, __FILE__, __LINE__);\
        g_tests_failed++;                                                      \
    } else {                                                                   \
        printf("PASS  %s\n", __func__);                                        \
    }                                                                          \
} while (0)

/* ---------------------------------------------------------------------------
 * Helper: FB configs for test stand-ins
 * ------------------------------------------------------------------------- */

static EmbedIQ_FB_Handle_t make_test_fb(const char *name)
{
    EmbedIQ_FB_Config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.name       = name;
    cfg.boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION;
    return embediq_fb_register(&cfg);
}

/* ---------------------------------------------------------------------------
 * Tests
 * ------------------------------------------------------------------------- */

/* test_wdg_all_checkin_no_fault
 * When every registered FB checks in within its timeout, no fault is emitted.
 */
static void test_wdg_all_checkin_no_fault(void)
{
    fb_engine__reset();
    wdg__init_state();

    EmbedIQ_FB_Handle_t fb = make_test_fb("fb_all_ok");

    embediq_wdg_register(fb, 200u);   /* 200 ms timeout */
    embediq_wdg_checkin(fb);          /* fresh heartbeat */
    wdg__trigger_check();             /* check immediately — elapsed ≈ 0 */

    ASSERT(wdg__fault_count() == 0u,
           "no fault when FB checks in within timeout");
}

/* test_wdg_missed_checkin_emits_event
 * An FB registered with a 1 ms timeout that never calls checkin (after the
 * initial registration timestamp ages past 1 ms) must trigger a fault.
 */
static void test_wdg_missed_checkin_emits_event(void)
{
    fb_engine__reset();
    wdg__init_state();

    EmbedIQ_FB_Handle_t fb = make_test_fb("fb_missed");

    embediq_wdg_register(fb, 1u);     /* 1 ms timeout */
    /* Intentionally do NOT call checkin. */
    embediq_osal_delay_ms(5u);        /* wait 5 ms — timeout expires */
    wdg__trigger_check();

    ASSERT(wdg__fault_count() >= 1u,
           "fault emitted when FB misses its checkin deadline");
    ASSERT(fb_engine__obs_event_count() >= 1u,
           "observatory FAULT event emitted for missed checkin");
}

/* test_wdg_multiple_fbs_all_must_checkin
 * Three FBs registered; A and B check in freshly after a delay; C does not.
 * Only C should trigger a fault (exactly one fault total).
 */
static void test_wdg_multiple_fbs_all_must_checkin(void)
{
    fb_engine__reset();
    wdg__init_state();

    EmbedIQ_FB_Handle_t fbA = make_test_fb("fb_a");
    EmbedIQ_FB_Handle_t fbB = make_test_fb("fb_b");
    EmbedIQ_FB_Handle_t fbC = make_test_fb("fb_c");

    embediq_wdg_register(fbA, 1u);
    embediq_wdg_register(fbB, 1u);
    embediq_wdg_register(fbC, 1u);

    embediq_osal_delay_ms(5u);   /* all three time out */

    /* A and B check in freshly — their timestamps are now < 1 ms old. */
    embediq_wdg_checkin(fbA);
    embediq_wdg_checkin(fbB);
    /* C never checks in. */

    wdg__trigger_check();

    ASSERT(wdg__fault_count() == 1u,
           "exactly one fault (for FB-C) when A and B check in on time");
}

/* test_wdg_unregister_removes_from_tracking
 * An FB that is unregistered before the check must not trigger a fault
 * even if its timeout has expired.
 */
static void test_wdg_unregister_removes_from_tracking(void)
{
    fb_engine__reset();
    wdg__init_state();

    EmbedIQ_FB_Handle_t fb = make_test_fb("fb_unreg");

    embediq_wdg_register(fb, 1u);
    /* Do NOT call checkin — will time out. */
    embediq_osal_delay_ms(5u);

    embediq_wdg_unregister(fb);   /* remove before check */
    wdg__trigger_check();

    ASSERT(wdg__fault_count() == 0u,
           "no fault for an unregistered FB even after timeout expiry");
}

/* ---------------------------------------------------------------------------
 * HAL WDG contract tests
 * ------------------------------------------------------------------------- */

/* test_hal_wdg_init_returns_ok
 * hal_wdg_init with a valid timeout must return HAL_OK.
 */
static void test_hal_wdg_init_returns_ok(void)
{
    hal_wdg_deinit();   /* ensure clean state */
    int rc = hal_wdg_init(500u);
    ASSERT(rc == HAL_OK, "hal_wdg_init returns HAL_OK");
    hal_wdg_deinit();
}

/* test_hal_wdg_kick_returns_ok_after_init
 * hal_wdg_kick must return HAL_OK after a successful init.
 */
static void test_hal_wdg_kick_returns_ok_after_init(void)
{
    hal_wdg_deinit();
    hal_wdg_init(500u);
    int rc = hal_wdg_kick();
    ASSERT(rc == HAL_OK, "hal_wdg_kick returns HAL_OK after init");
    hal_wdg_deinit();
}

/* test_hal_wdg_deinit_does_not_crash
 * Calling deinit (even twice) must not crash.
 */
static void test_hal_wdg_deinit_does_not_crash(void)
{
    hal_wdg_init(500u);
    hal_wdg_deinit();
    hal_wdg_deinit();   /* double deinit — must be safe */
    ASSERT(true, "hal_wdg_deinit does not crash on repeated calls");
}

/* test_hal_wdg_kick_before_init_returns_error
 * hal_wdg_kick before init must return HAL_ERR_INVALID.
 */
static void test_hal_wdg_kick_before_init_returns_error(void)
{
    hal_wdg_deinit();   /* ensure uninitialised */
    int rc = hal_wdg_kick();
    ASSERT(rc == HAL_ERR_INVALID,
           "hal_wdg_kick before init returns HAL_ERR_INVALID");
}

/* ---------------------------------------------------------------------------
 * test_wdg_checkin_emits_timing_event
 *
 * When all registered FBs check in within their timeout window,
 * monitor_scan() calls hal_wdg_kick() and must emit
 * EMBEDIQ_OBS_EVT_WDG_CHECKIN (0x50) in the TIMING band.
 *
 * Gated by EMBEDIQ_TRACE_TIMING — requires EMBEDIQ_TRACE_LEVEL >= 2.
 * embediq_driver_fbs is compiled with EMBEDIQ_TRACE_LEVEL=2 in
 * fbs/drivers/CMakeLists.txt so the macro expands to the real call.
 *
 * hal_wdg_init() is called explicitly so that hal_wdg_kick() returns
 * HAL_OK and does not emit a HAL_FAULT event before the TIMING emit.
 * ------------------------------------------------------------------------- */

static void test_wdg_checkin_emits_timing_event(void)
{
    fb_engine__reset();
    wdg__init_state();
    hal_wdg_init(500u);      /* ensure HAL is ready — no FAULT on kick */

    EmbedIQ_FB_Handle_t fb = make_test_fb("fb_timing_ok");
    embediq_wdg_register(fb, 200u);  /* generous timeout — will not miss */
    embediq_wdg_checkin(fb);         /* fresh heartbeat — elapsed ≈ 0 */

    obs__reset();                    /* clear any events from setup */
    obs__set_level(2u);              /* TIMING events require runtime level >= 2 */
    wdg__trigger_check();            /* all_ok=true → hal_wdg_kick() → TIMING emit */

    uint32_t count = obs__ring_count();
    ASSERT(count >= 1u,
           "all-checkin scan must emit at least one TIMING event");

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
    ASSERT(found,
           "EMBEDIQ_OBS_EVT_WDG_CHECKIN (0x50) must appear in Observatory ring");
    ASSERT(embediq_obs_event_family(EMBEDIQ_OBS_EVT_WDG_CHECKIN) ==
               EMBEDIQ_OBS_FAMILY_TIMING,
           "WDG_CHECKIN event must be in the TIMING family (band 0x50-0x5F)");

    hal_wdg_deinit();
}

/* ---------------------------------------------------------------------------
 * Missed check-ins with the monitor task running
 *
 * These tests boot fb_watchdog through the engine, so its monitor task runs.
 * Each one stops the task (wdg__stop_monitor) before returning, and arms
 * alarm() as a hang guard: a monitor that blocks while holding its mutex
 * would otherwise hang the binary.
 * ------------------------------------------------------------------------- */

#define WDG_TEST_PING_ID         0x1438u  /* community range, unique to this file */
#define MONITORED_TIMEOUT_MS     30u
#define FAULT_DEADLINE_MS        1000u
#define CHECKIN_WAIT_MAX_MS      500u
#define CHECKIN_LATENCY_MAX_US   50000u
#define HANG_GUARD_S             20u
#define MISSED_CHECKIN_REASON    1u       /* fb_watchdog.c WDG_REASON_MISSED_CHECKIN */

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

/* Sink FB: subscribes to MSG_WDG_MISSED_CHECKIN, records the missed ep. */
static embediq_osal_atomic32_t g_missed_rx = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);
static embediq_osal_atomic32_t g_missed_ep = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);

static void missed_sink_run_fn(EmbedIQ_FB_Handle_t fb, const void *msg, void *fb_data,
                               void *subfn_data)
{
    (void)fb; (void)fb_data; (void)subfn_data;
    embediq_osal_atomic32_store(&g_missed_ep, ((const uint8_t *)msg)[0]);
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

/* ---------------------------------------------------------------------------
 * test_monitor_does_not_hold_mutex_while_publishing
 *
 * The sink's HIGH queue is full and dispatch is not running, so the monitor's
 * MSG_WDG_MISSED_CHECKIN publish blocks. A check-in from another thread —
 * which takes the watchdog mutex — must still return at once.
 * ------------------------------------------------------------------------- */

static embediq_osal_atomic32_t g_checkin_done = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);
static embediq_osal_atomic32_t g_checkin_us   = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);

static void *checkin_caller(void *arg)
{
    uint32_t t0 = embediq_osal_time_us();
    embediq_wdg_checkin(arg);
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

    embediq_wdg_register(idle, MONITORED_TIMEOUT_MS);   /* never checks in */
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
    embediq_wdg_unregister(idle);
    wdg__stop_monitor();
    (void)embediq_engine_dispatch_shutdown();
    alarm(0u);

    printf("      detected=%d returned_in_%ums=%d checkin_latency=%u us\n",
           (int)detected, (unsigned)CHECKIN_WAIT_MAX_MS, (int)returned,
           (unsigned)latency_us);
    ASSERT(detected && returned && latency_us < CHECKIN_LATENCY_MAX_US,
           "check-in returns promptly while the monitor's publish is blocked");
}

/* ---------------------------------------------------------------------------
 * test_missed_checkin_faults_monitored_fb
 *
 * A missed check-in is the monitored FB's fault: its fault_fn runs on its own
 * dispatch thread with the missed-check-in reason, fb_watchdog stays RUNNING,
 * and MSG_WDG_MISSED_CHECKIN still reaches subscribers with the missed ep.
 * ------------------------------------------------------------------------- */

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

    embediq_wdg_register(mon, MONITORED_TIMEOUT_MS);   /* never checks in */
    bool faulted   = wait_flag(&g_fault_seen, FAULT_DEADLINE_MS);
    bool published = wait_flag(&g_missed_rx, FAULT_DEADLINE_MS);

    bool on_own_thread = faulted && pthread_equal(g_mon_fault_thread, g_mon_run_thread);
    bool reason_ok     = faulted && g_mon_fault_reason == MISSED_CHECKIN_REASON;
    bool mon_fault     = embediq_fb_get_state(mon) == EMBEDIQ_FB_STATE_FAULT;
    bool wdg_running   = embediq_fb_get_state(wdg) == EMBEDIQ_FB_STATE_RUNNING;
    bool ep_ok         = published
                         && embediq_osal_atomic32_load(&g_missed_ep)
                            == (uint32_t)embediq_engine_fb_ep(mon);

    embediq_wdg_unregister(mon);
    wdg__stop_monitor();
    (void)embediq_engine_dispatch_shutdown();
    alarm(0u);

    printf("      pinged=%d faulted=%d own_thread=%d reason_ok=%d mon_fault=%d "
           "wdg_running=%d published=%d ep_ok=%d\n",
           (int)pinged, (int)faulted, (int)on_own_thread, (int)reason_ok,
           (int)mon_fault, (int)wdg_running, (int)published, (int)ep_ok);
    ASSERT(pinged && on_own_thread && reason_ok && mon_fault && wdg_running && ep_ok,
           "missed check-in faults the monitored FB on its own dispatch thread");
}

/* ---------------------------------------------------------------------------
 * main
 * ------------------------------------------------------------------------- */

int main(void)
{
    /* Watchdog Driver FB tests */
    test_wdg_all_checkin_no_fault();
    test_wdg_missed_checkin_emits_event();
    test_wdg_multiple_fbs_all_must_checkin();
    test_wdg_unregister_removes_from_tracking();

    /* HAL WDG contract tests */
    test_hal_wdg_init_returns_ok();
    test_hal_wdg_kick_returns_ok_after_init();
    test_hal_wdg_deinit_does_not_crash();
    test_hal_wdg_kick_before_init_returns_error();

    /* XOBS-4 TIMING band test */
    test_wdg_checkin_emits_timing_event();

    /* Monitor task running: missed check-ins, two-phase scan */
    test_monitor_does_not_hold_mutex_while_publishing();
    test_missed_checkin_faults_monitored_fb();

    printf("\n");
    if (g_tests_failed == 0) {
        printf("All %d tests passed. (0 failed)\n", g_tests_run);
    } else {
        printf("%d of %d tests FAILED.\n", g_tests_failed, g_tests_run);
    }
    return (g_tests_failed > 0) ? 1 : 0;
}
