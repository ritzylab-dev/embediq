#define _POSIX_C_SOURCE 200809L

/*
 * hal/posix/hal_timer_posix.c — POSIX implementation of the HAL timer contract
 *
 * Fires the registered callback periodically using a dedicated pthread that
 * sleeps with CLOCK_MONOTONIC drift correction.  next_fire is tracked as an
 * absolute timespec so accumulated sleep overshoot is compensated each tick.
 *
 * Worker ownership. At most one worker exists. s_worker_owned is 1 from a
 * successful pthread_create until that thread has been joined; s_thread is
 * never overwritten while it is. stop() only clears s_running: the worker
 * sees it after its current sleep (and callback, if one is running) and
 * returns, but stays owned. start() on a stopped timer first joins that
 * worker — with s_running still 0, so it cannot fire again — and only then
 * sets s_running and creates the replacement. deinit() clears s_running and
 * joins the worker before it clears the callback, context and period. A
 * failed join keeps the worker owned and the configuration it may still read.
 * init() never changes the configuration while a worker is owned, so the
 * worker reads s_cb, s_ctx and s_period_us only as published by the
 * pthread_create that started it.
 *
 * Calling context: init, start, stop and deinit are called by the timer's
 * owner thread, one at a time. start() and deinit() may wait for the worker
 * to finish its current sleep and callback. The callback may call stop();
 * it never calls init, start or deinit (start or deinit would join the
 * calling thread).
 *
 * Only includes: hal_timer.h (core/include/hal) and C stdlib headers
 * (<stdatomic.h> is C11 libc; this file includes no embediq_*.h — what a HAL
 * source may include is listed in ARCHITECTURE.md, Layer Boundary Rules)
 * R-02: no malloc in this file.
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "hal_timer.h"

#include <pthread.h>
#include <stdatomic.h>
#include <time.h>
#include <string.h>
#include <stdint.h>

/* ---------------------------------------------------------------------------
 * Module state
 * ------------------------------------------------------------------------- */

static hal_timer_isr_t      s_cb        = NULL;
static void                *s_ctx       = NULL;
static uint32_t             s_period_us = 0u;
static pthread_t            s_thread;
static _Atomic int          s_running   = 0;  /* written by start/stop/deinit and by a
                                                 callback calling stop(); read by the worker */
static int                  s_inited    = 0;
static int                  s_worker_owned = 0;   /* s_thread is a joinable worker */

#ifdef EMBEDIQ_TEST_HOOKS
/* Test hold point: called by the worker after it has read the running flag
 * as set and before it calls the callback — the callback is then selected.
 * Atomic: a test may set it while a worker runs. */
typedef void (*hal_timer_select_hook_t)(void);
static _Atomic(hal_timer_select_hook_t) s_select_hook = NULL;

/** Install (fn) or remove (NULL) the selected-callback hold point (test use only). */
void hal_timer__set_select_hook(hal_timer_select_hook_t fn)
{
    atomic_store(&s_select_hook, fn);
}
#endif

/* ---------------------------------------------------------------------------
 * Timer thread — drift-corrected periodic callback
 * ------------------------------------------------------------------------- */

static void *timer_thread(void *arg)
{
    (void)arg;

    struct timespec next_fire;
    clock_gettime(CLOCK_MONOTONIC, &next_fire);

    while (atomic_load(&s_running)) {
        /* Advance absolute next-fire by period_us. */
        long add_ns = (long)s_period_us * 1000L;
        next_fire.tv_nsec += add_ns;
        while (next_fire.tv_nsec >= 1000000000L) {
            next_fire.tv_nsec -= 1000000000L;
            next_fire.tv_sec  += 1;
        }

        /* Drift-corrected sleep. */
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        long diff_ns = (long)((next_fire.tv_sec  - now.tv_sec)  * 1000000000L
                             + (next_fire.tv_nsec - now.tv_nsec));
        if (diff_ns > 0L) {
            struct timespec sleep_dur;
            sleep_dur.tv_sec  = diff_ns / 1000000000L;
            sleep_dur.tv_nsec = diff_ns % 1000000000L;
            nanosleep(&sleep_dur, NULL);
        }

        if (atomic_load(&s_running) && s_cb) {
#ifdef EMBEDIQ_TEST_HOOKS
            hal_timer_select_hook_t hook = atomic_load(&s_select_hook);
            if (hook) hook();
#endif
            s_cb(s_ctx);
        }
    }
    return NULL;
}

/* ---------------------------------------------------------------------------
 * HAL timer contract implementation
 * ------------------------------------------------------------------------- */

/* Join the owned worker. Called with s_running already 0, so the worker
 * cannot fire again. On failure the worker stays owned (it may still read
 * the configuration) and the error is observed. */
static int retire_worker(void)
{
    if (!s_worker_owned) return HAL_OK;
    if (pthread_join(s_thread, NULL) != 0) {
        EMBEDIQ_HAL_OBS_EMIT_ERROR(EMBEDIQ_HAL_SRC_TIMER, HAL_ERR_IO);
        return HAL_ERR_IO;
    }
    s_worker_owned = 0;
    return HAL_OK;
}

int hal_timer_init(uint32_t period_us, hal_timer_isr_t cb, void *ctx)
{
    if (!cb || period_us == 0u) {
        EMBEDIQ_HAL_OBS_EMIT_ERROR(EMBEDIQ_HAL_SRC_TIMER, HAL_ERR_INVALID);
        return HAL_ERR_INVALID;
    }
    if (s_worker_owned) {
        /* A worker (running, or stopped and not yet retired) may read the
         * configuration: deinit first. */
        EMBEDIQ_HAL_OBS_EMIT_ERROR(EMBEDIQ_HAL_SRC_TIMER, HAL_ERR_BUSY);
        return HAL_ERR_BUSY;
    }

    s_cb        = cb;
    s_ctx       = ctx;
    s_period_us = period_us;
    s_inited    = 1;
    return HAL_OK;
}

int hal_timer_start(void)
{
    if (!s_inited) {
        EMBEDIQ_HAL_OBS_EMIT_ERROR(EMBEDIQ_HAL_SRC_TIMER, HAL_ERR_INVALID);
        return HAL_ERR_INVALID;
    }
    if (atomic_load(&s_running)) {
        return HAL_OK;   /* already running: no second worker */
    }
    /* Retire the stopped worker first, while s_running is still 0. */
    int rc = retire_worker();
    if (rc != HAL_OK) return rc;   /* observed by retire_worker */

    atomic_store(&s_running, 1);
    if (pthread_create(&s_thread, NULL, timer_thread, NULL) != 0) {
        atomic_store(&s_running, 0);
        EMBEDIQ_HAL_OBS_EMIT_ERROR(EMBEDIQ_HAL_SRC_TIMER, HAL_ERR_IO);
        return HAL_ERR_IO;
    }
    s_worker_owned = 1;
    return HAL_OK;
}

int hal_timer_stop(void)
{
    if (!s_inited) {
        EMBEDIQ_HAL_OBS_EMIT_ERROR(EMBEDIQ_HAL_SRC_TIMER, HAL_ERR_INVALID);
        return HAL_ERR_INVALID;
    }
    atomic_store(&s_running, 0);
    return HAL_OK;
}

void hal_timer_deinit(void)
{
    if (!s_inited) {
        return;
    }
    atomic_store(&s_running, 0);
    if (retire_worker() != HAL_OK) {
        return;   /* the worker may still read the configuration: keep it */
    }
    s_cb        = NULL;
    s_ctx       = NULL;
    s_period_us = 0u;
    s_inited    = 0;
}
