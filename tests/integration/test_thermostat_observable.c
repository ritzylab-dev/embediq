#define _POSIX_C_SOURCE 200809L

/*
 * tests/integration/test_thermostat_observable.c
 *
 * Observable thermostat test, driving the production thermostat FBs.
 *
 * Setup:
 *   - Register the thermostat FB set in main.c order: fb_timer, fb_nvm,
 *     fb_watchdog, fb_temp_sensor, fb_temp_controller. fb_timer is registered
 *     first, so it holds endpoint 0.
 *   - embediq_engine_boot() (no dispatch threads are started, so the only
 *     controller activity is what this test delivers).
 *   - Take a baseline sequence after boot: the boot LIFECYCLE events are the
 *     engine's and stay valid.
 *   - Deliver MSG_TEMP_READING to the controller with fb_engine__deliver_msg()
 *     and a TempReading_t (the message's payload layout): 80, 90, 60 °C, which
 *     enter WARNING, CRITICAL and NORMAL in turn; then deliver NULL.
 *
 * Assertions:
 *   1. state_entry_reported_by_controller — each delivery adds exactly one APP
 *      event, from the controller's endpoint, with the state code and the
 *      reading in tenths of a degree
 *   2. no_engine_events_forged — no LIFECYCLE or FAULT event attributed to
 *      fb_timer is added after the baseline
 *   3. null_payload_is_ignored — a NULL delivery adds no event and does not crash
 *   4. obs_has_fsm_transition — at least one FSM_TRANS event in the ring
 *   5. sequence_gapless — no sequence gaps between consecutive events
 *
 * Run:      ./build/tests/integration/test_thermostat_observable
 * Expected: "All N tests passed. (0 failed)"
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "embediq_fb.h"
#include "embediq_obs.h"
#include "thermostat_msgs.h"

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>     /* setenv */
#include <string.h>

/* ---------------------------------------------------------------------------
 * Package-internal API
 * ------------------------------------------------------------------------- */

extern void     fb_engine__reset(void);
extern void     fb_engine__deliver_msg(EmbedIQ_FB_Handle_t handle,
                                       uint16_t msg_id, const void *msg);
extern uint32_t obs__ring_count(void);
extern bool     obs__ring_read(uint32_t idx, EmbedIQ_Event_t *out);
extern void     obs__set_level(uint8_t level);

/* Platform FB APIs */
extern EmbedIQ_FB_Handle_t fb_timer_register(void);
extern EmbedIQ_FB_Handle_t fb_nvm_register(void);
extern EmbedIQ_FB_Handle_t fb_watchdog_register(void);
extern void                fb_timer__stop_and_wait(void);

/* Application FB APIs */
extern EmbedIQ_FB_Handle_t fb_temp_sensor_register(void);
extern EmbedIQ_FB_Handle_t fb_temp_controller_register(void);

/* The controller's APP codes, as documented in fb_temp_controller.c. */
#define CTRL_EVT_STATE_NORMAL    1u
#define CTRL_EVT_STATE_WARNING   2u
#define CTRL_EVT_STATE_CRITICAL  3u

#define TEST_NVM_PATH  "/tmp/test_thermostat_observable_nvm.bin"

/* ---------------------------------------------------------------------------
 * Minimal test harness
 * ------------------------------------------------------------------------- */

static int g_tests_run    = 0;
static int g_tests_failed = 0;

#define ASSERT(cond, msg)  do {                                                \
    g_tests_run++;                                                             \
    if (!(cond)) {                                                             \
        fprintf(stderr, "FAIL  %-40s  %s:%d  %s\n",                            \
                __func__, __FILE__, __LINE__, (msg));                          \
        g_tests_failed++;                                                      \
    } else {                                                                   \
        printf("PASS  %s  %s\n", __func__, (msg));                             \
    }                                                                          \
} while (0)

/* ---------------------------------------------------------------------------
 * Observatory scan helpers — events are selected by sequence, not ring index
 * ------------------------------------------------------------------------- */

static uint8_t             g_timer_ep = 0xFFu;
static uint8_t             g_ctrl_ep  = 0xFFu;
static EmbedIQ_FB_Handle_t g_h_ctrl   = NULL;

/** Sequence the next event will carry (one past the newest in the ring). */
static uint32_t next_seq(void)
{
    uint32_t n = obs__ring_count();
    EmbedIQ_Event_t e = {0};
    if (n == 0u || !obs__ring_read(n - 1u, &e)) return 0u;
    return e.sequence + 1u;
}

typedef struct {
    uint32_t        from_ctrl;      /* any event with the controller's endpoint */
    uint32_t        app_from_ctrl;  /* APP events with the controller's endpoint */
    uint32_t        forged;         /* LIFECYCLE/FAULT attributed to fb_timer */
    EmbedIQ_Event_t last_app;
} scan_t;

static scan_t scan_since(uint32_t from_seq)
{
    scan_t r;
    memset(&r, 0, sizeof(r));
    uint32_t n = obs__ring_count();
    for (uint32_t i = 0u; i < n; i++) {
        EmbedIQ_Event_t e = {0};
        if (!obs__ring_read(i, &e) || e.sequence < from_seq) continue;
        if (e.source_fb_id == g_ctrl_ep) {
            r.from_ctrl++;
            if (e.event_type == EMBEDIQ_OBS_EVT_APP) {
                r.app_from_ctrl++;
                r.last_app = e;
            }
        }
        if (e.source_fb_id == g_timer_ep &&
            (e.event_type == EMBEDIQ_OBS_EVT_LIFECYCLE ||
             e.event_type == EMBEDIQ_OBS_EVT_FAULT)) {
            r.forged++;
        }
    }
    return r;
}

/* ---------------------------------------------------------------------------
 * Assertions
 * ------------------------------------------------------------------------- */

static void deliver_reading(float temperature_c, uint8_t want_code,
                            uint16_t want_tenths)
{
    TempReading_t reading = { .temperature_c = temperature_c };
    uint32_t before = next_seq();
    fb_engine__deliver_msg(g_h_ctrl, MSG_TEMP_READING, &reading);
    scan_t s = scan_since(before);

    ASSERT(s.app_from_ctrl == 1u, "exactly one APP event from the controller");
    ASSERT(s.last_app.state_or_flag == want_code, "APP code is the state entered");
    ASSERT(s.last_app.msg_id == want_tenths, "APP value is the reading in tenths");
}

static void test_state_entry_reported_by_controller(void)
{
    deliver_reading(80.0f, CTRL_EVT_STATE_WARNING,  800u);
    deliver_reading(90.0f, CTRL_EVT_STATE_CRITICAL, 900u);
    deliver_reading(60.0f, CTRL_EVT_STATE_NORMAL,   600u);
}

static void test_no_engine_events_forged(uint32_t baseline)
{
    scan_t s = scan_since(baseline);
    ASSERT(s.forged == 0u, "no LIFECYCLE or FAULT attributed to fb_timer after boot");
}

static void test_null_payload_is_ignored(void)
{
    uint32_t before = next_seq();
    fb_engine__deliver_msg(g_h_ctrl, MSG_TEMP_READING, NULL);
    scan_t s = scan_since(before);
    ASSERT(s.from_ctrl == 0u && s.forged == 0u, "a NULL payload adds no event");
}

static void test_obs_has_fsm_transition(void)
{
    uint32_t count = obs__ring_count();
    bool found = false;
    for (uint32_t i = 0; i < count; i++) {
        EmbedIQ_Event_t evt = {0};
        obs__ring_read(i, &evt);
        if (evt.event_type == EMBEDIQ_OBS_EVT_FSM_TRANS) {
            found = true;
            break;
        }
    }
    ASSERT(found, "the ring holds at least one FSM_TRANS event");
}

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

    ASSERT(gapless, "all event sequences are consecutive");
}

/* ---------------------------------------------------------------------------
 * main
 * ------------------------------------------------------------------------- */

int main(void)
{
    fb_engine__reset();
    obs__set_level(2u);   /* level 2: lifecycle, FSM and application events */
    setenv("EMBEDIQ_NVM_PATH", TEST_NVM_PATH, 1);

    EmbedIQ_FB_Handle_t h_timer = fb_timer_register();
    (void)fb_nvm_register();
    (void)fb_watchdog_register();
    (void)fb_temp_sensor_register();
    g_h_ctrl = fb_temp_controller_register();

    if (h_timer == NULL || g_h_ctrl == NULL || embediq_engine_boot() != 0) {
        fprintf(stderr, "FAIL  setup: the thermostat FB set did not boot\n");
        return 1;
    }
    g_timer_ep = embediq_engine_fb_ep(h_timer);
    g_ctrl_ep  = embediq_engine_fb_ep(g_h_ctrl);

    uint32_t baseline = next_seq();

    test_state_entry_reported_by_controller();
    test_no_engine_events_forged(baseline);
    test_null_payload_is_ignored();
    test_obs_has_fsm_transition();
    test_sequence_gapless();

    fb_timer__stop_and_wait();

    printf("\n");
    if (g_tests_failed == 0) {
        printf("All %d tests passed. (0 failed)\n", g_tests_run);
    } else {
        printf("%d of %d tests FAILED.\n", g_tests_failed, g_tests_run);
    }
    return (g_tests_failed > 0) ? 1 : 0;
}
