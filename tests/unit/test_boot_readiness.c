#define _POSIX_C_SOURCE 200809L

/*
 * tests/unit/test_boot_readiness.c — boot prepares every required resource
 * before any initializer, or fails cleanly and can be retried
 *
 * Fixture: three FBs — A and B subscribe (one message each), C subscribes to
 * nothing — and one platform library. Boot creates, in order: the three
 * execution locks, the dispatch semaphores of A and B, then the HIGH, NORMAL
 * and LOW queue of A, B and C (nine queues); the bus becomes ready last.
 *
 * Fault-injection matrix — each of the nine queue allocations, each of the
 * two semaphore allocations (B's after A's succeeded), and each of the three
 * execution locks fails once. For every row:
 *   - boot returns -1; no platform-library initializer and no init_fn runs;
 *     every FB stays IDLE; no dispatch semaphore remains, and dispatch_boot
 *     starts nothing;
 *   - one EMBEDIQ_FAULT_BOOT_RESOURCE event names the FB and the resource;
 *   - every queue, semaphore and execution lock created in the attempt is
 *     destroyed exactly once (OSAL create/destroy counts);
 *   - an async HIGH post returns EMBEDIQ_ERR, a publish is refused, both are
 *     observed as EMBEDIQ_FAULT_BUS_NOT_READY, and no queue drop is counted;
 *   - with the injection removed, boot succeeds without a reset: every init
 *     runs once, every FB is RUNNING, the queues are fresh, and HIGH, NORMAL
 *     and LOW messages reach their subscribers' dispatch threads; the failed
 *     attempt and the retry together leave exactly one boot's resources.
 * Publisher concurrent with boot: a thread started before boot posts
 * continuously. While a semaphore or queue allocation fails, not one post is
 * acknowledged; when boot succeeds, every acknowledged post is delivered.
 * On a bus that is not ready, a bridge route is refused and observed; the
 * legacy message_bus_boot() leaves the bus not ready after a queue failure
 * and makes it ready on the next call.
 * Also: a successful boot called again creates nothing; a boot that fails
 * late (missing dependency) keeps the prepared resources, and its retry
 * reuses the same semaphore and creates or destroys nothing; on a ready bus
 * a message nobody subscribes to is still posted with EMBEDIQ_OK.
 *
 * Run:      ./build/tests/unit/test_boot_readiness
 * Expected: "All N checks passed. (0 failed)"
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
#include "embediq_obs.h"
#include "embediq_osal.h"
#include "embediq_platform.h"

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern void          fb_engine__reset(void);
extern embediq_sem_t fb_engine__dispatch_sem(uint8_t ep_id);
extern bool          obs__ring_read(uint32_t idx, EmbedIQ_Event_t *out);
extern void          obs__reset(void);
extern bool          message_bus__test_recv(uint8_t ep_id, uint8_t prio, EmbedIQ_Msg_t *out);
extern uint32_t      message_bus__drop_count(void);
extern void          embediq_platform_lib_reset(void);
extern void          embediq_osal__fail_next_mutex_create(uint32_t nth);
extern void          embediq_osal__fail_next_queue_create(uint32_t nth);
extern void          embediq_osal__fail_next_sem_create(uint32_t nth);
extern void          embediq_osal__queue_counts(uint32_t *created, uint32_t *destroyed);
extern void          embediq_osal__sem_counts(uint32_t *created, uint32_t *destroyed);
extern void          embediq_osal__mutex_counts(uint32_t *created, uint32_t *destroyed);

#define MSG_BOOT_A      0x1440u   /* community range, unique to this file */
#define MSG_BOOT_B      0x1441u
#define MSG_BOOT_NOBODY 0x1442u   /* no FB subscribes */

#define FIXTURE_FBS         3u    /* A, B, C */
#define FIXTURE_SUBSCRIBERS 2u    /* A, B */
#define QUEUES_PER_FB       3u    /* HIGH, NORMAL, LOW */
#define FIXTURE_QUEUES      (FIXTURE_FBS * QUEUES_PER_FB)
#define FIXTURE_LOCKS       FIXTURE_FBS   /* one execution lock per FB */

/* Boots per injected failure (and successful boots) with a concurrent
 * publisher: each boot is a fresh chance for the publisher to land inside the
 * window between readiness and a failure; 200 makes an open window show up
 * reliably while keeping the sanitizer builds fast. */
#define CONCURRENT_BOOTS    200u
#define VIRTUAL_EP          0x40u  /* first External FB virtual endpoint */

/* Hang guard for delivery waits: a dispatch thread wakes in microseconds. */
#define DELIVERY_WAIT_MS    2000u

static int g_checks = 0;
static int g_failed = 0;

#define CHECK(cond, msg)  do {                                                  \
    g_checks++;                                                                 \
    if (!(cond)) {                                                              \
        fprintf(stderr, "FAIL  [%s] %s  %s:%d\n", g_row, (msg), __FILE__, __LINE__); \
        g_failed++;                                                             \
    } else {                                                                    \
        printf("PASS  [%s] %s\n", g_row, (msg));                                \
    }                                                                           \
} while (0)

/* Name of the case being checked. The longest is "execution lock B" (16
 * characters); 40 leaves room without truncation. */
#define ROW_NAME_MAX  40u
static char g_row[ROW_NAME_MAX];

/* ---------------------------------------------------------------------------
 * Fixture
 * ------------------------------------------------------------------------- */

static embediq_osal_atomic32_t g_inits    = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);
static embediq_osal_atomic32_t g_lib_init = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);
static embediq_osal_atomic32_t g_rx_a     = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);
static embediq_osal_atomic32_t g_rx_b     = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);

/* MSG_BOOT_A / MSG_BOOT_B carry no payload: msg is not read. */
static void rx_a(EmbedIQ_FB_Handle_t fb, const void *msg, void *d, void *s)
{
    (void)fb; (void)msg; (void)d; (void)s;
    (void)embediq_osal_atomic32_add(&g_rx_a, 1u);
}

static void rx_b(EmbedIQ_FB_Handle_t fb, const void *msg, void *d, void *s)
{
    (void)fb; (void)msg; (void)d; (void)s;
    (void)embediq_osal_atomic32_add(&g_rx_b, 1u);
}

EMBEDIQ_SUBS(g_subs_a, MSG_BOOT_A);
EMBEDIQ_SUBS(g_subs_b, MSG_BOOT_B);

static const EmbedIQ_SubFn_Config_t k_sf_a = {
    .name = "boot_rx_a", .init_order = 0,
    .subscriptions = g_subs_a, .subscription_count = 1, .run_fn = rx_a,
};
static const EmbedIQ_SubFn_Config_t k_sf_b = {
    .name = "boot_rx_b", .init_order = 0,
    .subscriptions = g_subs_b, .subscription_count = 1, .run_fn = rx_b,
};

static void init_a(EmbedIQ_FB_Handle_t h, void *d)
{
    (void)d;
    (void)embediq_osal_atomic32_add(&g_inits, 1u);
    (void)embediq_subfn_register(h, &k_sf_a);
}

static void init_b(EmbedIQ_FB_Handle_t h, void *d)
{
    (void)d;
    (void)embediq_osal_atomic32_add(&g_inits, 1u);
    (void)embediq_subfn_register(h, &k_sf_b);
}

static void init_c(EmbedIQ_FB_Handle_t h, void *d)
{
    (void)h; (void)d;
    (void)embediq_osal_atomic32_add(&g_inits, 1u);
}

static void lib_init(void) { (void)embediq_osal_atomic32_add(&g_lib_init, 1u); }

static const EmbedIQ_FB_Config_t k_a = {
    .name = "boot_a", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION, .init_fn = init_a,
    .subscriptions = g_subs_a, .subscription_count = 1,
};
static const EmbedIQ_FB_Config_t k_b = {
    .name = "boot_b", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION, .init_fn = init_b,
    .subscriptions = g_subs_b, .subscription_count = 1,
};
static const EmbedIQ_FB_Config_t k_c = {
    .name = "boot_c", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION, .init_fn = init_c,
};

static EmbedIQ_FB_Handle_t g_h[FIXTURE_FBS];

static void fixture(void)
{
    fb_engine__reset();
    embediq_platform_lib_reset();
    obs__reset();
    embediq_obs_set_transport(EMBEDIQ_OBS_TRANSPORT_TCP);   /* ring only, no output */
    embediq_osal_atomic32_store(&g_inits, 0u);
    embediq_osal_atomic32_store(&g_lib_init, 0u);
    embediq_osal_atomic32_store(&g_rx_a, 0u);
    embediq_osal_atomic32_store(&g_rx_b, 0u);
    g_h[0] = embediq_fb_register(&k_a);
    g_h[1] = embediq_fb_register(&k_b);
    g_h[2] = embediq_fb_register(&k_c);
    (void)embediq_platform_lib_declare(lib_init, NULL);
}

/* ---------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */

/* FAULT events with state_or_flag == state (and target/msg_id when >= 0). */
static uint32_t fault_events(uint8_t state, int target, int msg_id)
{
    uint32_t n = 0u;
    EmbedIQ_Event_t e;
    for (uint32_t i = 0u; obs__ring_read(i, &e); i++) {
        if (e.event_type != EMBEDIQ_OBS_EVT_FAULT || e.state_or_flag != state) continue;
        if (target >= 0 && e.target_fb_id != (uint8_t)target) continue;
        if (msg_id >= 0 && e.msg_id != (uint16_t)msg_id) continue;
        n++;
    }
    return n;
}

static EmbedIQ_Msg_t make_msg(uint16_t id, uint8_t prio)
{
    EmbedIQ_Msg_t m;
    memset(&m, 0, sizeof(m));
    m.msg_id   = id;
    m.priority = prio;
    return m;
}

typedef struct {
    uint32_t q_c, q_d, s_c, s_d, m_c, m_d;   /* queues, semaphores, mutexes */
} counts_t;

static counts_t counts_now(void)
{
    counts_t c;
    embediq_osal__queue_counts(&c.q_c, &c.q_d);
    embediq_osal__sem_counts(&c.s_c, &c.s_d);
    embediq_osal__mutex_counts(&c.m_c, &c.m_d);
    return c;
}

static bool wait_rx(embediq_osal_atomic32_t *counter, uint32_t want)
{
    uint32_t t0 = embediq_osal_time_ms();
    while (embediq_osal_atomic32_load(counter) < want) {
        if (embediq_osal_time_ms() - t0 >= DELIVERY_WAIT_MS) return false;
        embediq_osal_delay_ms(1u);
    }
    return true;
}

typedef enum { INJECT_LOCK, INJECT_QUEUE, INJECT_SEM } inject_t;

/* The failed boot leaves nothing behind that could look like a message path. */
static void check_failed_boot(int rc, uint8_t fail_ep, uint16_t resource, counts_t c0)
{
    CHECK(rc == -1, "boot returns -1");
    CHECK(embediq_osal_atomic32_load(&g_lib_init) == 0u, "no platform-library initializer ran");
    CHECK(embediq_osal_atomic32_load(&g_inits) == 0u, "no init_fn ran");
    bool idle = true;
    for (uint8_t i = 0u; i < FIXTURE_FBS; i++) {
        if (embediq_fb_get_state(g_h[i]) != EMBEDIQ_FB_STATE_IDLE) idle = false;
    }
    CHECK(idle, "every FB stays IDLE");
    CHECK(fault_events((uint8_t)EMBEDIQ_FAULT_BOOT_RESOURCE, fail_ep, resource) == 1u
          && fault_events((uint8_t)EMBEDIQ_FAULT_BOOT_RESOURCE, -1, -1) == 1u,
          "one BOOT_RESOURCE event names the FB and the resource");

    counts_t c = counts_now();
    CHECK(c.q_c - c0.q_c == c.q_d - c0.q_d, "every queue created in the attempt is destroyed exactly once");
    CHECK(c.s_c - c0.s_c == c.s_d - c0.s_d, "every semaphore created in the attempt is destroyed exactly once");
    CHECK(c.m_c - c0.m_c == c.m_d - c0.m_d,
          "every execution lock created in the attempt is destroyed exactly once");

    bool no_sem = true;
    for (uint8_t i = 0u; i < FIXTURE_FBS; i++) {
        if (fb_engine__dispatch_sem(embediq_engine_fb_ep(g_h[i])) != NULL) no_sem = false;
    }
    embediq_engine_dispatch_boot();   /* must start nothing after a failed boot */
    for (uint8_t i = 0u; i < FIXTURE_FBS; i++) {
        if (fb_engine__dispatch_sem(embediq_engine_fb_ep(g_h[i])) != NULL) no_sem = false;
    }
    CHECK(no_sem, "no dispatch semaphore remains and dispatch_boot starts nothing");

    /* An async HIGH post is not acknowledged; a publish is refused. */
    uint32_t drops = message_bus__drop_count();
    EmbedIQ_Msg_t hi = make_msg(MSG_BOOT_A, (uint8_t)EMBEDIQ_MSG_PRIORITY_HIGH);
    int32_t post = embediq_bus_post_async(embediq_bus_token_for(g_h[2]), &hi);
    EmbedIQ_Msg_t lo = make_msg(MSG_BOOT_B, (uint8_t)EMBEDIQ_MSG_PRIORITY_LOW);
    embediq_publish(g_h[2], &lo);
    EmbedIQ_Msg_t rx;
    bool queued = message_bus__test_recv(embediq_engine_fb_ep(g_h[0]),
                                         (uint8_t)EMBEDIQ_MSG_PRIORITY_HIGH, &rx);
    CHECK(post == EMBEDIQ_ERR && !queued, "an async HIGH post returns EMBEDIQ_ERR and queues nothing");
    CHECK(fault_events((uint8_t)EMBEDIQ_FAULT_BUS_NOT_READY, -1, MSG_BOOT_A) == 1u
          && fault_events((uint8_t)EMBEDIQ_FAULT_BUS_NOT_READY, -1, MSG_BOOT_B) == 1u,
          "the post and the publish are observed as BUS_NOT_READY");
    CHECK(message_bus__drop_count() == drops, "no queue drop is counted");
}

/* Recovery without a reset: the same registrations boot and deliver. */
static void check_recovery(void)
{
    uint32_t q_c0, q_d0, s_c0, s_d0;
    embediq_osal__queue_counts(&q_c0, &q_d0);
    embediq_osal__sem_counts(&s_c0, &s_d0);
    int rc = embediq_engine_boot();
    uint32_t q_c, q_d, s_c, s_d;
    embediq_osal__queue_counts(&q_c, &q_d);
    embediq_osal__sem_counts(&s_c, &s_d);

    CHECK(rc == 0, "recovery: boot succeeds without a reset");
    CHECK(embediq_osal_atomic32_load(&g_lib_init) == 1u
          && embediq_osal_atomic32_load(&g_inits) == FIXTURE_FBS,
          "recovery: the library and every init_fn run once");
    bool running = true;
    for (uint8_t i = 0u; i < FIXTURE_FBS; i++) {
        if (embediq_fb_get_state(g_h[i]) != EMBEDIQ_FB_STATE_RUNNING) running = false;
    }
    CHECK(running, "recovery: every FB is RUNNING");
    CHECK(q_c - q_c0 == FIXTURE_QUEUES && q_d == q_d0
          && s_c - s_c0 == FIXTURE_SUBSCRIBERS && s_d == s_d0,
          "recovery: all queues and semaphores are created afresh, none destroyed");

    embediq_engine_dispatch_boot();
    EmbedIQ_Msg_t hi  = make_msg(MSG_BOOT_A, (uint8_t)EMBEDIQ_MSG_PRIORITY_HIGH);
    EmbedIQ_Msg_t lo  = make_msg(MSG_BOOT_A, (uint8_t)EMBEDIQ_MSG_PRIORITY_LOW);
    EmbedIQ_Msg_t nrm = make_msg(MSG_BOOT_B, (uint8_t)EMBEDIQ_MSG_PRIORITY_NORMAL);
    int32_t post = embediq_bus_post_async(embediq_bus_token_for(g_h[2]), &hi);
    embediq_publish(g_h[2], &lo);
    embediq_publish(g_h[2], &nrm);
    CHECK(post == EMBEDIQ_OK, "recovery: an async HIGH post is acknowledged");
    CHECK(wait_rx(&g_rx_a, 2u) && wait_rx(&g_rx_b, 1u),
          "recovery: HIGH, LOW and NORMAL reach the subscribers' dispatch threads");
    EmbedIQ_Msg_t nobody = make_msg(MSG_BOOT_NOBODY, (uint8_t)EMBEDIQ_MSG_PRIORITY_HIGH);
    CHECK(embediq_bus_post_async(embediq_bus_token_for(g_h[2]), &nobody) == EMBEDIQ_OK
          && fault_events((uint8_t)EMBEDIQ_FAULT_BUS_NOT_READY, -1, MSG_BOOT_NOBODY) == 0u,
          "recovery: a message nobody subscribes to is still EMBEDIQ_OK");
    (void)embediq_engine_dispatch_shutdown();
}

static void run_row(inject_t kind, uint32_t nth, uint8_t fail_ep, uint16_t resource)
{
    fixture();
    counts_t c0 = counts_now();

    switch (kind) {
    case INJECT_LOCK:  embediq_osal__fail_next_mutex_create(nth); break;
    case INJECT_QUEUE: embediq_osal__fail_next_queue_create(nth); break;
    case INJECT_SEM:   embediq_osal__fail_next_sem_create(nth);   break;
    }
    int rc = embediq_engine_boot();
    embediq_osal__fail_next_mutex_create(0u);   /* disarm all */
    embediq_osal__fail_next_queue_create(0u);
    embediq_osal__fail_next_sem_create(0u);

    check_failed_boot(rc, fail_ep, resource, c0);
    check_recovery();

    counts_t c = counts_now();
    CHECK(c.q_c - c.q_d - (c0.q_c - c0.q_d) == FIXTURE_QUEUES
          && c.s_c - c.s_d - (c0.s_c - c0.s_d) == FIXTURE_SUBSCRIBERS
          && c.m_c - c.m_d - (c0.m_c - c0.m_d) == FIXTURE_LOCKS,
          "failed attempt plus retry hold exactly one boot's queues, semaphores and locks");
}

/* ---------------------------------------------------------------------------
 * Cases
 * ------------------------------------------------------------------------- */

static void test_fault_injection_matrix(void)
{
    static const char *const k_prio[QUEUES_PER_FB] = { "HIGH", "NORMAL", "LOW" };
    static const char k_fb[FIXTURE_FBS] = { 'A', 'B', 'C' };

    /* Queues are created per FB in registry order, HIGH, NORMAL, LOW. */
    for (uint32_t nth = 1u; nth <= FIXTURE_QUEUES; nth++) {
        uint8_t ep   = (uint8_t)((nth - 1u) / QUEUES_PER_FB);
        uint8_t prio = (uint8_t)((nth - 1u) % QUEUES_PER_FB);
        snprintf(g_row, sizeof(g_row), "queue %c %s", k_fb[ep], k_prio[prio]);
        run_row(INJECT_QUEUE, nth, ep,
                (uint16_t)(EMBEDIQ_BOOT_RESOURCE_QUEUE_HIGH + prio));
    }
    /* Semaphores: A's, then B's after A's succeeded. */
    for (uint32_t nth = 1u; nth <= FIXTURE_SUBSCRIBERS; nth++) {
        uint8_t ep = (uint8_t)(nth - 1u);
        snprintf(g_row, sizeof(g_row), "semaphore %c", k_fb[ep]);
        run_row(INJECT_SEM, nth, ep, (uint16_t)EMBEDIQ_BOOT_RESOURCE_DISPATCH_SEM);
    }
    /* Execution locks: the first, the middle and the last FB's. */
    for (uint32_t nth = 1u; nth <= FIXTURE_LOCKS; nth++) {
        uint8_t ep = (uint8_t)(nth - 1u);
        snprintf(g_row, sizeof(g_row), "execution lock %c", k_fb[ep]);
        run_row(INJECT_LOCK, nth, ep, (uint16_t)EMBEDIQ_BOOT_RESOURCE_EXEC_LOCK);
    }
}

/* ---------------------------------------------------------------------------
 * Publisher concurrent with boot
 * ------------------------------------------------------------------------- */

static embediq_osal_atomic32_t g_pub_stop     = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);
static embediq_osal_atomic32_t g_pub_attempts = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);
static embediq_osal_atomic32_t g_pub_ok       = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);
static embediq_bus_token_t    *g_pub_token;   /* written before the thread starts */

/* Posts HIGH to A until stopped: with HIGH, EMBEDIQ_OK means queued (a full
 * HIGH queue returns EMBEDIQ_ERR), so every acknowledged post is countable. */
static void *publisher(void *arg)
{
    (void)arg;
    EmbedIQ_Msg_t m = make_msg(MSG_BOOT_A, (uint8_t)EMBEDIQ_MSG_PRIORITY_HIGH);
    while (embediq_osal_atomic32_load(&g_pub_stop) == 0u) {
        if (embediq_bus_post_async(g_pub_token, &m) == EMBEDIQ_OK) {
            (void)embediq_osal_atomic32_add(&g_pub_ok, 1u);
        }
        (void)embediq_osal_atomic32_add(&g_pub_attempts, 1u);
    }
    return NULL;
}

/* Start the publisher on a fresh fixture and wait until it is posting. */
static pthread_t publisher_start(void)
{
    embediq_osal_atomic32_store(&g_pub_stop, 0u);
    embediq_osal_atomic32_store(&g_pub_attempts, 0u);
    embediq_osal_atomic32_store(&g_pub_ok, 0u);
    g_pub_token = embediq_bus_token_for(g_h[2]);
    pthread_t t;
    (void)pthread_create(&t, NULL, publisher, NULL);
    while (embediq_osal_atomic32_load(&g_pub_attempts) == 0u) { /* posting before boot */ }
    return t;
}

static uint32_t publisher_stop(pthread_t t)
{
    embediq_osal_atomic32_store(&g_pub_stop, 1u);
    (void)pthread_join(t, NULL);
    return embediq_osal_atomic32_load(&g_pub_ok);
}

static void concurrent_failing_boots(inject_t kind, uint32_t nth)
{
    uint32_t acked = 0u, attempts_acked = 0u, not_failed = 0u;
    for (uint32_t i = 0u; i < CONCURRENT_BOOTS; i++) {
        fixture();
        pthread_t t = publisher_start();
        if (kind == INJECT_SEM) embediq_osal__fail_next_sem_create(nth);
        else                    embediq_osal__fail_next_queue_create(nth);
        int rc = embediq_engine_boot();
        embediq_osal__fail_next_sem_create(0u);
        embediq_osal__fail_next_queue_create(0u);
        uint32_t ok = publisher_stop(t);
        if (rc != -1) not_failed++;
        if (ok > 0u) { attempts_acked++; acked += ok; }
    }
    printf("      %s: boots=%u not_failed=%u attempts_with_acknowledged_posts=%u "
           "acknowledged_posts=%u\n", g_row, (unsigned)CONCURRENT_BOOTS,
           (unsigned)not_failed, (unsigned)attempts_acked, (unsigned)acked);
    CHECK(not_failed == 0u, "every boot with the injected failure returns -1");
    CHECK(acked == 0u, "no post is acknowledged during a failed boot");
}

static void test_publisher_concurrent_with_failing_boot(void)
{
    for (uint32_t nth = 1u; nth <= FIXTURE_SUBSCRIBERS; nth++) {
        snprintf(g_row, sizeof(g_row), "concurrent, semaphore %u fails", (unsigned)nth);
        concurrent_failing_boots(INJECT_SEM, nth);
    }
    for (uint32_t nth = 1u; nth <= FIXTURE_QUEUES; nth++) {
        snprintf(g_row, sizeof(g_row), "concurrent, queue %u fails", (unsigned)nth);
        concurrent_failing_boots(INJECT_QUEUE, nth);
    }
}

static void test_publisher_concurrent_with_successful_boot(void)
{
    snprintf(g_row, sizeof(g_row), "concurrent, boot succeeds");
    uint32_t failed_boots = 0u, lost = 0u, acked_total = 0u;
    uint8_t  ep_a = 0u;
    for (uint32_t i = 0u; i < CONCURRENT_BOOTS; i++) {
        fixture();
        ep_a = embediq_engine_fb_ep(g_h[0]);
        pthread_t t = publisher_start();
        if (embediq_engine_boot() != 0) failed_boots++;
        uint32_t ok = publisher_stop(t);
        uint32_t got = 0u;
        EmbedIQ_Msg_t rx;
        while (message_bus__test_recv(ep_a, (uint8_t)EMBEDIQ_MSG_PRIORITY_HIGH, &rx)) got++;
        if (got != ok) lost++;
        acked_total += ok;
    }
    printf("      %s: boots=%u failed=%u boots_with_acknowledged_but_undelivered=%u "
           "acknowledged_posts=%u\n", g_row, (unsigned)CONCURRENT_BOOTS,
           (unsigned)failed_boots, (unsigned)lost, (unsigned)acked_total);
    CHECK(failed_boots == 0u, "every boot succeeds");
    CHECK(lost == 0u, "every acknowledged post is delivered, none acknowledged early");
    CHECK(acked_total > 0u, "the publisher posts after the bus becomes ready");
}

/* ---------------------------------------------------------------------------
 * Unready bus: bridge route and the legacy wrapper
 * ------------------------------------------------------------------------- */

static void test_bridge_route_refused_while_not_ready(void)
{
    snprintf(g_row, sizeof(g_row), "bridge route, not ready");
    fixture();
    uint32_t drops = message_bus__drop_count();
    EmbedIQ_Msg_t m = make_msg(MSG_BOOT_A, (uint8_t)EMBEDIQ_MSG_PRIORITY_NORMAL);
    CHECK(embediq_bus_bridge_route(VIRTUAL_EP, &m) == EMBEDIQ_ERR, "returns EMBEDIQ_ERR");
    CHECK(fault_events((uint8_t)EMBEDIQ_FAULT_BUS_NOT_READY, -1, MSG_BOOT_A) == 1u,
          "one BUS_NOT_READY event is observed");
    CHECK(message_bus__drop_count() == drops, "no queue drop is counted");
}

static void test_legacy_bus_boot_after_queue_failure(void)
{
    snprintf(g_row, sizeof(g_row), "legacy message_bus_boot");
    fixture();
    counts_t c0 = counts_now();
    embediq_osal__fail_next_queue_create(2u);
    message_bus_boot();
    embediq_osal__fail_next_queue_create(0u);
    counts_t c = counts_now();
    EmbedIQ_Msg_t hi = make_msg(MSG_BOOT_A, (uint8_t)EMBEDIQ_MSG_PRIORITY_HIGH);
    CHECK(embediq_bus_post_async(embediq_bus_token_for(g_h[2]), &hi) == EMBEDIQ_ERR,
          "after a queue failure the bus is not ready: a post is refused");
    CHECK(c.q_c - c0.q_c == c.q_d - c0.q_d, "every queue it created is destroyed");
    CHECK(fault_events((uint8_t)EMBEDIQ_FAULT_BOOT_RESOURCE, -1, -1) == 1u,
          "exactly one BOOT_RESOURCE event");

    message_bus_boot();
    EmbedIQ_Msg_t rx;
    CHECK(embediq_bus_post_async(embediq_bus_token_for(g_h[2]), &hi) == EMBEDIQ_OK
          && message_bus__test_recv(embediq_engine_fb_ep(g_h[0]),
                                    (uint8_t)EMBEDIQ_MSG_PRIORITY_HIGH, &rx)
          && rx.msg_id == MSG_BOOT_A,
          "a second call makes the bus ready and a post is delivered");
}

static void test_successful_boot_is_idempotent(void)
{
    snprintf(g_row, sizeof(g_row), "idempotent");
    fixture();
    CHECK(embediq_engine_boot() == 0, "first boot succeeds");
    uint32_t q_c0, q_d0, s_c0, s_d0;
    embediq_osal__queue_counts(&q_c0, &q_d0);
    embediq_osal__sem_counts(&s_c0, &s_d0);
    CHECK(embediq_engine_boot() == 0, "second boot returns 0");
    uint32_t q_c, q_d, s_c, s_d;
    embediq_osal__queue_counts(&q_c, &q_d);
    embediq_osal__sem_counts(&s_c, &s_d);
    CHECK(q_c == q_c0 && q_d == q_d0 && s_c == s_c0 && s_d == s_d0,
          "the second boot creates and destroys nothing");
    CHECK(embediq_osal_atomic32_load(&g_inits) == FIXTURE_FBS
          && embediq_osal_atomic32_load(&g_lib_init) == 1u,
          "no initializer runs twice");
}

/* A late failure (missing dependency, after the libraries ran) keeps the
 * prepared resources; its retry reuses them. */
static const char *const k_missing_dep[] = { "boot_absent" };
static const EmbedIQ_FB_Config_t k_d = {
    .name = "boot_d", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
    .subscriptions = g_subs_b, .subscription_count = 1,
    .depends_on = k_missing_dep, .depends_count = 1,
};

static void test_late_failure_keeps_resources_for_retry(void)
{
    snprintf(g_row, sizeof(g_row), "late failure");
    fixture();
    EmbedIQ_FB_Handle_t hd = embediq_fb_register(&k_d);
    uint8_t ep_d = embediq_engine_fb_ep(hd);

    CHECK(embediq_engine_boot() == -1, "boot fails on the missing dependency");
    embediq_sem_t sem = fb_engine__dispatch_sem(ep_d);
    uint32_t q_c0, q_d0, s_c0, s_d0;
    embediq_osal__queue_counts(&q_c0, &q_d0);
    embediq_osal__sem_counts(&s_c0, &s_d0);
    CHECK(sem != NULL, "the prepared semaphore is kept");

    CHECK(embediq_engine_boot() == -1, "the retry fails the same way");
    uint32_t q_c, q_d, s_c, s_d;
    embediq_osal__queue_counts(&q_c, &q_d);
    embediq_osal__sem_counts(&s_c, &s_d);
    CHECK(fb_engine__dispatch_sem(ep_d) == sem, "the retry reuses the same semaphore");
    CHECK(q_c == q_c0 && q_d == q_d0 && s_c == s_c0 && s_d == s_d0,
          "the retry creates and destroys nothing");
    EmbedIQ_Msg_t hi = make_msg(MSG_BOOT_A, (uint8_t)EMBEDIQ_MSG_PRIORITY_HIGH);
    CHECK(embediq_bus_post_async(embediq_bus_token_for(g_h[2]), &hi) == EMBEDIQ_OK,
          "the bus prepared before the late failure stays ready");
}

int main(void)
{
    test_fault_injection_matrix();
    test_publisher_concurrent_with_failing_boot();
    test_publisher_concurrent_with_successful_boot();
    test_bridge_route_refused_while_not_ready();
    test_legacy_bus_boot_after_queue_failure();
    test_successful_boot_is_idempotent();
    test_late_failure_keeps_resources_for_retry();
    fb_engine__reset();

    printf("\n");
    if (g_failed == 0) {
        printf("All %d checks passed. (0 failed)\n", g_checks);
    } else {
        printf("%d of %d checks FAILED.\n", g_failed, g_checks);
    }
    return (g_failed > 0) ? 1 : 0;
}
