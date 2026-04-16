/*
 * tests/unit/test_fb_telemetry.c — Unit tests for fb_telemetry Service FB
 *
 * Verifies the in-window aggregation invariants for gauge, counter, and
 * histogram metric types; table-full degradation; and window isolation
 * after a forced flush. Tests exercise the package-internal test API
 * (fb_telemetry__*) under EMBEDIQ_PLATFORM_HOST only — no dispatch
 * threads, no bus machinery.
 *
 * Run:      ./build/tests/unit/test_fb_telemetry
 * Expected: "All N tests passed. (0 failed)"
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "embediq_config.h"
#include "embediq_telemetry.h"

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

/* ---------------------------------------------------------------------------
 * Package-internal test API (implemented in fbs/services/fb_telemetry.c
 * under EMBEDIQ_PLATFORM_HOST). See that file for semantics.
 * ------------------------------------------------------------------------- */

typedef struct {
    uint16_t metric_id;
    uint8_t  type;           /* 0=GAUGE 1=COUNTER 2=HISTOGRAM */
    uint8_t  unit_id;
    uint32_t count;
    float    sum;
    float    min;
    float    max;
    float    last;
    uint32_t counter_total;
} fb_telemetry_entry_t;

extern void    fb_telemetry__reset(void);
extern void    fb_telemetry__agg_gauge(uint16_t metric_id, float value, uint8_t unit_id);
extern void    fb_telemetry__agg_counter(uint16_t metric_id, uint32_t delta, uint8_t unit_id);
extern void    fb_telemetry__agg_histogram(uint16_t metric_id, float observation, uint8_t unit_id);
extern bool    fb_telemetry__get_entry(uint16_t metric_id, fb_telemetry_entry_t *out);
extern uint8_t fb_telemetry__active_count(void);
extern void    fb_telemetry__force_flush(uint8_t fault_triggered);

/* ---------------------------------------------------------------------------
 * Minimal test harness (pattern follows test_fb_watchdog.c)
 * ------------------------------------------------------------------------- */

static int g_tests_run    = 0;
static int g_tests_failed = 0;

#define ASSERT(cond, msg)  do {                                                 \
    g_tests_run++;                                                              \
    if (!(cond)) {                                                              \
        fprintf(stderr, "FAIL  %-56s  %s:%d  %s\n",                             \
                __func__, __FILE__, __LINE__, (msg));                           \
        g_tests_failed++;                                                       \
    } else {                                                                    \
        printf("PASS  %s  %s\n", __func__, (msg));                              \
    }                                                                           \
} while (0)

#define APPROX_EQ(a, b, eps)  (fabsf((a) - (b)) <= (eps))

/* ---------------------------------------------------------------------------
 * TEST 1 — gauge accumulates correctly (count, min, max, avg, last)
 * ------------------------------------------------------------------------- */
static void test_gauge_accumulates(void)
{
    fb_telemetry__reset();

    fb_telemetry__agg_gauge(1u, 10.0f, EMBEDIQ_UNIT_CELSIUS);
    fb_telemetry__agg_gauge(1u, 20.0f, EMBEDIQ_UNIT_CELSIUS);
    fb_telemetry__agg_gauge(1u, 30.0f, EMBEDIQ_UNIT_CELSIUS);

    fb_telemetry__force_flush(0u);  /* semantic parity with a real batch tick */

    /* NB: force_flush publishes the batch and resets the table, so inspect
     * the entry BEFORE flush OR re-aggregate. We re-run without flushing
     * to verify pre-flush accumulator state. */
    fb_telemetry__reset();
    fb_telemetry__agg_gauge(1u, 10.0f, EMBEDIQ_UNIT_CELSIUS);
    fb_telemetry__agg_gauge(1u, 20.0f, EMBEDIQ_UNIT_CELSIUS);
    fb_telemetry__agg_gauge(1u, 30.0f, EMBEDIQ_UNIT_CELSIUS);

    fb_telemetry_entry_t e;
    ASSERT(fb_telemetry__get_entry(1u, &e), "entry for metric_id=1 must exist");
    ASSERT(e.count == 3u,                              "count == 3");
    ASSERT(APPROX_EQ(e.min,  10.0f, 0.001f),           "min  == 10.0");
    ASSERT(APPROX_EQ(e.max,  30.0f, 0.001f),           "max  == 30.0");
    ASSERT(APPROX_EQ(e.sum / (float)e.count, 20.0f, 0.001f), "avg  ≈ 20.0");
    ASSERT(APPROX_EQ(e.last, 30.0f, 0.001f),           "last == 30.0");
    ASSERT(e.unit_id == EMBEDIQ_UNIT_CELSIUS,          "unit_id preserved");
}

/* ---------------------------------------------------------------------------
 * TEST 2 — two different metric_ids produce two separate batch entries
 * ------------------------------------------------------------------------- */
static void test_two_metric_ids(void)
{
    fb_telemetry__reset();

    fb_telemetry__agg_gauge(1u,  5.0f, EMBEDIQ_UNIT_CELSIUS);
    fb_telemetry__agg_gauge(2u, 99.0f, EMBEDIQ_UNIT_PERCENT);

    ASSERT(fb_telemetry__active_count() == 2u,
           "active_count == 2 (would become batch entry_count)");

    fb_telemetry_entry_t e1, e2;
    ASSERT(fb_telemetry__get_entry(1u, &e1), "metric_id=1 present");
    ASSERT(fb_telemetry__get_entry(2u, &e2), "metric_id=2 present");
    ASSERT(APPROX_EQ(e1.last,  5.0f, 0.001f), "metric_id=1 last == 5.0");
    ASSERT(APPROX_EQ(e2.last, 99.0f, 0.001f), "metric_id=2 last == 99.0");
    ASSERT(e1.unit_id == EMBEDIQ_UNIT_CELSIUS, "metric_id=1 unit preserved");
    ASSERT(e2.unit_id == EMBEDIQ_UNIT_PERCENT, "metric_id=2 unit preserved");
}

/* ---------------------------------------------------------------------------
 * TEST 3 — counter accumulates delta correctly
 * ------------------------------------------------------------------------- */
static void test_counter_accumulates(void)
{
    fb_telemetry__reset();

    fb_telemetry__agg_counter(10u, 3u, EMBEDIQ_UNIT_COUNT);
    fb_telemetry__agg_counter(10u, 7u, EMBEDIQ_UNIT_COUNT);

    fb_telemetry_entry_t e;
    ASSERT(fb_telemetry__get_entry(10u, &e), "entry for metric_id=10 must exist");
    ASSERT(e.counter_total == 10u, "counter_total == 10 (3 + 7)");
    ASSERT(e.count == 2u,          "count == 2 (two reports)");
    ASSERT(e.unit_id == EMBEDIQ_UNIT_COUNT, "unit_id preserved");
}

/* ---------------------------------------------------------------------------
 * TEST 4 — histogram records min/max/mean
 * ------------------------------------------------------------------------- */
static void test_histogram_distribution(void)
{
    fb_telemetry__reset();

    fb_telemetry__agg_histogram(20u, 100.0f, EMBEDIQ_UNIT_MS);
    fb_telemetry__agg_histogram(20u, 200.0f, EMBEDIQ_UNIT_MS);
    fb_telemetry__agg_histogram(20u, 300.0f, EMBEDIQ_UNIT_MS);

    fb_telemetry_entry_t e;
    ASSERT(fb_telemetry__get_entry(20u, &e), "entry for metric_id=20 must exist");
    ASSERT(e.count == 3u,                              "count == 3");
    ASSERT(APPROX_EQ(e.min, 100.0f, 0.001f),           "min  == 100.0");
    ASSERT(APPROX_EQ(e.max, 300.0f, 0.001f),           "max  == 300.0");
    ASSERT(APPROX_EQ(e.sum / (float)e.count, 200.0f, 0.001f),
                                                       "mean ≈ 200.0");
    ASSERT(e.unit_id == EMBEDIQ_UNIT_MS,               "unit_id preserved");
}

/* ---------------------------------------------------------------------------
 * TEST 5 — table full drops new metric, does not crash
 * ------------------------------------------------------------------------- */
static void test_table_full_drops(void)
{
    fb_telemetry__reset();

    /* Fill the table exactly. */
    for (uint32_t i = 0u; i < EMBEDIQ_TELEMETRY_MAX_METRICS; i++) {
        fb_telemetry__agg_gauge((uint16_t)(100u + i), 1.0f,
                                EMBEDIQ_UNIT_COUNT);
    }
    ASSERT(fb_telemetry__active_count() == EMBEDIQ_TELEMETRY_MAX_METRICS,
           "table full at MAX_METRICS");

    /* Publish one more with a new metric_id — must drop silently. */
    uint16_t overflow_id = (uint16_t)(100u + EMBEDIQ_TELEMETRY_MAX_METRICS);
    fb_telemetry__agg_gauge(overflow_id, 999.0f, EMBEDIQ_UNIT_COUNT);

    ASSERT(fb_telemetry__active_count() == EMBEDIQ_TELEMETRY_MAX_METRICS,
           "active_count still MAX_METRICS — overflow dropped");

    fb_telemetry_entry_t e;
    ASSERT(!fb_telemetry__get_entry(overflow_id, &e),
           "overflow metric_id has no entry");
}

/* ---------------------------------------------------------------------------
 * TEST 6 — window reset clears accumulators (no cross-window contamination)
 * ------------------------------------------------------------------------- */
static void test_window_reset_isolation(void)
{
    fb_telemetry__reset();

    /* First window */
    fb_telemetry__agg_gauge(1u, 50.0f, EMBEDIQ_UNIT_PERCENT);
    fb_telemetry__force_flush(0u);   /* publishes batch + resets table */

    ASSERT(fb_telemetry__active_count() == 0u,
           "after flush, active_count == 0");

    /* Second window — clean start */
    fb_telemetry__agg_gauge(1u, 10.0f, EMBEDIQ_UNIT_PERCENT);

    fb_telemetry_entry_t e;
    ASSERT(fb_telemetry__get_entry(1u, &e),
           "metric_id=1 present in second window");
    ASSERT(e.count == 1u,
           "count == 1 (second window, no contamination from first)");
    ASSERT(APPROX_EQ(e.last, 10.0f, 0.001f),
           "last == 10.0 (no contamination from first window's 50.0)");
    ASSERT(APPROX_EQ(e.sum / (float)e.count, 10.0f, 0.001f),
           "avg == 10.0 (fresh accumulator)");
}

/* ---------------------------------------------------------------------------
 * Entry point
 * ------------------------------------------------------------------------- */

int main(void)
{
    test_gauge_accumulates();
    test_two_metric_ids();
    test_counter_accumulates();
    test_histogram_distribution();
    test_table_full_drops();
    test_window_reset_isolation();

    printf("\n");
    if (g_tests_failed == 0) {
        printf("All %d tests passed. (0 failed)\n", g_tests_run);
    } else {
        printf("%d of %d tests FAILED.\n", g_tests_failed, g_tests_run);
    }
    return (g_tests_failed > 0) ? 1 : 0;
}
