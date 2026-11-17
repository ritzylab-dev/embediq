#define _POSIX_C_SOURCE 200809L

/*
 * tests/unit/test_boot_readiness.c — boot prepares every required resource
 * before any initializer, or fails cleanly and can be retried
 *
 * Fixture: three FBs — A and B subscribe (one message each), C subscribes to
 * nothing — and one platform library. Boot creates, in order: the three
 * execution locks, then the HIGH, NORMAL and LOW queue of A, B and C (nine
 * queues), then the dispatch semaphores of A and B.
 *
 * Fault-injection matrix — each of the nine queue allocations, each of the
 * two semaphore allocations (B's after A's succeeded), and the second
 * execution lock fails once. For every row:
 *   - boot returns -1; no platform-library initializer and no init_fn runs;
 *     every FB stays IDLE; no dispatch semaphore remains, and dispatch_boot
 *     starts nothing;
 *   - one EMBEDIQ_FAULT_BOOT_RESOURCE event names the FB and the resource;
 *   - every queue and semaphore created in the attempt is destroyed exactly
 *     once (OSAL create/destroy counts);
 *   - an async HIGH post returns EMBEDIQ_ERR, a publish is refused, both are
 *     observed as EMBEDIQ_FAULT_BUS_NOT_READY, and no queue drop is counted;
 *   - with the injection removed, boot succeeds without a reset: every init
 *     runs once, every FB is RUNNING, the queues are fresh, and HIGH, NORMAL
 *     and LOW messages reach their subscribers' dispatch threads.
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

#define MSG_BOOT_A      0x1440u   /* community range, unique to this file */
#define MSG_BOOT_B      0x1441u
#define MSG_BOOT_NOBODY 0x1442u   /* no FB subscribes */

#define FIXTURE_FBS         3u    /* A, B, C */
#define FIXTURE_SUBSCRIBERS 2u    /* A, B */
#define QUEUES_PER_FB       3u    /* HIGH, NORMAL, LOW */
#define FIXTURE_QUEUES      (FIXTURE_FBS * QUEUES_PER_FB)

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
static void check_failed_boot(int rc, uint8_t fail_ep, uint16_t resource,
                              uint32_t q_c0, uint32_t q_d0, uint32_t s_c0, uint32_t s_d0)
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

    uint32_t q_c, q_d, s_c, s_d;
    embediq_osal__queue_counts(&q_c, &q_d);
    embediq_osal__sem_counts(&s_c, &s_d);
    CHECK(q_c - q_c0 == q_d - q_d0, "every queue created in the attempt is destroyed exactly once");
    CHECK(s_c - s_c0 == s_d - s_d0, "every semaphore created in the attempt is destroyed exactly once");

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
    uint32_t q_c0, q_d0, s_c0, s_d0;
    embediq_osal__queue_counts(&q_c0, &q_d0);
    embediq_osal__sem_counts(&s_c0, &s_d0);

    switch (kind) {
    case INJECT_LOCK:  embediq_osal__fail_next_mutex_create(nth); break;
    case INJECT_QUEUE: embediq_osal__fail_next_queue_create(nth); break;
    case INJECT_SEM:   embediq_osal__fail_next_sem_create(nth);   break;
    }
    int rc = embediq_engine_boot();
    embediq_osal__fail_next_mutex_create(0u);   /* disarm all */
    embediq_osal__fail_next_queue_create(0u);
    embediq_osal__fail_next_sem_create(0u);

    check_failed_boot(rc, fail_ep, resource, q_c0, q_d0, s_c0, s_d0);
    check_recovery();
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
    /* Execution lock: B's, after A's succeeded. */
    snprintf(g_row, sizeof(g_row), "execution lock B");
    run_row(INJECT_LOCK, 2u, 1u, (uint16_t)EMBEDIQ_BOOT_RESOURCE_EXEC_LOCK);
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
