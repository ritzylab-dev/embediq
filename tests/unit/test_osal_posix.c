#define _POSIX_C_SOURCE 200809L

/*
 * tests/unit/test_osal_posix.c — Unit tests for OSAL POSIX implementation
 *
 * Covers every contract in embediq_osal.h. Zero external test framework —
 * stdlib only. Each test exercises one behaviour with one assertion.
 *
 * Run:      ./build/tests/test_osal_posix
 * Expected: "All 10 tests passed. (0 failed)"
 *
 * ASAN:
 *   cmake -B build_asan -DEMBEDIQ_PLATFORM=host \
 *         -DCMAKE_C_FLAGS="-fsanitize=address -g"
 *   cmake --build build_asan
 *   ./build_asan/tests/test_osal_posix
 *
 * Note on memory leaks: v1 OSAL has no destroy API — handles are long-lived
 * in firmware. LSAN may report leaks at process exit; that is expected.
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "embediq_osal.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

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

static volatile int g_task_ran = 0;

static void task_set_flag(void *arg)
{
    (void)arg;
    g_task_ran = 1;
}

static void test_task_create_runs_function(void)
{
    g_task_ran = 0;
    EmbedIQ_Task_t *t = embediq_osal_task_create(
            "flag_setter", task_set_flag, NULL, 1, 4096);
    while (!g_task_ran) { nanosleep(&(struct timespec){0, 500 * 1000L}, NULL); }
    embediq_osal_task_delete(t);
    ASSERT(g_task_ran == 1, "task function must run");
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
    EmbedIQ_Signal_t *sig;
    volatile bool    *unblocked;
} isr_ctx_t;

static void isr_waiter(void *arg)
{
    isr_ctx_t *ctx = (isr_ctx_t *)arg;
    embediq_osal_signal_wait(ctx->sig);
    *ctx->unblocked = true;
}

static void isr_poster(void *arg)
{
    nanosleep(&(struct timespec){0, 20000 * 1000L}, NULL);                     /* ensure waiter is blocked first */
    embediq_osal_signal_from_isr((EmbedIQ_Signal_t *)arg);
}

static void test_signal_from_isr_unblocks_waiting_thread(void)
{
    EmbedIQ_Signal_t *sig  = embediq_osal_signal_create();
    volatile bool unblocked = false;
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
    while (!unblocked) { nanosleep(&(struct timespec){0, 500 * 1000L}, NULL); }

    embediq_osal_task_delete(poster);  /* already exited — cancel is no-op */
    embediq_osal_task_delete(waiter);  /* already exited — cancel is no-op */
    ASSERT(unblocked, "waiter thread must be unblocked by signal_from_isr");
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

    printf("\nAll %d tests passed. (%d failed)\n",
           g_tests_run - g_tests_failed, g_tests_failed);
    return g_tests_failed > 0 ? 1 : 0;
}
