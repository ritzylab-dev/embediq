#define _POSIX_C_SOURCE 200809L

/*
 * examples/gateway/fb_north_bridge.c — North Bridge FB
 *
 * Manages the northbound (cloud) connection for the EmbedIQ industrial edge
 * gateway demo.  Demonstrates two sub-functions in one Functional Block.
 *
 * Sub-function 1 — "north_timer":
 *   Subscribes to MSG_TIMER_1SEC.  Drives a connectivity FSM:
 *     NORTH_ONLINE -> NORTH_OFFLINE at tick 12  (simulated link drop)
 *     NORTH_OFFLINE -> NORTH_RECONNECTING at tick 22
 *     NORTH_RECONNECTING -> NORTH_ONLINE at tick 23  (+ buffer flush)
 *
 * Sub-function 2 — "north_rx":
 *   Subscribes to MSG_TELEMETRY and MSG_ALERT.
 *   ONLINE:   prints to stdout (simulated TCP/MQTT publish).
 *   OFFLINE/RECONNECTING: buffers MSG_TELEMETRY locally (max 10 slots).
 *             MSG_ALERT is always printed (never buffered — alerts are urgent).
 *
 * FSM Observatory events are emitted automatically by embediq_sm_process().
 *
 * Boot phase: EMBEDIQ_BOOT_PHASE_APPLICATION (3)
 *
 * Public API:
 *   fb_north_bridge_register() — register FB with framework
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
#include "embediq_sm.h"
#include "embediq_config.h"
#include "embediq_platform_msgs.h"
#include "gateway_msgs.h"

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

/* ---------------------------------------------------------------------------
 * Connectivity FSM states
 * ------------------------------------------------------------------------- */

#define NORTH_ONLINE        0u
#define NORTH_OFFLINE       1u
#define NORTH_RECONNECTING  2u

/* Tick thresholds for simulated connectivity events */
#define TICK_GO_OFFLINE      12u
#define TICK_GO_RECONNECTING 22u
#define TICK_LINK_RESTORED   23u

/* ---------------------------------------------------------------------------
 * Local telemetry buffer (used when OFFLINE/RECONNECTING)
 * ------------------------------------------------------------------------- */

#define NORTH_BUFFER_SIZE  10u

typedef struct {
    uint8_t  sensor_id;
    uint16_t seq_num;
    float    value;
} NorthBufferEntry_t;

/* ---------------------------------------------------------------------------
 * Module state (R-02: static, no malloc)
 * ------------------------------------------------------------------------- */

static uint32_t            g_tick        = 0u;
static uint8_t             g_conn_state  = NORTH_ONLINE;  /* mirror of FSM state */

static NorthBufferEntry_t  g_buffer[NORTH_BUFFER_SIZE];
static uint8_t             g_buf_count   = 0u;  /* entries currently stored  */

/* ---------------------------------------------------------------------------
 * Connectivity FSM guard functions (pure — read-only access to g_tick)
 * ------------------------------------------------------------------------- */

static bool guard_go_offline(const void *msg, void *fb_data)
{
    (void)msg; (void)fb_data;
    return g_tick == TICK_GO_OFFLINE;
}

static bool guard_go_reconnecting(const void *msg, void *fb_data)
{
    (void)msg; (void)fb_data;
    return g_tick == TICK_GO_RECONNECTING;
}

static bool guard_link_restored(const void *msg, void *fb_data)
{
    (void)msg; (void)fb_data;
    return g_tick == TICK_LINK_RESTORED;
}

/* ---------------------------------------------------------------------------
 * Connectivity FSM action functions
 * ------------------------------------------------------------------------- */

static void action_go_offline(const void *msg, void *fb_data)
{
    (void)msg; (void)fb_data;
    g_conn_state = NORTH_OFFLINE;
    printf("\n[NORTH] *** Link down at t=%us — buffering telemetry locally ***\n\n",
           (unsigned)g_tick);
}

static void action_go_reconnecting(const void *msg, void *fb_data)
{
    (void)msg; (void)fb_data;
    g_conn_state = NORTH_RECONNECTING;
    printf("\n[NORTH] Link reconnecting at t=%us...\n", (unsigned)g_tick);
}

static void action_link_restored(const void *msg, void *fb_data)
{
    (void)msg; (void)fb_data;
    g_conn_state = NORTH_ONLINE;
    printf("\n[NORTH] Link restored at t=%us — flushing %u buffered records\n\n",
           (unsigned)g_tick, (unsigned)g_buf_count);

    /* Flush local buffer in order. */
    static const char *k_sensor_names[] = { "Tank_Temp", "Tank_Pressure", "Pump_Flow" };
    for (uint8_t i = 0u; i < g_buf_count; i++) {
        const NorthBufferEntry_t *e = &g_buffer[i];
        const char *name = (e->sensor_id < SENSOR_COUNT)
                           ? k_sensor_names[e->sensor_id] : "unknown";
        printf("  [-> CLOUD buffered] TELEMETRY  %-16s  value=%.2f  seq=%u\n",
               name, (double)e->value, (unsigned)e->seq_num);
    }
    g_buf_count = 0u;
    printf("\n");
}

/* ---------------------------------------------------------------------------
 * Connectivity FSM table
 * ------------------------------------------------------------------------- */

static const EmbedIQ_SM_Row_t s_north_table[] = {
    { NORTH_ONLINE,       MSG_TIMER_1SEC, guard_go_offline,      action_go_offline,      NORTH_OFFLINE       },
    { NORTH_OFFLINE,      MSG_TIMER_1SEC, guard_go_reconnecting, action_go_reconnecting, NORTH_RECONNECTING  },
    { NORTH_RECONNECTING, MSG_TIMER_1SEC, guard_link_restored,   action_link_restored,   NORTH_ONLINE        },
    EMBEDIQ_SM_TABLE_END
};

static EmbedIQ_SM_t g_north_sm = { NORTH_ONLINE, s_north_table, "north_conn" };

/* ---------------------------------------------------------------------------
 * Sub-function 1: north_timer — drives connectivity FSM on each tick
 * ------------------------------------------------------------------------- */

static void north_timer(EmbedIQ_FB_Handle_t fb, const void *msg,
                        void *fb_data, void *subfn_data)
{
    (void)fb; (void)fb_data;
    EmbedIQ_SM_t *sm = (EmbedIQ_SM_t *)subfn_data;

    g_tick++;   /* advance tick before guard checks */
    embediq_sm_process(sm, msg, MSG_TIMER_1SEC, NULL);
}

/* ---------------------------------------------------------------------------
 * Sub-function 2: north_rx — publishes or buffers telemetry/alerts
 * ------------------------------------------------------------------------- */

static const char *sensor_name(uint8_t sid)
{
    static const char *k_names[] = { "Tank_Temp", "Tank_Pressure", "Pump_Flow" };
    return (sid < SENSOR_COUNT) ? k_names[sid] : "unknown";
}

static void north_rx(EmbedIQ_FB_Handle_t fb, const void *msg,
                     void *fb_data, void *subfn_data)
{
    (void)fb; (void)fb_data; (void)subfn_data;

    const EmbedIQ_Msg_t *m = (const EmbedIQ_Msg_t *)msg;

    if (m->msg_id == MSG_ALERT) {
        /* Alerts always go through — never buffered. */
        MSG_ALERT_Payload_t alert;
        memcpy(&alert, m->payload, sizeof(alert));
        if (alert.active) {
            printf("[-> CLOUD] ALERT ***  %-16s  value=%.2f  threshold=%.2f  ACTIVE\n",
                   sensor_name(alert.sensor_id),
                   (double)alert.value, (double)alert.threshold);
        } else {
            printf("[-> CLOUD] ALERT clr  %-16s  value=%.2f  threshold=%.2f  cleared\n",
                   sensor_name(alert.sensor_id),
                   (double)alert.value, (double)alert.threshold);
        }
        return;
    }

    if (m->msg_id == MSG_TELEMETRY) {
        MSG_TELEMETRY_Payload_t telem;
        memcpy(&telem, m->payload, sizeof(telem));

        if (g_conn_state == NORTH_ONLINE) {
            printf("[-> CLOUD] TELEMETRY  %-16s  value=%.2f  seq=%u\n",
                   sensor_name(telem.sensor_id),
                   (double)telem.value, (unsigned)telem.seq_num);
        } else {
            /* Buffer locally. */
            if (g_buf_count < NORTH_BUFFER_SIZE) {
                NorthBufferEntry_t *slot = &g_buffer[g_buf_count];
                slot->sensor_id = telem.sensor_id;
                slot->seq_num   = telem.seq_num;
                slot->value     = telem.value;
                g_buf_count++;
                printf("[BUFFER] %-16s  seq=%u  buffered=%u/%u\n",
                       sensor_name(telem.sensor_id),
                       (unsigned)telem.seq_num,
                       (unsigned)g_buf_count, NORTH_BUFFER_SIZE);
            } else {
                printf("[BUFFER] FULL — dropped %-16s seq=%u\n",
                       sensor_name(telem.sensor_id), (unsigned)telem.seq_num);
            }
        }
    }
}

/* ---------------------------------------------------------------------------
 * FB init — register two sub-functions
 * ------------------------------------------------------------------------- */

static void north_bridge_init(EmbedIQ_FB_Handle_t fb, void *fb_data)
{
    (void)fb_data;

    /* Sub-fn 1: timer handler drives the connectivity FSM. */
    static const uint16_t k_timer_subs[] = { MSG_TIMER_1SEC };
    static EmbedIQ_SubFn_Config_t k_timer_cfg = {
        .name               = "north_timer",
        .init_order         = 1u,
        .init_fn            = NULL,
        .run_fn             = north_timer,
        .exit_fn            = NULL,
        .subscriptions      = k_timer_subs,
        .subscription_count = 1u,
        .subfn_data         = &g_north_sm,
        .fsm                = NULL,
        .osal_signal        = NULL,
    };

    /* Sub-fn 2: data handler publishes or buffers telemetry and alerts. */
    static const uint16_t k_rx_subs[] = { MSG_TELEMETRY, MSG_ALERT };
    static EmbedIQ_SubFn_Config_t k_rx_cfg = {
        .name               = "north_rx",
        .init_order         = 2u,
        .init_fn            = NULL,
        .run_fn             = north_rx,
        .exit_fn            = NULL,
        .subscriptions      = k_rx_subs,
        .subscription_count = 2u,
        .subfn_data         = NULL,
        .fsm                = NULL,
        .osal_signal        = NULL,
    };

    embediq_subfn_register(fb, &k_timer_cfg);
    embediq_subfn_register(fb, &k_rx_cfg);
}

/* ---------------------------------------------------------------------------
 * Public: fb_north_bridge_register()
 * ------------------------------------------------------------------------- */

EMBEDIQ_SUBS(g_north_subs, MSG_TIMER_1SEC, MSG_TELEMETRY, MSG_ALERT);

EmbedIQ_FB_Handle_t fb_north_bridge_register(void)
{
    static const EmbedIQ_FB_Config_t k_config = {
        .name               = "fb_north_bridge",
        .boot_phase         = EMBEDIQ_BOOT_PHASE_APPLICATION,
        .init_fn            = north_bridge_init,
        .subscriptions      = g_north_subs,
        .subscription_count = 3u,
    };

    return embediq_fb_register(&k_config);
}
