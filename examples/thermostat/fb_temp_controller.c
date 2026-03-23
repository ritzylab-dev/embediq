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
 * State-entry actions use embediq_obs_emit() — never printf (R-sub-04).
 * The controller calls embediq_wdg_checkin() on every temperature reading,
 * proving liveness to the watchdog (registered with 5 000 ms timeout).
 *
 * Boot phase: EMBEDIQ_BOOT_PHASE_APPLICATION (3)
 *
 * Public API:
 *   fb_temp_controller_register() — register FB with framework
 *
 * Host/test-only API (EMBEDIQ_PLATFORM_HOST):
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

/* ---------------------------------------------------------------------------
 * Module state (static, R-02)
 * ------------------------------------------------------------------------- */

static EmbedIQ_FB_Handle_t g_ctrl_fb = NULL;

#ifdef EMBEDIQ_PLATFORM_HOST
static uint32_t g_reading_count = 0u;
#endif

/* ---------------------------------------------------------------------------
 * FSM guard functions
 * ------------------------------------------------------------------------- */

static bool guard_above_75(const void *msg, void *fb_data)
{
    (void)fb_data;
    return ((const TempReading_t *)msg)->temperature_c > 75.0f;
}

static bool guard_above_85(const void *msg, void *fb_data)
{
    (void)fb_data;
    return ((const TempReading_t *)msg)->temperature_c > 85.0f;
}

static bool guard_below_70(const void *msg, void *fb_data)
{
    (void)fb_data;
    return ((const TempReading_t *)msg)->temperature_c < 70.0f;
}

/* ---------------------------------------------------------------------------
 * FSM action functions — log state transitions via Observatory (no printf)
 * ------------------------------------------------------------------------- */

static void action_enter_warning(const void *msg, void *fb_data)
{
    (void)msg; (void)fb_data;
    /* "WARM — monitoring": emit lifecycle event for the WARNING entry. */
    embediq_obs_emit(EMBEDIQ_OBS_EVT_LIFECYCLE, 0u, 0xFFu,
                     CTRL_WARNING, MSG_TEMP_READING);
}

static void action_enter_critical(const void *msg, void *fb_data)
{
    (void)msg; (void)fb_data;
    /* "CRITICAL — taking action": emit a fault-category event. */
    embediq_obs_emit(EMBEDIQ_OBS_EVT_FAULT, 0u, 0xFFu,
                     CTRL_CRITICAL, MSG_TEMP_READING);
}

static void action_enter_normal(const void *msg, void *fb_data)
{
    (void)msg; (void)fb_data;
    /* Recovery to NORMAL: emit lifecycle event. */
    embediq_obs_emit(EMBEDIQ_OBS_EVT_LIFECYCLE, 0u, 0xFFu,
                     CTRL_NORMAL, MSG_TEMP_READING);
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

    embediq_sm_process(sm, msg, MSG_TEMP_READING, NULL);

    /* Prove liveness to the watchdog. */
    embediq_wdg_checkin(fb);

#ifdef EMBEDIQ_PLATFORM_HOST
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
 * Test-only API — EMBEDIQ_PLATFORM_HOST only
 * ------------------------------------------------------------------------- */

#ifdef EMBEDIQ_PLATFORM_HOST

/** Total MSG_TEMP_READING messages processed since last fb_engine__reset(). */
uint32_t ctrl__reading_count(void)
{
    return g_reading_count;
}

#endif /* EMBEDIQ_PLATFORM_HOST */
