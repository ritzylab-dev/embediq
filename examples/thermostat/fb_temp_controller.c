#define _POSIX_C_SOURCE 200809L

/*
 * examples/thermostat/fb_temp_controller.c — Temperature Controller FB
 *
 * Drives the EmbedIQ smart thermostat's FSM based on temperature readings.
 *
 * FSM (AGENTS.md §2D table-driven FSM):
 *   NORMAL   → WARNING   when temperature > 75 °C
 *   WARNING  → CRITICAL  when temperature > 85 °C
 *   CRITICAL → NORMAL    when temperature < 70 °C
 *
 * State-entry actions report an application-defined Observatory event
 * (EMBEDIQ_OBS_EVT_APP) from the controller's own endpoint — never printf
 * (R-stdio). APP codes, msg_id = the reading that caused the entry, in tenths
 * of a degree (0 below 0 °C, 65535 at or above 6553.5 °C):
 *   CTRL_EVT_STATE_NORMAL   (1) — entered NORMAL
 *   CTRL_EVT_STATE_WARNING  (2) — entered WARNING
 *   CTRL_EVT_STATE_CRITICAL (3) — entered CRITICAL
 * The controller calls embediq_wdg_checkin() on every temperature reading,
 * proving liveness to the watchdog (registered with 5 000 ms timeout).
 *
 * Boot phase: EMBEDIQ_BOOT_PHASE_APPLICATION (3)
 *
 * Public API:
 *   fb_temp_controller_register() — register FB with framework
 *
 * Test-only API (EMBEDIQ_TEST_HOOKS):
 *   ctrl__reading_count() — total MSG_TEMP_READING messages processed
 *
 * R-02: no malloc in this file.
 * R-sub-03: no OSAL calls.
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
#include "embediq_config.h"
#include "thermostat_msgs.h"

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#ifdef EMBEDIQ_TEST_HOOKS
#include <stdatomic.h>   /* host-only test counter */
#endif

/* Watchdog service API — platform service, not an OSAL call (R-sub-03). */
extern void embediq_wdg_register(void *fb, uint32_t timeout_ms);
extern void embediq_wdg_checkin(void *fb);

/* ---------------------------------------------------------------------------
 * FSM state constants
 * ------------------------------------------------------------------------- */

#define CTRL_NORMAL    0u
#define CTRL_WARNING   1u
#define CTRL_CRITICAL  2u

/* Watchdog timeout: 5 s is well above the 1 s tick rate, preventing false faults. */
#define CTRL_WDG_TIMEOUT_MS  5000u

/* EMBEDIQ_OBS_EVT_APP codes — see the file header. */
#define CTRL_EVT_STATE_NORMAL    1u
#define CTRL_EVT_STATE_WARNING   2u
#define CTRL_EVT_STATE_CRITICAL  3u

/* ---------------------------------------------------------------------------
 * Module state (static, R-02)
 * ------------------------------------------------------------------------- */

static EmbedIQ_FB_Handle_t g_ctrl_fb = NULL;

/* The controller's endpoint, the source of its APP events. Assigned in
 * ctrl_init: static storage cannot be initialised by a call. */
static uint8_t s_ep = 0xFFu;

#ifdef EMBEDIQ_TEST_HOOKS
/* Incremented in ctrl_run on the FB's thread, read by the test-only
 * ctrl__reading_count() accessor on the test thread: a C11 atomic counter,
 * so the handler needs no lock. The counter and accessor exist only with
 * EMBEDIQ_TEST_HOOKS. */
static _Atomic uint32_t g_reading_count = 0u;
#endif

/* ---------------------------------------------------------------------------
 * Payload access — msg is the MSG_TEMP_READING payload bytes (R-sub-34).
 * ctrl_run rejects NULL before the FSM runs, so guards and actions get a
 * payload; memcpy because the bytes carry no alignment guarantee.
 * ------------------------------------------------------------------------- */

static float reading_c(const void *msg)
{
    TempReading_t reading;
    memcpy(&reading, msg, sizeof(reading));
    return reading.temperature_c;
}

/** The reading in tenths of a degree, saturated to 0..65535. */
static uint16_t reading_tenths(const void *msg)
{
    float t = reading_c(msg) * 10.0f;
    if (!(t > 0.0f))     return 0u;        /* also NaN */
    if (t >= 65535.0f)   return 65535u;
    return (uint16_t)(t + 0.5f);
}

/* ---------------------------------------------------------------------------
 * FSM guard functions
 * ------------------------------------------------------------------------- */

static bool guard_above_75(const void *msg, void *fb_data)
{
    (void)fb_data;
    return reading_c(msg) > 75.0f;
}

static bool guard_above_85(const void *msg, void *fb_data)
{
    (void)fb_data;
    return reading_c(msg) > 85.0f;
}

static bool guard_below_70(const void *msg, void *fb_data)
{
    (void)fb_data;
    return reading_c(msg) < 70.0f;
}

/* ---------------------------------------------------------------------------
 * FSM action functions — report state entries via the Observatory (no printf)
 *
 * LIFECYCLE and FAULT events belong to the engine: an application FB never
 * emits them itself. A real fault is reported with embediq_fb_report_fault().
 * A high temperature is the application's own state, so it is an APP event.
 * ------------------------------------------------------------------------- */

static void action_enter_warning(const void *msg, void *fb_data)
{
    (void)fb_data;
    /* "WARM — monitoring" */
    embediq_obs_emit(EMBEDIQ_OBS_EVT_APP, s_ep, 0xFFu,
                     CTRL_EVT_STATE_WARNING, reading_tenths(msg));
}

static void action_enter_critical(const void *msg, void *fb_data)
{
    (void)fb_data;
    /* "CRITICAL — taking action": an application state, not an FB fault. */
    embediq_obs_emit(EMBEDIQ_OBS_EVT_APP, s_ep, 0xFFu,
                     CTRL_EVT_STATE_CRITICAL, reading_tenths(msg));
}

static void action_enter_normal(const void *msg, void *fb_data)
{
    (void)fb_data;
    /* Recovery to NORMAL */
    embediq_obs_emit(EMBEDIQ_OBS_EVT_APP, s_ep, 0xFFu,
                     CTRL_EVT_STATE_NORMAL, reading_tenths(msg));
}

/* ---------------------------------------------------------------------------
 * FSM transition table
 * ------------------------------------------------------------------------- */

static const EmbedIQ_SM_Row_t s_ctrl_table[] = {
    /* NORMAL → WARNING  when temp > 75 °C */
    { CTRL_NORMAL,   MSG_TEMP_READING, guard_above_75, action_enter_warning,  CTRL_WARNING  },
    /* WARNING → CRITICAL when temp > 85 °C */
    { CTRL_WARNING,  MSG_TEMP_READING, guard_above_85, action_enter_critical, CTRL_CRITICAL },
    /* CRITICAL → NORMAL  when temp < 70 °C */
    { CTRL_CRITICAL, MSG_TEMP_READING, guard_below_70, action_enter_normal,   CTRL_NORMAL   },
    EMBEDIQ_SM_TABLE_END
};

static EmbedIQ_SM_t g_ctrl_sm = { CTRL_NORMAL, s_ctrl_table, "thermo_ctrl" };

/* ---------------------------------------------------------------------------
 * Sub-function: handle MSG_TEMP_READING
 * ------------------------------------------------------------------------- */

static void ctrl_run(EmbedIQ_FB_Handle_t fb, const void *msg,
                     void *fb_data, void *subfn_data)
{
    (void)fb_data;
    EmbedIQ_SM_t *sm = (EmbedIQ_SM_t *)subfn_data;

    if (msg == NULL) return;   /* direct delivery without a payload */
    embediq_sm_process(sm, msg, MSG_TEMP_READING, NULL);

    /* Prove liveness to the watchdog. */
    embediq_wdg_checkin(fb);

#ifdef EMBEDIQ_TEST_HOOKS
    g_reading_count++;
#endif
}

/* ---------------------------------------------------------------------------
 * FB init — register sub-function; register with watchdog
 * ------------------------------------------------------------------------- */

static void ctrl_init(EmbedIQ_FB_Handle_t fb, void *fb_data)
{
    (void)fb_data;
    g_ctrl_fb = fb;
    s_ep = embediq_engine_fb_ep(fb);

    /* Register with watchdog: 5 000 ms timeout; first checkin is in ctrl_run. */
    embediq_wdg_register(fb, CTRL_WDG_TIMEOUT_MS);

    static const uint16_t k_subs[] = { MSG_TEMP_READING };
    static EmbedIQ_SubFn_Config_t k_cfg = {
        .name               = "ctrl_fsm",
        .init_order         = 1u,
        .init_fn            = NULL,
        .run_fn             = ctrl_run,
        .exit_fn            = NULL,
        .subscriptions      = k_subs,
        .subscription_count = 1u,
        .subfn_data         = &g_ctrl_sm,
        .fsm                = NULL,
        .osal_signal        = NULL,
    };
    embediq_subfn_register(fb, &k_cfg);
}

/* ---------------------------------------------------------------------------
 * Public: fb_temp_controller_register()
 * Call before embediq_engine_boot().
 * ------------------------------------------------------------------------- */

EMBEDIQ_SUBS(g_ctrl_subs, MSG_TEMP_READING);

EmbedIQ_FB_Handle_t fb_temp_controller_register(void)
{
    static const EmbedIQ_FB_Config_t k_cfg = {
        .name               = "fb_temp_controller",
        .boot_phase         = EMBEDIQ_BOOT_PHASE_APPLICATION,
        .init_fn            = ctrl_init,
        .subscriptions      = g_ctrl_subs,
        .subscription_count = 1u,
    };
    return embediq_fb_register(&k_cfg);
}

/* ---------------------------------------------------------------------------
 * Test-only API — EMBEDIQ_TEST_HOOKS only
 * ------------------------------------------------------------------------- */

#ifdef EMBEDIQ_TEST_HOOKS

/** Total MSG_TEMP_READING messages processed since last fb_engine__reset(). */
uint32_t ctrl__reading_count(void)
{
    return g_reading_count;
}

#endif /* EMBEDIQ_TEST_HOOKS */
