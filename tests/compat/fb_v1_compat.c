/*
 * tests/compat/fb_v1_compat.c — v1 Core API compatibility shim (Invariant I-14)
 *
 * Exercises every type, macro, enum value, and function declaration in the
 * seven Core headers. Compiled on every CI run to verify the v1 API surface
 * has not broken. MUST NOT be included in any production binary.
 *
 * CI command:
 *   gcc -I core/include -I generated -std=c11 -Wall -Werror \
 *       -DEMBEDIQ_MSG_MAX_PAYLOAD=64 -c tests/compat/fb_v1_compat.c
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
#include "embediq_msg_catalog.h"

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
EMBEDIQ_SUBS(my_subs, 0x0401u, 0x0402u);
EMBEDIQ_PUBS(my_pubs, 0x0403u);

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

EMBEDIQ_SUBS(my_subfn_subs, 0x0401u);

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
    { STATE_IDLE, 0x0401u, guard_always, action_noop, STATE_ARMED },
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
    msg.msg_id   = 0x0401u;
    msg.priority = (uint8_t)EMBEDIQ_MSG_PRIORITY_NORMAL;
    (void)msg;

    /* --- embediq_msg_catalog.h: generated message IDs and payload types --- */
    uint16_t tick_id = MSG_TIMER_TICK;
    MSG_TIMER_TICK_Payload_t tick_payload = { .tick_count = 0u };
    (void)tick_id;
    (void)tick_payload;

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

    return 0;
}
