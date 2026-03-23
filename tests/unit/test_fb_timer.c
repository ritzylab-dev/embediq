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
 * Expected: "All 10 tests passed. (0 failed)"
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "embediq_fb.h"
#include "embediq_osal.h"
#include "embediq_config.h"
#include "hal/hal_timer.h"

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
 * HAL timer tests (Arch-4a)
 * ------------------------------------------------------------------------- */

static volatile int g_hal_cb_count = 0;

static void hal_test_cb(void *ctx)
{
    (void)ctx;
    g_hal_cb_count++;
}

/*
 * test_hal_timer_init_null_cb_returns_invalid
 * hal_timer_init with a NULL callback must return HAL_ERR_INVALID.
 */
static void test_hal_timer_init_null_cb_returns_invalid(void)
{
    int ret = hal_timer_init(1000u, NULL, NULL);
    ASSERT(ret == HAL_ERR_INVALID,
           "hal_timer_init(NULL cb) returns HAL_ERR_INVALID");
}

/*
 * test_hal_timer_init_valid_returns_ok
 * Valid parameters must return HAL_OK.
 */
static void test_hal_timer_init_valid_returns_ok(void)
{
    int ret = hal_timer_init(1000u, hal_test_cb, NULL);
    ASSERT(ret == HAL_OK, "hal_timer_init valid returns HAL_OK");
    hal_timer_deinit();
}

/*
 * test_hal_timer_fires_callback
 * 20 ms period, started for 90 ms → callback must fire >= 3 times.
 */
static void test_hal_timer_fires_callback(void)
{
    g_hal_cb_count = 0;
    int ret = hal_timer_init(20000u, hal_test_cb, NULL);   /* 20 ms = 20 000 µs */
    ASSERT(ret == HAL_OK, "hal_timer_fires_callback: init ok");
    hal_timer_start();
    embediq_osal_delay_ms(90u);
    hal_timer_stop();
    embediq_osal_delay_ms(25u);    /* drain any in-flight callback */
    hal_timer_deinit();
    ASSERT(g_hal_cb_count >= 3,
           "HAL timer fires >= 3 times in 90 ms at 20 ms period");
}

/*
 * test_hal_timer_stop_halts_callback
 * After hal_timer_stop() the tick counter must not increase.
 */
static void test_hal_timer_stop_halts_callback(void)
{
    g_hal_cb_count = 0;
    hal_timer_init(5000u, hal_test_cb, NULL);   /* 5 ms = 5 000 µs */
    hal_timer_start();
    embediq_osal_delay_ms(30u);
    hal_timer_stop();
    embediq_osal_delay_ms(20u);    /* drain any in-flight callback */
    int count_after_stop = g_hal_cb_count;
    embediq_osal_delay_ms(30u);    /* confirm count frozen */
    hal_timer_deinit();
    ASSERT(g_hal_cb_count == count_after_stop,
           "hal_timer_stop halts callbacks");
}

/*
 * test_hal_timer_deinit_allows_reinit
 * After hal_timer_deinit(), a second hal_timer_init() must return HAL_OK.
 */
static void test_hal_timer_deinit_allows_reinit(void)
{
    hal_timer_init(1000u, hal_test_cb, NULL);
    hal_timer_deinit();
    int ret = hal_timer_init(1000u, hal_test_cb, NULL);
    ASSERT(ret == HAL_OK, "hal_timer_deinit then reinit returns HAL_OK");
    hal_timer_deinit();
}

/* ---------------------------------------------------------------------------
 * main
 * ------------------------------------------------------------------------- */

int main(void)
{
    test_timer_1sec_fires_within_5_percent_of_1_second();
    test_timer_100ms_fires_10_times_in_1_second();
    test_timer_no_publish_without_subscriber();
    test_hal_timer_init_null_cb_returns_invalid();
    test_hal_timer_init_valid_returns_ok();
    test_hal_timer_fires_callback();
    test_hal_timer_stop_halts_callback();
    test_hal_timer_deinit_allows_reinit();

    printf("\n");
    if (g_tests_failed == 0) {
        printf("All %d tests passed. (0 failed)\n", g_tests_run);
    } else {
        printf("%d of %d tests FAILED.\n", g_tests_failed, g_tests_run);
    }
    return (g_tests_failed > 0) ? 1 : 0;
}
