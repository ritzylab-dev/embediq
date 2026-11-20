/*
 * embediq_timer.h — Timer platform FB contract
 *
 * fb_timer publishes periodic tick messages onto the bus. Application FBs
 * SUBSCRIBE to them — they do not call any timer function. A tick message is
 * a notification: its payload is empty. This header names the tick record
 * type and shows the subscription pattern.
 *
 * Message IDs (from embediq_platform_msgs.h):
 *   MSG_TIMER_1MS    0x04B0   Published every 1 ms
 *   MSG_TIMER_10MS   0x04B1   Published every 10 ms
 *   MSG_TIMER_100MS  0x04B2   Published every 100 ms
 *   MSG_TIMER_1SEC   0x04B3   Published every 1 second
 *
 * Drift correction: fb_timer uses a drift-corrected sleep loop so that
 * accumulated tick count remains accurate over long run times.
 *
 * I-01: Compiles standalone with zero OSAL or BSP dependencies.
 * R-03: C11. Fixed-width types from <stdint.h> only.
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef EMBEDIQ_TIMER_H
#define EMBEDIQ_TIMER_H

#include "embediq_platform_msgs.h"  /* MSG_TIMER_1MS/10MS/100MS/1SEC */
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
 * Timer FB fault codes — state_or_flag of the EMBEDIQ_OBS_EVT_FAULT event the
 * timer FB emits. Distinct from the EmbedIQ_FB_State_t values (0–5) and from
 * every engine fault code in embediq_fb.h (0x80–0x83).
 * ------------------------------------------------------------------------- */

#define EMBEDIQ_FAULT_TIMER_HAL_INIT   0x90u   /**< hal_timer_init() refused the timer FB's configuration */
#define EMBEDIQ_FAULT_TIMER_HAL_START  0x91u   /**< hal_timer_start() failed                              */

/* ---------------------------------------------------------------------------
 * Tick record
 *
 * Tick record type. The timer FB publishes MSG_TIMER_* with an empty payload;
 * this type is kept for code that carries a tick count and a timestamp of its
 * own.
 *
 * tick_count:    Monotonic tick counter. Use for gap detection and rate
 *                measurement.
 * timestamp_us:  timestamp in microseconds supplied by the producer;
 *                informational — see I-13: never use it for ordering.
 * ------------------------------------------------------------------------- */

typedef struct {
    uint32_t tick_count;    /**< Monotonic tick counter (no wrap in session). */
    uint64_t timestamp_us;  /**< timestamp in microseconds supplied by the producer; informational */
} embediq_timer_tick_t;

/* ---------------------------------------------------------------------------
 * Usage pattern (no direct API — subscribe through a sub-function)
 *
 *   static void on_tick_1sec(EmbedIQ_FB_Handle_t fb, const void *msg,
 *                            void *fb_data, void *subfn_data)
 *   {
 *       (void)fb; (void)fb_data; (void)subfn_data;
 *       (void)msg;   // a tick carries no payload: msg is not read
 *       // one second has passed — act on it here
 *   }
 *
 *   // inside the FB's init_fn:
 *   static const uint16_t k_subs[] = { MSG_TIMER_1SEC };
 *   static EmbedIQ_SubFn_Config_t k_cfg = {
 *       .name               = "tick_1sec",
 *       .init_order         = 1u,
 *       .run_fn             = on_tick_1sec,
 *       .subscriptions      = k_subs,
 *       .subscription_count = 1u,
 *   };
 *   embediq_subfn_register(fb, &k_cfg);
 *
 * The FB also lists MSG_TIMER_1SEC in its own subscriptions.
 * ------------------------------------------------------------------------- */

#ifdef __cplusplus
}
#endif

#endif /* EMBEDIQ_TIMER_H */
