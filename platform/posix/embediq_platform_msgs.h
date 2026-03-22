/*
 * platform/posix/embediq_platform_msgs.h — Platform FB message ID definitions
 *
 * All Platform FB message IDs live in the official EmbedIQ component namespace
 * (0x0400–0x13FF per AGENTS.md §3B).  Application FBs subscribe to these IDs
 * to receive timer ticks, watchdog signals, and NVM notifications.
 *
 * Ranges:
 *   0x0401–0x0404  fb_timer   tick publications
 *   0x0405–0x0406  fb_watchdog diagnostic events
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef EMBEDIQ_PLATFORM_MSGS_H
#define EMBEDIQ_PLATFORM_MSGS_H

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
 * fb_timer — periodic tick messages
 * ------------------------------------------------------------------------- */

#define MSG_TIMER_1MS     0x0401u  /**< Published every 1 ms  */
#define MSG_TIMER_10MS    0x0402u  /**< Published every 10 ms */
#define MSG_TIMER_100MS   0x0403u  /**< Published every 100 ms */
#define MSG_TIMER_1SEC    0x0404u  /**< Published every 1 second */

/* ---------------------------------------------------------------------------
 * fb_watchdog — health monitoring messages
 * ------------------------------------------------------------------------- */

#define MSG_WDG_MISSED_CHECKIN  0x0405u  /**< A registered FB missed its checkin */
#define MSG_WDG_RESET_REASON    0x0406u  /**< Reset reason reported on startup */

#ifdef __cplusplus
}
#endif

#endif /* EMBEDIQ_PLATFORM_MSGS_H */
