#define _POSIX_C_SOURCE 200809L

/*
 * hal/posix/hal_timer_posix.c — POSIX implementation of the HAL timer contract
 *
 * Fires the registered callback periodically using a dedicated pthread that
 * sleeps with CLOCK_MONOTONIC drift correction.  next_fire is tracked as an
 * absolute timespec so accumulated sleep overshoot is compensated each tick.
 *
 * Only includes: hal_timer.h (core/include/hal) and C stdlib headers.
 * R-02: no malloc in this file.
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "hal_timer.h"

#include <pthread.h>
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
static volatile int         s_running   = 0;
static int                  s_inited    = 0;

/* ---------------------------------------------------------------------------
 * Timer thread — drift-corrected periodic callback
 * ------------------------------------------------------------------------- */

static void *timer_thread(void *arg)
{
    (void)arg;

    struct timespec next_fire;
    clock_gettime(CLOCK_MONOTONIC, &next_fire);

    while (s_running) {
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

        if (s_running && s_cb) {
            s_cb(s_ctx);
        }
    }
    return NULL;
}

/* ---------------------------------------------------------------------------
 * HAL timer contract implementation
 * ------------------------------------------------------------------------- */

int hal_timer_init(uint32_t period_us, hal_timer_isr_t cb, void *ctx)
{
    if (!cb) {
        return HAL_ERR_INVALID;
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
        return HAL_ERR_INVALID;
    }
    s_running = 1;
    if (pthread_create(&s_thread, NULL, timer_thread, NULL) != 0) {
        s_running = 0;
        return HAL_ERR_IO;
    }
    return HAL_OK;
}

int hal_timer_stop(void)
{
    if (!s_inited) {
        return HAL_ERR_INVALID;
    }
    s_running = 0;
    return HAL_OK;
}

void hal_timer_deinit(void)
{
    if (!s_inited) {
        return;
    }
    s_running = 0;
    pthread_join(s_thread, NULL);
    s_cb        = NULL;
    s_ctx       = NULL;
    s_period_us = 0u;
    s_inited    = 0;
}
