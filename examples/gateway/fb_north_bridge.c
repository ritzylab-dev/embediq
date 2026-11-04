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
 * Sub-function 2 — "north_telemetry":
 *   Subscribes to MSG_TELEMETRY.
 *   ONLINE:   sends the reading north (simulated TCP/MQTT publish).
 *   OFFLINE/RECONNECTING: buffers it locally (max 10 slots).
 *
 * Sub-function 3 — "north_alert":
 *   Subscribes to MSG_ALERT. Always sent north (never buffered — alerts are urgent).
 *
 * A sub-function receives the payload bytes only (no message id), so each
 * message type has its own sub-function.
 *
 * FSM Observatory events are emitted automatically by embediq_sm_process().
 * Every simulated send, buffer and link change is an application-defined
 * Observatory event (EMBEDIQ_OBS_EVT_APP) — codes below. The FB never prints.
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
#include "embediq_obs.h"
#include "gateway_msgs.h"

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

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

/* ---------------------------------------------------------------------------
 * Application event codes (EMBEDIQ_OBS_EVT_APP state_or_flag), target 0xFF.
 *
 * Link events — msg_id = tick (seconds since boot, low 16 bits):
 * ------------------------------------------------------------------------- */

#define NORTH_EVT_LINK_DOWN          0x01u  /**< Link lost; telemetry is buffered. */
#define NORTH_EVT_LINK_RECONNECTING  0x02u  /**< Link reconnecting. */
#define NORTH_EVT_LINK_RESTORED      0x03u  /**< Link restored; the buffer is flushed. */
#define NORTH_EVT_FLUSH_COUNT        0x04u  /**< Emitted before LINK_RESTORED.
                                                 msg_id = buffered records to flush. */

/* Per-sensor events — code = base + sensor_id (SENSOR_* in gateway_msgs.h,
 * 0–2). Values are × 100 (bar, deg C or L/min; demo values 3–90 fit), saturating
 * at 0 and 65535; no simulated value is negative. */
#define NORTH_EVT_TELEMETRY          0x10u  /**< Sent north. msg_id = value × 100. */
#define NORTH_EVT_TELEMETRY_FLUSHED  0x20u  /**< Buffered record sent on reconnect.
                                                 msg_id = value × 100. */
#define NORTH_EVT_BUFFERED           0x30u  /**< Buffered while offline. msg_id = seq. */
#define NORTH_EVT_BUFFER_FULL_DROP   0x40u  /**< Buffer full; dropped. msg_id = seq. */
#define NORTH_EVT_ALERT_ON           0x50u  /**< Alert raised. msg_id = value × 100. */
#define NORTH_EVT_ALERT_OFF          0x60u  /**< Alert cleared. msg_id = value × 100. */
#define NORTH_EVT_ALERT_THRESHOLD    0x70u  /**< Follows ALERT_ON / ALERT_OFF.
                                                 msg_id = threshold × 100. */

typedef struct {
    uint8_t  sensor_id;
    uint16_t seq_num;
    float    value;
} NorthBufferEntry_t;

/* ---------------------------------------------------------------------------
 * Module state (R-02: static, no malloc)
 * ------------------------------------------------------------------------- */

static uint8_t             s_ep          = 0xFFu;  /* this FB's endpoint (cached in init) */
static uint32_t            g_tick        = 0u;
static uint8_t             g_conn_state  = NORTH_ONLINE;  /* mirror of FSM state */

static NorthBufferEntry_t  g_buffer[NORTH_BUFFER_SIZE];
static uint8_t             g_buf_count   = 0u;  /* entries currently stored  */

/* ---------------------------------------------------------------------------
 * Observatory helpers
 * ------------------------------------------------------------------------- */

static void north_emit(uint8_t code, uint16_t value)
{
    embediq_obs_emit(EMBEDIQ_OBS_EVT_APP, s_ep, 0xFFu, code, value);
}

/** Per-sensor code: base + sensor_id. */
static uint8_t sensor_code(uint8_t base, uint8_t sensor_id)
{
    return (uint8_t)(base + ((sensor_id < SENSOR_COUNT) ? sensor_id : 0x0Fu));
}

/** value × 100 as a saturating uint16. */
static uint16_t hundredths(float value)
{
    float v = value * 100.0f + 0.5f;
    if (v <= 0.0f)     { return 0u; }
    if (v >= 65535.0f) { return 65535u; }
    return (uint16_t)v;
}

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
    north_emit(NORTH_EVT_LINK_DOWN, (uint16_t)g_tick);
}

static void action_go_reconnecting(const void *msg, void *fb_data)
{
    (void)msg; (void)fb_data;
    g_conn_state = NORTH_RECONNECTING;
    north_emit(NORTH_EVT_LINK_RECONNECTING, (uint16_t)g_tick);
}

static void action_link_restored(const void *msg, void *fb_data)
{
    (void)msg; (void)fb_data;
    g_conn_state = NORTH_ONLINE;
    north_emit(NORTH_EVT_FLUSH_COUNT, (uint16_t)g_buf_count);
    north_emit(NORTH_EVT_LINK_RESTORED, (uint16_t)g_tick);

    /* Flush local buffer in order. */
    for (uint8_t i = 0u; i < g_buf_count; i++) {
        const NorthBufferEntry_t *e = &g_buffer[i];
        north_emit(sensor_code(NORTH_EVT_TELEMETRY_FLUSHED, e->sensor_id),
                   hundredths(e->value));
    }
    g_buf_count = 0u;
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
 * Sub-function 2: north_telemetry — sends or buffers MSG_TELEMETRY
 * msg points at the MSG_TELEMETRY payload bytes.
 * ------------------------------------------------------------------------- */

static void north_telemetry(EmbedIQ_FB_Handle_t fb, const void *msg,
                            void *fb_data, void *subfn_data)
{
    (void)fb; (void)fb_data; (void)subfn_data;

    MSG_TELEMETRY_Payload_t telem;
    memcpy(&telem, msg, sizeof(telem));

    if (g_conn_state == NORTH_ONLINE) {
        north_emit(sensor_code(NORTH_EVT_TELEMETRY, telem.sensor_id),
                   hundredths(telem.value));
    } else {
        /* Buffer locally. */
        if (g_buf_count < NORTH_BUFFER_SIZE) {
            NorthBufferEntry_t *slot = &g_buffer[g_buf_count];
            slot->sensor_id = telem.sensor_id;
            slot->seq_num   = telem.seq_num;
            slot->value     = telem.value;
            g_buf_count++;
            north_emit(sensor_code(NORTH_EVT_BUFFERED, telem.sensor_id),
                       telem.seq_num);
        } else {
            north_emit(sensor_code(NORTH_EVT_BUFFER_FULL_DROP, telem.sensor_id),
                       telem.seq_num);
        }
    }
}

/* ---------------------------------------------------------------------------
 * Sub-function 3: north_alert — sends MSG_ALERT (never buffered)
 * msg points at the MSG_ALERT payload bytes.
 * ------------------------------------------------------------------------- */

static void north_alert(EmbedIQ_FB_Handle_t fb, const void *msg,
                        void *fb_data, void *subfn_data)
{
    (void)fb; (void)fb_data; (void)subfn_data;

    MSG_ALERT_Payload_t alert;
    memcpy(&alert, msg, sizeof(alert));
    north_emit(sensor_code(alert.active ? NORTH_EVT_ALERT_ON : NORTH_EVT_ALERT_OFF,
                           alert.sensor_id),
               hundredths(alert.value));
    north_emit(sensor_code(NORTH_EVT_ALERT_THRESHOLD, alert.sensor_id),
               hundredths(alert.threshold));
}

/* ---------------------------------------------------------------------------
 * FB init — register three sub-functions
 * ------------------------------------------------------------------------- */

static void north_bridge_init(EmbedIQ_FB_Handle_t fb, void *fb_data)
{
    (void)fb_data;
    s_ep = embediq_engine_fb_ep(fb);

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

    /* Sub-fn 2: sends or buffers telemetry. */
    static const uint16_t k_telem_subs[] = { MSG_TELEMETRY };
    static EmbedIQ_SubFn_Config_t k_telem_cfg = {
        .name               = "north_telemetry",
        .init_order         = 2u,
        .init_fn            = NULL,
        .run_fn             = north_telemetry,
        .exit_fn            = NULL,
        .subscriptions      = k_telem_subs,
        .subscription_count = 1u,
        .subfn_data         = NULL,
        .fsm                = NULL,
        .osal_signal        = NULL,
    };

    /* Sub-fn 3: sends alerts. */
    static const uint16_t k_alert_subs[] = { MSG_ALERT };
    static EmbedIQ_SubFn_Config_t k_alert_cfg = {
        .name               = "north_alert",
        .init_order         = 3u,
        .init_fn            = NULL,
        .run_fn             = north_alert,
        .exit_fn            = NULL,
        .subscriptions      = k_alert_subs,
        .subscription_count = 1u,
        .subfn_data         = NULL,
        .fsm                = NULL,
        .osal_signal        = NULL,
    };

    embediq_subfn_register(fb, &k_timer_cfg);
    embediq_subfn_register(fb, &k_telem_cfg);
    embediq_subfn_register(fb, &k_alert_cfg);
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
