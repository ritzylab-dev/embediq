#define _POSIX_C_SOURCE 200809L

/*
 * examples/thermostat/fb_temp_sensor.c — Temperature Sensor FB
 *
 * Simulates a temperature sensor for the EmbedIQ smart thermostat demo.
 *
 * Behaviour:
 *   Subscribes to MSG_TIMER_1SEC.  On each tick the simulated temperature
 *   is advanced and MSG_TEMP_READING is published.
 *
 *   Temperature oscillation (forcing the controller FSM through all states):
 *     20 °C → +5 °C/tick → 90 °C → -5 °C/tick → 20 °C → repeat
 *
 *   Timing from a cold start at 20 °C:
 *     tick 12  (≈ 12 s) temp = 80 °C → controller NORMAL → WARNING
 *     tick 14  (≈ 14 s) temp = 90 °C → controller WARNING → CRITICAL
 *     tick 24  (≈ 24 s) temp = 60 °C → controller CRITICAL → NORMAL
 *
 * Boot phase: EMBEDIQ_BOOT_PHASE_APPLICATION (3)
 *
 * Public API:
 *   fb_temp_sensor_register() — register FB with framework
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
#include "embediq_bus.h"
#include "embediq_msg.h"
#include "embediq_config.h"
#include "embediq_platform_msgs.h"
#include "thermostat_msgs.h"

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* ---------------------------------------------------------------------------
 * Module state (static, R-02)
 * ------------------------------------------------------------------------- */

static float g_temp = 20.0f;   /* current simulated temperature, °C */
static float g_step =  5.0f;   /* °C/tick (+5 rising, −5 falling)   */

/* ---------------------------------------------------------------------------
 * Sub-function: handle MSG_TIMER_1SEC tick
 * ------------------------------------------------------------------------- */

static void sensor_tick(EmbedIQ_FB_Handle_t fb, const void *msg,
                        void *fb_data, void *subfn_data)
{
    (void)msg; (void)fb_data; (void)subfn_data;

    /* Advance temperature oscillation. */
    g_temp += g_step;
    if (g_temp >= 90.0f) { g_step = -5.0f; }
    if (g_temp <= 20.0f) { g_step =  5.0f; }

    /* Publish MSG_TEMP_READING. */
    EmbedIQ_Msg_t m;
    memset(&m, 0, sizeof(m));
    m.msg_id   = MSG_TEMP_READING;
    m.priority = EMBEDIQ_MSG_PRIORITY_NORMAL;

    TempReading_t reading;
    reading.temperature_c = g_temp;
    memcpy(m.payload, &reading, sizeof(reading));
    m.payload_len = (uint16_t)sizeof(reading);

    embediq_publish(fb, &m);
}

/* ---------------------------------------------------------------------------
 * FB init — register sub-function for MSG_TIMER_1SEC
 * ------------------------------------------------------------------------- */

static void sensor_init(EmbedIQ_FB_Handle_t fb, void *fb_data)
{
    (void)fb_data;

    static const uint16_t k_subs[] = { MSG_TIMER_1SEC };
    static EmbedIQ_SubFn_Config_t k_cfg = {
        .name               = "sensor_tick",
        .init_order         = 1u,
        .init_fn            = NULL,
        .run_fn             = sensor_tick,
        .exit_fn            = NULL,
        .subscriptions      = k_subs,
        .subscription_count = 1u,
        .subfn_data         = NULL,
        .fsm                = NULL,
        .osal_signal        = NULL,
    };
    embediq_subfn_register(fb, &k_cfg);
}

/* ---------------------------------------------------------------------------
 * Test-only API — EMBEDIQ_PLATFORM_HOST only
 * ------------------------------------------------------------------------- */

#ifdef EMBEDIQ_PLATFORM_HOST

/** Return the last simulated temperature (°C) computed by sensor_tick. */
float sensor__last_temp(void)
{
    return g_temp;
}

#endif /* EMBEDIQ_PLATFORM_HOST */

/* ---------------------------------------------------------------------------
 * Public: fb_temp_sensor_register()
 * Call before embediq_engine_boot().
 * ------------------------------------------------------------------------- */

EMBEDIQ_SUBS(g_sensor_subs, MSG_TIMER_1SEC);
EMBEDIQ_PUBS(g_sensor_pubs, MSG_TEMP_READING);

EmbedIQ_FB_Handle_t fb_temp_sensor_register(void)
{
    static const EmbedIQ_FB_Config_t k_cfg = {
        .name               = "fb_temp_sensor",
        .boot_phase         = EMBEDIQ_BOOT_PHASE_APPLICATION,
        .init_fn            = sensor_init,
        .subscriptions      = g_sensor_subs,
        .subscription_count = 1u,
        .publications       = g_sensor_pubs,
        .publication_count  = 1u,
    };
    return embediq_fb_register(&k_cfg);
}
