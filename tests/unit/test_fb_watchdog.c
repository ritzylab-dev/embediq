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
 * Expected: "All 5 tests passed. (0 failed)"
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "embediq_fb.h"
#include "embediq_osal.h"
#include "embediq_config.h"

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* ---------------------------------------------------------------------------
 * Package-internal watchdog API
 * ------------------------------------------------------------------------- */

extern void     wdg__init_state(void);
extern void     wdg__trigger_check(void);
extern uint32_t wdg__fault_count(void);

/* Public watchdog API (declared in fb_watchdog.c) */
extern void embediq_wdg_register(void *fb, uint32_t timeout_ms);
extern void embediq_wdg_checkin(void *fb);
extern void embediq_wdg_unregister(void *fb);

/* Test-only FB engine API */
extern void     fb_engine__reset(void);
extern uint32_t fb_engine__obs_event_count(void);

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
 * main
 * ------------------------------------------------------------------------- */

int main(void)
{
    test_wdg_all_checkin_no_fault();
    test_wdg_missed_checkin_emits_event();
    test_wdg_multiple_fbs_all_must_checkin();
    test_wdg_unregister_removes_from_tracking();

    printf("\n");
    if (g_tests_failed == 0) {
        printf("All %d tests passed. (0 failed)\n", g_tests_run);
    } else {
        printf("%d of %d tests FAILED.\n", g_tests_failed, g_tests_run);
    }
    return (g_tests_failed > 0) ? 1 : 0;
}
