#define _POSIX_C_SOURCE 200809L

/*
 * test_harness.c — Self-tests for the EmbedIQ test harness infrastructure
 *
 * Verifies bus_inject(), EMBEDIQ_TEST_SOURCE_ID sentinel, priority routing,
 * unsubscribed message handling, and the scenario runner.
 *
 * Zero external test framework — stdlib + OSAL only.
 *
 * Run:      ./build/tests/unit/test_harness
 * Expected: "All 5 tests passed. (0 failed)"
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "embediq_test.h"
#include "embediq_bus.h"
#include "embediq_fb.h"
#include "embediq_msg.h"
#include "embediq_osal.h"
#include "embediq_config.h"

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

/* Test-only APIs from message_bus.c and fb_engine.c */
extern void     fb_engine__reset(void);
extern void     message_bus__reset(void);
extern bool     message_bus__test_recv(uint8_t ep_id, uint8_t priority,
                                       EmbedIQ_Msg_t *out);
extern uint32_t message_bus__drop_count(void);

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
    fb_engine__reset();
    message_bus__reset();
}

#define MSG_HARNESS_A  0x0430u
#define MSG_HARNESS_B  0x0431u
#define MSG_HARNESS_C  0x0432u

static EmbedIQ_Msg_t make_msg(uint16_t id, uint8_t prio)
{
    EmbedIQ_Msg_t m;
    memset(&m, 0, sizeof(m));
    m.msg_id   = id;
    m.priority = prio;
    return m;
}

/* ---------------------------------------------------------------------------
 * Test 1: bus_inject delivers to subscriber
 * ------------------------------------------------------------------------- */

static void test_bus_inject_delivers_to_subscriber(void)
{
    full_reset();

    EMBEDIQ_SUBS(subs_a, MSG_HARNESS_A);
    EmbedIQ_FB_Config_t cfg_dst = {
        .name = "fb_harness_dst", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
        .subscriptions = subs_a, .subscription_count = 1,
    };
    EmbedIQ_FB_Config_t cfg_src = {
        .name = "fb_harness_src", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
    };

    embediq_fb_register(&cfg_dst);
    embediq_fb_register(&cfg_src);
    uint8_t ep = embediq_bus_resolve_name("fb_harness_dst");
    embediq_engine_boot();
    message_bus_boot();

    EmbedIQ_Msg_t m = make_msg(MSG_HARNESS_A, EMBEDIQ_MSG_PRIORITY_NORMAL);
    bus_inject(&m);

    EmbedIQ_Msg_t out;
    bool got = message_bus__test_recv(ep, EMBEDIQ_MSG_PRIORITY_NORMAL, &out);
    ASSERT(got, "bus_inject must deliver to subscribed FB NORMAL queue");
}

/* ---------------------------------------------------------------------------
 * Test 2: bus_inject stamps EMBEDIQ_TEST_SOURCE_ID
 * ------------------------------------------------------------------------- */

static void test_bus_inject_source_id_is_test_sentinel(void)
{
    full_reset();

    EMBEDIQ_SUBS(subs_a2, MSG_HARNESS_A);
    EmbedIQ_FB_Config_t cfg_dst = {
        .name = "fb_src_check", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
        .subscriptions = subs_a2, .subscription_count = 1,
    };
    EmbedIQ_FB_Config_t cfg_pad = {
        .name = "fb_src_pad", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
    };

    embediq_fb_register(&cfg_dst);
    embediq_fb_register(&cfg_pad);
    uint8_t ep = embediq_bus_resolve_name("fb_src_check");
    embediq_engine_boot();
    message_bus_boot();

    EmbedIQ_Msg_t m = make_msg(MSG_HARNESS_A, EMBEDIQ_MSG_PRIORITY_NORMAL);
    bus_inject(&m);

    EmbedIQ_Msg_t out;
    message_bus__test_recv(ep, EMBEDIQ_MSG_PRIORITY_NORMAL, &out);
    ASSERT(out.source_endpoint_id == EMBEDIQ_TEST_SOURCE_ID,
           "bus_inject must stamp source_endpoint_id = 0xFE (test sentinel)");
}

/* ---------------------------------------------------------------------------
 * Test 3: bus_inject HIGH priority routes to HIGH queue
 * ------------------------------------------------------------------------- */

static void test_bus_inject_high_priority_routes_to_high_queue(void)
{
    full_reset();

    EMBEDIQ_SUBS(subs_b, MSG_HARNESS_B);
    EmbedIQ_FB_Config_t cfg_dst = {
        .name = "fb_hipri_dst", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
        .subscriptions = subs_b, .subscription_count = 1,
    };
    EmbedIQ_FB_Config_t cfg_pad = {
        .name = "fb_hipri_pad", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
    };

    embediq_fb_register(&cfg_dst);
    embediq_fb_register(&cfg_pad);
    uint8_t ep = embediq_bus_resolve_name("fb_hipri_dst");
    embediq_engine_boot();
    message_bus_boot();

    EmbedIQ_Msg_t m = make_msg(MSG_HARNESS_B, EMBEDIQ_MSG_PRIORITY_HIGH);
    bus_inject(&m);

    EmbedIQ_Msg_t out;
    bool got_high   = message_bus__test_recv(ep, EMBEDIQ_MSG_PRIORITY_HIGH, &out);
    bool got_normal = message_bus__test_recv(ep, EMBEDIQ_MSG_PRIORITY_NORMAL, &out);
    ASSERT(got_high && !got_normal,
           "HIGH-priority inject must land in HIGH queue, not NORMAL");
}

/* ---------------------------------------------------------------------------
 * Test 4: bus_inject with unsubscribed message does not crash
 * ------------------------------------------------------------------------- */

static void test_bus_inject_unsubscribed_message_no_crash(void)
{
    full_reset();

    EmbedIQ_FB_Config_t cfg = {
        .name = "fb_nosub", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
    };
    embediq_fb_register(&cfg);
    embediq_engine_boot();
    message_bus_boot();

    EmbedIQ_Msg_t m = make_msg(MSG_HARNESS_C, EMBEDIQ_MSG_PRIORITY_NORMAL);
    bus_inject(&m);  /* no subscriber — must not crash */

    ASSERT(1, "bus_inject with no subscriber must not crash");
}

/* ---------------------------------------------------------------------------
 * Test 5: scenario runner replays sequence
 * ------------------------------------------------------------------------- */

static void test_scenario_runner_replays_sequence(void)
{
    full_reset();

    EMBEDIQ_SUBS(subs_scen, MSG_HARNESS_A);
    EmbedIQ_FB_Config_t cfg_dst = {
        .name = "fb_scenario_dst", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
        .subscriptions = subs_scen, .subscription_count = 1,
    };
    EmbedIQ_FB_Config_t cfg_pad = {
        .name = "fb_scenario_pad", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
    };

    embediq_fb_register(&cfg_dst);
    embediq_fb_register(&cfg_pad);
    uint8_t ep = embediq_bus_resolve_name("fb_scenario_dst");
    embediq_engine_boot();
    message_bus_boot();

    embediq_test_scenario_step_t steps[3];
    steps[0].msg = make_msg(MSG_HARNESS_A, EMBEDIQ_MSG_PRIORITY_NORMAL);
    steps[1].msg = make_msg(MSG_HARNESS_A, EMBEDIQ_MSG_PRIORITY_NORMAL);
    steps[2].msg = make_msg(MSG_HARNESS_A, EMBEDIQ_MSG_PRIORITY_NORMAL);

    embediq_test_run_scenario(steps, 3);

    EmbedIQ_Msg_t r1, r2, r3, r4;
    bool w1 = message_bus__test_recv(ep, EMBEDIQ_MSG_PRIORITY_NORMAL, &r1);
    bool w2 = message_bus__test_recv(ep, EMBEDIQ_MSG_PRIORITY_NORMAL, &r2);
    bool w3 = message_bus__test_recv(ep, EMBEDIQ_MSG_PRIORITY_NORMAL, &r3);
    bool w4 = message_bus__test_recv(ep, EMBEDIQ_MSG_PRIORITY_NORMAL, &r4);

    ASSERT(w1 && w2 && w3 && !w4,
           "scenario runner must deliver exactly 3 messages in order");
}

/* ---------------------------------------------------------------------------
 * main
 * ------------------------------------------------------------------------- */

int main(void)
{
    test_bus_inject_delivers_to_subscriber();
    test_bus_inject_source_id_is_test_sentinel();
    test_bus_inject_high_priority_routes_to_high_queue();
    test_bus_inject_unsubscribed_message_no_crash();
    test_scenario_runner_replays_sequence();

    printf("\nAll %d tests passed. (%d failed)\n",
           g_tests_run - g_tests_failed, g_tests_failed);
    return g_tests_failed > 0 ? 1 : 0;
}
