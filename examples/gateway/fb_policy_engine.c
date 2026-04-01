#define _POSIX_C_SOURCE 200809L

/*
 * examples/gateway/fb_policy_engine.c — Policy Engine FB
 *
 * Applies threshold rules to field sensor readings.  Publishes MSG_ALERT
 * only when alert state changes (no duplicate alerts per sustained breach).
 * Publishes MSG_TELEMETRY once per two field readings (throttle 2:1).
 *
 * Thresholds:
 *   SENSOR_TANK_TEMP     >  70.0 deg C  -> alert
 *   SENSOR_TANK_PRESSURE >   8.5 bar    -> alert
 *   SENSOR_PUMP_FLOW     <   5.0 L/min  -> alert (dry-run detection)
 *
 * Boot phase: EMBEDIQ_BOOT_PHASE_APPLICATION (3)
 *
 * Public API:
 *   fb_policy_engine_register() — register FB with framework
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
#include "gateway_msgs.h"

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* ---------------------------------------------------------------------------
 * Per-sensor threshold table (parallel to SENSOR_* constants)
 * high_thresh > value triggers alert; low_thresh < value triggers alert.
 * Set unused bound to +/-INFINITY equivalent using large floats.
 * ------------------------------------------------------------------------- */

typedef struct {
    float    high_thresh;   /**< Alert when value >  high_thresh. 1e9 = unused. */
    float    low_thresh;    /**< Alert when value <  low_thresh.  -1e9 = unused. */
} ThreshRule_t;

static const ThreshRule_t k_rules[SENSOR_COUNT] = {
    /* SENSOR_TANK_TEMP     */ { THRESH_TANK_TEMP_HIGH_C,   -1e9f },
    /* SENSOR_TANK_PRESSURE */ { THRESH_TANK_PRESSURE_HIGH, -1e9f },
    /* SENSOR_PUMP_FLOW     */ {  1e9f, THRESH_PUMP_FLOW_LOW       },
};

/* ---------------------------------------------------------------------------
 * Module state (R-02: static, no malloc)
 * ------------------------------------------------------------------------- */

static bool     g_alert_active[SENSOR_COUNT];     /* current alert state per sensor */
static uint8_t  g_telem_counter[SENSOR_COUNT];    /* throttle counter (publish every 2) */
static uint16_t g_telem_seq[SENSOR_COUNT];        /* telemetry sequence number */

/* ---------------------------------------------------------------------------
 * Internal helpers
 * ------------------------------------------------------------------------- */

static void publish_alert(EmbedIQ_FB_Handle_t fb, uint8_t sensor_id,
                          float value, float threshold, bool active)
{
    EmbedIQ_Msg_t m;
    memset(&m, 0, sizeof(m));
    m.msg_id   = MSG_ALERT;
    m.priority = EMBEDIQ_MSG_PRIORITY_HIGH;

    MSG_ALERT_Payload_t payload;
    payload.sensor_id = sensor_id;
    payload.active    = active;
    payload.value     = value;
    payload.threshold = threshold;
    memcpy(m.payload, &payload, sizeof(payload));
    m.payload_len = (uint16_t)sizeof(payload);

    embediq_publish(fb, &m);
}

static void publish_telemetry(EmbedIQ_FB_Handle_t fb, uint8_t sensor_id,
                              float value)
{
    EmbedIQ_Msg_t m;
    memset(&m, 0, sizeof(m));
    m.msg_id   = MSG_TELEMETRY;
    m.priority = EMBEDIQ_MSG_PRIORITY_NORMAL;

    MSG_TELEMETRY_Payload_t payload;
    payload.sensor_id = sensor_id;
    payload.seq_num   = g_telem_seq[sensor_id]++;
    payload.value     = value;
    memcpy(m.payload, &payload, sizeof(payload));
    m.payload_len = (uint16_t)sizeof(payload);

    embediq_publish(fb, &m);
}

/* ---------------------------------------------------------------------------
 * Sub-function: handle MSG_FIELD_READING
 * ------------------------------------------------------------------------- */

static void policy_run(EmbedIQ_FB_Handle_t fb, const void *msg,
                       void *fb_data, void *subfn_data)
{
    (void)fb_data; (void)subfn_data;

    const EmbedIQ_Msg_t *m = (const EmbedIQ_Msg_t *)msg;
    MSG_FIELD_READING_Payload_t reading;
    memcpy(&reading, m->payload, sizeof(reading));

    uint8_t sid = reading.sensor_id;
    if (sid >= SENSOR_COUNT) { return; }

    float value     = reading.raw_value;
    float hi_thresh = k_rules[sid].high_thresh;
    float lo_thresh = k_rules[sid].low_thresh;

    /* Determine if threshold exceeded. */
    bool breached = (value > hi_thresh) || (value < lo_thresh);
    float active_thresh = (value > hi_thresh) ? hi_thresh : lo_thresh;

    /* Publish MSG_ALERT only on state change. */
    if (breached && !g_alert_active[sid]) {
        g_alert_active[sid] = true;
        publish_alert(fb, sid, value, active_thresh, true);
    } else if (!breached && g_alert_active[sid]) {
        g_alert_active[sid] = false;
        publish_alert(fb, sid, value, active_thresh, false);
    }

    /* Throttle telemetry: publish every 2nd reading. */
    g_telem_counter[sid]++;
    if (g_telem_counter[sid] >= 2u) {
        g_telem_counter[sid] = 0u;
        publish_telemetry(fb, sid, value);
    }
}

/* ---------------------------------------------------------------------------
 * FB init — register sub-function
 * ------------------------------------------------------------------------- */

static void policy_engine_init(EmbedIQ_FB_Handle_t fb, void *fb_data)
{
    (void)fb_data;

    static const uint16_t k_subs[] = { MSG_FIELD_READING };
    static EmbedIQ_SubFn_Config_t k_cfg = {
        .name               = "policy_run",
        .init_order         = 1u,
        .init_fn            = NULL,
        .run_fn             = policy_run,
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
 * Public: fb_policy_engine_register()
 * ------------------------------------------------------------------------- */

EMBEDIQ_SUBS(g_policy_subs, MSG_FIELD_READING);
EMBEDIQ_PUBS(g_policy_pubs, MSG_ALERT, MSG_TELEMETRY);

EmbedIQ_FB_Handle_t fb_policy_engine_register(void)
{
    static const EmbedIQ_FB_Config_t k_config = {
        .name               = "fb_policy_engine",
        .boot_phase         = EMBEDIQ_BOOT_PHASE_APPLICATION,
        .init_fn            = policy_engine_init,
        .subscriptions      = g_policy_subs,
        .subscription_count = 1u,
        .publications       = g_policy_pubs,
        .publication_count  = 2u,
    };

    return embediq_fb_register(&k_config);
}
