#define _POSIX_C_SOURCE 200809L

/*
 * tests/integration/test_thermostat_full.c — Full thermostat integration test
 *
 * Boots all five Functional Blocks of the smart thermostat, then drives the
 * sensor→controller message chain manually at 1-second intervals for 15 s.
 * The remaining three FBs (fb_timer, fb_nvm, fb_watchdog) run their real
 * background tasks throughout, exercising the full Phase 1 platform stack.
 *
 * Why manual delivery?
 *   Phase 1 does not yet include a framework-managed dispatch thread per FB
 *   (that is a Phase 2 deliverable).  The sensor and controller are tested
 *   via fb_engine__deliver_msg() — the same mechanism used by the existing
 *   test_thermostat_observable.c — but now driven by a real-time OSAL task
 *   instead of a synchronous in-test loop.
 *
 * Five FBs under test:
 *   fb_timer          (Phase 1) — runs real 1 ms background tick task
 *   fb_nvm            (Phase 2) — filesystem-backed key-value store
 *   fb_watchdog       (Phase 2) — health-token monitor (100 ms check interval)
 *   fb_temp_sensor    (Phase 3) — simulated temperature sensor
 *   fb_temp_controller(Phase 3) — thermal FSM + watchdog checkin
 *
 * Timing from a cold start at 20 °C (5 °C/tick, 1 tick/s):
 *   tick 12  (≈ 12 s)  temp = 80 °C → NORMAL → WARNING  (FSM transition 1)
 *   tick 14  (≈ 14 s)  temp = 90 °C → WARNING → CRITICAL (FSM transition 2)
 * After 15 s we guarantee ≥ 2 FSM transitions.
 *
 * Assertions:
 *   1. engine_boot_succeeds         — embediq_engine_boot() returns 0
 *   2. all_five_fbs_running         — all handles in RUNNING state after boot
 *   3. temp_readings_received       — ≥ 8 MSG_TEMP_READING processed
 *   4. fsm_transition_occurred      — ≥ 1 FSM_TRANS event in Observatory
 *   5. no_fault_fbs                 — no FB in FAULT state after run
 *   6. sequence_gapless             — Observatory sequences consecutive
 *
 * Note: test runs for ≈ 15 seconds (real-time).  CTest timeout: 60 s.
 *
 * Run:      ./build/tests/integration/test_thermostat_full
 * Expected: "All 6 tests passed. (0 failed)"
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "embediq_fb.h"
#include "embediq_obs.h"
#include "embediq_osal.h"
#include "embediq_platform_msgs.h"
#include "thermostat_msgs.h"

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* ---------------------------------------------------------------------------
 * Package-internal test APIs
 * ------------------------------------------------------------------------- */

extern void     fb_engine__reset(void);
extern void     fb_engine__deliver_msg(void *handle, uint16_t msg_id,
                                       const void *msg);
extern uint32_t obs__ring_count(void);
extern bool     obs__ring_read(uint32_t idx, EmbedIQ_Event_t *out);
extern void     obs__set_level(uint8_t level);

/* Platform FB APIs */
extern void *fb_timer_register(void);
extern void *fb_nvm_register(void);
extern void *fb_watchdog_register(void);
extern void  fb_timer__stop_and_wait(void);
extern void  nvm__set_path(const char *path);

/* Application FB APIs */
extern void *   fb_temp_sensor_register(void);
extern void *   fb_temp_controller_register(void);
extern float    sensor__last_temp(void);
extern uint32_t ctrl__reading_count(void);

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
 * NVM redirect — never touches ~/.embediq in tests
 * ------------------------------------------------------------------------- */

#define TEST_NVM_PATH  "/tmp/test_thermostat_nvm.json"

/* ---------------------------------------------------------------------------
 * Shared handles
 * ------------------------------------------------------------------------- */

static void *g_h_timer  = NULL;
static void *g_h_nvm    = NULL;
static void *g_h_wdg    = NULL;
static void *g_h_sensor = NULL;
static void *g_h_ctrl   = NULL;

/* ---------------------------------------------------------------------------
 * Simulation task
 *
 * Drives the sensor→controller message chain at 1-second intervals,
 * mimicking what a Phase 2 framework task loop would deliver automatically.
 *
 * Each iteration:
 *   1. Deliver MSG_TIMER_1SEC to fb_temp_sensor   → sensor_tick() advances temp
 *   2. Read the new temperature from sensor__last_temp()
 *   3. Deliver MSG_TEMP_READING to fb_temp_controller → ctrl_run() fires FSM +
 *      calls embediq_wdg_checkin() to satisfy the watchdog
 * ------------------------------------------------------------------------- */

static volatile bool g_sim_running = false;

static void sim_run(void *arg)
{
    (void)arg;
    while (g_sim_running) {
        embediq_osal_delay_ms(1000u);
        if (!g_sim_running) break;

        /* Tick the sensor — advances the simulated temperature. */
        fb_engine__deliver_msg(g_h_sensor, MSG_TIMER_1SEC, NULL);

        /* Deliver the resulting temperature reading to the controller. */
        TempReading_t rd;
        rd.temperature_c = sensor__last_temp();
        fb_engine__deliver_msg(g_h_ctrl, MSG_TEMP_READING, &rd);
    }
}

/* ---------------------------------------------------------------------------
 * Tests
 * ------------------------------------------------------------------------- */

/* test_engine_boot_succeeds
 * All 5 FBs must register and boot without dependency errors.
 */
static void test_engine_boot_succeeds(void)
{
    fb_engine__reset();
    obs__set_level(2u);       /* verbose: capture FSM_TRANS events */
    nvm__set_path(TEST_NVM_PATH);

    g_h_timer  = fb_timer_register();
    g_h_nvm    = fb_nvm_register();
    g_h_wdg    = fb_watchdog_register();
    g_h_sensor = fb_temp_sensor_register();
    g_h_ctrl   = fb_temp_controller_register();

    int ret = embediq_engine_boot();
    ASSERT(ret == 0, "embediq_engine_boot() returns 0 for valid 5-FB configuration");
}

/* test_all_five_fbs_running
 * Immediately after boot all five handles must be in RUNNING state.
 */
static void test_all_five_fbs_running(void)
{
    bool all_running =
        (embediq_fb_get_state(g_h_timer)  == EMBEDIQ_FB_STATE_RUNNING) &&
        (embediq_fb_get_state(g_h_nvm)    == EMBEDIQ_FB_STATE_RUNNING) &&
        (embediq_fb_get_state(g_h_wdg)    == EMBEDIQ_FB_STATE_RUNNING) &&
        (embediq_fb_get_state(g_h_sensor) == EMBEDIQ_FB_STATE_RUNNING) &&
        (embediq_fb_get_state(g_h_ctrl)   == EMBEDIQ_FB_STATE_RUNNING);

    ASSERT(all_running, "all 5 FBs are in RUNNING state after embediq_engine_boot()");
}

/* test_temp_readings_received
 * Start the simulation task, run 15 s (≥ 14 ticks), then stop.
 * Controller must have processed ≥ 8 MSG_TEMP_READING messages.
 */
static void test_temp_readings_received(void)
{
    g_sim_running = true;
    embediq_osal_task_create("sim_run", sim_run, NULL, 0u, 4096u);

    printf("  [running 15 s simulation...]\n");
    embediq_osal_delay_ms(15000u);

    g_sim_running = false;
    embediq_osal_delay_ms(200u);   /* allow current sim iteration to finish */

    ASSERT(ctrl__reading_count() >= 8u,
           "controller processed >= 8 MSG_TEMP_READING messages in 15 s");
}

/* test_fsm_transition_occurred
 * At tick 12 temp = 80 °C > 75 °C → NORMAL → WARNING transition.
 * The Observatory must record at least one FSM_TRANS event.
 */
static void test_fsm_transition_occurred(void)
{
    uint32_t count = obs__ring_count();
    bool found = false;
    for (uint32_t i = 0u; i < count && !found; i++) {
        EmbedIQ_Event_t evt = {0};
        obs__ring_read(i, &evt);
        if (evt.event_type == EMBEDIQ_OBS_EVT_FSM_TRANS) {
            found = true;
        }
    }
    ASSERT(found, "observatory ring contains >= 1 FSM_TRANS event after 15 s");
}

/* test_no_fault_fbs
 * No FB must have entered FAULT state during the 15-second run.
 * (Controller calls embediq_wdg_checkin every second; watchdog 5 s timeout.)
 */
static void test_no_fault_fbs(void)
{
    bool no_fault =
        (embediq_fb_get_state(g_h_timer)  != EMBEDIQ_FB_STATE_FAULT) &&
        (embediq_fb_get_state(g_h_nvm)    != EMBEDIQ_FB_STATE_FAULT) &&
        (embediq_fb_get_state(g_h_wdg)    != EMBEDIQ_FB_STATE_FAULT) &&
        (embediq_fb_get_state(g_h_sensor) != EMBEDIQ_FB_STATE_FAULT) &&
        (embediq_fb_get_state(g_h_ctrl)   != EMBEDIQ_FB_STATE_FAULT);

    ASSERT(no_fault, "no FB is in FAULT state after a 15 s run");
}

/* test_sequence_gapless
 * All Observatory events recorded must have consecutive sequence numbers
 * (no ring-buffer overflow, no lost events).
 */
static void test_sequence_gapless(void)
{
    uint32_t count = obs__ring_count();
    bool gapless = (count > 0u);

    for (uint32_t i = 1u; i < count && gapless; i++) {
        EmbedIQ_Event_t prev = {0}, curr = {0};
        obs__ring_read(i - 1u, &prev);
        obs__ring_read(i,      &curr);
        if (curr.sequence != prev.sequence + 1u) {
            gapless = false;
        }
    }

    ASSERT(gapless, "all observatory event sequences are consecutive (no gaps)");
}

/* ---------------------------------------------------------------------------
 * main
 * ------------------------------------------------------------------------- */

int main(void)
{
    /* ---- setup + boot ---- */
    test_engine_boot_succeeds();
    test_all_five_fbs_running();

    /* ---- 15-second simulated run + post-run assertions ---- */
    test_temp_readings_received();   /* contains 15 s sleep */
    test_fsm_transition_occurred();
    test_no_fault_fbs();
    test_sequence_gapless();

    /* Stop the real timer thread cleanly before process exit. */
    fb_timer__stop_and_wait();

    printf("\n");
    if (g_tests_failed == 0) {
        printf("All %d tests passed. (0 failed)\n", g_tests_run);
    } else {
        printf("%d of %d tests FAILED.\n", g_tests_failed, g_tests_run);
    }
    return (g_tests_failed > 0) ? 1 : 0;
}
