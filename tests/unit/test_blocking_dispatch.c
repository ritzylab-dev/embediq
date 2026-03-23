#define _POSIX_C_SOURCE 200809L

/*
 * tests/unit/test_blocking_dispatch.c — Unit tests for blocking dispatch
 *
 * Verifies the P2-T0 blocking-dispatch feature:
 *   1. Dispatch semaphore created per subscribing FB after engine_boot().
 *   2. Notify callback registered with bus after engine_boot().
 *   3. Publish triggers notify (semaphore posted once per enqueue).
 *   4. N publishes post semaphore exactly N times.
 *   5. embediq_sem_post_from_isr() correctly posts a semaphore.
 *
 * Zero external framework — stdlib + OSAL only.
 * No dispatch threads started — semaphores tested directly via trywait.
 *
 * Run:      ./build/tests/unit/test_blocking_dispatch
 * Expected: "All 5 tests passed. (0 failed)"
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "embediq_bus.h"
#include "embediq_fb.h"
#include "embediq_msg.h"
#include "embediq_osal.h"
#include "embediq_config.h"

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

/* ---------------------------------------------------------------------------
 * Test-only API (fb_engine.c + message_bus.c, EMBEDIQ_PLATFORM_HOST)
 * ------------------------------------------------------------------------- */

extern void              fb_engine__reset(void);
extern embediq_sem_t     fb_engine__dispatch_sem(uint8_t ep_id);
extern embediq_notify_fn_t message_bus__get_notify_fn(void);

/* ---------------------------------------------------------------------------
 * Minimal test harness
 * ------------------------------------------------------------------------- */

static int g_tests_run    = 0;
static int g_tests_failed = 0;

#define ASSERT(cond, msg)  do {                                                \
    g_tests_run++;                                                             \
    if (!(cond)) {                                                             \
        fprintf(stderr, "FAIL  %-56s  %s:%d\n", __func__, __FILE__, __LINE__);\
        g_tests_failed++;                                                      \
    } else {                                                                   \
        printf("PASS  %s\n", __func__);                                        \
    }                                                                          \
} while (0)

static void full_reset(void)
{
    fb_engine__reset();   /* also resets message_bus via message_bus__reset() */
}

static EmbedIQ_Msg_t make_msg(uint16_t id, uint8_t prio)
{
    EmbedIQ_Msg_t m;
    memset(&m, 0, sizeof(m));
    m.msg_id   = id;
    m.priority = prio;
    return m;
}

/* Message IDs — official EmbedIQ range, distinct from other test files. */
#define MSG_DISP_A  0x0410u
#define MSG_DISP_B  0x0411u
#define MSG_DISP_C  0x0412u

/* ---------------------------------------------------------------------------
 * test_dispatch_sem_created_per_fb
 *
 * After embediq_engine_boot(), every FB with subscriptions must have a
 * non-NULL dispatch semaphore.  FBs without subscriptions must have NULL.
 * Semaphores are created inside engine_boot — no dispatch threads needed.
 * ------------------------------------------------------------------------- */

static void test_dispatch_sem_created_per_fb(void)
{
    full_reset();

    EMBEDIQ_SUBS(subs_a, MSG_DISP_A);

    EmbedIQ_FB_Config_t cfg_sub = {
        .name = "fb_sem_sub", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
        .subscriptions = subs_a, .subscription_count = 1,
    };
    EmbedIQ_FB_Config_t cfg_nosub = {
        .name = "fb_sem_nosub", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
    };

    embediq_fb_register(&cfg_sub);
    embediq_fb_register(&cfg_nosub);
    embediq_engine_boot();

    uint8_t ep_sub   = embediq_bus_resolve_name("fb_sem_sub");
    uint8_t ep_nosub = embediq_bus_resolve_name("fb_sem_nosub");

    ASSERT(fb_engine__dispatch_sem(ep_sub)   != NULL &&
           fb_engine__dispatch_sem(ep_nosub) == NULL,
           "subscribing FB must have non-NULL sem; non-subscribing must have NULL");
}

/* ---------------------------------------------------------------------------
 * test_notify_fn_registered_after_boot
 *
 * embediq_engine_boot() must register the engine notify callback with the
 * bus.  message_bus__get_notify_fn() must return non-NULL after boot.
 * ------------------------------------------------------------------------- */

static void test_notify_fn_registered_after_boot(void)
{
    full_reset();

    EmbedIQ_FB_Config_t cfg = {
        .name = "fb_notify_check", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
    };
    embediq_fb_register(&cfg);
    embediq_engine_boot();

    ASSERT(message_bus__get_notify_fn() != NULL,
           "bus notify callback must be non-NULL after engine_boot");
}

/* ---------------------------------------------------------------------------
 * test_enqueue_triggers_notify
 *
 * Publishing a message to a subscribed FB must post that FB's dispatch
 * semaphore.  embediq_sem_trywait() must succeed after embediq_publish().
 * ------------------------------------------------------------------------- */

static void test_enqueue_triggers_notify(void)
{
    full_reset();

    EMBEDIQ_SUBS(subs, MSG_DISP_B);

    EmbedIQ_FB_Config_t cfg_dst = {
        .name = "fb_trig_dst", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
        .subscriptions = subs, .subscription_count = 1,
    };
    EmbedIQ_FB_Config_t cfg_src = {
        .name = "fb_trig_src", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
    };

    embediq_fb_register(&cfg_dst);
    EmbedIQ_FB_Handle_t src = embediq_fb_register(&cfg_src);
    embediq_engine_boot();
    message_bus_boot();

    uint8_t       ep_dst = embediq_bus_resolve_name("fb_trig_dst");
    embediq_sem_t sem    = fb_engine__dispatch_sem(ep_dst);

    EmbedIQ_Msg_t msg = make_msg(MSG_DISP_B, EMBEDIQ_MSG_PRIORITY_NORMAL);
    embediq_publish(src, &msg);

    ASSERT(embediq_sem_trywait(sem),
           "publish must post the destination FB's dispatch semaphore");
}

/* ---------------------------------------------------------------------------
 * test_dispatch_sem_count_matches_enqueues
 *
 * Publishing N messages to one subscriber must post the semaphore N times.
 * Exactly N non-blocking trywait() calls must succeed; the (N+1)th must fail.
 * ------------------------------------------------------------------------- */

static void test_dispatch_sem_count_matches_enqueues(void)
{
    full_reset();

    EMBEDIQ_SUBS(subs, MSG_DISP_C);

    EmbedIQ_FB_Config_t cfg_dst = {
        .name = "fb_count_dst", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
        .subscriptions = subs, .subscription_count = 1,
    };
    EmbedIQ_FB_Config_t cfg_src = {
        .name = "fb_count_src", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
    };

    embediq_fb_register(&cfg_dst);
    EmbedIQ_FB_Handle_t src = embediq_fb_register(&cfg_src);
    embediq_engine_boot();
    message_bus_boot();

    uint8_t       ep_dst = embediq_bus_resolve_name("fb_count_dst");
    embediq_sem_t sem    = fb_engine__dispatch_sem(ep_dst);

    /* Publish 3 messages. */
    for (int i = 0; i < 3; i++) {
        EmbedIQ_Msg_t msg = make_msg(MSG_DISP_C, EMBEDIQ_MSG_PRIORITY_NORMAL);
        embediq_publish(src, &msg);
    }

    /* Exactly 3 trywait() calls must succeed; 4th must fail. */
    bool w1 = embediq_sem_trywait(sem);
    bool w2 = embediq_sem_trywait(sem);
    bool w3 = embediq_sem_trywait(sem);
    bool w4 = embediq_sem_trywait(sem);   /* must fail — semaphore drained */

    ASSERT(w1 && w2 && w3 && !w4,
           "N publishes must post semaphore exactly N times");
}

/* ---------------------------------------------------------------------------
 * test_sem_post_from_isr_works
 *
 * embediq_sem_post_from_isr() must post the semaphore so that a subsequent
 * embediq_sem_trywait() returns true.  Tests the ISR-safe code path
 * independent of the engine and bus.
 * ------------------------------------------------------------------------- */

static void test_sem_post_from_isr_works(void)
{
    full_reset();

    embediq_sem_t sem = embediq_sem_create(0u, UINT32_MAX);

    embediq_sem_post_from_isr(sem);
    bool got = embediq_sem_trywait(sem);

    embediq_sem_destroy(sem);

    ASSERT(got, "sem_post_from_isr must increment semaphore so trywait succeeds");
}

/* ---------------------------------------------------------------------------
 * Entry point
 * ------------------------------------------------------------------------- */

int main(void)
{
    test_dispatch_sem_created_per_fb();
    test_notify_fn_registered_after_boot();
    test_enqueue_triggers_notify();
    test_dispatch_sem_count_matches_enqueues();
    test_sem_post_from_isr_works();

    printf("\nAll %d tests passed. (%d failed)\n",
           g_tests_run - g_tests_failed, g_tests_failed);
    return g_tests_failed > 0 ? 1 : 0;
}
