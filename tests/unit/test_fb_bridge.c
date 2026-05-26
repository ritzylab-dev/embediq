#define _POSIX_C_SOURCE 200809L

/*
 * test_fb_bridge.c — Unit tests for the fb_bridge C framework (Item 5a)
 *
 * 8 in-process tests covering External FB pool management, virtual endpoint
 * assignment, publish routing, subscribe filtering, source endpoint stamping,
 * and bridge bus message emission. Compiled with EMBEDIQ_BRIDGE_TRANSPORT_QUEUE
 * so no socket is needed — all tests run in-process against the real bus.
 *
 * Test pattern matches tests/unit/test_message_bus.c exactly: custom ASSERT
 * macro that prints filename/line on failure, each test calls full_reset()
 * then registers a harness receiver FB subscribed to the message_ids it
 * needs to observe, drains via message_bus__test_recv(ep, prio, &out).
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "embediq_ext_fb.h"
#include "embediq_bus.h"
#include "embediq_fb.h"
#include "embediq_msg.h"
#include "embediq_osal.h"
#include "embediq_config.h"
#include "embediq_platform_msgs.h"

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

/* ---------------------------------------------------------------------------
 * Test-only APIs (EMBEDIQ_PLATFORM_HOST builds)
 * ------------------------------------------------------------------------- */

extern void     fb_engine__reset(void);
extern void     message_bus__reset(void);
extern bool     message_bus__test_recv(uint8_t ep_id, uint8_t prio,
                                       EmbedIQ_Msg_t *out);

/* fb_bridge package-internal entry point: register the FB so engine_boot
 * resolves its handle; queue transport needs no listener task. */
extern EmbedIQ_FB_Handle_t fb_bridge_register(void);

/* ---------------------------------------------------------------------------
 * Minimal test harness (mirrors test_message_bus.c)
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

/* Test harness msg_ids (in the official allocations community range so they
 * don't collide with any production message). */
#define MSG_HARNESS_A 0x0430u
#define MSG_HARNESS_B 0x0431u
#define MSG_HARNESS_C 0x0432u

/* Helper: full reset, register fb_bridge + a harness receiver FB subscribed
 * to every msg_id we want to observe, boot the engine + bus, return the
 * harness receiver's endpoint id.
 *
 * The harness FB subscribes to all three MSG_HARNESS_* test ids AND the
 * three fb_bridge bus messages so a single ep_id can drain everything. */
static uint8_t boot_with_harness_receiver(void)
{
    fb_engine__reset();
    message_bus__reset();

    (void)fb_bridge_register();

    EMBEDIQ_SUBS(harness_subs,
                 MSG_HARNESS_A,
                 MSG_HARNESS_B,
                 MSG_HARNESS_C,
                 MSG_BRIDGE_EXTFB_CONNECTED,
                 MSG_BRIDGE_EXTFB_DISCONNECTED,
                 MSG_BRIDGE_FAULT);

    EmbedIQ_FB_Config_t cfg_recv = {
        .name               = "fb_test_harness_recv",
        .boot_phase         = EMBEDIQ_BOOT_PHASE_APPLICATION,
        .subscriptions      = harness_subs,
        .subscription_count = 6u,
    };
    embediq_fb_register(&cfg_recv);

    embediq_engine_boot();
    message_bus_boot();

    return embediq_bus_resolve_name("fb_test_harness_recv");
}

/* Drain one message from the harness receiver's NORMAL queue. */
static bool drain_one(uint8_t recv_ep, EmbedIQ_Msg_t *out)
{
    return message_bus__test_recv(recv_ep, EMBEDIQ_MSG_PRIORITY_NORMAL, out);
}

/* Drain all pending messages from a queue (used to clear setup noise). */
static void drain_all(uint8_t recv_ep)
{
    EmbedIQ_Msg_t scratch;
    while (message_bus__test_recv(recv_ep, EMBEDIQ_MSG_PRIORITY_NORMAL, &scratch)) {
        /* discard */
    }
}

/* ---------------------------------------------------------------------------
 * test_ext_fb_pool_init_deinit — slot is returned to the pool on deinit
 * and can be reused immediately.
 * ------------------------------------------------------------------------- */

static void test_ext_fb_pool_init_deinit(void)
{
    (void)boot_with_harness_receiver();

    embediq_ext_fb_t *h = embediq_ext_fb_init("test_fb");
    ASSERT(h != NULL, "init returns a non-NULL handle");

    embediq_ext_fb_deinit(h);

    embediq_ext_fb_t *h2 = embediq_ext_fb_init("test_fb_2");
    ASSERT(h2 != NULL, "slot was returned to the pool and can be reused");

    embediq_ext_fb_deinit(h2);
}

/* ---------------------------------------------------------------------------
 * test_ext_fb_pool_exhaustion — the (N+1)th init must return NULL, no crash.
 * ------------------------------------------------------------------------- */

static void test_ext_fb_pool_exhaustion(void)
{
    (void)boot_with_harness_receiver();

    embediq_ext_fb_t *handles[EMBEDIQ_BRIDGE_MAX_EXT_FBS];
    for (uint8_t i = 0u; i < EMBEDIQ_BRIDGE_MAX_EXT_FBS; i++) {
        handles[i] = embediq_ext_fb_init("fill");
        ASSERT(handles[i] != NULL, "every pool slot allocates");
    }

    embediq_ext_fb_t *overflow = embediq_ext_fb_init("overflow");
    ASSERT(overflow == NULL, "pool full: init returns NULL with no crash");

    for (uint8_t i = 0u; i < EMBEDIQ_BRIDGE_MAX_EXT_FBS; i++) {
        embediq_ext_fb_deinit(handles[i]);
    }
}

/* ---------------------------------------------------------------------------
 * test_ext_fb_virtual_endpoint_range — published msg has source_endpoint_id
 * inside the 0x40-0x7F External FB virtual range.
 * ------------------------------------------------------------------------- */

static void test_ext_fb_virtual_endpoint_range(void)
{
    uint8_t recv_ep = boot_with_harness_receiver();

    embediq_ext_fb_t *h = embediq_ext_fb_init("range_test");
    ASSERT(h != NULL, "init succeeds");

    drain_all(recv_ep);   /* discard the CONNECTED notification */

    EmbedIQ_Msg_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.msg_id      = MSG_HARNESS_A;
    msg.priority    = (uint8_t)EMBEDIQ_MSG_PRIORITY_NORMAL;
    msg.payload_len = 0u;
    (void)embediq_ext_fb_publish(h, &msg);

    EmbedIQ_Msg_t recv;
    bool got = drain_one(recv_ep, &recv);
    ASSERT(got &&
           recv.source_endpoint_id >= 0x40u &&
           recv.source_endpoint_id <= 0x7Fu,
           "source_endpoint_id stamped in External FB virtual range 0x40-0x7F");

    embediq_ext_fb_deinit(h);
}

/* ---------------------------------------------------------------------------
 * test_ext_fb_publish_reaches_subscriber — message published by an External
 * FB arrives at an internal subscriber's queue.
 * ------------------------------------------------------------------------- */

static void test_ext_fb_publish_reaches_subscriber(void)
{
    uint8_t recv_ep = boot_with_harness_receiver();

    embediq_ext_fb_t *h = embediq_ext_fb_init("publisher");
    ASSERT(h != NULL, "init succeeds");

    drain_all(recv_ep);

    EmbedIQ_Msg_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.msg_id      = MSG_HARNESS_B;
    msg.priority    = (uint8_t)EMBEDIQ_MSG_PRIORITY_NORMAL;
    msg.payload_len = 0u;
    embediq_err_t err = embediq_ext_fb_publish(h, &msg);
    ASSERT(err == EMBEDIQ_OK, "publish returns EMBEDIQ_OK");

    EmbedIQ_Msg_t recv;
    bool got = drain_one(recv_ep, &recv);
    ASSERT(got && recv.msg_id == MSG_HARNESS_B,
           "subscribed internal FB receives the published message");

    embediq_ext_fb_deinit(h);
}

/* ---------------------------------------------------------------------------
 * test_ext_fb_subscribe_filter — embediq_ext_fb_recv() must time out when
 * no matching message has been delivered to the External FB's inbound queue.
 * ------------------------------------------------------------------------- */

static void test_ext_fb_subscribe_filter(void)
{
    (void)boot_with_harness_receiver();

    embediq_ext_fb_t *h = embediq_ext_fb_init("filter_test");
    ASSERT(h != NULL, "init succeeds");

    uint16_t ids[] = { MSG_HARNESS_A };
    (void)embediq_ext_fb_subscribe(h, ids, 1u);

    /* No message was ever delivered to the External FB's inbound queue:
     * recv must return a non-OK status (TIMEOUT / EMPTY). */
    EmbedIQ_Msg_t out;
    memset(&out, 0, sizeof(out));
    embediq_err_t err = embediq_ext_fb_recv(h, &out, 50u);
    ASSERT(err != EMBEDIQ_OK,
           "recv with no inbound traffic returns TIMEOUT, not OK");

    embediq_ext_fb_deinit(h);
}

/* ---------------------------------------------------------------------------
 * test_ext_fb_source_stamp — explicit check that publish stamps the slot's
 * virtual endpoint id into the message's source_endpoint_id.
 * ------------------------------------------------------------------------- */

static void test_ext_fb_source_stamp(void)
{
    uint8_t recv_ep = boot_with_harness_receiver();

    embediq_ext_fb_t *h = embediq_ext_fb_init("stamp_test");
    ASSERT(h != NULL, "init succeeds");

    drain_all(recv_ep);   /* discard CONNECTED */

    EmbedIQ_Msg_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.msg_id      = MSG_HARNESS_C;
    msg.priority    = (uint8_t)EMBEDIQ_MSG_PRIORITY_NORMAL;
    msg.payload_len = 0u;
    (void)embediq_ext_fb_publish(h, &msg);

    EmbedIQ_Msg_t recv;
    bool got = drain_one(recv_ep, &recv);
    ASSERT(got &&
           recv.msg_id == MSG_HARNESS_C &&
           recv.source_endpoint_id >= 0x40u &&
           recv.source_endpoint_id <= 0x7Fu,
           "publish stamps virtual endpoint id as source_endpoint_id");

    embediq_ext_fb_deinit(h);
}

/* ---------------------------------------------------------------------------
 * test_ext_fb_msg_bridge_connected — init publishes MSG_BRIDGE_EXTFB_CONNECTED
 * with transport=1 (queue) and a virtual endpoint id in 0x40-0x7F.
 * ------------------------------------------------------------------------- */

static void test_ext_fb_msg_bridge_connected(void)
{
    uint8_t recv_ep = boot_with_harness_receiver();

    embediq_ext_fb_t *h = embediq_ext_fb_init("connect_event_test");
    ASSERT(h != NULL, "init succeeds");

    EmbedIQ_Msg_t recv;
    bool got = drain_one(recv_ep, &recv);
    ASSERT(got && recv.msg_id == MSG_BRIDGE_EXTFB_CONNECTED,
           "init publishes MSG_BRIDGE_EXTFB_CONNECTED");

    const EmbedIQ_Msg_BridgeExtFbConnected_t *payload =
        (const EmbedIQ_Msg_BridgeExtFbConnected_t *)recv.payload;
    ASSERT(payload->transport == 1u,
           "CONNECTED payload reports queue transport (transport=1)");
    ASSERT(payload->virtual_endpoint_id >= 0x40u &&
           payload->virtual_endpoint_id <= 0x7Fu,
           "CONNECTED payload carries a virtual endpoint id in 0x40-0x7F");

    embediq_ext_fb_deinit(h);
}

/* ---------------------------------------------------------------------------
 * test_ext_fb_msg_bridge_disconnected — deinit publishes
 * MSG_BRIDGE_EXTFB_DISCONNECTED with reason=0 (clean).
 * ------------------------------------------------------------------------- */

static void test_ext_fb_msg_bridge_disconnected(void)
{
    uint8_t recv_ep = boot_with_harness_receiver();

    embediq_ext_fb_t *h = embediq_ext_fb_init("disconnect_event_test");
    ASSERT(h != NULL, "init succeeds");

    drain_all(recv_ep);   /* clear the CONNECTED notification */

    embediq_ext_fb_deinit(h);

    EmbedIQ_Msg_t recv;
    bool got = drain_one(recv_ep, &recv);
    ASSERT(got && recv.msg_id == MSG_BRIDGE_EXTFB_DISCONNECTED,
           "deinit publishes MSG_BRIDGE_EXTFB_DISCONNECTED");

    const EmbedIQ_Msg_BridgeExtFbDisconnected_t *payload =
        (const EmbedIQ_Msg_BridgeExtFbDisconnected_t *)recv.payload;
    ASSERT(payload->reason == 0u,
           "DISCONNECTED payload reports clean disconnect (reason=0)");
}

/* ---------------------------------------------------------------------------
 * main
 * ------------------------------------------------------------------------- */

int main(void)
{
    test_ext_fb_pool_init_deinit();
    test_ext_fb_pool_exhaustion();
    test_ext_fb_virtual_endpoint_range();
    test_ext_fb_publish_reaches_subscriber();
    test_ext_fb_subscribe_filter();
    test_ext_fb_source_stamp();
    test_ext_fb_msg_bridge_connected();
    test_ext_fb_msg_bridge_disconnected();

    printf("\nAll %d tests passed. (%d failed)\n",
           g_tests_run - g_tests_failed, g_tests_failed);
    return g_tests_failed > 0 ? 1 : 0;
}
