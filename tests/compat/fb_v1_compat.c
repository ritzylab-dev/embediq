/*
 * tests/compat/fb_v1_compat.c — v1 Core API compatibility shim (Invariant I-14)
 *
 * Exercises every type, macro, enum value, and function declaration in the
 * three Core headers. Compiled on every CI run to verify the v1 API surface
 * has not broken. MUST NOT be included in any production binary.
 *
 * CI command:
 *   gcc -I core/include -std=c11 -Wall -Werror -c tests/compat/fb_v1_compat.c
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "embediq_fb.h"
#include "embediq_subfn.h"
#include "embediq_sm.h"

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
    return 0;
}
