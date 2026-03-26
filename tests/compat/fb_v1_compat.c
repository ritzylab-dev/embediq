/*
 * tests/compat/fb_v1_compat.c — v1 Core API compatibility shim (Invariant I-14)
 *
 * Exercises every type, macro, enum value, and function declaration in all
 * 18 Core contract headers. Compiled on every CI run to verify the v1 API surface
 * has not broken. MUST NOT be included in any production binary.
 *
 * CI commands:
 *   Step 3 (core headers only):
 *     gcc -I core/include -std=c11 -Wall -Werror tests/compat/fb_v1_compat.c -o /dev/null
 *   Step 5 (core + generated catalog):
 *     gcc -I core/include -I generated -std=c11 -Wall -Werror \
 *         -DEMBEDIQ_MSG_MAX_PAYLOAD=64 tests/compat/fb_v1_compat.c -o /dev/null
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "embediq_fb.h"
#include "embediq_subfn.h"
#include "embediq_sm.h"
#include "embediq_osal.h"
#include "embediq_msg.h"
#include "embediq_bus.h"
#include "embediq_obs.h"
#include "embediq_platform_msgs.h"
#include "embediq_config.h"
#include "embediq_nvm.h"
#include "embediq_timer.h"
#include "embediq_wdg.h"
#include "embediq_time.h"
#include "embediq_endpoint.h"
#include "embediq_meta.h"
#include "embediq_ota.h"
#include "embediq_mqtt.h"
#include "embediq_bridge.h"

/* Generated catalog — present only when -I generated/ is on the compile line. */
#if __has_include("embediq_msg_catalog.h")
#include "embediq_msg_catalog.h"
#define EMBEDIQ_COMPAT_HAS_CATALOG 1
#endif

/* ---------------------------------------------------------------------------
 * FB callbacks — referenced by my_fb_config initialiser
 * ------------------------------------------------------------------------- */

static void my_fb__init(EmbedIQ_FB_Handle_t fb, void *fb_data)
{
    (void)fb;
    (void)fb_data;
}

static void my_fb__run(EmbedIQ_FB_Handle_t fb, void *fb_data)
{
    (void)fb;
    (void)fb_data;
}

static void my_fb__exit(EmbedIQ_FB_Handle_t fb, void *fb_data)
{
    (void)fb;
    (void)fb_data;
}

static void my_fb__fault(EmbedIQ_FB_Handle_t fb, void *fb_data, uint32_t reason)
{
    (void)fb;
    (void)fb_data;
    (void)reason;
}

/* Use required macros — never compound literals (R-sub-01). */
EMBEDIQ_SUBS(my_subs, MSG_TIMER_1MS, MSG_TIMER_10MS);
EMBEDIQ_PUBS(my_pubs, MSG_TIMER_100MS);

/* const char * const [] decays to const char * const * — matches depends_on. */
static const char * const my_deps[] = { "fb_timer" };

/* Exercises every field of EmbedIQ_FB_Config_t. */
static const EmbedIQ_FB_Config_t my_fb_config = {
    .name               = "my_fb",
    .priority           = 5u,
    .stack_size         = 2048u,
    .init_fn            = my_fb__init,
    .run_fn             = my_fb__run,
    .exit_fn            = my_fb__exit,
    .fault_fn           = my_fb__fault,
    .fb_data            = NULL,
    .subscriptions      = my_subs,
    .subscription_count = 2u,
    .publications       = my_pubs,
    .publication_count  = 1u,
    .depends_on         = my_deps,
    .depends_count      = 1u,
    .boot_phase         = EMBEDIQ_BOOT_PHASE_APPLICATION,
};

/* ---------------------------------------------------------------------------
 * Sub-function callbacks — referenced by my_subfn_config initialiser
 * ------------------------------------------------------------------------- */

static void my_subfn__init(EmbedIQ_FB_Handle_t fb, void *fb_data,
                           void *subfn_data)
{
    (void)fb;
    (void)fb_data;
    (void)subfn_data;
}

static void my_subfn__run(EmbedIQ_FB_Handle_t fb, const void *msg,
                          void *fb_data, void *subfn_data)
{
    (void)fb;
    (void)msg;
    (void)fb_data;
    (void)subfn_data;
}

static void my_subfn__exit(EmbedIQ_FB_Handle_t fb, void *fb_data,
                           void *subfn_data)
{
    (void)fb;
    (void)fb_data;
    (void)subfn_data;
}

EMBEDIQ_SUBS(my_subfn_subs, MSG_TIMER_1MS);

/* Exercises every field of EmbedIQ_SubFn_Config_t. */
static const EmbedIQ_SubFn_Config_t my_subfn_config = {
    .name               = "my_subfn",
    .init_order         = 0u,
    .init_fn            = my_subfn__init,
    .run_fn             = my_subfn__run,
    .exit_fn            = my_subfn__exit,
    .subscriptions      = my_subfn_subs,
    .subscription_count = 1u,
    .subfn_data         = NULL,
    .fsm                = NULL,
    .osal_signal        = NULL,
};

/* ---------------------------------------------------------------------------
 * FSM table — one transition row + sentinel
 * ------------------------------------------------------------------------- */

#define STATE_IDLE  0u
#define STATE_ARMED 1u

static bool guard_always(const void *msg, void *fb_data)
{
    (void)msg;
    (void)fb_data;
    return true;
}

static void action_noop(const void *msg, void *fb_data)
{
    (void)msg;
    (void)fb_data;
}

static const EmbedIQ_SM_Row_t my_sm_table[] = {
    { STATE_IDLE, MSG_TIMER_1MS, guard_always, action_noop, STATE_ARMED },
    EMBEDIQ_SM_TABLE_END,
};

/* Exercises every field of EmbedIQ_SM_t. */
static EmbedIQ_SM_t my_sm = {
    .current_state = STATE_IDLE,
    .table         = my_sm_table,
    .name          = "my_sm",
};

/* ---------------------------------------------------------------------------
 * Entry point
 * All objects referenced to silence -Wunused-variable.
 * ------------------------------------------------------------------------- */

int main(void)
{
    (void)my_fb_config;
    (void)my_subfn_config;
    (void)my_sm;

    /* --- embediq_msg.h: message envelope and priority enum --- */
    EmbedIQ_Msg_t msg = {0};
    msg.msg_id   = MSG_TIMER_1MS;
    msg.priority = (uint8_t)EMBEDIQ_MSG_PRIORITY_NORMAL;
    (void)msg;

    /* --- embediq_msg_catalog.h: generated message IDs and payload types --- */
#ifdef EMBEDIQ_COMPAT_HAS_CATALOG
    uint16_t ready_id = MSG_SYSTEM_READY;
    MSG_SYSTEM_READY_Payload_t ready_payload = { .boot_phase = 0u };
    (void)ready_id;
    (void)ready_payload;
#endif

    /* --- embediq_obs.h: event record, event type macros, transport enum --- */
    EmbedIQ_Event_t evt = {0};
    evt.event_type = EMBEDIQ_OBS_EVT_LIFECYCLE;
    evt.sequence   = 0u;   /* sequence is canonical ordering (I-13) */
    (void)evt;

    EmbedIQ_Obs_Transport_t tr = EMBEDIQ_OBS_TRANSPORT_STDOUT;
    (void)tr;

    /* --- embediq_osal.h: opaque pointer types reachable via headers --- */
    EmbedIQ_Task_t   *task  = NULL;
    EmbedIQ_Queue_t  *queue = NULL;
    EmbedIQ_Mutex_t  *mutex = NULL;
    EmbedIQ_Timer_t  *timer = NULL;
    (void)task; (void)queue; (void)mutex; (void)timer;

    /* --- embediq_config.h: sizing constants --- */
    (void)EMBEDIQ_MAX_ENDPOINTS;
    (void)EMBEDIQ_MSG_MAX_PAYLOAD;
    (void)EMBEDIQ_OBS_RING_DEPTH;

    /* --- embediq_nvm.h: NVM API types --- */
    embediq_err_t (*nvm_set_fn)(const char *, const void *, uint32_t) = NULL;
    (void)nvm_set_fn;

    /* --- embediq_timer.h: tick payload struct --- */
    embediq_timer_tick_t tick = { .tick_count = 0u, .timestamp_us = 0u };
    (void)tick;

    /* --- embediq_wdg.h: watchdog types --- */
    embediq_fb_id_t wdg_id = 0u;
    (void)wdg_id;

    /* --- embediq_time.h: time API types --- */
    embediq_err_t (*set_epoch_fn)(uint64_t) = NULL;
    (void)set_epoch_fn;

    /* --- embediq_endpoint.h: endpoint router types --- */
    embediq_ep_id_t ep = EMBEDIQ_EP_INVALID;
    (void)ep;

    /* --- embediq_meta.h: FB metadata types --- */
    const EmbedIQ_FB_Meta_t *meta = NULL;
    (void)meta;

    /* --- embediq_ota.h: OTA types --- */
    embediq_ota_state_t ota_state = EMBEDIQ_OTA_STATE_IDLE;
    (void)ota_state;

    /* --- embediq_mqtt.h: MQTT types --- */
    embediq_mqtt_state_t mqtt_state = EMBEDIQ_MQTT_STATE_DISCONNECTED;
    (void)mqtt_state;

    /* --- embediq_bridge.h: bridge types --- */
    const embediq_bridge_ops_t *bridge_ops = NULL;
    (void)bridge_ops;

    return 0;
}
