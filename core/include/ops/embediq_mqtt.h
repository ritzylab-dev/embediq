/*
 * embediq_mqtt.h — Cloud MQTT transport contract (v2)
 *
 * Defines embediq_mqtt_ops_t: the ops table contract for MQTT 3.1.1 transport.
 * fb_cloud_mqtt (Layer 2 Service FB) owns the FSM, reconnection policy, and
 * message routing — NOT the socket transport or TLS.
 *
 * Version history:
 *   v1 — initial contract: connect(host, port, client_id) — incomplete
 *   v2 — connect() now takes embediq_mqtt_connect_params_t covering all
 *         MQTT CONNECT packet fields (LWT, keepalive, username, password,
 *         clean_session, connect_timeout). Reason: MQTT CONNECT is one
 *         atomic packet — all parameters go together.
 *
 * Platform integration pattern:
 *   1. Platform boot code calls embediq_mqtt_register_ops() with a populated
 *      embediq_mqtt_ops_t table (once, during EMBEDIQ_BOOT_PHASE_PLATFORM).
 *   2. fb_cloud_mqtt calls connect(), subscribe(), publish() via ops table.
 *   3. Platform calls ops->on_receive() when a subscribed message arrives.
 *      on_receive() is registered by fb_cloud_mqtt — do not call directly.
 *
 * All ops functions MUST be safe to call from the fb_cloud_mqtt dispatch thread.
 * connect() may block up to connect_timeout_ms; all other ops must return
 * within 50 ms.
 *
 * Platform independence: fb_cloud_mqtt is a Service FB. It never includes
 * platform headers. All platform-specific code lives in hal_mqtt_<target>.c.
 * boundary_checker.py CI enforces this invariant.
 *
 * I-01: Compiles standalone with zero OSAL or BSP dependencies.
 * R-03: C11. Fixed-width types from <stdint.h> and <stdbool.h> only.
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
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
 * ABI version
 * ------------------------------------------------------------------------- */

/** ABI version for embediq_mqtt_ops_t. Increment on any breaking field change. */
#define EMBEDIQ_MQTT_OPS_VERSION  2u

/* ---------------------------------------------------------------------------
 * MQTT CONNECT parameters
 *
 * Passed to connect() in a single struct — the MQTT CONNECT packet is one
 * atomic operation. All parameters are sent to the broker together.
 *
 * fb_cloud_mqtt builds this struct from embediq_cfg reads at init and on
 * MSG_CFG_RELOAD. The will_topic is derived from client_id:
 *   "embediq/{client_id}/status"
 *
 * All pointer fields remain valid for the duration of the connect() call.
 * The platform implementation must not retain pointers after connect() returns.
 * ------------------------------------------------------------------------- */
typedef struct {
    /* --- Variable — may change on MSG_CFG_RELOAD --- */
    const char    *host;               /**< Broker hostname or IP (NUL-terminated). */
    uint16_t       port;               /**< 1883 = plain MQTT; 8883 = MQTT over TLS. */
    const char    *client_id;          /**< MQTT client identifier (NUL-terminated, unique per device). */
    const char    *username;           /**< NULL = no username auth. */
    const char    *password;           /**< NULL = no password. Sensitive — never log. */

    /* --- Stable — set once at boot, rarely changes --- */
    uint16_t       keepalive_sec;      /**< MQTT keepalive interval in seconds.
                                        *   0 = keepalive disabled.
                                        *   Default: 60. Cellular: use 120–300. */
    bool           clean_session;      /**< true = broker discards session on disconnect.
                                        *   Default: true. */
    uint32_t       connect_timeout_ms; /**< Max ms to wait for CONNACK.
                                        *   0 = use EMBEDIQ_MQTT_CONNECT_TIMEOUT_MS. */

    /* --- Last Will and Testament (LWT) --- */
    const char    *will_topic;         /**< NULL = no LWT. Derived: "embediq/{client_id}/status". */
    const uint8_t *will_payload;       /**< LWT payload bytes. NULL if will_topic is NULL. */
    uint32_t       will_payload_len;   /**< Length of will_payload in bytes. */
    uint8_t        will_qos;           /**< LWT QoS: 0 or 1. */
    bool           will_retain;        /**< true = broker retains LWT message (recommended). */

} embediq_mqtt_connect_params_t;

/* ---------------------------------------------------------------------------
 * MQTT platform operations table
 * ------------------------------------------------------------------------- */
typedef struct {
    /**
     * MUST BE FIRST — ABI versioning (I-17).
     * Set to EMBEDIQ_MQTT_OPS_VERSION at ops table declaration.
     */
    uint32_t version;

    /**
     * Open a TCP/TLS connection and send MQTT CONNECT.
     *
     * Blocks until CONNACK is received or connect_timeout_ms elapses.
     * For TLS (port 8883): platform implementation calls
     * hal_tls_posix_ops_slot(0)->connect_async() internally and waits.
     *
     * @param params  All MQTT CONNECT parameters. Pointer valid for duration of call.
     * @return EMBEDIQ_OK if CONNACK received and accepted; EMBEDIQ_ERR otherwise.
     */
    embediq_err_t (*connect)(const embediq_mqtt_connect_params_t *params);

    /** Send MQTT DISCONNECT and close the connection. */
    embediq_err_t (*disconnect)(void);

    /**
     * Publish a message to a topic.
     *
     * @param topic    NUL-terminated topic string.
     * @param payload  Message payload bytes.
     * @param len      Payload length in bytes.
     * @param qos      QoS level: 0 = at-most-once, 1 = at-least-once.
     * @return EMBEDIQ_OK on success; EMBEDIQ_ERR if not connected or send fails.
     */
    embediq_err_t (*publish)(const char *topic,
                             const uint8_t *payload, uint32_t len,
                             uint8_t qos);

    /**
     * Subscribe to a topic filter.
     *
     * @param topic  NUL-terminated topic filter (may contain wildcards).
     * @param qos    Requested QoS level.
     * @return EMBEDIQ_OK if SUBACK received; EMBEDIQ_ERR otherwise.
     */
    embediq_err_t (*subscribe)(const char *topic, uint8_t qos);

    /** Unsubscribe from a topic filter. */
    embediq_err_t (*unsubscribe)(const char *topic);

    /**
     * Callback invoked by the platform when a message arrives on a subscribed topic.
     * Registered by fb_cloud_mqtt at init — do not set or call directly.
     *
     * @param topic    NUL-terminated topic of the received message.
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
 * @return EMBEDIQ_OK on success; EMBEDIQ_ERR if ops is NULL or any fn pointer is NULL.
 */
embediq_err_t embediq_mqtt_register_ops(const embediq_mqtt_ops_t *ops);

/** Get the registered MQTT ops table. Returns NULL if none registered. */
const embediq_mqtt_ops_t *embediq_mqtt_ops_get(void);

/** Register the on_receive callback (called by fb_cloud_mqtt at init). */
void embediq_mqtt_set_on_receive(
    void (*cb)(const char *topic, const uint8_t *payload, uint32_t len));

/** Invoke the registered on_receive callback (called by hal_mqtt_posix). */
void embediq_mqtt_on_receive_call(const char *topic,
                                   const uint8_t *payload, uint32_t len);

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
