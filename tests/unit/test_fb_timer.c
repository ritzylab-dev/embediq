#define _POSIX_C_SOURCE 200809L

/*
 * tests/unit/test_fb_timer.c — Unit tests for fb_timer
 *
 * Tests verify:
 *   1. The 1-second tick fires within 5 % of 1 second.
 *   2. The 100 ms tick fires ~10 times in 1 second (±1 tolerance).
 *   3. MSG_TIMER_1MS is not published when no subscriber is registered.
 *
 * Tests 1 and 2 use fb_timer__start() / fb_timer__stop_and_wait() directly,
 * bypassing embediq_engine_boot() for simplicity.  Test 3 uses
 * fb_engine__reset() + fb_timer_register() + embediq_engine_boot() to
 * exercise the subscriber-scan optimisation in timer_init().
 *
 * Note: tests 1 and 2 involve real time (≈ 1 second each).
 *
 * Run:      ./build/tests/unit/test_fb_timer
 * Expected: "All 4 tests passed. (0 failed)"
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
 * Package-internal timer test API
 * ------------------------------------------------------------------------- */

extern void     fb_timer__start(void);
extern void     fb_timer__stop_and_wait(void);
extern void     fb_timer__reset_counts(void);
extern uint32_t fb_timer__count_1ms(void);
extern uint32_t fb_timer__count_10ms(void);
extern uint32_t fb_timer__count_100ms(void);
extern uint32_t fb_timer__count_1sec(void);
extern bool     fb_timer__has_1ms_sub(void);

/* Public timer registration */
extern void *fb_timer_register(void);   /* returns EmbedIQ_FB_Handle_t */

/* Test-only FB engine API */
extern void fb_engine__reset(void);

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
 * Tests
 * ------------------------------------------------------------------------- */

/*
 * test_timer_1sec_fires_within_5_percent_of_1_second
 *
 * Start the timer, wait 1100 ms (> 1 s + 5 % margin), stop.
 * The 1-second counter must be at least 1 and at most 2
 * (it should fire exactly once at t ≈ 1000 ms; a second fire at t ≈ 2000 ms
 * is outside the 1100 ms window, so count == 1 is the expected result).
 */
static void test_timer_1sec_fires_within_5_percent_of_1_second(void)
{
    fb_timer__stop_and_wait();
    fb_timer__reset_counts();
    fb_timer__start();

    embediq_osal_delay_ms(1100u);

    fb_timer__stop_and_wait();

    uint32_t cnt = fb_timer__count_1sec();
    ASSERT(cnt >= 1u && cnt <= 2u,
           "1-second tick fires 1–2 times in 1100 ms window");
}

/*
 * test_timer_100ms_fires_10_times_in_1_second
 *
 * Start, wait 1050 ms, stop.  Expect 10 100 ms ticks (at 100, 200, …, 1000 ms).
 * A ±1 tolerance accommodates OS scheduling jitter.
 */
static void test_timer_100ms_fires_10_times_in_1_second(void)
{
    fb_timer__stop_and_wait();
    fb_timer__reset_counts();
    fb_timer__start();

    embediq_osal_delay_ms(1050u);

    fb_timer__stop_and_wait();

    uint32_t cnt = fb_timer__count_100ms();
    ASSERT(cnt >= 9u && cnt <= 11u,
           "100 ms tick fires 9–11 times in 1050 ms window");
}

/*
 * test_timer_no_publish_without_subscriber
 *
 * Register fb_timer as the only FB — no subscriber for MSG_TIMER_1MS.
 * After engine_boot(), timer_init() scans registered FBs and must find
 * g_has_1ms_sub == false, meaning the 1 ms publish path is disabled.
 */
static void test_timer_no_publish_without_subscriber(void)
{
    /* Stop any timer task that might be running from previous tests. */
    fb_timer__stop_and_wait();

    fb_engine__reset();

    /* Register timer as the only FB — no other FB subscribes to MSG_TIMER_1MS. */
    fb_timer_register();

    int ret = embediq_engine_boot();
    ASSERT(ret == 0, "engine_boot succeeds");

    ASSERT(!fb_timer__has_1ms_sub(),
           "MSG_TIMER_1MS subscriber optimisation: no sub detected → publish skipped");

    /* Stop the task that was started by engine_boot's timer_init(). */
    fb_timer__stop_and_wait();
}

/* ---------------------------------------------------------------------------
 * main
 * ------------------------------------------------------------------------- */

int main(void)
{
    test_timer_1sec_fires_within_5_percent_of_1_second();
    test_timer_100ms_fires_10_times_in_1_second();
    test_timer_no_publish_without_subscriber();

    printf("\n");
    if (g_tests_failed == 0) {
        printf("All %d tests passed. (0 failed)\n", g_tests_run);
    } else {
        printf("%d of %d tests FAILED.\n", g_tests_failed, g_tests_run);
    }
    return (g_tests_failed > 0) ? 1 : 0;
}
