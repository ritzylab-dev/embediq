/*
 * embediq_mqtt.h — Cloud MQTT transport contract
 *
 * Defines the platform operations table and connection FSM states for
 * MQTT 3.1.1 transport. fb_cloud_mqtt (Phase 2, P2-T4) owns the FSM,
 * reconnection policy, and message routing — NOT the socket transport.
 *
 * Platform integration pattern:
 *   1. Platform boot code calls embediq_mqtt_register_ops() with a populated
 *      embediq_mqtt_ops_t table (once, during EMBEDIQ_BOOT_PHASE_PLATFORM).
 *   2. fb_cloud_mqtt calls connect(), subscribe(), publish() via ops table.
 *   3. Platform calls ops->on_receive() when a subscribed message arrives.
 *      on_receive() is registered by fb_cloud_mqtt — do not call directly.
 *
 * All ops functions MUST be safe to call from the fb_cloud_mqtt dispatch thread.
 * connect() may block up to its configured timeout; all other ops must return
 * within 50 ms.
 *
 * I-01: Compiles standalone with zero OSAL or BSP dependencies.
 * R-03: C11. Fixed-width types from <stdint.h> only.
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef EMBEDIQ_MQTT_H
#define EMBEDIQ_MQTT_H

#include "embediq_osal.h"  /* embediq_err_t */
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
 * MQTT platform operations table
 * ------------------------------------------------------------------------- */

/** ABI version for embediq_mqtt_ops_t. Increment on any breaking field change. */
#define EMBEDIQ_MQTT_OPS_VERSION  1u

typedef struct {
    /**
     * MUST BE FIRST — ABI versioning (I-17).
     * Set to EMBEDIQ_MQTT_OPS_VERSION at ops table declaration.
     */
    uint32_t version;

    /**
     * Open a TCP connection and send MQTT CONNECT.
     *
     * @param host       Broker hostname or IP address (NUL-terminated).
     * @param port       Broker port (default 1883, TLS 8883).
     * @param client_id  MQTT client identifier (NUL-terminated, unique per device).
     * @return EMBEDIQ_OK if CONNACK received and accepted, EMBEDIQ_ERR otherwise.
     */
    embediq_err_t (*connect)(const char *host, uint16_t port,
                             const char *client_id);

    /** Send MQTT DISCONNECT and close the TCP connection. */
    embediq_err_t (*disconnect)(void);

    /**
     * Publish a message to a topic.
     *
     * @param topic    NUL-terminated topic string.
     * @param payload  Message payload bytes.
     * @param len      Payload length in bytes.
     * @param qos      QoS level: 0 = at-most-once, 1 = at-least-once.
     *                 v1: QoS 2 is not required.
     * @return EMBEDIQ_OK on success, EMBEDIQ_ERR if not connected or send fails.
     */
    embediq_err_t (*publish)(const char *topic,
                             const uint8_t *payload, uint32_t len,
                             uint8_t qos);

    /**
     * Subscribe to a topic filter.
     *
     * @param topic  NUL-terminated topic filter string (may contain wildcards).
     * @param qos    Requested QoS level for incoming messages.
     * @return EMBEDIQ_OK if SUBACK received, EMBEDIQ_ERR otherwise.
     */
    embediq_err_t (*subscribe)(const char *topic, uint8_t qos);

    /** Unsubscribe from a topic filter. */
    embediq_err_t (*unsubscribe)(const char *topic);

    /**
     * Callback invoked by the platform transport when a message arrives on a
     * subscribed topic. Registered by fb_cloud_mqtt at init — do not set
     * or call directly.
     *
     * @param topic    NUL-terminated topic string of the received message.
     * @param payload  Received payload bytes.
     * @param len      Payload length.
     */
    void (*on_receive)(const char *topic,
                       const uint8_t *payload, uint32_t len);
} embediq_mqtt_ops_t;

/* ---------------------------------------------------------------------------
 * Registration
 * ------------------------------------------------------------------------- */

/**
 * Register the platform MQTT operations table.
 * Call once during EMBEDIQ_BOOT_PHASE_PLATFORM before fb_cloud_mqtt initialises.
 *
 * @param ops  Pointer to a statically allocated ops table. Must remain valid
 *             for the lifetime of the process.
 * @return EMBEDIQ_OK on success, EMBEDIQ_ERR if ops is NULL or any fn pointer is NULL.
 */
embediq_err_t embediq_mqtt_register_ops(const embediq_mqtt_ops_t *ops);

/* ---------------------------------------------------------------------------
 * MQTT FSM states — observable via Observatory events
 * ------------------------------------------------------------------------- */

typedef enum {
    EMBEDIQ_MQTT_STATE_DISCONNECTED  = 0,  /**< No connection attempt in progress.   */
    EMBEDIQ_MQTT_STATE_CONNECTING,         /**< TCP + MQTT CONNECT in progress.      */
    EMBEDIQ_MQTT_STATE_CONNECTED,          /**< CONNACK received; ready to pub/sub.  */
    EMBEDIQ_MQTT_STATE_RECONNECTING,       /**< Connection lost; back-off and retry. */
    EMBEDIQ_MQTT_STATE_ERROR               /**< Unrecoverable error; needs reset.    */
} embediq_mqtt_state_t;

#ifdef __cplusplus
}
#endif

#endif /* EMBEDIQ_MQTT_H */
