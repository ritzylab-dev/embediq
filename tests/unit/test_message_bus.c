#define _POSIX_C_SOURCE 200809L

/*
 * tests/unit/test_message_bus.c — Unit tests for core message bus
 *
 * Covers routing, all three overflow policies, endpoint resolution, and
 * Observatory drop events.  Zero external framework — stdlib + OSAL only.
 * Each test exercises exactly one behaviour with one ASSERT.
 *
 * Run:      ./build/tests/unit/test_message_bus
 * Expected: "All 15 tests passed. (0 failed)"
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "embediq_bus.h"
#include "embediq_fb.h"
#include "embediq_msg.h"
#include "embediq_obs.h"
#include "embediq_osal.h"
#include "embediq_config.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

/* ---------------------------------------------------------------------------
 * Test-only API (fb_engine.c + message_bus.c, EMBEDIQ_PLATFORM_HOST)
 * ------------------------------------------------------------------------- */

extern void     fb_engine__reset(void);
extern uint32_t fb_engine__obs_event_count(void);

extern void     message_bus__reset(void);
extern bool     message_bus__test_recv(uint8_t ep_id, uint8_t prio,
                                       EmbedIQ_Msg_t *out);
extern uint32_t message_bus__drop_count(void);

extern bool     obs__ring_read(uint32_t idx, EmbedIQ_Event_t *out);

/* F4 named constants */
#define F4_PUB_THREADS      4u
#define F4_OVERFLOW_EXTRA   3u
#define F4_OVF_ROUNDS       2000u
#define F4_ASYNC_WAIT_MS    50u
#define F4_MSG_OVF          0x1440u  /* community range, unique to this file */
#define F4_MSG_ASYNC_HIGH   0x1441u
#define F4_MSG_ASYNC_NORM   0x1442u

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

/* Helper: reset both engine and bus between tests. */
static void full_reset(void)
{
    fb_engine__reset();
    message_bus__reset();
}

/* Helper: fill a message with a given id, priority, and correlation_id. */
static EmbedIQ_Msg_t make_msg(uint16_t id, uint8_t prio, uint32_t corr)
{
    EmbedIQ_Msg_t m;
    memset(&m, 0, sizeof(m));
    m.msg_id         = id;
    m.priority       = prio;
    m.correlation_id = corr;
    return m;
}

/* ---------------------------------------------------------------------------
 * test_publish_reaches_all_subscribers
 * Two FBs both subscribe to MSG_TEMP.  Publishing one message must enqueue
 * it to both subscribers' NORMAL queues.
 * ------------------------------------------------------------------------- */

#define MSG_TEMP  0x0400u

static void test_publish_reaches_all_subscribers(void)
{
    full_reset();

    EMBEDIQ_SUBS(temp_subs, MSG_TEMP);

    EmbedIQ_FB_Config_t cfg_a = {
        .name = "fb_sub_a", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
        .subscriptions = temp_subs, .subscription_count = 1,
    };
    EmbedIQ_FB_Config_t cfg_b = {
        .name = "fb_sub_b", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
        .subscriptions = temp_subs, .subscription_count = 1,
    };
    EmbedIQ_FB_Config_t cfg_src = {
        .name = "fb_src", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
    };

    embediq_fb_register(&cfg_a);
    embediq_fb_register(&cfg_b);
    EmbedIQ_FB_Handle_t src = embediq_fb_register(&cfg_src);
    embediq_engine_boot();
    message_bus_boot();

    EmbedIQ_Msg_t msg = make_msg(MSG_TEMP, EMBEDIQ_MSG_PRIORITY_NORMAL, 1u);
    embediq_publish(src, &msg);

    uint8_t ep_a = embediq_bus_resolve_name("fb_sub_a");
    uint8_t ep_b = embediq_bus_resolve_name("fb_sub_b");
    EmbedIQ_Msg_t rx_a, rx_b;
    bool got_a = message_bus__test_recv(ep_a, EMBEDIQ_MSG_PRIORITY_NORMAL, &rx_a);
    bool got_b = message_bus__test_recv(ep_b, EMBEDIQ_MSG_PRIORITY_NORMAL, &rx_b);

    ASSERT(got_a && got_b && rx_a.msg_id == MSG_TEMP && rx_b.msg_id == MSG_TEMP,
           "both subscribers must receive the published message");
}

/* ---------------------------------------------------------------------------
 * test_publish_broadcast_reaches_all_endpoints
 * target_endpoint_id = 0xFF (broadcast).  All subscribed FBs must receive.
 * ------------------------------------------------------------------------- */

#define MSG_BCAST 0x0401u

static void test_publish_broadcast_reaches_all_endpoints(void)
{
    full_reset();

    EMBEDIQ_SUBS(bcast_subs, MSG_BCAST);

    EmbedIQ_FB_Config_t cfg_x = {
        .name = "fb_bcast_x", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
        .subscriptions = bcast_subs, .subscription_count = 1,
    };
    EmbedIQ_FB_Config_t cfg_y = {
        .name = "fb_bcast_y", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
        .subscriptions = bcast_subs, .subscription_count = 1,
    };
    EmbedIQ_FB_Config_t cfg_z = {
        .name = "fb_bcast_z", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
        .subscriptions = bcast_subs, .subscription_count = 1,
    };
    EmbedIQ_FB_Config_t cfg_src = {
        .name = "fb_bcast_src", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
    };

    embediq_fb_register(&cfg_x);
    embediq_fb_register(&cfg_y);
    embediq_fb_register(&cfg_z);
    EmbedIQ_FB_Handle_t src = embediq_fb_register(&cfg_src);
    embediq_engine_boot();
    message_bus_boot();

    EmbedIQ_Msg_t msg = make_msg(MSG_BCAST, EMBEDIQ_MSG_PRIORITY_NORMAL, 0u);
    msg.target_endpoint_id = 0xFFu;   /* broadcast */
    embediq_publish(src, &msg);

    uint8_t ex = embediq_bus_resolve_name("fb_bcast_x");
    uint8_t ey = embediq_bus_resolve_name("fb_bcast_y");
    uint8_t ez = embediq_bus_resolve_name("fb_bcast_z");
    EmbedIQ_Msg_t rx;
    bool gx = message_bus__test_recv(ex, EMBEDIQ_MSG_PRIORITY_NORMAL, &rx);
    bool gy = message_bus__test_recv(ey, EMBEDIQ_MSG_PRIORITY_NORMAL, &rx);
    bool gz = message_bus__test_recv(ez, EMBEDIQ_MSG_PRIORITY_NORMAL, &rx);

    ASSERT(gx && gy && gz,
           "broadcast must reach all three subscribed endpoints");
}

/* ---------------------------------------------------------------------------
 * test_high_queue_full_blocks_publisher
 * Fill the HIGH queue to capacity (EMBEDIQ_HIGH_QUEUE_DEPTH = 16), then
 * publish one more.  A drain task unblocks the publisher after ~30 ms.
 * Verify elapsed time >= 20 ms (publisher was blocked, not dropped).
 * ------------------------------------------------------------------------- */

#define MSG_HIGH 0x0402u

typedef struct { uint8_t ep_id; } drain_ctx_t;

static void drain_one_high(void *arg)
{
    drain_ctx_t *ctx = (drain_ctx_t *)arg;
    /* Sleep 30 ms then drain one HIGH message to unblock the publisher. */
    nanosleep(&(struct timespec){0, 30 * 1000000L}, NULL);
    EmbedIQ_Msg_t buf;
    message_bus__test_recv(ctx->ep_id, EMBEDIQ_MSG_PRIORITY_HIGH, &buf);
}

static void test_high_queue_full_blocks_publisher(void)
{
    full_reset();

    EMBEDIQ_SUBS(high_subs, MSG_HIGH);

    EmbedIQ_FB_Config_t cfg_dst = {
        .name = "fb_high_dst", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
        .subscriptions = high_subs, .subscription_count = 1,
    };
    EmbedIQ_FB_Config_t cfg_src = {
        .name = "fb_high_src", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
    };

    embediq_fb_register(&cfg_dst);
    EmbedIQ_FB_Handle_t src = embediq_fb_register(&cfg_src);
    embediq_engine_boot();
    message_bus_boot();

    uint8_t dst_ep = embediq_bus_resolve_name("fb_high_dst");

    /* Fill the HIGH queue to capacity — none of these should block. */
    for (int i = 0; i < EMBEDIQ_HIGH_QUEUE_DEPTH; i++) {
        EmbedIQ_Msg_t m = make_msg(MSG_HIGH, EMBEDIQ_MSG_PRIORITY_HIGH,
                                   (uint32_t)i);
        embediq_publish(src, &m);
    }

    /* Start drain task — will free one slot after 30 ms. */
    static drain_ctx_t ctx;
    ctx.ep_id = dst_ep;
    EmbedIQ_Task_t *drain = embediq_osal_task_create(
            "drain_high", drain_one_high, &ctx, 1, 4096);

    /* This publish must block until the drain task makes space. */
    uint32_t t0 = embediq_osal_time_ms();
    EmbedIQ_Msg_t extra = make_msg(MSG_HIGH, EMBEDIQ_MSG_PRIORITY_HIGH, 0xFFu);
    embediq_publish(src, &extra);
    uint32_t elapsed = embediq_osal_time_ms() - t0;

    embediq_osal_task_delete(drain);

    ASSERT(elapsed >= 20u,
           "HIGH queue full must block publisher until space is available");
}

/* ---------------------------------------------------------------------------
 * test_normal_queue_full_drops_oldest
 * Fill NORMAL queue with EMBEDIQ_NORMAL_QUEUE_DEPTH messages (corr_id 0..31).
 * Publish one more (corr_id 100).  The OLDEST (corr_id 0) must be dropped.
 * Draining the first message must yield corr_id 1, not 0.
 * A drop event must have been emitted.
 * ------------------------------------------------------------------------- */

#define MSG_NORM 0x0403u

static void test_normal_queue_full_drops_oldest(void)
{
    full_reset();

    EMBEDIQ_SUBS(norm_subs, MSG_NORM);

    EmbedIQ_FB_Config_t cfg_dst = {
        .name = "fb_norm_dst", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
        .subscriptions = norm_subs, .subscription_count = 1,
    };
    EmbedIQ_FB_Config_t cfg_src = {
        .name = "fb_norm_src", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
    };

    embediq_fb_register(&cfg_dst);
    EmbedIQ_FB_Handle_t src = embediq_fb_register(&cfg_src);
    embediq_engine_boot();
    message_bus_boot();

    uint8_t dst_ep = embediq_bus_resolve_name("fb_norm_dst");

    /* Fill queue with messages 0..NORMAL_DEPTH-1. */
    for (int i = 0; i < EMBEDIQ_NORMAL_QUEUE_DEPTH; i++) {
        EmbedIQ_Msg_t m = make_msg(MSG_NORM, EMBEDIQ_MSG_PRIORITY_NORMAL,
                                   (uint32_t)i);
        embediq_publish(src, &m);
    }

    /* Publish one more — overflow: oldest (corr_id 0) must be dropped. */
    EmbedIQ_Msg_t overflow = make_msg(MSG_NORM, EMBEDIQ_MSG_PRIORITY_NORMAL, 100u);
    embediq_publish(src, &overflow);

    /* First message drained must be corr_id 1 (not 0). */
    EmbedIQ_Msg_t first;
    message_bus__test_recv(dst_ep, EMBEDIQ_MSG_PRIORITY_NORMAL, &first);

    ASSERT(first.correlation_id == 1u && message_bus__drop_count() == 1u,
           "oldest message must be dropped and drop event emitted");
}

/* ---------------------------------------------------------------------------
 * test_low_queue_full_drops_incoming
 * Fill LOW queue to capacity (corr_id 0..15), then publish one more
 * (corr_id 100).  The NEW message must be dropped.  Queue must still start
 * with corr_id 0.  Drop event must be emitted.
 * ------------------------------------------------------------------------- */

#define MSG_LOW  0x0404u

static void test_low_queue_full_drops_incoming(void)
{
    full_reset();

    EMBEDIQ_SUBS(low_subs, MSG_LOW);

    EmbedIQ_FB_Config_t cfg_dst = {
        .name = "fb_low_dst", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
        .subscriptions = low_subs, .subscription_count = 1,
    };
    EmbedIQ_FB_Config_t cfg_src = {
        .name = "fb_low_src", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
    };

    embediq_fb_register(&cfg_dst);
    EmbedIQ_FB_Handle_t src = embediq_fb_register(&cfg_src);
    embediq_engine_boot();
    message_bus_boot();

    uint8_t dst_ep = embediq_bus_resolve_name("fb_low_dst");

    /* Fill LOW queue to capacity (depth = EMBEDIQ_LOW_QUEUE_DEPTH = 16). */
    for (int i = 0; i < EMBEDIQ_LOW_QUEUE_DEPTH; i++) {
        EmbedIQ_Msg_t m = make_msg(MSG_LOW, EMBEDIQ_MSG_PRIORITY_LOW,
                                   (uint32_t)i);
        embediq_publish(src, &m);
    }

    /* Publish one more — this message must be dropped (LOW policy). */
    EmbedIQ_Msg_t dropped = make_msg(MSG_LOW, EMBEDIQ_MSG_PRIORITY_LOW, 100u);
    embediq_publish(src, &dropped);

    /* First message drained must still be corr_id 0 (original oldest). */
    EmbedIQ_Msg_t first;
    message_bus__test_recv(dst_ep, EMBEDIQ_MSG_PRIORITY_LOW, &first);

    ASSERT(first.correlation_id == 0u && message_bus__drop_count() == 1u,
           "incoming message must be dropped and original queue head preserved");
}

/* ---------------------------------------------------------------------------
 * test_endpoint_registry_resolve_name
 * embediq_bus_resolve_name() and embediq_bus_resolve_id() must be consistent.
 * ------------------------------------------------------------------------- */

static void test_endpoint_registry_resolve_name(void)
{
    full_reset();

    EmbedIQ_FB_Config_t cfg = {
        .name = "fb_resolve_me", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
    };
    embediq_fb_register(&cfg);
    embediq_engine_boot();
    message_bus_boot();

    uint8_t     ep   = embediq_bus_resolve_name("fb_resolve_me");
    const char *name = embediq_bus_resolve_id(ep);

    ASSERT(ep != 0xFFu && name != NULL &&
           strcmp(name, "fb_resolve_me") == 0,
           "resolve_name and resolve_id must round-trip consistently");
}

/* ---------------------------------------------------------------------------
 * test_no_subscriber_message_is_dropped_silently
 * Publishing a message with no registered subscribers must not emit any
 * Observatory event (no drop event, no noise).
 * ------------------------------------------------------------------------- */

#define MSG_UNSUB 0x0405u

static void test_no_subscriber_message_is_dropped_silently(void)
{
    full_reset();

    EmbedIQ_FB_Config_t cfg_src = {
        .name = "fb_unsub_src", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
    };
    EmbedIQ_FB_Handle_t src = embediq_fb_register(&cfg_src);
    embediq_engine_boot();
    message_bus_boot();

    uint32_t obs_before  = fb_engine__obs_event_count();
    uint32_t drop_before = message_bus__drop_count();

    EmbedIQ_Msg_t msg = make_msg(MSG_UNSUB, EMBEDIQ_MSG_PRIORITY_NORMAL, 0u);
    embediq_publish(src, &msg);   /* MSG_UNSUB has no subscribers */

    ASSERT(fb_engine__obs_event_count() == obs_before &&
           message_bus__drop_count()    == drop_before,
           "message with no subscribers must be dropped silently, no events");
}

/* ---------------------------------------------------------------------------
 * test_drop_emits_observatory_event
 * Any queue overflow (NORMAL or LOW) must call embediq_obs_emit() with
 * EMBEDIQ_OBS_EVT_QUEUE_DROP.  Verified via message_bus__drop_count().
 * ------------------------------------------------------------------------- */

#define MSG_DROP_OBS 0x0406u

static void test_drop_emits_observatory_event(void)
{
    full_reset();

    EMBEDIQ_SUBS(drop_subs, MSG_DROP_OBS);

    EmbedIQ_FB_Config_t cfg_dst = {
        .name = "fb_drop_dst", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
        .subscriptions = drop_subs, .subscription_count = 1,
    };
    EmbedIQ_FB_Config_t cfg_src = {
        .name = "fb_drop_src", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
    };

    embediq_fb_register(&cfg_dst);
    EmbedIQ_FB_Handle_t src = embediq_fb_register(&cfg_src);
    embediq_engine_boot();
    message_bus_boot();

    /* Fill LOW queue to capacity. */
    for (int i = 0; i < EMBEDIQ_LOW_QUEUE_DEPTH; i++) {
        EmbedIQ_Msg_t m = make_msg(MSG_DROP_OBS, EMBEDIQ_MSG_PRIORITY_LOW,
                                   (uint32_t)i);
        embediq_publish(src, &m);
    }

    uint32_t drop_before = message_bus__drop_count();

    /* One more LOW message — must be dropped and emit an obs event. */
    EmbedIQ_Msg_t overflow = make_msg(MSG_DROP_OBS, EMBEDIQ_MSG_PRIORITY_LOW, 99u);
    embediq_publish(src, &overflow);

    ASSERT(message_bus__drop_count() == drop_before + 1u,
           "queue overflow must emit an Observatory QUEUE_DROP event");
}

/* ---------------------------------------------------------------------------
 * test_xobs3_queue_depth_constant_and_threshold_exist
 * Verify EMBEDIQ_OBS_EVT_BUS_QUEUE_DEPTH (0x42) and EMBEDIQ_QUEUE_WARN_THRESHOLD
 * exist and have the correct values. Compile-time contract check.
 * ------------------------------------------------------------------------- */

static void test_xobs3_queue_depth_constant_and_threshold_exist(void)
{
    /* These _Static_asserts verify at compile time — the ASSERT below is
     * a runtime confirmation that both constants resolve. */
    _Static_assert(EMBEDIQ_OBS_EVT_BUS_QUEUE_DEPTH == 0x42u,
                   "BUS_QUEUE_DEPTH must be 0x42u (not 0x41 — that is LIB_DEINIT)");
    _Static_assert(EMBEDIQ_QUEUE_WARN_THRESHOLD >= 1u &&
                   EMBEDIQ_QUEUE_WARN_THRESHOLD <= 100u,
                   "threshold must be 1–100");

    ASSERT(EMBEDIQ_OBS_EVT_BUS_QUEUE_DEPTH == 0x42u &&
           EMBEDIQ_QUEUE_WARN_THRESHOLD == 75u,
           "XOBS-3 constants must exist with correct values");
}

/* ---------------------------------------------------------------------------
 * test_xobs3_bus_functions_with_depth_check
 * Verify that publish still works correctly after the BUS_QUEUE_DEPTH_CHECK
 * macro was added to all three priority branches. Floods NORMAL queue to
 * above the threshold — messages must still be delivered and drops still
 * counted. (The RESOURCE emit is ((void)0) at default host trace level.)
 * ------------------------------------------------------------------------- */

#define MSG_DEPTH_TEST 0x0407u

static void test_xobs3_bus_functions_with_depth_check(void)
{
    full_reset();

    EMBEDIQ_SUBS(depth_subs, MSG_DEPTH_TEST);
    EmbedIQ_FB_Config_t cfg_dst = {
        .name = "fb_depth_dst", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
        .subscriptions = depth_subs, .subscription_count = 1,
    };
    EmbedIQ_FB_Config_t cfg_src = {
        .name = "fb_depth_src", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
    };

    embediq_fb_register(&cfg_dst);
    EmbedIQ_FB_Handle_t src = embediq_fb_register(&cfg_src);
    embediq_engine_boot();
    message_bus_boot();

    /* Fill NORMAL queue to 100% capacity — all should succeed (drop-oldest policy) */
    for (int i = 0; i < EMBEDIQ_NORMAL_QUEUE_DEPTH + 4; i++) {
        EmbedIQ_Msg_t m = make_msg(MSG_DEPTH_TEST, EMBEDIQ_MSG_PRIORITY_NORMAL,
                                   (uint32_t)i);
        embediq_publish(src, &m);
    }

    /* Verify: 4 drops counted (overflow handler still works with depth check in place) */
    ASSERT(message_bus__drop_count() == 4u,
           "drop-oldest must still function after XOBS-3 depth check additions");
}

/* ---------------------------------------------------------------------------
 * test_xobs3_all_priority_branches_survive_depth_check
 * Verify HIGH, NORMAL, LOW branches all function with BUS_QUEUE_DEPTH_CHECK.
 * Publish one message to each priority — all must be delivered.
 * ------------------------------------------------------------------------- */

static void test_xobs3_all_priority_branches_survive_depth_check(void)
{
    full_reset();

    EMBEDIQ_SUBS(all_subs, MSG_DEPTH_TEST);
    EmbedIQ_FB_Config_t cfg_dst = {
        .name = "fb_allpri_dst", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
        .subscriptions = all_subs, .subscription_count = 1,
    };
    EmbedIQ_FB_Config_t cfg_src = {
        .name = "fb_allpri_src", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
    };

    embediq_fb_register(&cfg_dst);
    EmbedIQ_FB_Handle_t src = embediq_fb_register(&cfg_src);
    uint8_t dst_ep = embediq_bus_resolve_name("fb_allpri_dst");
    embediq_engine_boot();
    message_bus_boot();

    /* Publish one message to each priority */
    EmbedIQ_Msg_t mh = make_msg(MSG_DEPTH_TEST, EMBEDIQ_MSG_PRIORITY_HIGH, 10u);
    EmbedIQ_Msg_t mn = make_msg(MSG_DEPTH_TEST, EMBEDIQ_MSG_PRIORITY_NORMAL, 20u);
    EmbedIQ_Msg_t ml = make_msg(MSG_DEPTH_TEST, EMBEDIQ_MSG_PRIORITY_LOW, 30u);
    embediq_publish(src, &mh);
    embediq_publish(src, &mn);
    embediq_publish(src, &ml);

    /* Verify: all three messages delivered */
    EmbedIQ_Msg_t rh, rn, rl;
    bool gh = message_bus__test_recv(dst_ep, EMBEDIQ_MSG_PRIORITY_HIGH, &rh);
    bool gn = message_bus__test_recv(dst_ep, EMBEDIQ_MSG_PRIORITY_NORMAL, &rn);
    bool gl = message_bus__test_recv(dst_ep, EMBEDIQ_MSG_PRIORITY_LOW, &rl);

    ASSERT(gh && gn && gl &&
           rh.correlation_id == 10u &&
           rn.correlation_id == 20u &&
           rl.correlation_id == 30u,
           "all three priority branches must deliver with depth check in place");
}

/* ===========================================================================
 * F4 — atomic drop-oldest, embediq_bus_post_async
 * ======================================================================== */

/* Count QUEUE_DROP events for msg_id to ep (prio < 0 = any priority). */
static uint32_t f4_count_drops(uint16_t msg_id, uint8_t ep, int prio)
{
    uint32_t n = 0u;
    EmbedIQ_Event_t e;
    for (uint32_t i = 0u; obs__ring_read(i, &e); i++) {
        if (e.event_type == EMBEDIQ_OBS_EVT_QUEUE_DROP && e.msg_id == msg_id
            && e.target_fb_id == ep && (prio < 0 || e.state_or_flag == (uint8_t)prio)) {
            n++;
        }
    }
    return n;
}

/* ---------------------------------------------------------------------------
 * test_normal_overflow_drop_oldest_atomic
 * Repeated F4_OVF_ROUNDS times on one bus: 4 threads publish NORMAL_DEPTH + 3
 * uniquely tagged messages at once into a NORMAL queue nobody drains. In
 * every round exactly 3 drops are counted and the queue then holds
 * NORMAL_DEPTH distinct messages. With the old three-call drop path a
 * publisher could lose its own message to another publisher between its
 * recv and its re-send — counted as one drop while two messages vanished.
 * ------------------------------------------------------------------------- */

static EmbedIQ_FB_Handle_t     g_f4_src  = NULL;
static embediq_osal_atomic32_t g_f4_go   = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);
static embediq_osal_atomic32_t g_f4_next = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);

static void *f4_ovf_publisher(void *arg)
{
    (void)arg;
    while (embediq_osal_atomic32_load(&g_f4_go) == 0u) { /* start together */ }
    for (;;) {
        uint32_t tag = embediq_osal_atomic32_add(&g_f4_next, 1u);   /* 1-based */
        if (tag > (uint32_t)EMBEDIQ_NORMAL_QUEUE_DEPTH + F4_OVERFLOW_EXTRA) break;
        EmbedIQ_Msg_t m = make_msg(F4_MSG_OVF, EMBEDIQ_MSG_PRIORITY_NORMAL, tag);
        embediq_publish(g_f4_src, &m);
    }
    return NULL;
}

static void test_normal_overflow_drop_oldest_atomic(void)
{
    full_reset();
    EMBEDIQ_SUBS(subs, F4_MSG_OVF);
    EmbedIQ_FB_Config_t cfg_dst = {
        .name = "fb_f4_ovf_dst", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
        .subscriptions = subs, .subscription_count = 1,
    };
    EmbedIQ_FB_Config_t cfg_src = {
        .name = "fb_f4_ovf_src", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
    };
    embediq_fb_register(&cfg_dst);
    g_f4_src = embediq_fb_register(&cfg_src);
    embediq_engine_boot();
    embediq_obs_set_transport(EMBEDIQ_OBS_TRANSPORT_NULL);   /* quiet: 3 drops x rounds */
    uint8_t dst = embediq_bus_resolve_name("fb_f4_ovf_dst");

    uint32_t bad_rounds = 0u, first_bad_held = 0u, first_bad_drops = 0u;
    for (uint32_t r = 0u; r < F4_OVF_ROUNDS; r++) {
        uint32_t drops_before = message_bus__drop_count();
        embediq_osal_atomic32_store(&g_f4_go, 0u);
        embediq_osal_atomic32_store(&g_f4_next, 0u);
        pthread_t th[F4_PUB_THREADS];
        for (uint32_t i = 0u; i < F4_PUB_THREADS; i++) {
            pthread_create(&th[i], NULL, f4_ovf_publisher, NULL);
        }
        embediq_osal_atomic32_store(&g_f4_go, 1u);
        for (uint32_t i = 0u; i < F4_PUB_THREADS; i++) {
            pthread_join(th[i], NULL);
        }

        bool seen[EMBEDIQ_NORMAL_QUEUE_DEPTH + F4_OVERFLOW_EXTRA + 1u];
        memset(seen, 0, sizeof(seen));
        uint32_t held = 0u;
        bool unique = true;
        EmbedIQ_Msg_t rx;
        while (message_bus__test_recv(dst, EMBEDIQ_MSG_PRIORITY_NORMAL, &rx)) {
            uint32_t tag = rx.correlation_id;
            if (tag == 0u || tag >= sizeof(seen) || seen[tag]) unique = false;
            else seen[tag] = true;
            held++;
        }
        uint32_t drops = message_bus__drop_count() - drops_before;
        if (!unique || held != EMBEDIQ_NORMAL_QUEUE_DEPTH || drops != F4_OVERFLOW_EXTRA) {
            if (bad_rounds == 0u) { first_bad_held = held; first_bad_drops = drops; }
            bad_rounds++;
        }
    }
    printf("      rounds=%u bad_rounds=%u (first bad: held=%u drops=%u)\n",
           (unsigned)F4_OVF_ROUNDS, (unsigned)bad_rounds,
           (unsigned)first_bad_held, (unsigned)first_bad_drops);
    ASSERT(bad_rounds == 0u,
           "every concurrent NORMAL overflow round: exactly 3 drops, DEPTH unique msgs held");
}

/* ---------------------------------------------------------------------------
 * test_post_async_never_blocks_on_full_high
 * With the subscriber's HIGH queue full, post_async(HIGH) from a thread
 * returns EMBEDIQ_ERR within 50 ms and records a HIGH QUEUE_DROP.
 * ------------------------------------------------------------------------- */

static embediq_bus_token_t    *g_f4_tok  = NULL;
static embediq_osal_atomic32_t g_f4_done = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);
static int32_t                 g_f4_rc   = 0;

static void *f4_async_high_poster(void *arg)
{
    (void)arg;
    EmbedIQ_Msg_t m = make_msg(F4_MSG_ASYNC_HIGH, EMBEDIQ_MSG_PRIORITY_HIGH, 0xA5u);
    g_f4_rc = embediq_bus_post_async(g_f4_tok, &m);
    embediq_osal_atomic32_store(&g_f4_done, 1u);
    return NULL;
}

static void test_post_async_never_blocks_on_full_high(void)
{
    full_reset();
    EMBEDIQ_SUBS(subs, F4_MSG_ASYNC_HIGH);
    EmbedIQ_FB_Config_t cfg_dst = {
        .name = "fb_f4_hi_dst", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
        .subscriptions = subs, .subscription_count = 1,
    };
    EmbedIQ_FB_Config_t cfg_src = {
        .name = "fb_f4_hi_src", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
    };
    embediq_fb_register(&cfg_dst);
    EmbedIQ_FB_Handle_t src = embediq_fb_register(&cfg_src);
    embediq_engine_boot();
    uint8_t dst = embediq_bus_resolve_name("fb_f4_hi_dst");

    for (uint32_t i = 0u; i < EMBEDIQ_HIGH_QUEUE_DEPTH; i++) {    /* fill HIGH */
        EmbedIQ_Msg_t m = make_msg(F4_MSG_ASYNC_HIGH, EMBEDIQ_MSG_PRIORITY_HIGH, i);
        embediq_publish(src, &m);
    }

    g_f4_tok = embediq_bus_token_for(src);
    embediq_osal_atomic32_store(&g_f4_done, 0u);
    pthread_t th;
    pthread_create(&th, NULL, f4_async_high_poster, NULL);
    uint32_t t0 = embediq_osal_time_ms();
    while (embediq_osal_atomic32_load(&g_f4_done) == 0u
           && (embediq_osal_time_ms() - t0) < F4_ASYNC_WAIT_MS) {
        embediq_osal_delay_ms(1u);
    }
    bool returned = embediq_osal_atomic32_load(&g_f4_done) != 0u;
    if (!returned) {   /* it blocked — drain so the thread can finish, then fail */
        EmbedIQ_Msg_t rx;
        while (message_bus__test_recv(dst, EMBEDIQ_MSG_PRIORITY_HIGH, &rx)) { }
    }
    pthread_join(th, NULL);

    uint32_t hi_drops = f4_count_drops(F4_MSG_ASYNC_HIGH, dst, EMBEDIQ_MSG_PRIORITY_HIGH);
    printf("      returned=%d rc=%d high_drops=%u\n", (int)returned, (int)g_f4_rc,
           (unsigned)hi_drops);
    ASSERT(returned && g_f4_rc == EMBEDIQ_ERR && hi_drops == 1u,
           "post_async on a full HIGH queue returns EMBEDIQ_ERR at once and records a drop");
}

/* ---------------------------------------------------------------------------
 * test_post_async_delivers_like_publish
 * A NORMAL post_async reaches the subscriber, stamped with the token's FB.
 * ------------------------------------------------------------------------- */

static void test_post_async_delivers_like_publish(void)
{
    full_reset();
    EMBEDIQ_SUBS(subs, F4_MSG_ASYNC_NORM);
    EmbedIQ_FB_Config_t cfg_dst = {
        .name = "fb_f4_nm_dst", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
        .subscriptions = subs, .subscription_count = 1,
    };
    EmbedIQ_FB_Config_t cfg_src = {
        .name = "fb_f4_nm_src", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
    };
    embediq_fb_register(&cfg_dst);
    EmbedIQ_FB_Handle_t src = embediq_fb_register(&cfg_src);
    embediq_engine_boot();
    uint8_t dst    = embediq_bus_resolve_name("fb_f4_nm_dst");
    uint8_t src_ep = embediq_bus_resolve_name("fb_f4_nm_src");

    EmbedIQ_Msg_t m = make_msg(F4_MSG_ASYNC_NORM, EMBEDIQ_MSG_PRIORITY_NORMAL, 0x5Au);
    m.source_endpoint_id = 0x77u;   /* must be overwritten from the token */
    int32_t rc = embediq_bus_post_async(embediq_bus_token_for(src), &m);

    EmbedIQ_Msg_t rx;
    memset(&rx, 0, sizeof(rx));
    bool got = message_bus__test_recv(dst, EMBEDIQ_MSG_PRIORITY_NORMAL, &rx);
    ASSERT(rc == EMBEDIQ_OK && got && rx.correlation_id == 0x5Au
           && rx.source_endpoint_id == src_ep,
           "post_async NORMAL delivers with source_endpoint_id from the token");
}

/* ---------------------------------------------------------------------------
 * test_token_for_is_stable_and_static
 * Same FB → same token pointer; different FBs → different; NULL → NULL.
 * ------------------------------------------------------------------------- */

static void test_token_for_is_stable_and_static(void)
{
    full_reset();
    EmbedIQ_FB_Config_t cfg_a = {
        .name = "fb_f4_tok_a", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
    };
    EmbedIQ_FB_Config_t cfg_b = {
        .name = "fb_f4_tok_b", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
    };
    EmbedIQ_FB_Handle_t a = embediq_fb_register(&cfg_a);
    EmbedIQ_FB_Handle_t b = embediq_fb_register(&cfg_b);
    embediq_engine_boot();

    embediq_bus_token_t *ta1 = embediq_bus_token_for(a);
    embediq_bus_token_t *ta2 = embediq_bus_token_for(a);
    embediq_bus_token_t *tb  = embediq_bus_token_for(b);
    ASSERT(ta1 != NULL && ta1 == ta2 && tb != NULL && tb != ta1
           && embediq_bus_token_for(NULL) == NULL,
           "token_for: stable per FB, distinct across FBs, NULL for NULL");
}

/* ---------------------------------------------------------------------------
 * Entry point
 * ------------------------------------------------------------------------- */

int main(void)
{
    test_publish_reaches_all_subscribers();
    test_publish_broadcast_reaches_all_endpoints();
    test_high_queue_full_blocks_publisher();
    test_normal_queue_full_drops_oldest();
    test_low_queue_full_drops_incoming();
    test_endpoint_registry_resolve_name();
    test_no_subscriber_message_is_dropped_silently();
    test_drop_emits_observatory_event();
    test_xobs3_queue_depth_constant_and_threshold_exist();
    test_xobs3_bus_functions_with_depth_check();
    test_xobs3_all_priority_branches_survive_depth_check();
    test_normal_overflow_drop_oldest_atomic();
    test_post_async_never_blocks_on_full_high();
    test_post_async_delivers_like_publish();
    test_token_for_is_stable_and_static();

    printf("\nAll %d tests passed. (%d failed)\n",
           g_tests_run - g_tests_failed, g_tests_failed);
    return g_tests_failed > 0 ? 1 : 0;
}
