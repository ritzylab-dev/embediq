/*
 * embediq_platform_msgs.h — Platform FB message ID definitions
 *
 * Lives in core/include/ so that application FBs can subscribe to timer,
 * watchdog, NVM, and cloud messages without including platform-layer headers
 * (AGENTS.md §3 layer dependency rule — Layer 2 must not be included by Layer 3).
 *
 * Namespace: 0x0400–0x13FF  Official EmbedIQ components (AGENTS.md §2A)
 *
 * Ranges (from messages_registry.json — authoritative):
 *   fb_nvm          0x044C–0x047D  (1100–1149)  store/retrieve notifications
 *   fb_watchdog     0x047E–0x04AF  (1150–1199)  health diagnostics
 *   fb_timer        0x04B0–0x04E1  (1200–1249)  periodic tick publications
 *   fb_cloud_mqtt   0x0578–0x05DB  (1400–1499)  connection + delivery events
 *   fb_ota          0x05DC–0x063F  (1500–1599)  firmware update lifecycle
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
 * fb_nvm — non-volatile store notifications (0x044C–0x047D, reserved)
 *
 * IDs assigned at implementation (P2-T*). Reserve the range now so no
 * other module claims these IDs first.
 * ------------------------------------------------------------------------- */

/* 0x044C — MSG_NVM_WRITE_DONE      (reserved — P2 implementation) */
/* 0x044D — MSG_NVM_FLUSH_DONE      (reserved — P2 implementation) */
/* 0x044E–0x047D — reserved for additional NVM events              */

/* ---------------------------------------------------------------------------
 * fb_watchdog — health monitoring messages (0x047E–0x04AF)
 * ------------------------------------------------------------------------- */

#define MSG_WDG_MISSED_CHECKIN  0x047Eu  /**< A registered FB missed its checkin deadline */
#define MSG_WDG_RESET_REASON    0x047Fu  /**< Reset reason emitted once on startup */

/* ---------------------------------------------------------------------------
 * fb_timer — periodic tick messages (0x04B0–0x04E1)
 * ------------------------------------------------------------------------- */

#define MSG_TIMER_1MS     0x04B0u  /**< Published every 1 ms   */
#define MSG_TIMER_10MS    0x04B1u  /**< Published every 10 ms  */
#define MSG_TIMER_100MS   0x04B2u  /**< Published every 100 ms */
#define MSG_TIMER_1SEC    0x04B3u  /**< Published every 1 second */

/* ---------------------------------------------------------------------------
 * fb_cloud_mqtt — connectivity and delivery events (0x0578–0x05DB, reserved)
 *
 * IDs assigned at implementation (P2-T4). Reserve range.
 * ------------------------------------------------------------------------- */

/** fb_cloud_mqtt entered CONNECTED state. */
#define MSG_MQTT_CONNECTED       0x0578u

/** fb_cloud_mqtt entered DISCONNECTED or RECONNECTING state. */
#define MSG_MQTT_DISCONNECTED    0x0579u

/**
 * Inbound cloud command received on embediq/{client_id}/cmd.
 * Payload: raw JSON bytes from cloud. Application FBs subscribe and parse.
 * Also used for ota_check and rotate_cert commands — fb_ota and
 * fb_provisioning subscribe to this message ID.
 */
#define MSG_MQTT_CMD_RX          0x057Au

/**
 * Configuration reload request.
 * Published by fb_cloud_mqtt when the cloud pushes new config (desired state).
 * Also published internally on config change to trigger reconnect.
 * No payload. Subscribers re-read their config from embediq_cfg.
 */
#define MSG_CFG_RELOAD           0x057Bu

/* 0x057C–0x05DB — reserved for additional MQTT events */

/* ---------------------------------------------------------------------------
 * fb_ota — firmware update lifecycle (0x05DC–0x063F, reserved)
 *
 * IDs assigned at implementation (P2-T5). Reserve range.
 * ------------------------------------------------------------------------- */

/* 0x05DC — MSG_OTA_STARTED         (reserved — P2 implementation) */
/* 0x05DD — MSG_OTA_PROGRESS        (reserved — P2 implementation) */
/* 0x05DE — MSG_OTA_COMPLETE        (reserved — P2 implementation) */
/* 0x05DF — MSG_OTA_FAILED          (reserved — P2 implementation) */
/* 0x05E0–0x063F — reserved for additional OTA events              */

/* ---------------------------------------------------------------------------
 * fb_bridge -- External FB bus messages (0x06A4-0x06D4)
 *
 * IDs assigned at implementation (Item 5a). Range 0x06A7-0x06D4 reserved.
 * ------------------------------------------------------------------------- */

#include <stdint.h>
#include "embediq_config.h"           /* EMBEDIQ_BRIDGE_NAME_LEN */

#define MSG_BRIDGE_EXTFB_CONNECTED    0x06A4u  /**< External FB connected + identified */
#define MSG_BRIDGE_EXTFB_DISCONNECTED 0x06A5u  /**< External FB slot released (all disconnect reasons) */
#define MSG_BRIDGE_FAULT              0x06A6u  /**< fb_bridge internal fault */
/* 0x06A7-0x06D4 -- reserved for future bridge events */

/* MSG_BRIDGE_EXTFB_CONNECTED payload -- 36 bytes (fits 64-byte bus payload cap) */
typedef struct __attribute__((packed)) {
    uint8_t  virtual_endpoint_id;   /* 0x40-0x7F */
    uint8_t  transport;              /* 0=socket, 1=queue */
    uint16_t reserved;
    char     name[EMBEDIQ_BRIDGE_NAME_LEN];   /* External FB name from IDENTIFY frame / init call */
} EmbedIQ_Msg_BridgeExtFbConnected_t;

/* MSG_BRIDGE_EXTFB_DISCONNECTED payload -- 4 bytes */
typedef struct __attribute__((packed)) {
    uint8_t  virtual_endpoint_id;   /* 0x40-0x7F */
    uint8_t  reason;                /* 0=clean, 1=timeout, 2=pool_full, 3=protocol_error */
    uint16_t reserved;
} EmbedIQ_Msg_BridgeExtFbDisconnected_t;

/* MSG_BRIDGE_FAULT payload -- 4 bytes */
typedef struct __attribute__((packed)) {
    uint8_t  fault_code;            /* 0=pool_exhausted, 1=socket_error, 2=parse_error */
    uint8_t  transport;             /* 0=socket, 1=queue */
    uint16_t reserved;
} EmbedIQ_Msg_BridgeFault_t;

#ifdef __cplusplus
}
#endif

#endif /* EMBEDIQ_PLATFORM_MSGS_H */
