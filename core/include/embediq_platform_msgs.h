/*
 * embediq_platform_msgs.h — Platform FB message ID definitions
 *
 * Lives in core/include/ so that application FBs can subscribe to timer,
 * watchdog, NVM, and cloud messages without including platform-layer headers
 * (AGENTS.md §3 layer dependency rule — Shell 2 must not be included by Shell 3).
 *
 * Namespace: 0x0400–0x13FF  Official EmbedIQ components (AGENTS.md §2A)
 *
 * Ranges:
 *   0x0401–0x0404  fb_timer   periodic tick publications
 *   0x0405–0x0406  fb_watchdog health diagnostics
 *   0x0407–0x040F  fb_nvm     store/retrieve notifications (reserved, IDs TBD)
 *   0x0410–0x041F  fb_cloud_mqtt connection + delivery events (reserved, IDs TBD)
 *   0x0420–0x042F  thermostat application messages (see messages/thermostat.iq)
 *   0x0430–0x043F  fb_ota     firmware update lifecycle (reserved, IDs TBD)
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
 * fb_timer — periodic tick messages (0x0401–0x0404)
 * ------------------------------------------------------------------------- */

#define MSG_TIMER_1MS     0x0401u  /**< Published every 1 ms   */
#define MSG_TIMER_10MS    0x0402u  /**< Published every 10 ms  */
#define MSG_TIMER_100MS   0x0403u  /**< Published every 100 ms */
#define MSG_TIMER_1SEC    0x0404u  /**< Published every 1 second */

/* ---------------------------------------------------------------------------
 * fb_watchdog — health monitoring messages (0x0405–0x0406)
 * ------------------------------------------------------------------------- */

#define MSG_WDG_MISSED_CHECKIN  0x0405u  /**< A registered FB missed its checkin deadline */
#define MSG_WDG_RESET_REASON    0x0406u  /**< Reset reason emitted once on startup */

/* ---------------------------------------------------------------------------
 * fb_nvm — non-volatile store notifications (0x0407–0x040F, reserved)
 *
 * IDs assigned at implementation (P2-T*). Reserve the range now so no
 * other module claims these IDs first.
 * ------------------------------------------------------------------------- */

/* 0x0407 — MSG_NVM_WRITE_DONE      (reserved — P2 implementation) */
/* 0x0408 — MSG_NVM_FLUSH_DONE      (reserved — P2 implementation) */
/* 0x0409–0x040F — reserved for additional NVM events              */

/* ---------------------------------------------------------------------------
 * fb_cloud_mqtt — connectivity and delivery events (0x0410–0x041F, reserved)
 *
 * IDs assigned at implementation (P2-T4). Reserve range.
 * ------------------------------------------------------------------------- */

/* 0x0410 — MSG_MQTT_CONNECTED      (reserved — P2 implementation) */
/* 0x0411 — MSG_MQTT_DISCONNECTED   (reserved — P2 implementation) */
/* 0x0412 — MSG_MQTT_MESSAGE_RX     (reserved — P2 implementation) */
/* 0x0413–0x041F — reserved for additional MQTT events             */

/* ---------------------------------------------------------------------------
 * fb_ota — firmware update lifecycle (0x0430–0x043F, reserved)
 *
 * 0x0420–0x042F is the thermostat application namespace (thermostat.iq).
 * fb_ota starts at 0x0430 to avoid collision.
 * IDs assigned at implementation (P2-T5). Reserve range.
 * ------------------------------------------------------------------------- */

/* 0x0430 — MSG_OTA_STARTED         (reserved — P2 implementation) */
/* 0x0431 — MSG_OTA_PROGRESS        (reserved — P2 implementation) */
/* 0x0432 — MSG_OTA_COMPLETE        (reserved — P2 implementation) */
/* 0x0433 — MSG_OTA_FAILED          (reserved — P2 implementation) */
/* 0x0434–0x043F — reserved for additional OTA events              */

#ifdef __cplusplus
}
#endif

#endif /* EMBEDIQ_PLATFORM_MSGS_H */
