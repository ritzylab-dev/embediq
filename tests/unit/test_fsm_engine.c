#define _POSIX_C_SOURCE 200809L

/*
 * tests/unit/test_fsm_engine.c — Unit tests for the FSM engine
 *
 * Fixture: thermostat FSM with three states (NORMAL / WARNING / CRITICAL)
 * and one message type (MSG_TEMP_UPDATE).  Guard functions read a
 * temperature value from the test-local TempMsg_t struct.
 *
 * Message IDs are in the official EmbedIQ component range (0x0400–0x13FF).
 *
 * Run:      ./build/tests/unit/test_fsm_engine
 * Expected: "All 10 tests passed. (0 failed)"
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "embediq_sm.h"
#include "embediq_obs.h"

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>

/* ---------------------------------------------------------------------------
 * Test-only API imported from fb_engine.c (EMBEDIQ_PLATFORM_HOST)
 * ------------------------------------------------------------------------- */

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
 * Thermostat FSM fixture
 *
 * States
 * ------------------------------------------------------------------------- */

#define STATE_NORMAL    0u
#define STATE_WARNING   1u
#define STATE_CRITICAL  2u

/* Message IDs — official EmbedIQ component range (0x0400–0x13FF). */
#define MSG_TEMP_UPDATE 0x0410u
#define MSG_UNKNOWN     0x0411u   /* not subscribed by any row */

/* Test-local temperature message (test code only — not a production struct). */
typedef struct {
    float temperature;
} TempMsg_t;

/* Temperature thresholds used by guard functions. */
#define WARN_THRESHOLD      75.0f
#define CRITICAL_THRESHOLD  90.0f
#define NORMAL_THRESHOLD    70.0f

/* ---------------------------------------------------------------------------
 * Guard functions — PURE: no side effects, no shared-state mutation
 * ------------------------------------------------------------------------- */

static bool is_above_warn(const void *msg, void *fb_data)
{
    (void)fb_data;
    return ((const TempMsg_t *)msg)->temperature > WARN_THRESHOLD;
}

static bool is_above_critical(const void *msg, void *fb_data)
{
    (void)fb_data;
    return ((const TempMsg_t *)msg)->temperature > CRITICAL_THRESHOLD;
}

static bool is_normal(const void *msg, void *fb_data)
{
    (void)fb_data;
    return ((const TempMsg_t *)msg)->temperature < NORMAL_THRESHOLD;
}

/* ---------------------------------------------------------------------------
 * Main thermostat transition table
 * ------------------------------------------------------------------------- */

static const EmbedIQ_SM_Row_t s_thermo_table[] = {
    /* NORMAL  → WARNING   when temp > WARN_THRESHOLD */
    { STATE_NORMAL,   MSG_TEMP_UPDATE, is_above_warn,     NULL, STATE_WARNING  },
    /* WARNING → CRITICAL  when temp > CRITICAL_THRESHOLD */
    { STATE_WARNING,  MSG_TEMP_UPDATE, is_above_critical, NULL, STATE_CRITICAL },
    /* CRITICAL→ NORMAL    when temp < NORMAL_THRESHOLD */
    { STATE_CRITICAL, MSG_TEMP_UPDATE, is_normal,         NULL, STATE_NORMAL   },
    EMBEDIQ_SM_TABLE_END
};

/* ---------------------------------------------------------------------------
 * Action-function fixtures
 * ------------------------------------------------------------------------- */

static bool s_action_called = false;

static void on_transition(const void *msg, void *fb_data)
{
    (void)msg; (void)fb_data;
    s_action_called = true;
}

static const EmbedIQ_SM_Row_t s_action_table[] = {
    { STATE_NORMAL, MSG_TEMP_UPDATE, NULL, on_transition, STATE_WARNING },
    EMBEDIQ_SM_TABLE_END
};

/* ---------------------------------------------------------------------------
 * Null-guard table: NULL guard always matches → unconditional transition
 * ------------------------------------------------------------------------- */

static const EmbedIQ_SM_Row_t s_null_guard_table[] = {
    { STATE_NORMAL, MSG_TEMP_UPDATE, NULL, NULL, STATE_WARNING },
    EMBEDIQ_SM_TABLE_END
};

/* ---------------------------------------------------------------------------
 * Sentinel-termination fixture
 *
 * Layout: [always-false row] [SENTINEL] [probe row]
 * Verifies that the probe guard is NEVER called after the sentinel.
 * ------------------------------------------------------------------------- */

static bool s_sentinel_probe_called = false;

static bool always_false_guard(const void *msg, void *fb_data)
{
    (void)msg; (void)fb_data;
    return false;
}

static bool sentinel_probe_guard(const void *msg, void *fb_data)
{
    (void)msg; (void)fb_data;
    s_sentinel_probe_called = true;
    return true;
}

static const EmbedIQ_SM_Row_t s_sentinel_table[] = {
    { STATE_NORMAL, MSG_TEMP_UPDATE, always_false_guard, NULL, STATE_WARNING },
    EMBEDIQ_SM_TABLE_END,
    /* Row below is AFTER the sentinel and must never be evaluated. */
    { STATE_NORMAL, MSG_TEMP_UPDATE, sentinel_probe_guard, NULL, STATE_WARNING },
};

/* ---------------------------------------------------------------------------
 * Helper: reset obs counter before each test
 * ------------------------------------------------------------------------- */

static void reset_obs(void) { fb_engine__reset(); }

/* ---------------------------------------------------------------------------
 * Tests
 * ------------------------------------------------------------------------- */

static void test_normal_to_warning_when_guard_passes(void)
{
    reset_obs();
    EmbedIQ_SM_t sm = { STATE_NORMAL, s_thermo_table, "thermo" };
    TempMsg_t msg   = { 80.0f };   /* above WARN_THRESHOLD (75) */

    embediq_sm_process(&sm, &msg, MSG_TEMP_UPDATE, NULL);

    ASSERT(sm.current_state == STATE_WARNING,
           "state must advance to WARNING when guard passes");
}

static void test_normal_stays_when_guard_fails(void)
{
    reset_obs();
    EmbedIQ_SM_t sm = { STATE_NORMAL, s_thermo_table, "thermo" };
    TempMsg_t msg   = { 65.0f };   /* below WARN_THRESHOLD (75) — guard fails */

    embediq_sm_process(&sm, &msg, MSG_TEMP_UPDATE, NULL);

    ASSERT(sm.current_state == STATE_NORMAL,
           "state must remain NORMAL when guard fails");
}

static void test_warning_to_critical_on_higher_temp(void)
{
    reset_obs();
    EmbedIQ_SM_t sm = { STATE_WARNING, s_thermo_table, "thermo" };
    TempMsg_t msg   = { 95.0f };   /* above CRITICAL_THRESHOLD (90) */

    embediq_sm_process(&sm, &msg, MSG_TEMP_UPDATE, NULL);

    ASSERT(sm.current_state == STATE_CRITICAL,
           "state must advance to CRITICAL from WARNING on high temp");
}

static void test_critical_to_normal_on_cooldown(void)
{
    reset_obs();
    EmbedIQ_SM_t sm = { STATE_CRITICAL, s_thermo_table, "thermo" };
    TempMsg_t msg   = { 65.0f };   /* below NORMAL_THRESHOLD (70) */

    embediq_sm_process(&sm, &msg, MSG_TEMP_UPDATE, NULL);

    ASSERT(sm.current_state == STATE_NORMAL,
           "state must return to NORMAL from CRITICAL on cooldown");
}

static void test_no_match_stays_in_current_state(void)
{
    reset_obs();
    EmbedIQ_SM_t sm = { STATE_WARNING, s_thermo_table, "thermo" };
    TempMsg_t msg   = { 80.0f };

    /* MSG_UNKNOWN has no rows in the table — no transition should occur. */
    embediq_sm_process(&sm, &msg, MSG_UNKNOWN, NULL);

    ASSERT(sm.current_state == STATE_WARNING,
           "state must not change when no row matches the msg_id");
}

static void test_action_fn_called_on_transition(void)
{
    reset_obs();
    s_action_called  = false;
    EmbedIQ_SM_t sm  = { STATE_NORMAL, s_action_table, "action_test" };
    TempMsg_t msg    = { 0.0f };   /* guard is NULL → always matches */

    embediq_sm_process(&sm, &msg, MSG_TEMP_UPDATE, NULL);

    ASSERT(s_action_called,
           "action_fn must be called when a transition is taken");
}

static void test_null_action_fn_transitions_without_crash(void)
{
    reset_obs();
    /* s_thermo_table rows have NULL action_fn — transition must still occur. */
    EmbedIQ_SM_t sm = { STATE_NORMAL, s_thermo_table, "thermo" };
    TempMsg_t msg   = { 80.0f };   /* guard passes */

    embediq_sm_process(&sm, &msg, MSG_TEMP_UPDATE, NULL);

    ASSERT(sm.current_state == STATE_WARNING,
           "transition must complete even with NULL action_fn");
}

static void test_null_guard_fn_always_matches(void)
{
    reset_obs();
    EmbedIQ_SM_t sm = { STATE_NORMAL, s_null_guard_table, "null_guard" };
    TempMsg_t msg   = { 0.0f };   /* irrelevant — guard is NULL */

    embediq_sm_process(&sm, &msg, MSG_TEMP_UPDATE, NULL);

    ASSERT(sm.current_state == STATE_WARNING,
           "NULL guard must be treated as always-true");
}

static void test_observatory_event_emitted_on_every_transition(void)
{
    reset_obs();
    EmbedIQ_SM_t sm = { STATE_NORMAL, s_thermo_table, "thermo" };
    TempMsg_t msg   = { 80.0f };

    uint32_t before = fb_engine__obs_event_count();
    embediq_sm_process(&sm, &msg, MSG_TEMP_UPDATE, NULL);
    uint32_t after  = fb_engine__obs_event_count();

    ASSERT(after == before + 1u,
           "obs event count must increase by 1 on each transition");
}

static void test_sentinel_terminates_scan(void)
{
    reset_obs();
    s_sentinel_probe_called = false;
    EmbedIQ_SM_t sm = { STATE_NORMAL, s_sentinel_table, "sentinel" };
    TempMsg_t msg   = { 80.0f };

    /* always_false_guard rejects the first row; sentinel stops the scan;
     * the probe row after the sentinel must never be evaluated. */
    embediq_sm_process(&sm, &msg, MSG_TEMP_UPDATE, NULL);

    ASSERT(!s_sentinel_probe_called,
           "sentinel must terminate scan before any row placed after it");
}

/* ---------------------------------------------------------------------------
 * main
 * ------------------------------------------------------------------------- */

int main(void)
{
    test_normal_to_warning_when_guard_passes();
    test_normal_stays_when_guard_fails();
    test_warning_to_critical_on_higher_temp();
    test_critical_to_normal_on_cooldown();
    test_no_match_stays_in_current_state();
    test_action_fn_called_on_transition();
    test_null_action_fn_transitions_without_crash();
    test_null_guard_fn_always_matches();
    test_observatory_event_emitted_on_every_transition();
    test_sentinel_terminates_scan();

    printf("\n");
    if (g_tests_failed == 0) {
        printf("All %d tests passed. (0 failed)\n", g_tests_run);
    } else {
        printf("%d of %d tests FAILED.\n", g_tests_failed, g_tests_run);
    }
    return (g_tests_failed > 0) ? 1 : 0;
}
