#define _POSIX_C_SOURCE 200809L

/*
 * examples/gateway/fb_field_receiver.c — Field Sensor Receiver FB
 *
 * Simulates an RS-485/Modbus poll cycle for the EmbedIQ industrial edge
 * gateway demo.  On each MSG_TIMER_1SEC tick, reads the next row from a
 * scripted sensor data table and publishes MSG_FIELD_READING for each of
 * the three field sensors (Tank_Temp, Tank_Pressure, Pump_Flow).
 *
 * Scripted events (relative to first tick):
 *   Tick  5: Tank_Pressure spikes to 9.2 bar (threshold 8.5) — alert fires
 *   Tick  6: Tank_Pressure 8.7 bar — alert sustained, no duplicate
 *   Tick  7: Tank_Pressure 8.7 bar — alert sustained
 *   Tick  8: Tank_Pressure 7.5 bar — alert clears
 *   Tick 23: Pump_Flow drops to 3.2 L/min (threshold 5.0) — dry-run alert
 *   Tick 24: Pump_Flow 3.1 L/min — alert sustained
 *   Tick 25: Pump_Flow 22.0 L/min — alert clears
 *
 *   Ticks 11–21: connectivity offline — field_receiver is unaffected;
 *   it continues publishing; fb_north_bridge buffers locally.
 *
 * Boot phase: EMBEDIQ_BOOT_PHASE_APPLICATION (3)
 *
 * Public API:
 *   fb_field_receiver_register() — register FB with framework
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
#include "gateway_msgs.h"

#include <stdint.h>
#include <string.h>

/* ---------------------------------------------------------------------------
 * Scripted sensor data table (30 rows — one per second of demo)
 *
 * Columns: Tank_Temp (deg C), Tank_Pressure (bar), Pump_Flow (L/min)
 * ------------------------------------------------------------------------- */

typedef struct { float temp; float pressure; float flow; } SensorFrame_t;

static const SensorFrame_t k_frames[] = {
    /* tick  0 */ { 22.5f,  4.2f, 28.0f },
    /* tick  1 */ { 23.1f,  5.1f, 27.5f },
    /* tick  2 */ { 23.8f,  6.3f, 29.0f },
    /* tick  3 */ { 24.2f,  7.1f, 28.3f },
    /* tick  4 */ { 24.9f,  7.8f, 27.9f },
    /* tick  5 */ { 25.3f,  9.2f, 28.1f },  /* pressure spike: 9.2 > 8.5 */
    /* tick  6 */ { 25.1f,  8.7f, 27.8f },  /* pressure still high */
    /* tick  7 */ { 24.8f,  8.7f, 27.6f },  /* pressure still high */
    /* tick  8 */ { 24.5f,  7.5f, 28.2f },  /* pressure normalises */
    /* tick  9 */ { 24.2f,  6.8f, 27.4f },
    /* tick 10 */ { 23.8f,  6.1f, 27.1f },
    /* tick 11 */ { 23.5f,  5.8f, 26.9f },  /* north bridge goes OFFLINE */
    /* tick 12 */ { 23.2f,  5.5f, 26.7f },
    /* tick 13 */ { 23.0f,  5.2f, 26.5f },
    /* tick 14 */ { 22.8f,  5.0f, 26.3f },
    /* tick 15 */ { 22.5f,  4.8f, 26.1f },
    /* tick 16 */ { 22.3f,  4.6f, 25.9f },
    /* tick 17 */ { 22.1f,  4.4f, 25.7f },
    /* tick 18 */ { 21.9f,  4.3f, 25.5f },
    /* tick 19 */ { 21.7f,  4.2f, 25.3f },
    /* tick 20 */ { 21.5f,  4.1f, 25.1f },
    /* tick 21 */ { 21.3f,  4.0f, 24.9f },  /* reconnecting */
    /* tick 22 */ { 21.2f,  3.9f, 24.7f },  /* ONLINE — buffer flushed */
    /* tick 23 */ { 21.0f,  3.8f,  3.2f },  /* pump dry-run: 3.2 < 5.0 */
    /* tick 24 */ { 20.8f,  3.8f,  3.1f },  /* pump still dry */
    /* tick 25 */ { 20.6f,  3.7f, 22.0f },  /* pump flow recovered */
    /* tick 26 */ { 20.5f,  3.7f, 24.5f },
    /* tick 27 */ { 20.4f,  3.6f, 25.1f },
    /* tick 28 */ { 20.3f,  3.6f, 25.8f },
    /* tick 29 */ { 20.2f,  3.5f, 26.2f },
};

#define FRAME_COUNT  ((uint32_t)(sizeof(k_frames) / sizeof(k_frames[0])))

/* ---------------------------------------------------------------------------
 * Module state (R-02: static, no malloc)
 * ------------------------------------------------------------------------- */

static uint32_t g_tick = 0u;

/* ---------------------------------------------------------------------------
 * Internal helpers
 * ------------------------------------------------------------------------- */

static void publish_reading(EmbedIQ_FB_Handle_t fb, uint8_t sensor_id,
                            uint8_t unit_id, float value)
{
    EmbedIQ_Msg_t m;
    memset(&m, 0, sizeof(m));
    m.msg_id   = MSG_FIELD_READING;
    m.priority = EMBEDIQ_MSG_PRIORITY_NORMAL;

    MSG_FIELD_READING_Payload_t payload;
    payload.sensor_id = sensor_id;
    payload.unit_id   = unit_id;
    payload.raw_value = value;
    memcpy(m.payload, &payload, sizeof(payload));
    m.payload_len = (uint16_t)sizeof(payload);

    embediq_publish(fb, &m);
}

/* ---------------------------------------------------------------------------
 * Sub-function: handle MSG_TIMER_1SEC — publish one poll cycle
 * ------------------------------------------------------------------------- */

static void field_poll(EmbedIQ_FB_Handle_t fb, const void *msg,
                       void *fb_data, void *subfn_data)
{
    (void)msg; (void)fb_data; (void)subfn_data;

    uint32_t idx = g_tick % FRAME_COUNT;
    const SensorFrame_t *f = &k_frames[idx];

    publish_reading(fb, SENSOR_TANK_TEMP,     UNIT_CELSIUS, f->temp);
    publish_reading(fb, SENSOR_TANK_PRESSURE, UNIT_BAR,     f->pressure);
    publish_reading(fb, SENSOR_PUMP_FLOW,     UNIT_LPM,     f->flow);

    g_tick++;
}

/* ---------------------------------------------------------------------------
 * FB init — register sub-function
 * ------------------------------------------------------------------------- */

static void field_receiver_init(EmbedIQ_FB_Handle_t fb, void *fb_data)
{
    (void)fb_data;

    static const uint16_t k_subs[] = { MSG_TIMER_1SEC };
    static EmbedIQ_SubFn_Config_t k_cfg = {
        .name               = "field_poll",
        .init_order         = 1u,
        .init_fn            = NULL,
        .run_fn             = field_poll,
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
 * Public: fb_field_receiver_register()
 * ------------------------------------------------------------------------- */

EMBEDIQ_SUBS(g_field_subs, MSG_TIMER_1SEC);
EMBEDIQ_PUBS(g_field_pubs, MSG_FIELD_READING);

EmbedIQ_FB_Handle_t fb_field_receiver_register(void)
{
    static const EmbedIQ_FB_Config_t k_config = {
        .name               = "fb_field_receiver",
        .boot_phase         = EMBEDIQ_BOOT_PHASE_APPLICATION,
        .init_fn            = field_receiver_init,
        .subscriptions      = g_field_subs,
        .subscription_count = 1u,
        .publications       = g_field_pubs,
        .publication_count  = 1u,
    };

    return embediq_fb_register(&k_config);
}
