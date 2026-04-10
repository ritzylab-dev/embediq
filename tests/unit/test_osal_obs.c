#define _POSIX_C_SOURCE 200809L

/*
 * tests/unit/test_osal_obs.c — Behavioral tests for OSAL Observatory fault emission
 *
 * Verifies that OSAL operations on failure paths emit EMBEDIQ_OBS_EVT_OSAL_FAULT
 * to the Observatory ring (XOBS-1, PR #92). Tests: queue-full, mutex timeout,
 * and signal timeout. Task-creation failure is not tested (requires resource
 * exhaustion, not deterministically triggerable).
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "embediq_osal.h"
#include "embediq_obs.h"

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

/* Observatory internal API (host builds only — EMBEDIQ_PLATFORM_HOST) */
extern void     obs__reset(void);
extern uint32_t obs__ring_count(void);
extern bool     obs__ring_read(uint32_t idx, EmbedIQ_Event_t *out);

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

/* ---------------------------------------------------------------------------
 * OSAL test 1: queue send on full queue emits OSAL FAULT
 * ------------------------------------------------------------------------- */

static void test_osal_obs_queue_full_emits_fault(void)
{
    obs__reset();
    EmbedIQ_Queue_t *q = embediq_osal_queue_create(1u, sizeof(uint8_t));
    uint8_t item = 0x42u;

    embediq_osal_queue_send(q, &item, 0u);  /* fill the single-depth queue */
    /* Second send with timeout=0 — queue full, must emit FAULT immediately */
    embediq_osal_queue_send(q, &item, 0u);

    EmbedIQ_Event_t evt = {0};
    ASSERT(obs__ring_count() >= 1u,
           "queue send on full queue must emit at least one FAULT event");
    obs__ring_read(0u, &evt);
    ASSERT(evt.event_type    == EMBEDIQ_OBS_EVT_OSAL_FAULT,
           "event_type must be EMBEDIQ_OBS_EVT_OSAL_FAULT (0x66)");
    ASSERT(evt.source_fb_id  == EMBEDIQ_OSAL_SRC_QUEUES,
           "source_fb_id must be EMBEDIQ_OSAL_SRC_QUEUES");
    ASSERT(evt.state_or_flag == 2u,
           "state_or_flag must be 2 (QUEUE_FULL encoding)");

    embediq_osal_queue_destroy(q);
}

/* ---------------------------------------------------------------------------
 * OSAL test 2: mutex lock timeout emits OSAL FAULT
 * ------------------------------------------------------------------------- */

static EmbedIQ_Mutex_t  *g_osal_obs_mutex       = NULL;
static EmbedIQ_Signal_t *g_osal_obs_lock_ready   = NULL;
static EmbedIQ_Signal_t *g_osal_obs_lock_release = NULL;

static void hold_mutex_task(void *arg)
{
    (void)arg;
    embediq_osal_mutex_lock(g_osal_obs_mutex, UINT32_MAX);
    embediq_osal_signal_from_isr(g_osal_obs_lock_ready);   /* signal: lock acquired */
    embediq_osal_signal_wait(g_osal_obs_lock_release);      /* hold until told to release */
    embediq_osal_mutex_unlock(g_osal_obs_mutex);
}

static void test_osal_obs_mutex_timeout_emits_fault(void)
{
    obs__reset();
    g_osal_obs_mutex        = embediq_osal_mutex_create();
    g_osal_obs_lock_ready   = embediq_osal_signal_create();
    g_osal_obs_lock_release = embediq_osal_signal_create();

    EmbedIQ_Task_t *t = embediq_osal_task_create(
            "hold_mutex", hold_mutex_task, NULL, 1u, 4096u);
    embediq_osal_signal_wait(g_osal_obs_lock_ready);  /* wait until holder has lock */

    /* Lock with timeout=0 while held by another thread — must fail + emit FAULT */
    embediq_osal_mutex_lock(g_osal_obs_mutex, 0u);

    EmbedIQ_Event_t evt = {0};
    ASSERT(obs__ring_count() >= 1u,
           "mutex timeout must emit at least one FAULT event");
    obs__ring_read(0u, &evt);
    ASSERT(evt.event_type    == EMBEDIQ_OBS_EVT_OSAL_FAULT,
           "event_type must be EMBEDIQ_OBS_EVT_OSAL_FAULT");
    ASSERT(evt.source_fb_id  == EMBEDIQ_OSAL_SRC_MUTEXES,
           "source_fb_id must be EMBEDIQ_OSAL_SRC_MUTEXES");
    ASSERT(evt.state_or_flag == 1u,
           "state_or_flag must be 1 (MUTEX_TIMEOUT encoding)");

    embediq_osal_signal_from_isr(g_osal_obs_lock_release);  /* release holder */
    embediq_osal_task_delete(t);
}

/* ---------------------------------------------------------------------------
 * OSAL test 3: signal wait timeout emits OSAL FAULT
 * ------------------------------------------------------------------------- */

static void test_osal_obs_signal_timeout_emits_fault(void)
{
    obs__reset();
    EmbedIQ_Signal_t *sig = embediq_osal_signal_create();

    /* Wait with 1 ms timeout — signal never posted, must timeout + emit FAULT */
    embediq_osal_signal_wait_timeout(sig, 1u);

    EmbedIQ_Event_t evt = {0};
    ASSERT(obs__ring_count() >= 1u,
           "signal wait timeout must emit at least one FAULT event");
    obs__ring_read(0u, &evt);
    ASSERT(evt.event_type    == EMBEDIQ_OBS_EVT_OSAL_FAULT,
           "event_type must be EMBEDIQ_OBS_EVT_OSAL_FAULT");
    ASSERT(evt.source_fb_id  == EMBEDIQ_OSAL_SRC_SIGNALS,
           "source_fb_id must be EMBEDIQ_OSAL_SRC_SIGNALS");
    ASSERT(evt.state_or_flag == 3u,
           "state_or_flag must be 3 (SIGNAL_TIMEOUT encoding)");
}

/* ---------------------------------------------------------------------------
 * Entry point
 * ------------------------------------------------------------------------- */

int main(void)
{
    test_osal_obs_queue_full_emits_fault();
    test_osal_obs_mutex_timeout_emits_fault();
    test_osal_obs_signal_timeout_emits_fault();

    printf("\n");
    if (g_tests_failed == 0) {
        printf("All %d tests passed. (0 failed)\n", g_tests_run);
    } else {
        printf("%d of %d tests FAILED.\n", g_tests_failed, g_tests_run);
    }
    return (g_tests_failed > 0) ? 1 : 0;
}
