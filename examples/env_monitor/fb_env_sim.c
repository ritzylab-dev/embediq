#define _POSIX_C_SOURCE 200809L

/*
 * examples/env_monitor/fb_env_sim.c — Environmental Sensor Simulator FB
 *
 * Simulates three sensors using a triangle wave (no math.h required):
 *   Temperature: 22°C ± 4°C, period 120 ticks (2 minutes)
 *   Humidity:    55% ± 10%,  period 150 ticks (2.5 minutes)
 *   CO2:         450 ± 100ppm, period 200 ticks (~3 minutes)
 *
 * On each MSG_TIMER_1SEC tick: publishes one MSG_TELEMETRY_GAUGE per sensor.
 * fb_telemetry aggregates and batches. fb_cloud_mqtt publishes to MQTT.
 *
 * Also handles MSG_MQTT_CMD_RX — parses simple text commands:
 *   "alert:NN.N"  → sets temperature alert threshold to NN.N °C
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
#include "telemetry_msg_catalog.h"
#include "env_monitor_metrics.h"

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>   /* atof */

/* ---------------------------------------------------------------------------
 * Triangle wave helper — no floating-point division, no math.h
 *
 * Returns a value oscillating between min and max with the given period.
 * At tick=0 → min. At tick=period/2 → max. At tick=period → min again.
 *
 * Derivation: half-period integer steps. For period P and amplitude A:
 *   half = period / 2
 *   phase = tick % period
 *   if phase < half: value = min + (max-min) * phase / half
 *   else:            value = max - (max-min) * (phase-half) / half
 * All intermediate values are float to preserve precision.
 * ------------------------------------------------------------------------- */

static float triangle_wave(uint32_t tick, uint32_t period,
                            float min, float max)
{
    if (period == 0u) { return min; }
    uint32_t phase    = tick % period;
    uint32_t half     = period / 2u;
    float    range    = max - min;
    float    fhalf    = (float)half;
    if (phase < half) {
        return min + range * ((float)phase / fhalf);
    }
    return max - range * ((float)(phase - half) / fhalf);
}

/* ---------------------------------------------------------------------------
 * Module state
 * ------------------------------------------------------------------------- */

static EmbedIQ_FB_Handle_t  s_fb               = NULL;
static uint32_t              s_tick             = 0u;
static float                 s_alert_threshold  = ENV_TEMP_ALERT_DEFAULT;

/* ---------------------------------------------------------------------------
 * Publish one MSG_TELEMETRY_GAUGE
 * Payload: {metric_id, value, unit_id} — 7 bytes, always < EMBEDIQ_MSG_MAX_PAYLOAD
 * ------------------------------------------------------------------------- */

static void publish_gauge(uint16_t metric_id, float value, uint8_t unit_id)
{
    EmbedIQ_Msg_t msg;
    (void)memset(&msg, 0, sizeof(msg));
    msg.msg_id      = MSG_TELEMETRY_GAUGE;
    msg.priority    = 1u;  /* NORMAL */
    msg.payload_len = (uint16_t)sizeof(MSG_TELEMETRY_GAUGE_Payload_t);

    MSG_TELEMETRY_GAUGE_Payload_t pl;
    pl.metric_id = metric_id;
    pl.value     = value;
    pl.unit_id   = unit_id;
    (void)memcpy(msg.payload, &pl, sizeof(pl));

    embediq_publish(s_fb, &msg);
}

/* ---------------------------------------------------------------------------
 * Sub-function: MSG_TIMER_1SEC — simulate sensors, publish gauges
 * ------------------------------------------------------------------------- */

static void sf_tick(EmbedIQ_FB_Handle_t fb, const void *msg,
                    void *fb_data, void *subfn_data)
{
    (void)fb; (void)msg; (void)fb_data; (void)subfn_data;

    s_tick++;

    float temp     = triangle_wave(s_tick, 120u, 18.0f, 30.0f);
    float humidity = triangle_wave(s_tick,  150u, 45.0f, 65.0f);
    float co2      = triangle_wave(s_tick,  200u, 380.0f, 560.0f);

    publish_gauge(ENV_METRIC_TEMPERATURE, temp,     ENV_UNIT_CELSIUS);
    publish_gauge(ENV_METRIC_HUMIDITY,    humidity,  ENV_UNIT_PERCENT);
    publish_gauge(ENV_METRIC_CO2,         co2,       ENV_UNIT_PPM);

    /* Alert: publish alert_level gauge based on temperature threshold */
    float alert = (temp > s_alert_threshold) ? 1.0f : 0.0f;
    publish_gauge(ENV_METRIC_ALERT_LEVEL, alert, ENV_UNIT_NONE);

    if (s_tick % 10u == 0u) {
        printf("[ENV SIM] tick=%u temp=%.1f°C humidity=%.1f%% CO2=%.0fppm alert=%.0f\n",
               (unsigned)s_tick, (double)temp, (double)humidity,
               (double)co2, (double)alert);
    }
}

/* ---------------------------------------------------------------------------
 * Sub-function: MSG_MQTT_CMD_RX — handle cloud commands
 * Command format (raw bytes in payload): "alert:27.5" → set threshold to 27.5°C
 * ------------------------------------------------------------------------- */

static void sf_cmd(EmbedIQ_FB_Handle_t fb, const void *msg,
                   void *fb_data, void *subfn_data)
{
    (void)fb; (void)fb_data; (void)subfn_data;
    if (!msg) { return; }
    const EmbedIQ_Msg_t *m = (const EmbedIQ_Msg_t *)msg;
    if (m->payload_len == 0u) { return; }

    /* Safe NUL-terminated copy of payload.
     * Buffer size derivation: EMBEDIQ_MSG_MAX_PAYLOAD bytes content + 1 NUL
     * = EMBEDIQ_MSG_MAX_PAYLOAD + 1 bytes total. */
    char cmd[EMBEDIQ_MSG_MAX_PAYLOAD + 1u];
    uint16_t len = (m->payload_len < (uint16_t)EMBEDIQ_MSG_MAX_PAYLOAD) ?
                   m->payload_len : (uint16_t)EMBEDIQ_MSG_MAX_PAYLOAD;
    (void)memcpy(cmd, m->payload, len);
    cmd[len] = '\0';

    printf("[ENV SIM] Cloud command received: %s\n", cmd);

    /* Parse "alert:NN.N" */
    if (strncmp(cmd, "alert:", 6u) == 0) {
        float threshold = (float)atof(cmd + 6u);
        if (threshold > 0.0f && threshold < 100.0f) {
            s_alert_threshold = threshold;
            printf("[ENV SIM] Alert threshold updated to %.1f°C\n",
                   (double)s_alert_threshold);
        }
    }
}

/* ---------------------------------------------------------------------------
 * FB init — register sub-functions
 * Signature: void (*)(EmbedIQ_FB_Handle_t fb, void *fb_data)
 * ------------------------------------------------------------------------- */

static void env_sim_init(EmbedIQ_FB_Handle_t fb, void *fb_data)
{
    (void)fb_data;
    s_fb   = fb;
    s_tick = 0u;
    s_alert_threshold = ENV_TEMP_ALERT_DEFAULT;

    static const uint16_t k_timer_subs[] = { MSG_TIMER_1SEC    };
    static const uint16_t k_cmd_subs[]   = { MSG_MQTT_CMD_RX   };

    static EmbedIQ_SubFn_Config_t k_timer_cfg = {
        .name               = "env_tick",
        .init_order         = 1u,
        .run_fn             = sf_tick,
        .subscriptions      = k_timer_subs,
        .subscription_count = 1u,
    };
    static EmbedIQ_SubFn_Config_t k_cmd_cfg = {
        .name               = "env_cmd",
        .init_order         = 2u,
        .run_fn             = sf_cmd,
        .subscriptions      = k_cmd_subs,
        .subscription_count = 1u,
    };

    (void)embediq_subfn_register(fb, &k_timer_cfg);
    (void)embediq_subfn_register(fb, &k_cmd_cfg);

    printf("[ENV SIM] Initialised. Publishing temp/humidity/CO2 every second.\n");
    printf("[ENV SIM] Alert threshold: %.1f°C\n", (double)s_alert_threshold);
}

/* ---------------------------------------------------------------------------
 * Public registration
 * ------------------------------------------------------------------------- */

EMBEDIQ_SUBS(g_env_sim_subs, MSG_TIMER_1SEC, MSG_MQTT_CMD_RX);

EmbedIQ_FB_Handle_t fb_env_sim_register(void)
{
    static const EmbedIQ_FB_Config_t k_cfg = {
        .name               = "fb_env_sim",
        .boot_phase         = EMBEDIQ_BOOT_PHASE_APPLICATION,
        .init_fn            = env_sim_init,
        .subscriptions      = g_env_sim_subs,
        .subscription_count = 2u,
    };
    return embediq_fb_register(&k_cfg);
}
