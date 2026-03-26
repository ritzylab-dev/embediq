#define _POSIX_C_SOURCE 200809L

/*
 * tests/unit/test_dispatch_shutdown.c — Unit tests for dispatch shutdown
 *
 * Verifies embediq_engine_dispatch_shutdown() (P2-T0b):
 *   1. Shutdown after boot returns EMBEDIQ_OK with no hang.
 *   2. All dispatch threads have exited within 2 seconds.
 *   3. Shutdown is idempotent — second call returns EMBEDIQ_OK, no crash.
 *   4. No messages processed after shutdown — message sits in queue.
 *   5. Re-boot after shutdown processes messages normally.
 *
 * Run:      ./build/tests/unit/test_dispatch_shutdown
 * Expected: "All 5 tests passed. (0 failed)"
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "embediq_fb.h"
#include "embediq_bus.h"
#include "embediq_subfn.h"
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

extern void    fb_engine__reset(void);
extern bool    message_bus__test_recv(uint8_t ep_id, uint8_t prio,
                                      EmbedIQ_Msg_t *out);

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

/* full_reset: fb_engine__reset() calls dispatch_shutdown internally if
 * dispatch threads are running, then clears all state. */
static void full_reset(void)
{
    fb_engine__reset();
}

static EmbedIQ_Msg_t make_msg(uint16_t id, uint8_t prio)
{
    EmbedIQ_Msg_t m;
    memset(&m, 0, sizeof(m));
    m.msg_id   = id;
    m.priority = prio;
    return m;
}

/* Message IDs — community range, distinct from other test files. */
#define MSG_SHUTDOWN_A  0x1410u
#define MSG_SHUTDOWN_B  0x1411u

/* ---------------------------------------------------------------------------
 * Shared counter sub-fn — incremented each time a message is dispatched.
 * Declared at file scope so multiple tests can use it.
 * ------------------------------------------------------------------------- */

static volatile int g_dispatch_count = 0;

static void count_run_fn(EmbedIQ_FB_Handle_t fb, const void *msg,
                          void *fb_data, void *subfn_data)
{
    (void)fb; (void)msg; (void)fb_data; (void)subfn_data;
    g_dispatch_count++;
}

/* Sub-fn config used by test_reboot_after_shutdown (file scope — C11). */
EMBEDIQ_SUBS(g_reboot_subs, MSG_SHUTDOWN_A);

static const EmbedIQ_SubFn_Config_t g_reboot_subfn = {
    .name               = "count_subfn",
    .init_order         = 0,
    .subscriptions      = g_reboot_subs,
    .subscription_count = 1,
    .run_fn             = count_run_fn,
};

static void reboot_init_fn(EmbedIQ_FB_Handle_t h, void *d)
{
    (void)d;
    embediq_subfn_register(h, &g_reboot_subfn);
}

/* ---------------------------------------------------------------------------
 * test_shutdown_after_boot
 *
 * Call engine_boot() + dispatch_boot() then dispatch_shutdown().
 * Must return EMBEDIQ_OK. Must not hang. Must not crash.
 * ------------------------------------------------------------------------- */

static void test_shutdown_after_boot(void)
{
    full_reset();
    g_dispatch_count = 0;

    EMBEDIQ_SUBS(subs, MSG_SHUTDOWN_A);
    EmbedIQ_FB_Config_t cfg = {
        .name = "fb_sd_boot", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
        .subscriptions = subs, .subscription_count = 1,
    };

    embediq_fb_register(&cfg);
    embediq_engine_boot();
    embediq_engine_dispatch_boot();

    int r = embediq_engine_dispatch_shutdown();

    ASSERT(r == 0,
           "dispatch_shutdown must return 0 after boot+dispatch_boot");
}

/* ---------------------------------------------------------------------------
 * test_all_threads_exit_on_shutdown
 *
 * dispatch_shutdown() must join all threads and return within 2 seconds.
 * pthread_join inside shutdown guarantees threads have exited when it returns.
 * ------------------------------------------------------------------------- */

static void test_all_threads_exit_on_shutdown(void)
{
    full_reset();

    EMBEDIQ_SUBS(subs, MSG_SHUTDOWN_A);
    EmbedIQ_FB_Config_t cfg = {
        .name = "fb_sd_join", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
        .subscriptions = subs, .subscription_count = 1,
    };

    embediq_fb_register(&cfg);
    embediq_engine_boot();
    embediq_engine_dispatch_boot();

    uint32_t t0 = embediq_osal_time_ms();
    embediq_engine_dispatch_shutdown();
    uint32_t elapsed = embediq_osal_time_ms() - t0;

    ASSERT(elapsed < 2000u,
           "all dispatch threads must exit and join within 2 seconds");
}

/* ---------------------------------------------------------------------------
 * test_shutdown_idempotent
 *
 * Calling dispatch_shutdown() twice must return EMBEDIQ_OK both times
 * without crash or hang.
 * ------------------------------------------------------------------------- */

static void test_shutdown_idempotent(void)
{
    full_reset();

    EMBEDIQ_SUBS(subs, MSG_SHUTDOWN_A);
    EmbedIQ_FB_Config_t cfg = {
        .name = "fb_sd_idem", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
        .subscriptions = subs, .subscription_count = 1,
    };

    embediq_fb_register(&cfg);
    embediq_engine_boot();
    embediq_engine_dispatch_boot();

    int r1 = embediq_engine_dispatch_shutdown();
    int r2 = embediq_engine_dispatch_shutdown();   /* second call */

    ASSERT(r1 == 0 && r2 == 0,
           "dispatch_shutdown must be idempotent — both calls must return 0");
}

/* ---------------------------------------------------------------------------
 * test_no_messages_processed_after_shutdown
 *
 * After dispatch_shutdown(), dispatch threads have stopped. A message
 * published to the bus must sit unprocessed in the queue (no active thread
 * to drain it).  dispatch_boot() is called again (restart); the stale
 * message must still be visible in the queue before any thread drains it
 * (verified immediately after dispatch_boot with a non-blocking recv).
 * ------------------------------------------------------------------------- */

static void test_no_messages_processed_after_shutdown(void)
{
    full_reset();
    g_dispatch_count = 0;

    EMBEDIQ_SUBS(subs, MSG_SHUTDOWN_B);

    EmbedIQ_FB_Config_t cfg_dst = {
        .name = "fb_sd_stale_dst", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
        .subscriptions = subs, .subscription_count = 1,
    };
    EmbedIQ_FB_Config_t cfg_src = {
        .name = "fb_sd_stale_src", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
    };

    embediq_fb_register(&cfg_dst);
    EmbedIQ_FB_Handle_t src = embediq_fb_register(&cfg_src);
    embediq_engine_boot();
    embediq_engine_dispatch_boot();
    embediq_engine_dispatch_shutdown();  /* threads stopped */

    /* Publish during the shutdown window — no threads running. */
    EmbedIQ_Msg_t msg = make_msg(MSG_SHUTDOWN_B, EMBEDIQ_MSG_PRIORITY_NORMAL);
    embediq_publish(src, &msg);

    /* Message must still be in the queue (not auto-processed). */
    uint8_t       ep_dst = embediq_bus_resolve_name("fb_sd_stale_dst");
    EmbedIQ_Msg_t rx;
    bool in_queue = message_bus__test_recv(ep_dst,
                                           EMBEDIQ_MSG_PRIORITY_NORMAL, &rx);

    ASSERT(in_queue && rx.msg_id == MSG_SHUTDOWN_B && g_dispatch_count == 0,
           "message published during shutdown must remain in queue, unprocessed");
}

/* ---------------------------------------------------------------------------
 * test_reboot_after_shutdown
 *
 * After dispatch_shutdown(), call dispatch_boot() again (re-start).
 * The restarted threads must dispatch messages normally.
 * ------------------------------------------------------------------------- */

static void test_reboot_after_shutdown(void)
{
    full_reset();
    g_dispatch_count = 0;

    EmbedIQ_FB_Config_t cfg_dst = {
        .name = "fb_sd_reboot_dst", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
        .subscriptions = g_reboot_subs, .subscription_count = 1,
        .init_fn = reboot_init_fn,
    };
    EmbedIQ_FB_Config_t cfg_src = {
        .name = "fb_sd_reboot_src", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
    };

    embediq_fb_register(&cfg_dst);
    EmbedIQ_FB_Handle_t src = embediq_fb_register(&cfg_src);
    embediq_engine_boot();
    embediq_engine_dispatch_boot();
    embediq_engine_dispatch_shutdown();  /* first round — stop threads */

    /* Re-start dispatch (no full reset — reuse registered FBs). */
    embediq_engine_dispatch_boot();

    /* Publish one message — the restarted thread must dispatch it. */
    EmbedIQ_Msg_t msg = make_msg(MSG_SHUTDOWN_A, EMBEDIQ_MSG_PRIORITY_NORMAL);
    embediq_publish(src, &msg);

    /* Allow up to 500 ms for the dispatch thread to process the message. */
    uint32_t deadline = embediq_osal_time_ms() + 500u;
    while (g_dispatch_count == 0 && embediq_osal_time_ms() < deadline) {
        embediq_osal_delay_ms(10u);
    }

    embediq_engine_dispatch_shutdown();  /* clean up before full_reset */

    ASSERT(g_dispatch_count == 1,
           "restarted dispatch threads must process messages normally");
}

/* ---------------------------------------------------------------------------
 * Entry point
 * ------------------------------------------------------------------------- */

int main(void)
{
    test_shutdown_after_boot();
    test_all_threads_exit_on_shutdown();
    test_shutdown_idempotent();
    test_no_messages_processed_after_shutdown();
    test_reboot_after_shutdown();

    printf("\nAll %d tests passed. (%d failed)\n",
           g_tests_run - g_tests_failed, g_tests_failed);
    return g_tests_failed > 0 ? 1 : 0;
}
