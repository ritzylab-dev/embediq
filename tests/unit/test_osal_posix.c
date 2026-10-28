#define _POSIX_C_SOURCE 200809L

/*
 * tests/unit/test_osal_posix.c — Unit tests for OSAL POSIX implementation
 *
 * Covers every contract in embediq_osal.h. Zero external test framework —
 * stdlib only. Each test exercises one behaviour with one assertion.
 *
 * Run:      ./build/tests/test_osal_posix
 * Expected: "All 20 tests passed. (0 failed)"
 *
 * ASAN:
 *   cmake -B build_asan -DEMBEDIQ_PLATFORM=host \
 *         -DCMAKE_C_FLAGS="-fsanitize=address -g"
 *   cmake --build build_asan
 *   ./build_asan/tests/test_osal_posix
 *
 * TSan:
 *   cmake -B build_tsan -DEMBEDIQ_PLATFORM=host -DEMBEDIQ_TSAN=ON
 *   cmake --build build_tsan
 *   ./build_tsan/tests/unit/test_osal_posix   (must produce zero TSan reports)
 *
 * Note on memory leaks: most tests keep their handles for the life of the
 * process (as firmware does). LSAN may report leaks at process exit; that is
 * expected. mutex/signal destroy are exercised by test_destroy_functions_do_not_crash.
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "embediq_osal.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/* F1 concurrency tests — thread count and per-thread iterations. */
#define F1_THREADS 8u
#define F1_ITERS   100000u

/* test_critical_section_is_fast — pairs timed and the ceiling (CI allowance). */
#define F1_CRIT_PAIRS      1000000u
#define F1_CRIT_MAX_AVG_NS 2000u

/* test_signal_post_wakes_waiter — post delay and wake deadline. */
#define F1_POST_DELAY_MS   10u
#define F1_WAKE_DEADLINE_MS 200u

/* F4 replace-oldest tests. */
#define F4_Q_DEPTH         4u
#define F4_REPLACE_ITEMS   50000u
#define F4_RECV_POLL_MS    1u

/* Weak fallback so this file links before the OSAL implements the call
 * (TDD-first commit); the OSAL's strong definition replaces it. */
__attribute__((weak)) embediq_err_t
embediq_osal_queue_send_replace_oldest(EmbedIQ_Queue_t *q, const void *item, bool *out_dropped)
{
    (void)q; (void)item;
    if (out_dropped) *out_dropped = false;
    return EMBEDIQ_ERR;
}

/* ---------------------------------------------------------------------------
 * Minimal test harness — one ASSERT per test function
 * ------------------------------------------------------------------------- */

static int g_tests_run    = 0;
static int g_tests_failed = 0;

#define ASSERT(cond, msg)  do {                                               \
    g_tests_run++;                                                            \
    if (!(cond)) {                                                            \
        fprintf(stderr, "FAIL  %-56s  %s:%d\n", __func__, __FILE__, __LINE__);\
        g_tests_failed++;                                                     \
    } else {                                                                  \
        printf("PASS  %s\n", __func__);                                       \
    }                                                                         \
} while (0)

/* ---------------------------------------------------------------------------
 * test_task_create_runs_function
 * The task function must execute and set the shared flag.
 * ------------------------------------------------------------------------- */

static embediq_osal_atomic32_t g_task_ran = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);

static void task_set_flag(void *arg)
{
    (void)arg;
    embediq_osal_atomic32_store(&g_task_ran, 1u);
}

static void test_task_create_runs_function(void)
{
    embediq_osal_atomic32_store(&g_task_ran, 0u);
    EmbedIQ_Task_t *t = embediq_osal_task_create(
            "flag_setter", task_set_flag, NULL, 1, 4096);
    while (embediq_osal_atomic32_load(&g_task_ran) == 0u) {
        nanosleep(&(struct timespec){0, 500 * 1000L}, NULL);
    }
    embediq_osal_task_delete(t);
    ASSERT(embediq_osal_atomic32_load(&g_task_ran) == 1u, "task function must run");
}

/* ---------------------------------------------------------------------------
 * test_task_delete_stops_execution
 * Counter must not increment after task_delete returns.
 * ------------------------------------------------------------------------- */

static volatile int g_loop_count = 0;

static void incrementing_task(void *arg)
{
    (void)arg;
    while (1) {
        g_loop_count++;
        nanosleep(&(struct timespec){0, 200 * 1000L}, NULL);     /* cancellation point */
    }
}

static void test_task_delete_stops_execution(void)
{
    g_loop_count = 0;
    EmbedIQ_Task_t *t = embediq_osal_task_create(
            "counter", incrementing_task, NULL, 1, 4096);
    nanosleep(&(struct timespec){0, 5000 * 1000L}, NULL);  /* let it run for a bit */
    embediq_osal_task_delete(t);       /* cancel + join */
    int snapshot = g_loop_count;
    nanosleep(&(struct timespec){0, 5000 * 1000L}, NULL);  /* would keep incrementing if still alive */
    ASSERT(g_loop_count == snapshot, "counter must not change after task_delete");
}

/* ---------------------------------------------------------------------------
 * test_queue_send_recv_roundtrip
 * Item sent must equal item received.
 * ------------------------------------------------------------------------- */

static void test_queue_send_recv_roundtrip(void)
{
    EmbedIQ_Queue_t *q  = embediq_osal_queue_create(4, sizeof(uint32_t));
    uint32_t sent = 0xDEADBEEFu, recvd = 0;
    embediq_osal_queue_send(q, &sent, 0);
    embediq_osal_queue_recv(q, &recvd, 0);
    ASSERT(recvd == sent, "received value must equal sent value");
}

/* ---------------------------------------------------------------------------
 * test_queue_send_timeout_returns_false
 * Sending to a full queue with timeout=0 must return false immediately.
 * ------------------------------------------------------------------------- */

static void test_queue_send_timeout_returns_false(void)
{
    EmbedIQ_Queue_t *q  = embediq_osal_queue_create(1, sizeof(uint8_t));
    uint8_t item = 0xFFu;
    embediq_osal_queue_send(q, &item, 0);              /* fill it */
    embediq_err_t ok = embediq_osal_queue_send(q, &item, 0);   /* must fail */
    ASSERT(ok != EMBEDIQ_OK, "send to full queue with timeout=0 must return non-OK");
}

/* ---------------------------------------------------------------------------
 * test_queue_send_block_forever
 * send with UINT32_MAX must block until a receiver creates space.
 * ------------------------------------------------------------------------- */

static void recv_after_delay(void *arg)
{
    EmbedIQ_Queue_t *q = (EmbedIQ_Queue_t *)arg;
    nanosleep(&(struct timespec){0, 30000 * 1000L}, NULL); /* 30 ms — give main time to block */
    uint8_t buf;
    embediq_osal_queue_recv(q, &buf, UINT32_MAX);
}

static void test_queue_send_block_forever(void)
{
    EmbedIQ_Queue_t *q = embediq_osal_queue_create(1, sizeof(uint8_t));
    uint8_t item = 0x42u;
    embediq_osal_queue_send(q, &item, 0);   /* fill queue (depth 1) */
    EmbedIQ_Task_t *t = embediq_osal_task_create(
            "recv_delay", recv_after_delay, q, 1, 4096);
    embediq_err_t ok = embediq_osal_queue_send(q, &item, UINT32_MAX); /* blocks */
    embediq_osal_task_delete(t);
    ASSERT(ok == EMBEDIQ_OK, "send with UINT32_MAX must succeed once space is available");
}

/* ---------------------------------------------------------------------------
 * test_signal_wait_and_post
 * signal_wait must unblock after signal_from_isr is called.
 * ------------------------------------------------------------------------- */

static void post_after_delay(void *arg)
{
    EmbedIQ_Signal_t *sig = (EmbedIQ_Signal_t *)arg;
    nanosleep(&(struct timespec){0, 20000 * 1000L}, NULL);
    embediq_osal_signal_from_isr(sig);
}

static void test_signal_wait_and_post(void)
{
    EmbedIQ_Signal_t *sig = embediq_osal_signal_create();
    EmbedIQ_Task_t *t = embediq_osal_task_create(
            "poster", post_after_delay, sig, 1, 4096);
    embediq_osal_signal_wait(sig);     /* blocks until poster fires */
    embediq_osal_task_delete(t);
    ASSERT(true, "signal_wait must return after signal_from_isr");
}

/* ---------------------------------------------------------------------------
 * test_signal_from_isr_unblocks_waiting_thread
 * Two concurrent pthreads: one waits on the signal, one posts via
 * signal_from_isr. Verifies the waiter is unblocked with no race.
 * ------------------------------------------------------------------------- */

typedef struct {
    EmbedIQ_Signal_t        *sig;
    embediq_osal_atomic32_t *unblocked;
} isr_ctx_t;

static void isr_waiter(void *arg)
{
    isr_ctx_t *ctx = (isr_ctx_t *)arg;
    embediq_osal_signal_wait(ctx->sig);
    embediq_osal_atomic32_store(ctx->unblocked, 1u);
}

static void isr_poster(void *arg)
{
    nanosleep(&(struct timespec){0, 20000 * 1000L}, NULL);                     /* ensure waiter is blocked first */
    embediq_osal_signal_from_isr((EmbedIQ_Signal_t *)arg);
}

static void test_signal_from_isr_unblocks_waiting_thread(void)
{
    EmbedIQ_Signal_t *sig  = embediq_osal_signal_create();
    embediq_osal_atomic32_t unblocked = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);
    isr_ctx_t ctx = { .sig = sig, .unblocked = &unblocked };

    EmbedIQ_Task_t *waiter = embediq_osal_task_create(
            "waiter", isr_waiter, &ctx, 1, 4096);
    EmbedIQ_Task_t *poster  = embediq_osal_task_create(
            "poster", isr_poster,  sig,  1, 4096);

    /* Wait until the waiter sets the flag.  Both threads exit naturally; we
     * must NOT cancel them early.  task_delete() cancels before joining —
     * cancelling the poster during its 20 ms sleep would prevent it from
     * ever posting the signal.  Polling here guarantees both threads have
     * already finished before we call delete. */
    while (embediq_osal_atomic32_load(&unblocked) == 0u) {
        nanosleep(&(struct timespec){0, 500 * 1000L}, NULL);
    }

    embediq_osal_task_delete(poster);  /* already exited — cancel is no-op */
    embediq_osal_task_delete(waiter);  /* already exited — cancel is no-op */
    ASSERT(embediq_osal_atomic32_load(&unblocked) == 1u,
           "waiter thread must be unblocked by signal_from_isr");
}

/* ---------------------------------------------------------------------------
 * test_time_us_increases_monotonically
 * Two successive samples separated by 1 ms must show >= 1000 us elapsed.
 * Uses unsigned subtraction — correct even across 32-bit wrap (I-13).
 * ------------------------------------------------------------------------- */

static void test_time_us_increases_monotonically(void)
{
    uint32_t t1 = embediq_osal_time_us();
    nanosleep(&(struct timespec){0, 1000 * 1000L}, NULL);
    uint32_t t2 = embediq_osal_time_us();
    ASSERT((uint32_t)(t2 - t1) >= 1000u,
           "time_us must increase monotonically");
}

/* ---------------------------------------------------------------------------
 * test_mutex_lock_unlock
 * Lock must succeed on an unlocked mutex; unlock must not block.
 * ------------------------------------------------------------------------- */

static void test_mutex_lock_unlock(void)
{
    EmbedIQ_Mutex_t *m = embediq_osal_mutex_create();
    embediq_err_t locked = embediq_osal_mutex_lock(m, UINT32_MAX);
    embediq_osal_mutex_unlock(m);
    ASSERT(locked == EMBEDIQ_OK, "mutex_lock with UINT32_MAX must succeed on an unlocked mutex");
}

/* ---------------------------------------------------------------------------
 * test_mutex_timeout_returns_false
 * Lock with a short timeout must fail while another thread holds the mutex.
 * Uses a signal to guarantee the holder has acquired the lock before the
 * main thread attempts its timed acquisition.
 * ------------------------------------------------------------------------- */

static EmbedIQ_Mutex_t  *g_held_mutex       = NULL;
static EmbedIQ_Signal_t *g_lock_acquired    = NULL;

static void hold_mutex_fn(void *arg)
{
    (void)arg;
    embediq_osal_mutex_lock(g_held_mutex, UINT32_MAX);
    embediq_osal_signal_from_isr(g_lock_acquired); /* signal: lock acquired */
    nanosleep(&(struct timespec){0, 300000 * 1000L}, NULL); /* hold for 300 ms */
    embediq_osal_mutex_unlock(g_held_mutex);
}

static void test_mutex_timeout_returns_false(void)
{
    g_held_mutex    = embediq_osal_mutex_create();
    g_lock_acquired = embediq_osal_signal_create();

    EmbedIQ_Task_t *holder = embediq_osal_task_create(
            "holder", hold_mutex_fn, NULL, 1, 4096);
    embediq_osal_signal_wait(g_lock_acquired); /* wait until holder has lock */
    embediq_err_t got = embediq_osal_mutex_lock(g_held_mutex, 50); /* 50 ms timeout */
    if (got == EMBEDIQ_OK) embediq_osal_mutex_unlock(g_held_mutex);
    embediq_osal_task_delete(holder);
    ASSERT(got != EMBEDIQ_OK, "mutex_lock must return non-OK when timed out while held");
}

/* ---------------------------------------------------------------------------
 * test_queue_destroy
 * Create a queue, enqueue one item, destroy it. Must not crash.
 * Also test destroy(NULL) — must return silently.
 * ------------------------------------------------------------------------- */

static void test_queue_destroy(void)
{
    EmbedIQ_Queue_t *q = embediq_osal_queue_create(4, sizeof(uint32_t));
    uint32_t val = 42u;
    embediq_osal_queue_send(q, &val, 0u);
    embediq_osal_queue_destroy(q);

    /* NULL destroy must not crash. */
    embediq_osal_queue_destroy(NULL);

    ASSERT(1, "queue_destroy must complete without crash");
}

/* ===========================================================================
 * F1 — critical section, 32-bit atomics, destroy/post functions
 * ======================================================================== */

/* ---------------------------------------------------------------------------
 * test_critical_section_mutual_exclusion
 * F1_THREADS threads each increment a plain (non-atomic) counter F1_ITERS
 * times inside the critical section. No increment may be lost. Under TSan
 * this test is the proof that the section serialises.
 * ------------------------------------------------------------------------- */

static uint32_t g_plain_counter = 0u;

static void *crit_incrementer(void *arg)
{
    (void)arg;
    for (uint32_t i = 0u; i < F1_ITERS; i++) {
        embediq_osal_critical_enter();
        g_plain_counter++;
        embediq_osal_critical_exit();
    }
    return NULL;
}

static void test_critical_section_mutual_exclusion(void)
{
    pthread_t th[F1_THREADS];
    g_plain_counter = 0u;
    for (uint32_t i = 0u; i < F1_THREADS; i++) {
        pthread_create(&th[i], NULL, crit_incrementer, NULL);
    }
    for (uint32_t i = 0u; i < F1_THREADS; i++) {
        pthread_join(th[i], NULL);
    }
    ASSERT(g_plain_counter == F1_THREADS * F1_ITERS,
           "critical section must not lose increments");
}

/* ---------------------------------------------------------------------------
 * test_critical_section_is_fast
 * Uncontended enter/exit must average < 1 µs (asserted < 2 µs for CI noise).
 * ------------------------------------------------------------------------- */

static void test_critical_section_is_fast(void)
{
    uint32_t t0 = embediq_osal_time_us();
    for (uint32_t i = 0u; i < F1_CRIT_PAIRS; i++) {
        embediq_osal_critical_enter();
        embediq_osal_critical_exit();
    }
    uint32_t elapsed_us = embediq_osal_time_us() - t0;
    uint64_t avg_ns = ((uint64_t)elapsed_us * 1000u) / F1_CRIT_PAIRS;
    printf("      critical enter/exit: %u pairs in %u us — avg %llu ns\n",
           (unsigned)F1_CRIT_PAIRS, (unsigned)elapsed_us, (unsigned long long)avg_ns);
    ASSERT(avg_ns < F1_CRIT_MAX_AVG_NS, "critical enter/exit must average < 2 us");
}

/* ---------------------------------------------------------------------------
 * test_atomic32_add_from_threads
 * F1_THREADS threads each add 1 F1_ITERS times; the final value is exact.
 * ------------------------------------------------------------------------- */

static embediq_osal_atomic32_t g_atomic_counter = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);

static void *atomic_incrementer(void *arg)
{
    (void)arg;
    for (uint32_t i = 0u; i < F1_ITERS; i++) {
        (void)embediq_osal_atomic32_add(&g_atomic_counter, 1u);
    }
    return NULL;
}

static void test_atomic32_add_from_threads(void)
{
    pthread_t th[F1_THREADS];
    embediq_osal_atomic32_store(&g_atomic_counter, 0u);
    for (uint32_t i = 0u; i < F1_THREADS; i++) {
        pthread_create(&th[i], NULL, atomic_incrementer, NULL);
    }
    for (uint32_t i = 0u; i < F1_THREADS; i++) {
        pthread_join(th[i], NULL);
    }
    ASSERT(embediq_osal_atomic32_load(&g_atomic_counter) == F1_THREADS * F1_ITERS,
           "atomic32_add must not lose increments");
}

/* ---------------------------------------------------------------------------
 * test_atomic32_cas_semantics
 * CAS succeeds only when the current value equals expected.
 * ------------------------------------------------------------------------- */

static void test_atomic32_cas_semantics(void)
{
    embediq_osal_atomic32_t a = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);
    bool first      = embediq_osal_atomic32_cas(&a, 0u, 5u);
    uint32_t after1 = embediq_osal_atomic32_load(&a);
    bool second     = embediq_osal_atomic32_cas(&a, 0u, 9u);
    uint32_t after2 = embediq_osal_atomic32_load(&a);
    ASSERT(first && after1 == 5u && !second && after2 == 5u,
           "cas(0->5) succeeds, then cas(0->9) fails and leaves 5");
}

/* ---------------------------------------------------------------------------
 * test_atomic32_lock_free
 * Documents the host guarantee: int-sized atomics are always lock-free.
 * ------------------------------------------------------------------------- */

static void test_atomic32_lock_free(void)
{
    ASSERT(ATOMIC_INT_LOCK_FREE == 2, "host int atomics must be always lock-free");
}

/* ---------------------------------------------------------------------------
 * test_signal_post_wakes_waiter
 * A thread blocked in signal_wait returns after a non-ISR signal_post.
 * ------------------------------------------------------------------------- */

static EmbedIQ_Signal_t       *g_post_sig = NULL;
static embediq_osal_atomic32_t g_post_woke = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);

static void *post_waiter(void *arg)
{
    (void)arg;
    embediq_osal_signal_wait(g_post_sig);
    embediq_osal_atomic32_store(&g_post_woke, 1u);
    return NULL;
}

static void test_signal_post_wakes_waiter(void)
{
    pthread_t th;
    g_post_sig = embediq_osal_signal_create();
    embediq_osal_atomic32_store(&g_post_woke, 0u);
    pthread_create(&th, NULL, post_waiter, NULL);

    embediq_osal_delay_ms(F1_POST_DELAY_MS);
    uint32_t t0 = embediq_osal_time_ms();
    embediq_osal_signal_post(g_post_sig);
    while (embediq_osal_atomic32_load(&g_post_woke) == 0u
           && (embediq_osal_time_ms() - t0) < F1_WAKE_DEADLINE_MS) {
        embediq_osal_delay_ms(1u);
    }
    bool woke = embediq_osal_atomic32_load(&g_post_woke) == 1u;
    if (woke) {
        pthread_join(th, NULL);
    }
    ASSERT(woke, "signal_post must wake the waiter within 200 ms");
}

/* ---------------------------------------------------------------------------
 * test_destroy_functions_do_not_crash
 * mutex_destroy / signal_destroy release their handles; NULL is accepted.
 * The ASan build is the real check.
 * ------------------------------------------------------------------------- */

static void test_destroy_functions_do_not_crash(void)
{
    EmbedIQ_Mutex_t  *m   = embediq_osal_mutex_create();
    EmbedIQ_Signal_t *sig = embediq_osal_signal_create();
    embediq_osal_mutex_destroy(m);
    embediq_osal_signal_destroy(sig);
    embediq_osal_mutex_destroy(NULL);
    embediq_osal_signal_destroy(NULL);
    ASSERT(true, "mutex_destroy and signal_destroy must complete without crash");
}

/* ===========================================================================
 * F4 — atomic send-or-replace-oldest
 * ======================================================================== */

/* ---------------------------------------------------------------------------
 * test_queue_send_replace_oldest_keeps_newest
 * Full depth-4 queue holding 1..4: replace_oldest(5) drops 1 → 2,3,4,5.
 * ------------------------------------------------------------------------- */

static void test_queue_send_replace_oldest_keeps_newest(void)
{
    EmbedIQ_Queue_t *q = embediq_osal_queue_create(F4_Q_DEPTH, sizeof(uint32_t));
    for (uint32_t v = 1u; v <= F4_Q_DEPTH; v++) {
        (void)embediq_osal_queue_send(q, &v, 0u);
    }
    uint32_t five = F4_Q_DEPTH + 1u;
    bool dropped = false;
    embediq_err_t rc = embediq_osal_queue_send_replace_oldest(q, &five, &dropped);

    bool ok = rc == EMBEDIQ_OK && dropped;
    for (uint32_t want = 2u; ok && want <= F4_Q_DEPTH + 1u; want++) {
        uint32_t got = 0u;
        ok = embediq_osal_queue_recv(q, &got, 0u) == EMBEDIQ_OK && got == want;
    }
    embediq_osal_queue_destroy(q);
    ASSERT(ok, "replace_oldest on a full queue must drop the oldest and keep 2,3,4,5");
}

/* ---------------------------------------------------------------------------
 * test_queue_send_replace_oldest_concurrent_with_recv
 * A producer replace_oldest()s 1..50,000 while a consumer receives. Every
 * received value was sent, values arrive strictly increasing, and
 * received + dropped == sent.
 * ------------------------------------------------------------------------- */

static EmbedIQ_Queue_t        *g_f4_q;
static embediq_osal_atomic32_t g_f4_done    = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);
static uint32_t                g_f4_dropped = 0u;   /* producer-only until joined */
static uint32_t                g_f4_received = 0u;  /* consumer-only until joined */
static bool                    g_f4_order_ok = true;

static void *f4_producer(void *arg)
{
    (void)arg;
    for (uint32_t v = 1u; v <= F4_REPLACE_ITEMS; v++) {
        bool dropped = false;
        if (embediq_osal_queue_send_replace_oldest(g_f4_q, &v, &dropped) != EMBEDIQ_OK) {
            g_f4_dropped++;            /* treat a failed enqueue as lost */
        } else if (dropped) {
            g_f4_dropped++;
        }
    }
    embediq_osal_atomic32_store(&g_f4_done, 1u);
    return NULL;
}

static void *f4_consumer(void *arg)
{
    (void)arg;
    uint32_t last = 0u;
    for (;;) {
        uint32_t v = 0u;
        if (embediq_osal_queue_recv(g_f4_q, &v, F4_RECV_POLL_MS) == EMBEDIQ_OK) {
            if (v == 0u || v > F4_REPLACE_ITEMS || v <= last) g_f4_order_ok = false;
            last = v;
            g_f4_received++;
        } else if (embediq_osal_atomic32_load(&g_f4_done) != 0u) {
            break;   /* producer finished and the queue is drained */
        }
    }
    return NULL;
}

static void test_queue_send_replace_oldest_concurrent_with_recv(void)
{
    g_f4_q = embediq_osal_queue_create(F4_Q_DEPTH, sizeof(uint32_t));
    embediq_osal_atomic32_store(&g_f4_done, 0u);
    g_f4_dropped = 0u; g_f4_received = 0u; g_f4_order_ok = true;

    pthread_t prod, cons;
    pthread_create(&cons, NULL, f4_consumer, NULL);
    pthread_create(&prod, NULL, f4_producer, NULL);
    pthread_join(prod, NULL);
    pthread_join(cons, NULL);

    printf("      sent=%u received=%u dropped=%u\n", (unsigned)F4_REPLACE_ITEMS,
           (unsigned)g_f4_received, (unsigned)g_f4_dropped);
    bool ok = g_f4_order_ok && g_f4_received > 0u
              && g_f4_received + g_f4_dropped == F4_REPLACE_ITEMS;
    embediq_osal_queue_destroy(g_f4_q);
    ASSERT(ok, "concurrent replace_oldest: received + dropped == sent, in order");
}

/* ---------------------------------------------------------------------------
 * Entry point
 * ------------------------------------------------------------------------- */

int main(void)
{
    test_task_create_runs_function();
    test_task_delete_stops_execution();
    test_queue_send_recv_roundtrip();
    test_queue_send_timeout_returns_false();
    test_queue_send_block_forever();
    test_signal_wait_and_post();
    test_signal_from_isr_unblocks_waiting_thread();
    test_time_us_increases_monotonically();
    test_mutex_lock_unlock();
    test_mutex_timeout_returns_false();
    test_queue_destroy();
    test_critical_section_mutual_exclusion();
    test_critical_section_is_fast();
    test_atomic32_add_from_threads();
    test_atomic32_cas_semantics();
    test_atomic32_lock_free();
    test_signal_post_wakes_waiter();
    test_destroy_functions_do_not_crash();
    test_queue_send_replace_oldest_keeps_newest();
    test_queue_send_replace_oldest_concurrent_with_recv();

    printf("\nAll %d tests passed. (%d failed)\n",
           g_tests_run - g_tests_failed, g_tests_failed);
    return g_tests_failed > 0 ? 1 : 0;
}
