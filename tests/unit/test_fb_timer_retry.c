#define _POSIX_C_SOURCE 200809L

/*
 * tests/unit/test_fb_timer_retry.c — the real timer FB across a boot retry,
 * a repeated start, and HAL failures
 *
 * hal/posix/peripherals/hal_timer_posix.c is compiled into this executable
 * with pthread_create and pthread_join renamed (CMake source definitions) to
 * timer_test_create / timer_test_join below, which call the real functions
 * and count every worker created, exited and joined; a create can be made to
 * fail. The timer FB, engine and bus are the real libraries. Ticks are
 * counted from a subscriber's queue (MSG_TIMER_1MS) through the bus receive
 * helper. Every wait is for a count or an event; its time bound is only a
 * hang guard.
 *
 * Cases:
 *   retry after a late failure   timer FB (platform phase) and two
 *                                application FBs whose dependencies form a
 *                                cycle: boot fails after the timer started,
 *                                ticks flow; one dependency entry is
 *                                corrected (no FB added or removed, no
 *                                reset); the retry boot returns 0, fresh ticks
 *                                arrive, one worker in all, no timer FAULT,
 *                                and none remains after deinit
 *   ... with a callback held     the same, with the worker held at a selected
 *                                callback across the retry boot and released
 *                                after it returns: that callback's tick is
 *                                delivered and fresh ticks follow
 *   start twice                  the test start hook after boot, twice: ticks
 *                                continue, one worker
 *   start fails                  thread creation fails: one FAULT with
 *                                EMBEDIQ_FAULT_TIMER_HAL_START, no worker, no
 *                                tick; the next start ticks
 *   another owner                the test owns a running HAL timer before the
 *                                FB initialises: one FAULT with
 *                                EMBEDIQ_FAULT_TIMER_HAL_INIT, no tick, the
 *                                test's callback and context keep running
 *
 * Run:      ./build/tests/unit/test_fb_timer_retry
 * Expected: "All N checks passed. (0 failed)"
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "embediq_fb.h"
#include "embediq_bus.h"
#include "embediq_msg.h"
#include "embediq_obs.h"
#include "embediq_osal.h"
#include "embediq_platform_msgs.h"
#include "embediq_timer.h"
#include "hal_timer.h"

#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern EmbedIQ_FB_Handle_t fb_timer_register(void);
extern void fb_timer__start(void);
extern void fb_timer__stop_and_wait(void);
extern void fb_engine__reset(void);
extern void obs__reset(void);
extern bool obs__ring_read(uint32_t idx, EmbedIQ_Event_t *out);
extern bool message_bus__test_recv(uint8_t ep_id, uint8_t prio, EmbedIQ_Msg_t *out);
extern void hal_timer__set_select_hook(void (*fn)(void));

int timer_test_create(pthread_t *t, const pthread_attr_t *attr,
                      void *(*fn)(void *), void *arg);
int timer_test_join(pthread_t t, void **ret);

/* Ticks to see before and after each step: five 1 ms ticks show the timer
 * running without making a case slow. */
#define TICKS_WANTED  5u

/* Hang guard for every wait: a 1 ms timer delivers five ticks in about 5 ms. */
#define WAIT_MS       2000u

/* Workers one case may create: at most two (a retired one and its
 * replacement); 8 slots leave margin. */
#define MAX_WORKERS   8

static int g_checks = 0;
static int g_failed = 0;

#define CHECK(cond, msg)  do {                                                  \
    g_checks++;                                                                 \
    if (!(cond)) {                                                              \
        fprintf(stderr, "FAIL  %s: %s  %s:%d\n", __func__, (msg), __FILE__,     \
                __LINE__);                                                      \
        g_failed++;                                                             \
    } else {                                                                    \
        printf("PASS  %s: %s\n", __func__, (msg));                              \
    }                                                                           \
} while (0)

/* ---------------------------------------------------------------------------
 * Worker accounting — every field under g_mx
 * ------------------------------------------------------------------------- */

typedef struct {
    void *(*fn)(void *);
    void  *arg;
} entry_t;

static pthread_mutex_t g_mx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_cv = PTHREAD_COND_INITIALIZER;
static entry_t   g_entry[MAX_WORKERS];
static int       g_created;
static int       g_exited;
static int       g_joined;
static int       g_fail_creates;

static void *entry_gate(void *raw)
{
    entry_t *e = (entry_t *)raw;
    void *r = e->fn(e->arg);
    pthread_mutex_lock(&g_mx);
    g_exited++;
    pthread_cond_broadcast(&g_cv);
    pthread_mutex_unlock(&g_mx);
    return r;
}

int timer_test_create(pthread_t *t, const pthread_attr_t *attr,
                      void *(*fn)(void *), void *arg)
{
    pthread_mutex_lock(&g_mx);
    if (g_fail_creates > 0 || g_created >= MAX_WORKERS) {
        if (g_fail_creates > 0) g_fail_creates--;
        pthread_mutex_unlock(&g_mx);
        return EAGAIN;
    }
    entry_t *e = &g_entry[g_created++];
    e->fn  = fn;
    e->arg = arg;
    pthread_mutex_unlock(&g_mx);
    return pthread_create(t, attr, entry_gate, e);
}

int timer_test_join(pthread_t t, void **ret)
{
    int rc = pthread_join(t, ret);
    if (rc == 0) {
        pthread_mutex_lock(&g_mx);
        g_joined++;
        pthread_mutex_unlock(&g_mx);
    }
    return rc;
}

static void workers(int *created, int *exited, int *joined)
{
    pthread_mutex_lock(&g_mx);
    *created = g_created;
    *exited  = g_exited;
    *joined  = g_joined;
    pthread_mutex_unlock(&g_mx);
}

/* Selected-callback hold point: when armed, the next selection waits. */
static bool g_hold_armed, g_held, g_release;

static void select_hold(void)
{
    pthread_mutex_lock(&g_mx);
    if (g_hold_armed) {
        g_hold_armed = false;
        g_held = true;
        pthread_cond_broadcast(&g_cv);
        while (!g_release) pthread_cond_wait(&g_cv, &g_mx);
    }
    pthread_mutex_unlock(&g_mx);
}

static bool wait_held(void)
{
    uint32_t t0 = embediq_osal_time_ms();
    pthread_mutex_lock(&g_mx);
    while (!g_held && embediq_osal_time_ms() - t0 < WAIT_MS) {
        pthread_mutex_unlock(&g_mx);
        embediq_osal_delay_ms(1u);
        pthread_mutex_lock(&g_mx);
    }
    bool held = g_held;
    pthread_mutex_unlock(&g_mx);
    return held;
}

static void release_held(void)
{
    pthread_mutex_lock(&g_mx);
    g_release = true;
    pthread_cond_broadcast(&g_cv);
    pthread_mutex_unlock(&g_mx);
}

/* ---------------------------------------------------------------------------
 * Fixture
 * ------------------------------------------------------------------------- */

EMBEDIQ_SUBS(g_subs_1ms, MSG_TIMER_1MS);

static const char *const k_a_deps[] = { "retry_b" };
static const char *g_b_deps[]       = { "retry_a" };   /* corrected between boots */

static const EmbedIQ_FB_Config_t k_a = {
    .name = "retry_a", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
    .subscriptions = g_subs_1ms, .subscription_count = 1,
    .depends_on = k_a_deps, .depends_count = 1,
};
static const EmbedIQ_FB_Config_t k_b = {
    .name = "retry_b", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
    .depends_on = g_b_deps, .depends_count = 1,
};
static const EmbedIQ_FB_Config_t k_sub = {
    .name = "retry_sub", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
    .subscriptions = g_subs_1ms, .subscription_count = 1,
};

static uint8_t g_timer_ep;

/* Fresh engine and counters; no timer worker alive. */
static void case_begin(void)
{
    fb_timer__stop_and_wait();
    hal_timer__set_select_hook(NULL);
    fb_engine__reset();
    obs__reset();
    embediq_obs_set_transport(EMBEDIQ_OBS_TRANSPORT_TCP);   /* ring only, no output */
    pthread_mutex_lock(&g_mx);
    g_created = g_exited = g_joined = 0;
    g_fail_creates = 0;
    g_hold_armed = g_held = g_release = false;
    pthread_mutex_unlock(&g_mx);
    g_b_deps[0] = "retry_a";
}

/* Messages waiting in ep's NORMAL queue, removed. */
static uint32_t drain(uint8_t ep)
{
    uint32_t n = 0u;
    EmbedIQ_Msg_t m;
    while (message_bus__test_recv(ep, (uint8_t)EMBEDIQ_MSG_PRIORITY_NORMAL, &m)) {
        if (m.msg_id == MSG_TIMER_1MS) n++;
    }
    return n;
}

/* Wait until at least want ticks have reached ep (polling the queue). */
static bool wait_ticks(uint8_t ep, uint32_t want)
{
    uint32_t got = 0u, t0 = embediq_osal_time_ms();
    while (got < want) {
        got += drain(ep);
        if (got >= want) break;
        if (embediq_osal_time_ms() - t0 >= WAIT_MS) return false;
        embediq_osal_delay_ms(1u);
    }
    return true;
}

/* FAULT events the timer FB emitted with state (0 = either timer code). */
static uint32_t timer_faults(uint8_t state)
{
    uint32_t n = 0u;
    EmbedIQ_Event_t e;
    for (uint32_t i = 0u; obs__ring_read(i, &e); i++) {
        if (e.event_type != EMBEDIQ_OBS_EVT_FAULT || e.source_fb_id != g_timer_ep) continue;
        if (state == 0u ? (e.state_or_flag == (uint8_t)EMBEDIQ_FAULT_TIMER_HAL_INIT
                           || e.state_or_flag == (uint8_t)EMBEDIQ_FAULT_TIMER_HAL_START)
                        : e.state_or_flag == state) {
            n++;
        }
    }
    return n;
}

/* After deinit: every worker created was retired. */
static bool none_left(void)
{
    fb_timer__stop_and_wait();
    int c, x, j;
    workers(&c, &x, &j);
    return c == x && c == j;
}

/* ---------------------------------------------------------------------------
 * Cases
 * ------------------------------------------------------------------------- */

static void retry_after_late_failure(bool hold_across_retry)
{
    case_begin();
    g_timer_ep = embediq_engine_fb_ep(fb_timer_register());
    uint8_t ep_a = embediq_engine_fb_ep(embediq_fb_register(&k_a));
    (void)embediq_fb_register(&k_b);
    hal_timer__set_select_hook(select_hold);

    CHECK(embediq_engine_boot() == -1, "boot fails on the dependency cycle");
    CHECK(wait_ticks(ep_a, TICKS_WANTED), "the platform timer started and ticks");

    if (hold_across_retry) {
        pthread_mutex_lock(&g_mx);
        g_hold_armed = true;
        pthread_mutex_unlock(&g_mx);
        CHECK(wait_held(), "a selected callback is held across the retry");
    }

    g_b_deps[0] = "fb_timer";   /* same FBs, one dependency entry corrected */
    CHECK(embediq_engine_boot() == 0, "the retry boot returns 0");
    (void)drain(ep_a);          /* ticks queued before the retry */

    if (hold_across_retry) {
        release_held();
        CHECK(wait_ticks(ep_a, 1u), "the held callback's tick is delivered");
    }
    CHECK(wait_ticks(ep_a, TICKS_WANTED), "fresh ticks arrive after the retry");

    int c, x, j;
    workers(&c, &x, &j);
    CHECK(c == 1 && x == 0, "exactly one timer worker exists");
    CHECK(timer_faults(0u) == 0u, "the timer FB emits no FAULT");
    CHECK(none_left(), "after deinit no worker remains");
}

static void test_retry_after_late_failure(void)
{
    retry_after_late_failure(false);
}

static void test_retry_with_callback_held_across_it(void)
{
    retry_after_late_failure(true);
}

static void test_start_twice(void)
{
    case_begin();
    g_timer_ep = embediq_engine_fb_ep(fb_timer_register());
    uint8_t ep = embediq_engine_fb_ep(embediq_fb_register(&k_sub));
    CHECK(embediq_engine_boot() == 0, "boot");
    CHECK(wait_ticks(ep, TICKS_WANTED), "ticks");
    fb_timer__start();
    fb_timer__start();
    (void)drain(ep);
    CHECK(wait_ticks(ep, TICKS_WANTED), "ticks keep arriving after two more starts");
    int c, x, j;
    workers(&c, &x, &j);
    CHECK(c == 1 && x == 0, "one worker");
    CHECK(timer_faults(0u) == 0u, "no FAULT");
    CHECK(none_left(), "after deinit no worker remains");
}

static void test_start_failure(void)
{
    case_begin();
    g_timer_ep = embediq_engine_fb_ep(fb_timer_register());
    uint8_t ep = embediq_engine_fb_ep(embediq_fb_register(&k_sub));
    pthread_mutex_lock(&g_mx);
    g_fail_creates = 1;                 /* the timer's worker cannot be created */
    pthread_mutex_unlock(&g_mx);
    CHECK(embediq_engine_boot() == 0, "boot");
    int c, x, j;
    workers(&c, &x, &j);
    CHECK(timer_faults((uint8_t)EMBEDIQ_FAULT_TIMER_HAL_START) == 1u
          && timer_faults((uint8_t)EMBEDIQ_FAULT_TIMER_HAL_INIT) == 0u,
          "one FAULT with EMBEDIQ_FAULT_TIMER_HAL_START");
    CHECK(c == 0 && drain(ep) == 0u, "no worker, no tick");
    fb_timer__start();
    CHECK(wait_ticks(ep, TICKS_WANTED), "once creation works again, a start ticks");
    workers(&c, &x, &j);
    CHECK(c == 1, "one worker");
    CHECK(none_left(), "after deinit no worker remains");
}

static embediq_osal_atomic32_t g_own_calls = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);
static void                   *g_own_ctx_seen;   /* written by the worker */
static pthread_mutex_t         g_own_mx = PTHREAD_MUTEX_INITIALIZER;
static int                     g_own_marker;

static void own_callback(void *ctx)
{
    pthread_mutex_lock(&g_own_mx);
    g_own_ctx_seen = ctx;
    pthread_mutex_unlock(&g_own_mx);
    (void)embediq_osal_atomic32_add(&g_own_calls, 1u);
}

static void test_another_owner(void)
{
    case_begin();
    CHECK(hal_timer_init(1000u, own_callback, &g_own_marker) == HAL_OK
          && hal_timer_start() == HAL_OK, "the test owns a running HAL timer");
    g_timer_ep = embediq_engine_fb_ep(fb_timer_register());
    uint8_t ep = embediq_engine_fb_ep(embediq_fb_register(&k_sub));
    CHECK(embediq_engine_boot() == 0, "boot");
    CHECK(timer_faults((uint8_t)EMBEDIQ_FAULT_TIMER_HAL_INIT) == 1u
          && timer_faults((uint8_t)EMBEDIQ_FAULT_TIMER_HAL_START) == 0u,
          "one FAULT with EMBEDIQ_FAULT_TIMER_HAL_INIT");

    uint32_t want = embediq_osal_atomic32_load(&g_own_calls) + TICKS_WANTED;
    uint32_t t0 = embediq_osal_time_ms();
    while (embediq_osal_atomic32_load(&g_own_calls) < want
           && embediq_osal_time_ms() - t0 < WAIT_MS) {
        embediq_osal_delay_ms(1u);
    }
    pthread_mutex_lock(&g_own_mx);
    void *ctx = g_own_ctx_seen;
    pthread_mutex_unlock(&g_own_mx);
    CHECK(embediq_osal_atomic32_load(&g_own_calls) >= want && ctx == &g_own_marker,
          "the test's callback and context keep running");
    CHECK(drain(ep) == 0u, "the timer FB publishes no tick");
    hal_timer_deinit();   /* the test's own timer */
    int c, x, j;
    workers(&c, &x, &j);
    CHECK(c == 1 && c == x && c == j, "the test's worker is the only one, and retired");
}

int main(void)
{
    test_retry_after_late_failure();
    test_retry_with_callback_held_across_it();
    test_start_twice();
    test_start_failure();
    test_another_owner();
    case_begin();
    fb_engine__reset();

    printf("\n");
    if (g_failed == 0) {
        printf("All %d checks passed. (0 failed)\n", g_checks);
    } else {
        printf("%d of %d checks FAILED.\n", g_failed, g_checks);
    }
    return (g_failed > 0) ? 1 : 0;
}
