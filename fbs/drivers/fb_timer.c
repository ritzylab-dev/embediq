/*
 * fbs/drivers/fb_timer.c — Timer Driver FB (portable)
 *
 * Publishes periodic tick messages at 1 ms, 10 ms, 100 ms, and 1 second
 * intervals.  Uses the HAL timer contract (hal/hal_timer.h) — no POSIX or
 * platform-specific headers included.
 *
 * Optimisation: MSG_TIMER_1MS is only published if at least one registered FB
 * subscribes to it (checked during init_fn, after all FBs are registered).
 * The other tick periods are always published.
 *
 * Message IDs (core/include/embediq_platform_msgs.h):
 *   MSG_TIMER_1MS   = 0x04B0
 *   MSG_TIMER_10MS  = 0x04B1
 *   MSG_TIMER_100MS = 0x04B2
 *   MSG_TIMER_1SEC  = 0x04B3
 *
 * Boot phase: EMBEDIQ_BOOT_PHASE_PLATFORM (1)
 *
 * Host/test-only API (EMBEDIQ_PLATFORM_HOST):
 *   fb_timer__start()         — init + start HAL timer without engine_boot
 *   fb_timer__stop_and_wait() — stop + deinit HAL timer
 *   fb_timer__reset_counts()  — zero all tick counters
 *   fb_timer__count_1ms()     — ticks fired since last reset
 *   fb_timer__count_10ms()
 *   fb_timer__count_100ms()
 *   fb_timer__count_1sec()
 *   fb_timer__has_1ms_sub()   — true if MSG_TIMER_1MS subscriber found at init
 *
 * R-02: no malloc in this file.
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "embediq_platform_msgs.h"
#include "embediq_fb.h"
#include "embediq_bus.h"
#include "embediq_msg.h"
#include "embediq_osal.h"
#include "embediq_config.h"
#include "embediq_obs.h"
#include "hal/hal_timer.h"

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* ---------------------------------------------------------------------------
 * Module state
 * ------------------------------------------------------------------------- */

static EmbedIQ_FB_Handle_t  g_fb          = NULL;
static embediq_bus_token_t *s_token       = NULL;  /* async-post identity of g_fb */
static embediq_osal_atomic32_t g_running  = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);  /* read by the HAL timer thread */
static bool                 g_has_1ms_sub = false;
static volatile uint32_t    g_tick        = 0u;

#ifdef EMBEDIQ_PLATFORM_HOST
static volatile uint32_t    g_cnt_1ms     = 0u;
static volatile uint32_t    g_cnt_10ms    = 0u;
static volatile uint32_t    g_cnt_100ms   = 0u;
static volatile uint32_t    g_cnt_1sec    = 0u;
#endif /* EMBEDIQ_PLATFORM_HOST */

/* ---------------------------------------------------------------------------
 * HAL timer callback — invoked every 1 ms on the HAL timer thread (ISR-like
 * context): posts through embediq_bus_post_async(), which never blocks.
 *
 * g_tick is touched only by this callback; fb_timer__reset_counts() writes it
 * from tests strictly after fb_timer__stop_and_wait() has joined the timer
 * thread, so it needs no atomic.
 * ------------------------------------------------------------------------- */

static void post_tick(uint16_t msg_id)
{
    if (!s_token) return;
    EmbedIQ_Msg_t m;
    memset(&m, 0, sizeof(m));
    m.msg_id   = msg_id;
    m.priority = EMBEDIQ_MSG_PRIORITY_NORMAL;
    if (embediq_bus_post_async(s_token, &m) != EMBEDIQ_OK) {
        /* NORMAL never fails: overflow drops the oldest and is counted and
         * observed by the bus. Nothing further to do from a timer callback. */
    }
}

static void timer_callback(void *ctx)
{
    (void)ctx;

    if (embediq_osal_atomic32_load(&g_running) == 0u) {
        return;
    }

    uint32_t tick = ++g_tick;

#ifdef EMBEDIQ_PLATFORM_HOST
    g_cnt_1ms++;
#endif

    /* 1 ms — only publish if a subscriber was found at init. */
    if (g_has_1ms_sub) {
        post_tick(MSG_TIMER_1MS);
    }

    /* 10 ms */
    if ((tick % 10u) == 0u) {
#ifdef EMBEDIQ_PLATFORM_HOST
        g_cnt_10ms++;
#endif
        post_tick(MSG_TIMER_10MS);
    }

    /* 100 ms */
    if ((tick % 100u) == 0u) {
#ifdef EMBEDIQ_PLATFORM_HOST
        g_cnt_100ms++;
#endif
        post_tick(MSG_TIMER_100MS);
    }

    /* 1 s */
    if ((tick % 1000u) == 0u) {
#ifdef EMBEDIQ_PLATFORM_HOST
        g_cnt_1sec++;
#endif
        post_tick(MSG_TIMER_1SEC);
    }
}

/* ---------------------------------------------------------------------------
 * FB init / exit (called by embediq_engine_boot)
 * ------------------------------------------------------------------------- */

static void timer_init(EmbedIQ_FB_Handle_t fb, void *fb_data)
{
    (void)fb_data;
    g_fb = fb;

    /* Scan all registered FBs to detect a MSG_TIMER_1MS subscriber. */
    g_has_1ms_sub = false;
    uint8_t n = embediq_engine_fb_count();
    for (uint8_t i = 0u; i < n && !g_has_1ms_sub; i++) {
        const EmbedIQ_FB_Config_t *cfg = embediq_engine_fb_config(i);
        if (!cfg) continue;
        for (uint8_t s = 0u; s < cfg->subscription_count; s++) {
            if (cfg->subscriptions[s] == MSG_TIMER_1MS) {
                g_has_1ms_sub = true;
                break;
            }
        }
    }

    s_token = embediq_bus_token_for(g_fb);
    embediq_osal_atomic32_store(&g_running, 1u);
    hal_timer_init(1000u, timer_callback, NULL);
    hal_timer_start();
}

static void timer_exit(EmbedIQ_FB_Handle_t fb, void *fb_data)
{
    (void)fb; (void)fb_data;
    embediq_osal_atomic32_store(&g_running, 0u);
    hal_timer_stop();
    hal_timer_deinit();
}

/* ---------------------------------------------------------------------------
 * Public: fb_timer_register()
 * Call before embediq_engine_boot().
 * ------------------------------------------------------------------------- */

EMBEDIQ_PUBS(g_timer_pubs,
    MSG_TIMER_1MS,
    MSG_TIMER_10MS,
    MSG_TIMER_100MS,
    MSG_TIMER_1SEC);

EmbedIQ_FB_Handle_t fb_timer_register(void)
{
    static const EmbedIQ_FB_Config_t k_cfg = {
        .name              = "fb_timer",
        .boot_phase        = EMBEDIQ_BOOT_PHASE_PLATFORM,
        .init_fn           = timer_init,
        .exit_fn           = timer_exit,
        .publications      = g_timer_pubs,
        .publication_count = 4u,
    };
    return embediq_fb_register(&k_cfg);
}

/* ---------------------------------------------------------------------------
 * Test-only API — EMBEDIQ_PLATFORM_HOST only
 * ------------------------------------------------------------------------- */

#ifdef EMBEDIQ_PLATFORM_HOST

/** Start the HAL timer directly without going through embediq_engine_boot(). */
void fb_timer__start(void)
{
    s_token = g_fb ? embediq_bus_token_for(g_fb) : NULL;
    embediq_osal_atomic32_store(&g_running, 1u);
    hal_timer_init(1000u, timer_callback, NULL);
    hal_timer_start();
}

/**
 * Stop the HAL timer and wait for any in-flight callback to complete.
 * Safe to call even if the timer is already stopped.
 */
void fb_timer__stop_and_wait(void)
{
    embediq_osal_atomic32_store(&g_running, 0u);
    hal_timer_stop();
    embediq_osal_delay_ms(20u);
    hal_timer_deinit();
}

/** Reset all tick counters and the tick accumulator to zero. */
void fb_timer__reset_counts(void)
{
    g_tick      = 0u;
    g_cnt_1ms   = 0u;
    g_cnt_10ms  = 0u;
    g_cnt_100ms = 0u;
    g_cnt_1sec  = 0u;
}

uint32_t fb_timer__count_1ms(void)   { return g_cnt_1ms;   }
uint32_t fb_timer__count_10ms(void)  { return g_cnt_10ms;  }
uint32_t fb_timer__count_100ms(void) { return g_cnt_100ms; }
uint32_t fb_timer__count_1sec(void)  { return g_cnt_1sec;  }

/** True if MSG_TIMER_1MS subscriber was detected during the last init. */
bool fb_timer__has_1ms_sub(void) { return g_has_1ms_sub; }

#endif /* EMBEDIQ_PLATFORM_HOST */
