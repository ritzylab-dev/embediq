/*
 * embediq_timer.h — Timer platform FB contract
 *
 * fb_timer publishes periodic tick messages onto the bus.
 * Application FBs SUBSCRIBE to tick messages — they do not call any timer
 * functions directly. This header defines the tick payload type and documents
 * the subscription pattern.
 *
 * Message IDs (from embediq_platform_msgs.h):
 *   MSG_TIMER_1MS    0x04B0   Published every 1 ms
 *   MSG_TIMER_10MS   0x04B1   Published every 10 ms
 *   MSG_TIMER_100MS  0x04B2   Published every 100 ms
 *   MSG_TIMER_1SEC   0x04B3   Published every 1 second
 *
 * Drift correction: fb_timer uses a drift-corrected sleep loop so that
 * accumulated tick count remains accurate over long run times. Callers
 * should use tick_count for accurate counting, not timestamp_us arithmetic.
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
 * Tick payload structure
 *
 * Carried in the payload[] bytes of EmbedIQ_Msg_t for all MSG_TIMER_* messages.
 * Deserialise with: memcpy(&tick, msg->payload, sizeof(embediq_timer_tick_t));
 *
 * tick_count:    Monotonic tick counter since boot. Use for gap detection and
 *                rate measurement. Never wraps within a normal session.
 * timestamp_us:  embediq_time_us() at publish time. Use for display / logging
 *                only — see I-13: timestamp_us wraps at ~71 min on MCU.
 * ------------------------------------------------------------------------- */

typedef struct {
    uint32_t tick_count;    /**< Monotonic tick counter since boot (no wrap in session). */
    uint64_t timestamp_us;  /**< embediq_time_us() at publish time. Informational only. */
} embediq_timer_tick_t;

/* ---------------------------------------------------------------------------
 * Usage pattern (no direct API — subscribe via the message bus)
 *
 *   EMBEDIQ_SUBS(my_subs, MSG_TIMER_1SEC);
 *
 *   static void on_tick_1sec(EmbedIQ_FB_Handle_t fb,
 *                            const EmbedIQ_Msg_t *msg)
 *   {
 *       embediq_timer_tick_t tick;
 *       memcpy(&tick, msg->payload, sizeof(tick));
 *       // use tick.tick_count ...
 *   }
 *
 *   embediq_subfn_register(fb, MSG_TIMER_1SEC, on_tick_1sec);
 * ------------------------------------------------------------------------- */

#ifdef __cplusplus
}
#endif

#endif /* EMBEDIQ_TIMER_H */
