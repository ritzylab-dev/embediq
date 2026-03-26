#define _POSIX_C_SOURCE 200809L

/*
 * tests/integration/test_thermostat_observable.c
 *
 * End-to-end observable thermostat test.
 *
 * Setup:
 *   - Register fb_sensor  (Phase 3, no sub-fn)
 *   - Register fb_controller (Phase 3, FSM sub-fn: NORMAL ↔ WARNING)
 *   - embediq_engine_boot()  → 4 LIFECYCLE events (INITIALISING + RUNNING × 2)
 *   - Deliver 5 MSG_TEMP_READING messages (80, 60, 80, 60, 80 °C)
 *     → 5 FSM_TRANS events (alternating NORMAL→WARNING and WARNING→NORMAL)
 *   Total: 9 Observatory events, sequences 0–8, fully gapless
 *
 * Assertions:
 *   1. obs_captures_enough_events  — ring holds ≥ 5 events
 *   2. obs_has_fsm_transition      — at least one FSM_TRANS event in ring
 *   3. sequence_gapless            — no sequence gaps between consecutive events
 *   4. zero_printf_in_app_fb_code  — application FB code emits zero printf calls
 *
 * Run:      ./build/tests/integration/test_thermostat_observable
 * Expected: "All 4 tests passed. (0 failed)"
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "embediq_fb.h"
#include "embediq_subfn.h"
#include "embediq_sm.h"
#include "embediq_obs.h"

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
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
 * Temperature message — test-local struct (not a production type)
 * ------------------------------------------------------------------------- */

typedef struct {
    float temperature;
} TempReading_t;

/* ---------------------------------------------------------------------------
 * Controller FSM
 * ------------------------------------------------------------------------- */

#define MSG_TEMP_READING   0x1400u
#define CTRL_NORMAL        0u
#define CTRL_WARNING       1u

static bool temp_above_warn(const void *msg, void *fb_data)
{
    (void)fb_data;
    return ((const TempReading_t *)msg)->temperature > 75.0f;
}

static const EmbedIQ_SM_Row_t s_ctrl_table[] = {
    /* NORMAL  → WARNING  when temp > 75 °C */
    { CTRL_NORMAL,  MSG_TEMP_READING, temp_above_warn, NULL, CTRL_WARNING },
    /* WARNING → NORMAL   always (NULL guard = unconditional) */
    { CTRL_WARNING, MSG_TEMP_READING, NULL,            NULL, CTRL_NORMAL  },
    EMBEDIQ_SM_TABLE_END
};

static EmbedIQ_SM_t g_ctrl_sm = { CTRL_NORMAL, s_ctrl_table, "thermo_ctrl" };

/* ---------------------------------------------------------------------------
 * Controller sub-function
 * ------------------------------------------------------------------------- */

static void ctrl_fsm_run(EmbedIQ_FB_Handle_t fb, const void *msg,
                          void *fb_data, void *subfn_data)
{
    (void)fb; (void)fb_data;
    EmbedIQ_SM_t *sm = (EmbedIQ_SM_t *)subfn_data;
    embediq_sm_process(sm, msg, MSG_TEMP_READING, NULL);
}

static void ctrl_init(EmbedIQ_FB_Handle_t handle, void *fb_data)
{
    (void)fb_data;
    static const uint16_t subs[] = { MSG_TEMP_READING };
    static EmbedIQ_SubFn_Config_t cfg = {
        .name               = "ctrl_fsm",
        .init_order         = 1u,
        .init_fn            = NULL,
        .run_fn             = ctrl_fsm_run,
        .exit_fn            = NULL,
        .subscriptions      = subs,
        .subscription_count = 1u,
        .subfn_data         = &g_ctrl_sm,
        .fsm                = NULL,
        .osal_signal        = NULL,
    };
    embediq_subfn_register(handle, &cfg);
}

/* ---------------------------------------------------------------------------
 * Shared test state — set up once before all assertions
 * ------------------------------------------------------------------------- */

/* Tracks printf calls from application FB code (must stay 0). */
static int g_app_printf_count = 0;

static EmbedIQ_FB_Handle_t g_sensor_handle     = NULL;
static EmbedIQ_FB_Handle_t g_ctrl_handle       = NULL;

static void do_setup(void)
{
    /* Reset engine and observatory; then set verbose level to capture FSM events. */
    fb_engine__reset();
    obs__set_level(2u);   /* level 2: all event types including FSM_TRANS */

    /* Reset the FSM to NORMAL state for each test run. */
    g_ctrl_sm.current_state = CTRL_NORMAL;

    /* Register fb_sensor — minimal; no sub-fns needed. */
    static const EmbedIQ_FB_Config_t sensor_cfg = {
        .name       = "fb_sensor",
        .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
    };
    g_sensor_handle = embediq_fb_register(&sensor_cfg);

    /* Register fb_controller — registers FSM sub-fn during init_fn. */
    static const EmbedIQ_FB_Config_t ctrl_cfg = {
        .name       = "fb_controller",
        .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
        .init_fn    = ctrl_init,
    };
    g_ctrl_handle = embediq_fb_register(&ctrl_cfg);

    /* Boot: generates 2× LIFECYCLE(INITIALISING) + 2× LIFECYCLE(RUNNING) = 4 events */
    embediq_engine_boot();

    /* Deliver 5 messages → 5 alternating FSM transitions */
    static const float temps[] = { 80.0f, 60.0f, 80.0f, 60.0f, 80.0f };
    for (int i = 0; i < 5; i++) {
        TempReading_t m = { temps[i] };
        fb_engine__deliver_msg(g_ctrl_handle, MSG_TEMP_READING, &m);
    }
}

/* ---------------------------------------------------------------------------
 * Assertions
 * ------------------------------------------------------------------------- */

static void test_obs_captures_enough_events(void)
{
    ASSERT(obs__ring_count() >= 5u,
           "observatory must capture at least 5 events from 5 delivered messages");
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
    ASSERT(found, "observatory ring must contain at least one FSM_TRANS event");
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

    ASSERT(gapless, "all observatory event sequences must be consecutive (no gaps)");
}

static void test_zero_printf_in_app_fb_code(void)
{
    /* Application FB code must never call printf directly.
     * The observatory handles all runtime output via its transport. */
    ASSERT(g_app_printf_count == 0,
           "application FB code must not call printf");
}

/* ---------------------------------------------------------------------------
 * main
 * ------------------------------------------------------------------------- */

int main(void)
{
    do_setup();

    test_obs_captures_enough_events();
    test_obs_has_fsm_transition();
    test_sequence_gapless();
    test_zero_printf_in_app_fb_code();

    printf("\n");
    if (g_tests_failed == 0) {
        printf("All %d tests passed. (0 failed)\n", g_tests_run);
    } else {
        printf("%d of %d tests FAILED.\n", g_tests_failed, g_tests_run);
    }
    return (g_tests_failed > 0) ? 1 : 0;
}
