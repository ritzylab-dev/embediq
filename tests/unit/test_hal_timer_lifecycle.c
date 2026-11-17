#define _POSIX_C_SOURCE 200809L

/*
 * tests/unit/test_hal_timer_lifecycle.c — POSIX timer worker ownership
 *
 * hal/posix/peripherals/hal_timer_posix.c is compiled into this executable
 * with pthread_create and pthread_join renamed (CMake source definitions) to
 * timer_test_create / timer_test_join below. They call the real functions
 * and record each worker's creation, exit and join as ordered events, and
 * can hold a worker — at its entry, or inside a callback — until the HAL
 * tries to retire it. Nothing waits for a fixed time: every wait is for an
 * event, bounded only as a hang guard.
 *
 * Cases:
 *   restart retires the old worker   old worker held at entry across
 *                                    stop/start: it exits, unseen by any
 *                                    callback, before the replacement exists
 *   repeated cycles                  ten stop/start cycles, each with a
 *                                    callback in progress across the restart
 *   start while running              no second worker
 *   active reinit refused            BUSY while a worker is owned, running or
 *                                    stopped; the live callback and context
 *                                    are unchanged; init succeeds after deinit
 *   callback requests stop           no further callback; restart retires it
 *   never-started and double deinit  no join; second deinit is a no-op
 *   create failure and recovery      IO, observed, nothing owned; the next
 *                                    start succeeds, also after a stop
 *   failed retirement                in deinit or in a restart: the
 *                                    configuration is kept, init is
 *                                    refused and no replacement is created
 *                                    until the worker is retired
 *   invalid init                     NULL callback, zero period: INVALID,
 *                                    observed
 *
 * Run:      ./build/tests/unit/test_hal_timer_lifecycle
 * Expected: "All N checks passed. (0 failed)"
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "hal_timer.h"
#include "embediq_obs.h"

#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/* The most workers one case creates is ten (the restart cycles); 16 slots
 * leave margin. */
#define MAX_WORKERS  16

/* Hang guard for every wait. A 1 ms timer fires hundreds of times in this
 * span; a correct run waits a few milliseconds at most. */
#define WAIT_MS  2000u

/* Timer period for the cases: 1 ms, short enough that a worker reaches its
 * next check quickly once stopped. */
#define PERIOD_US  1000u

/* Ten stop/start cycles, as the restart contract is per cycle. */
#define CYCLES  10

int timer_test_create(pthread_t *t, const pthread_attr_t *attr,
                      void *(*fn)(void *), void *arg);
int timer_test_join(pthread_t t, void **ret);

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
 * Worker bookkeeping — every field under g_mx
 * ------------------------------------------------------------------------- */

typedef struct {
    void *(*fn)(void *);
    void  *arg;
    int    idx;
} entry_t;

static pthread_mutex_t g_mx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_cv = PTHREAD_COND_INITIALIZER;

static entry_t   g_entry[MAX_WORKERS];
static pthread_t g_tid[MAX_WORKERS];
static int       g_created;                  /* workers created            */
static bool      g_released[MAX_WORKERS];    /* may leave the entry gate   */
static bool      g_retire[MAX_WORKERS];      /* the HAL tried to join it   */
static bool      g_exited[MAX_WORKERS];
static bool      g_joined[MAX_WORKERS];      /* joined by the HAL          */
static bool      g_reaped[MAX_WORKERS];      /* joined by reap() instead   */
static unsigned  g_create_seq[MAX_WORKERS];
static unsigned  g_exit_seq[MAX_WORKERS];
static unsigned  g_last_cb_seq[MAX_WORKERS];
static unsigned  g_cb[MAX_WORKERS];          /* callbacks per worker       */
static bool      g_in_cb[MAX_WORKERS];       /* held inside a callback     */
static unsigned  g_seq;                      /* event order                */
static int       g_create_while_live;        /* creates with a worker alive */
static int       g_join_calls;
static int       g_fail_creates;             /* next N creates fail        */
static int       g_fail_joins;               /* next N joins fail          */
static bool      g_hold_entry;               /* new workers wait at entry  */
static bool      g_hold_in_cb;               /* first callback per worker waits */
static unsigned  g_stop_after;               /* callback N calls stop()    */
static unsigned  g_cb_target;                /* p_cb_reaches() threshold   */
static void     *g_ctx_seen;
static unsigned  g_hal_errors;               /* timer HAL error events     */
static uint8_t   g_hal_last_err;             /* its state_or_flag          */

static _Thread_local int tl_worker = -1;

static void *entry_gate(void *raw)
{
    entry_t *e = (entry_t *)raw;
    tl_worker = e->idx;
    pthread_mutex_lock(&g_mx);
    while (!g_released[e->idx]) pthread_cond_wait(&g_cv, &g_mx);
    pthread_mutex_unlock(&g_mx);

    void *r = e->fn(e->arg);

    pthread_mutex_lock(&g_mx);
    g_exited[e->idx]   = true;
    g_exit_seq[e->idx] = ++g_seq;
    pthread_cond_broadcast(&g_cv);
    pthread_mutex_unlock(&g_mx);
    return r;
}

int timer_test_create(pthread_t *t, const pthread_attr_t *attr,
                      void *(*fn)(void *), void *arg)
{
    pthread_mutex_lock(&g_mx);
    if (g_fail_creates > 0) {
        g_fail_creates--;
        pthread_mutex_unlock(&g_mx);
        return EAGAIN;
    }
    if (g_created >= MAX_WORKERS) {
        pthread_mutex_unlock(&g_mx);
        return EAGAIN;
    }
    int idx = g_created++;
    for (int i = 0; i < idx; i++) {
        if (!g_exited[i]) { g_create_while_live++; break; }
    }
    g_create_seq[idx] = ++g_seq;
    g_entry[idx]      = (entry_t){ fn, arg, idx };
    g_released[idx]   = !g_hold_entry;
    pthread_mutex_unlock(&g_mx);

    int rc = pthread_create(t, attr, entry_gate, &g_entry[idx]);
    pthread_mutex_lock(&g_mx);
    g_tid[idx] = *t;
    pthread_mutex_unlock(&g_mx);
    return rc;
}

/* Index of the live worker whose handle is t, or -1. A pthread_t value may
 * be reused once its thread is joined, so the newest worker not yet joined
 * wins. Caller holds g_mx. */
static int worker_of(pthread_t t)
{
    for (int i = g_created - 1; i >= 0; i--) {
        if (!g_joined[i] && !g_reaped[i] && pthread_equal(g_tid[i], t)) return i;
    }
    return -1;
}

int timer_test_join(pthread_t t, void **ret)
{
    pthread_mutex_lock(&g_mx);
    g_join_calls++;
    if (g_fail_joins > 0) {
        g_fail_joins--;
        pthread_mutex_unlock(&g_mx);
        return EDEADLK;
    }
    int idx = worker_of(t);
    if (idx >= 0) {
        /* Retirement attempted: a held worker may now proceed. */
        g_released[idx] = true;
        g_retire[idx]   = true;
        pthread_cond_broadcast(&g_cv);
    }
    pthread_mutex_unlock(&g_mx);

    int rc = pthread_join(t, ret);

    pthread_mutex_lock(&g_mx);
    if (rc == 0 && idx >= 0) g_joined[idx] = true;
    pthread_mutex_unlock(&g_mx);
    return rc;
}

/* The HAL's observation path: count timer HAL errors. */
void embediq_obs_emit_from_isr(uint8_t event_type, uint8_t source,
                               uint8_t target, uint8_t state, uint16_t msg_id)
{
    (void)target; (void)msg_id;
    if (event_type != EMBEDIQ_OBS_EVT_HAL_FAULT || source != EMBEDIQ_HAL_SRC_TIMER) return;
    pthread_mutex_lock(&g_mx);
    g_hal_errors++;
    g_hal_last_err = state;
    pthread_mutex_unlock(&g_mx);
}

static uint8_t hal_code(int err) { return (uint8_t)(-err); }

static void tick(void *ctx)
{
    pthread_mutex_lock(&g_mx);
    int idx = tl_worker;
    bool stop_now = false;
    if (idx >= 0) {
        g_cb[idx]++;
        g_last_cb_seq[idx] = ++g_seq;
        g_ctx_seen = ctx;
        if (g_hold_in_cb && g_cb[idx] == 1u) {
            g_in_cb[idx] = true;
            pthread_cond_broadcast(&g_cv);
            while (!g_retire[idx]) pthread_cond_wait(&g_cv, &g_mx);
            g_in_cb[idx] = false;
        }
        stop_now = (g_stop_after != 0u && g_cb[idx] == g_stop_after);
    }
    pthread_cond_broadcast(&g_cv);
    pthread_mutex_unlock(&g_mx);
    if (stop_now) (void)hal_timer_stop();   /* a callback may request stop */
}

/* ---------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */

typedef bool (*pred_t)(int arg);

/* Wait under g_mx until pred(arg) holds; false after WAIT_MS (hang guard). */
static bool wait_for(pred_t pred, int arg)
{
    struct timespec dl;
    clock_gettime(CLOCK_REALTIME, &dl);
    dl.tv_sec  += (time_t)(WAIT_MS / 1000u);
    dl.tv_nsec += (long)(WAIT_MS % 1000u) * 1000000L;
    if (dl.tv_nsec >= 1000000000L) { dl.tv_sec++; dl.tv_nsec -= 1000000000L; }
    pthread_mutex_lock(&g_mx);
    bool ok = true;
    while (!pred(arg)) {
        if (pthread_cond_timedwait(&g_cv, &g_mx, &dl) == ETIMEDOUT) { ok = pred(arg); break; }
    }
    pthread_mutex_unlock(&g_mx);
    return ok;
}

static bool p_cb_at_least_3(int w)  { return g_cb[w] >= 3u; }
static bool p_has_cb(int w)         { return g_cb[w] >= 1u; }
static bool p_in_cb(int w)          { return g_in_cb[w]; }
static bool p_exited(int w)         { return g_exited[w]; }
static bool p_cb_reaches(int w)     { return g_cb[w] >= g_cb_target; }

/* Release everything and join every worker the HAL did not join, so a
 * failing case cannot leave a thread behind for the next one. */
static void reap(void)
{
    pthread_mutex_lock(&g_mx);
    g_hold_entry = false;
    g_hold_in_cb = false;
    for (int i = 0; i < g_created; i++) { g_released[i] = true; g_retire[i] = true; }
    pthread_cond_broadcast(&g_cv);
    int n = g_created;
    pthread_mutex_unlock(&g_mx);
    for (int i = 0; i < n; i++) {
        pthread_mutex_lock(&g_mx);
        bool done = g_joined[i] || g_reaped[i];
        pthread_t t = g_tid[i];
        pthread_mutex_unlock(&g_mx);
        if (!done && pthread_join(t, NULL) == 0) {
            pthread_mutex_lock(&g_mx);
            g_reaped[i] = true;   /* not counted as retired by the HAL */
            pthread_mutex_unlock(&g_mx);
        }
    }
}

/* Start a case: the timer is deinitialised and no worker of ours is alive. */
static void begin(void)
{
    hal_timer_deinit();
    reap();
    pthread_mutex_lock(&g_mx);
    memset(g_released, 0, sizeof(g_released));
    memset(g_retire, 0, sizeof(g_retire));
    memset(g_exited, 0, sizeof(g_exited));
    memset(g_joined, 0, sizeof(g_joined));
    memset(g_reaped, 0, sizeof(g_reaped));
    memset(g_create_seq, 0, sizeof(g_create_seq));
    memset(g_exit_seq, 0, sizeof(g_exit_seq));
    memset(g_last_cb_seq, 0, sizeof(g_last_cb_seq));
    memset(g_cb, 0, sizeof(g_cb));
    memset(g_in_cb, 0, sizeof(g_in_cb));
    g_created = 0;
    g_create_while_live = 0;
    g_join_calls = 0;
    g_fail_creates = 0;
    g_fail_joins = 0;
    g_hold_entry = false;
    g_hold_in_cb = false;
    g_stop_after = 0u;
    g_cb_target = 0u;
    g_ctx_seen = NULL;
    g_hal_errors = 0u;
    g_hal_last_err = 0u;
    pthread_mutex_unlock(&g_mx);
}

/* Every worker created was joined by the HAL and exited before the next one
 * was created. Caller holds no lock; reads after the case's threads are gone. */
static bool all_retired_in_order(void)
{
    pthread_mutex_lock(&g_mx);
    bool ok = g_create_while_live == 0;
    for (int i = 0; i < g_created; i++) {
        if (!g_joined[i] || !g_exited[i]) ok = false;
        if (i + 1 < g_created && !(g_exit_seq[i] < g_create_seq[i + 1])) ok = false;
    }
    pthread_mutex_unlock(&g_mx);
    return ok;
}

static int  g_marker_a;
static int  g_marker_b;

static void other_tick(void *ctx) { (void)ctx; }

/* ---------------------------------------------------------------------------
 * Cases
 * ------------------------------------------------------------------------- */

static void test_restart_retires_old_worker(void)
{
    begin();
    pthread_mutex_lock(&g_mx);
    g_hold_entry = true;        /* each worker waits at entry until joined or released */
    pthread_mutex_unlock(&g_mx);

    CHECK(hal_timer_init(PERIOD_US, tick, &g_marker_a) == HAL_OK, "init");
    CHECK(hal_timer_start() == HAL_OK, "start");
    CHECK(hal_timer_stop() == HAL_OK, "stop");
    CHECK(hal_timer_start() == HAL_OK, "restart");

    /* Let the replacement (and anything else still alive) run. */
    pthread_mutex_lock(&g_mx);
    g_hold_entry = false;
    for (int i = 0; i < g_created; i++) g_released[i] = true;
    pthread_cond_broadcast(&g_cv);
    int created = g_created;
    pthread_mutex_unlock(&g_mx);
    CHECK(created == 2 && wait_for(p_cb_at_least_3, 1), "the replacement fires");

    hal_timer_deinit();
    reap();
    pthread_mutex_lock(&g_mx);
    unsigned old_cbs = g_cb[0];
    pthread_mutex_unlock(&g_mx);
    CHECK(old_cbs == 0u, "the old worker never fires after the restart");
    CHECK(all_retired_in_order(), "the old worker exits before the replacement is created");
}

static void test_repeated_cycles(void)
{
    begin();
    pthread_mutex_lock(&g_mx);
    g_hold_in_cb = true;        /* each worker's first callback lasts until retirement */
    pthread_mutex_unlock(&g_mx);

    CHECK(hal_timer_init(PERIOD_US, tick, &g_marker_a) == HAL_OK, "init");
    bool started = true, in_cb = true;
    for (int c = 0; c < CYCLES; c++) {
        if (hal_timer_start() != HAL_OK) started = false;
        if (!wait_for(p_in_cb, c)) in_cb = false;   /* callback in progress */
        (void)hal_timer_stop();
    }
    CHECK(started, "every restart succeeds");
    CHECK(in_cb, "each cycle has a callback in progress at stop");
    hal_timer_deinit();
    reap();

    pthread_mutex_lock(&g_mx);
    bool cb_before_next = g_created == CYCLES;
    for (int i = 0; i + 1 < g_created; i++) {
        if (!(g_last_cb_seq[i] < g_create_seq[i + 1])) cb_before_next = false;
    }
    int created = g_created;
    pthread_mutex_unlock(&g_mx);
    CHECK(created == CYCLES, "one worker per cycle");
    CHECK(cb_before_next, "no worker fires after its replacement exists");
    CHECK(all_retired_in_order(), "each worker exits before the next is created");
}

static void test_start_while_running(void)
{
    begin();
    CHECK(hal_timer_init(PERIOD_US, tick, &g_marker_a) == HAL_OK, "init");
    CHECK(hal_timer_start() == HAL_OK, "start");
    CHECK(hal_timer_start() == HAL_OK, "start again returns OK");
    CHECK(wait_for(p_has_cb, 0), "the worker fires");
    hal_timer_deinit();
    reap();
    pthread_mutex_lock(&g_mx);
    int created = g_created;
    pthread_mutex_unlock(&g_mx);
    CHECK(created == 1, "no second worker");
    CHECK(all_retired_in_order(), "the worker is retired by deinit");
}

static void test_active_reinit_refused(void)
{
    begin();
    CHECK(hal_timer_init(PERIOD_US, tick, &g_marker_a) == HAL_OK, "init");
    CHECK(hal_timer_start() == HAL_OK, "start");
    CHECK(wait_for(p_has_cb, 0), "the worker fires");

    CHECK(hal_timer_init(PERIOD_US, other_tick, &g_marker_b) == HAL_ERR_BUSY,
          "init while running is BUSY");
    pthread_mutex_lock(&g_mx);
    unsigned errors = g_hal_errors;
    uint8_t  last   = g_hal_last_err;
    g_cb_target = g_cb[0] + 3u;     /* three more callbacks after the refusal */
    pthread_mutex_unlock(&g_mx);
    CHECK(errors == 1u && last == hal_code(HAL_ERR_BUSY), "the refusal is observed");
    CHECK(wait_for(p_cb_reaches, 0), "the original callback keeps firing");
    pthread_mutex_lock(&g_mx);
    void *ctx = g_ctx_seen;
    pthread_mutex_unlock(&g_mx);
    CHECK(ctx == &g_marker_a, "with the original context");

    CHECK(hal_timer_stop() == HAL_OK, "stop");
    CHECK(hal_timer_init(PERIOD_US, other_tick, &g_marker_b) == HAL_ERR_BUSY,
          "init while stopped but not retired is BUSY");
    hal_timer_deinit();
    CHECK(hal_timer_init(PERIOD_US, other_tick, &g_marker_b) == HAL_OK,
          "init after deinit succeeds");
    hal_timer_deinit();
    reap();
    CHECK(all_retired_in_order(), "the worker is retired by deinit");
}

static void test_callback_requests_stop(void)
{
    begin();
    pthread_mutex_lock(&g_mx);
    g_stop_after = 3u;          /* the third callback calls hal_timer_stop() */
    pthread_mutex_unlock(&g_mx);
    CHECK(hal_timer_init(PERIOD_US, tick, &g_marker_a) == HAL_OK, "init");
    CHECK(hal_timer_start() == HAL_OK, "start");
    CHECK(wait_for(p_exited, 0), "the worker returns after the stop");
    pthread_mutex_lock(&g_mx);
    unsigned cbs = g_cb[0];
    g_stop_after = 0u;
    pthread_mutex_unlock(&g_mx);
    CHECK(cbs == 3u, "no callback after the one that stopped the timer");

    CHECK(hal_timer_start() == HAL_OK, "restart");
    CHECK(wait_for(p_has_cb, 1), "the replacement fires");
    hal_timer_deinit();
    reap();
    CHECK(all_retired_in_order(), "the stopped worker is retired before the replacement");
}

static void test_never_started_and_double_deinit(void)
{
    begin();
    CHECK(hal_timer_init(PERIOD_US, tick, &g_marker_a) == HAL_OK, "init");
    hal_timer_deinit();
    hal_timer_deinit();
    pthread_mutex_lock(&g_mx);
    int joins = g_join_calls, created = g_created;
    pthread_mutex_unlock(&g_mx);
    CHECK(created == 0 && joins == 0, "no worker, no join");
    CHECK(hal_timer_init(PERIOD_US, tick, &g_marker_a) == HAL_OK, "init after double deinit");
    hal_timer_deinit();
}

static void test_create_failure_and_recovery(void)
{
    begin();
    CHECK(hal_timer_init(PERIOD_US, tick, &g_marker_a) == HAL_OK, "init");
    pthread_mutex_lock(&g_mx);
    g_fail_creates = 1;
    pthread_mutex_unlock(&g_mx);
    CHECK(hal_timer_start() == HAL_ERR_IO, "start fails with IO");
    pthread_mutex_lock(&g_mx);
    unsigned errors = g_hal_errors;
    uint8_t  last   = g_hal_last_err;
    pthread_mutex_unlock(&g_mx);
    CHECK(errors == 1u && last == hal_code(HAL_ERR_IO), "the failure is observed");
    CHECK(hal_timer_init(PERIOD_US, tick, &g_marker_a) == HAL_OK,
          "nothing is owned after the failure");
    CHECK(hal_timer_start() == HAL_OK, "the next start succeeds");
    CHECK(wait_for(p_has_cb, 0), "and fires");

    /* After a stop: the old worker is retired, then the create fails. */
    CHECK(hal_timer_stop() == HAL_OK, "stop");
    pthread_mutex_lock(&g_mx);
    g_fail_creates = 1;
    pthread_mutex_unlock(&g_mx);
    CHECK(hal_timer_start() == HAL_ERR_IO, "restart with a failing create is IO");
    CHECK(hal_timer_start() == HAL_OK, "the following start succeeds");
    CHECK(wait_for(p_has_cb, 1), "and fires");
    hal_timer_deinit();
    reap();
    CHECK(all_retired_in_order(), "every worker is retired, one at a time");
}

static void test_failed_retirement_keeps_configuration(void)
{
    begin();
    CHECK(hal_timer_init(PERIOD_US, tick, &g_marker_a) == HAL_OK, "init");
    CHECK(hal_timer_start() == HAL_OK, "start");
    CHECK(wait_for(p_has_cb, 0), "the worker fires");
    pthread_mutex_lock(&g_mx);
    g_fail_joins = 1;
    pthread_mutex_unlock(&g_mx);
    hal_timer_deinit();                     /* the join fails */
    pthread_mutex_lock(&g_mx);
    unsigned errors = g_hal_errors;
    uint8_t  last   = g_hal_last_err;
    pthread_mutex_unlock(&g_mx);
    CHECK(errors == 1u && last == hal_code(HAL_ERR_IO), "the failed retirement is observed");
    CHECK(hal_timer_init(PERIOD_US, other_tick, &g_marker_b) == HAL_ERR_BUSY,
          "the worker is still owned: init is refused");

    /* A restart whose join fails is refused and keeps the worker owned. */
    pthread_mutex_lock(&g_mx);
    g_fail_joins = 1;
    int created_before = g_created;
    pthread_mutex_unlock(&g_mx);
    CHECK(hal_timer_start() == HAL_ERR_IO, "a restart whose join fails returns IO");
    pthread_mutex_lock(&g_mx);
    int created_after = g_created;
    pthread_mutex_unlock(&g_mx);
    CHECK(created_after == created_before, "and creates no replacement");
    CHECK(hal_timer_init(PERIOD_US, other_tick, &g_marker_b) == HAL_ERR_BUSY,
          "the worker is still owned after the failed restart");

    /* The configuration survived: a restart retires the worker and fires
     * the original callback with the original context. */
    CHECK(hal_timer_start() == HAL_OK, "restart");
    CHECK(wait_for(p_has_cb, 1), "the replacement fires");
    pthread_mutex_lock(&g_mx);
    void *ctx = g_ctx_seen;
    pthread_mutex_unlock(&g_mx);
    CHECK(ctx == &g_marker_a, "with the original callback and context");
    hal_timer_deinit();
    reap();
    CHECK(all_retired_in_order(), "both workers are retired, one at a time");
}

static void test_invalid_init(void)
{
    begin();
    CHECK(hal_timer_init(PERIOD_US, NULL, NULL) == HAL_ERR_INVALID, "NULL callback is INVALID");
    CHECK(hal_timer_init(0u, tick, NULL) == HAL_ERR_INVALID, "zero period is INVALID");
    pthread_mutex_lock(&g_mx);
    unsigned errors = g_hal_errors;
    uint8_t  last   = g_hal_last_err;
    pthread_mutex_unlock(&g_mx);
    CHECK(errors == 2u && last == hal_code(HAL_ERR_INVALID), "both are observed");
    CHECK(hal_timer_start() == HAL_ERR_INVALID, "start without a valid init is INVALID");
}

int main(void)
{
    test_restart_retires_old_worker();
    test_repeated_cycles();
    test_start_while_running();
    test_active_reinit_refused();
    test_callback_requests_stop();
    test_never_started_and_double_deinit();
    test_create_failure_and_recovery();
    test_failed_retirement_keeps_configuration();
    test_invalid_init();
    begin();   /* leaves no worker behind */

    printf("\n");
    if (g_failed == 0) {
        printf("All %d checks passed. (0 failed)\n", g_checks);
    } else {
        printf("%d of %d checks FAILED.\n", g_failed, g_checks);
    }
    return (g_failed > 0) ? 1 : 0;
}
