#define _GNU_SOURCE   /* PTHREAD_MUTEX_ERRORCHECK + other GNU extensions */

/*
 * hal/posix/ops/hal_mqtt_posix.c — POSIX MQTT ops table (Paho MQTT C v1.3.13)
 *
 * Phase A: plain TCP port 1883.
 * on_receive: Paho background thread → embediq_mqtt_on_receive_call()
 * (thread-safe: embediq_publish uses mutex-protected queue).
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ops/embediq_mqtt.h"
#include "embediq_config.h"
#include "embediq_platform.h"
#include "hal/hal_defs.h"

#include <MQTTClient.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#ifndef EMBEDIQ_HAL_SRC_MQTT
#  define EMBEDIQ_HAL_SRC_MQTT  0xD9u
#endif

static MQTTClient s_client    = NULL;
static bool       s_connected = false;

/* ---------------------------------------------------------------------------
 * Paho callbacks
 * ------------------------------------------------------------------------- */

static int paho_msg_arrived(void *context, char *topic_name, int topic_len,
                             MQTTClient_message *message)
{
    (void)context; (void)topic_len;
    if (topic_name && message) {
        embediq_mqtt_on_receive_call(topic_name,
                                     (const uint8_t *)message->payload,
                                     (uint32_t)message->payloadlen);
    }
    MQTTClient_freeMessage(&message);
    MQTTClient_free(topic_name);
    return 1;
}

static void paho_conn_lost(void *context, char *cause)
{
    (void)context; (void)cause;
    s_connected = false;
}

/* ---------------------------------------------------------------------------
 * Paho trace callback — Debug builds only.
 * Routes Paho's internal Log(TRACE_MINIMUM, ...) and Log(LOG_SEVERE, ...)
 * calls to stdout so Socket_error() messages are visible (e.g., EPROTOTYPE).
 * NDEBUG is set by CMake in Release/RelWithDebInfo builds.
 * ------------------------------------------------------------------------- */
#ifndef NDEBUG
static void paho_trace_cb(enum MQTTCLIENT_TRACE_LEVELS level, char *message)
{
    (void)level;
    printf("[PAHO TRACE] %s\n", message);
}
#endif /* !NDEBUG */

/* ---------------------------------------------------------------------------
 * Ops implementations
 * EMBEDIQ_HAL_OBS_EMIT_ERROR required before every non-OK return.
 * ------------------------------------------------------------------------- */

static embediq_err_t mqtt_connect(const embediq_mqtt_connect_params_t *params)
{
    if (!params || !params->host || !params->client_id) {
        EMBEDIQ_HAL_OBS_EMIT_ERROR(EMBEDIQ_HAL_SRC_MQTT, HAL_ERR_INVALID);
        return EMBEDIQ_ERR_INVALID;
    }

    char uri[128];
    (void)snprintf(uri, sizeof(uri), "tcp://%s:%u",
                   params->host, (unsigned)params->port);

    if (s_client) {
        if (s_connected) { (void)MQTTClient_disconnect(s_client, 0); }
        MQTTClient_destroy(&s_client);
        s_client    = NULL;
        s_connected = false;
    }

    int rc = MQTTClient_create(&s_client, uri, params->client_id,
                                MQTTCLIENT_PERSISTENCE_NONE, NULL);
    if (rc != MQTTCLIENT_SUCCESS) {
        EMBEDIQ_HAL_OBS_EMIT_ERROR(EMBEDIQ_HAL_SRC_MQTT, HAL_ERR_IO);
        printf("[MQTT HAL] MQTTClient_create failed: rc=%d uri='%s'\n", rc, uri);
        s_client = NULL;
        return EMBEDIQ_ERR;
    }

    (void)MQTTClient_setCallbacks(s_client, NULL,
                                   paho_conn_lost, paho_msg_arrived, NULL);

    MQTTClient_connectOptions opts = MQTTClient_connectOptions_initializer;
    opts.MQTTVersion       = MQTTVERSION_3_1_1;  /* = 4; explicit v3.1.1 avoids double-attempt
                                                   * overhead from MQTTVERSION_DEFAULT = 0.
                                                   * Mosquitto 2.x requires MQTT 3.1.1 minimum. */
    opts.username          = (params->username && params->username[0]) ?
                             params->username : NULL;
    opts.password          = (params->password && params->password[0]) ?
                             params->password : NULL;
    opts.keepAliveInterval = (int)params->keepalive_sec;
    opts.cleansession      = params->clean_session ? 1 : 0;
    opts.connectTimeout    = (params->connect_timeout_ms > 0u) ?
                             (int)(params->connect_timeout_ms / 1000u) :
                             (int)(EMBEDIQ_MQTT_CONNECT_TIMEOUT_MS / 1000u);

    MQTTClient_willOptions will = MQTTClient_willOptions_initializer;
    if (params->will_topic && params->will_payload) {
        will.topicName        = params->will_topic;
        will.qos              = (int)params->will_qos;
        will.retained         = params->will_retain ? 1 : 0;
        will.payload.len      = (int)params->will_payload_len;
        will.payload.data     = (void *)(uintptr_t)params->will_payload;
        opts.will             = &will;
    }

    printf("[MQTT HAL] Connecting: uri='%s' timeout=%ds keepalive=%ds\n",
           uri, opts.connectTimeout, opts.keepAliveInterval);
    rc = MQTTClient_connect(s_client, &opts);
    if (rc != MQTTCLIENT_SUCCESS) {
        EMBEDIQ_HAL_OBS_EMIT_ERROR(EMBEDIQ_HAL_SRC_MQTT, HAL_ERR_IO);
        printf("[MQTT HAL] MQTTClient_connect failed: rc=%d\n", rc);
        MQTTClient_destroy(&s_client);
        s_client    = NULL;
        return EMBEDIQ_ERR;
    }

    s_connected = true;
    return EMBEDIQ_OK;
}

static embediq_err_t mqtt_disconnect(void)
{
    if (s_client && s_connected) {
        (void)MQTTClient_disconnect(s_client, 1000);
        s_connected = false;
    }
    return EMBEDIQ_OK;
}

static embediq_err_t mqtt_publish(const char *topic,
                                   const uint8_t *payload, uint32_t len,
                                   uint8_t qos)
{
    if (!s_client || !s_connected || !topic) {
        EMBEDIQ_HAL_OBS_EMIT_ERROR(EMBEDIQ_HAL_SRC_MQTT, HAL_ERR_INVALID);
        return EMBEDIQ_ERR;
    }
    MQTTClient_message msg = MQTTClient_message_initializer;
    msg.payload    = (void *)(uintptr_t)payload;
    msg.payloadlen = (int)len;
    msg.qos        = (int)qos;
    msg.retained   = 0;

    MQTTClient_deliveryToken token;
    int rc = MQTTClient_publishMessage(s_client, topic, &msg, &token);
    if (rc != MQTTCLIENT_SUCCESS) {
        EMBEDIQ_HAL_OBS_EMIT_ERROR(EMBEDIQ_HAL_SRC_MQTT, HAL_ERR_IO);
        return EMBEDIQ_ERR;
    }
    if (qos > 0u) {
        rc = MQTTClient_waitForCompletion(s_client, token, 1000u);
        if (rc != MQTTCLIENT_SUCCESS) {
            EMBEDIQ_HAL_OBS_EMIT_ERROR(EMBEDIQ_HAL_SRC_MQTT, HAL_ERR_IO);
            return EMBEDIQ_ERR;
        }
    }
    return EMBEDIQ_OK;
}

static embediq_err_t mqtt_subscribe(const char *topic, uint8_t qos)
{
    if (!s_client || !s_connected || !topic) {
        EMBEDIQ_HAL_OBS_EMIT_ERROR(EMBEDIQ_HAL_SRC_MQTT, HAL_ERR_INVALID);
        return EMBEDIQ_ERR;
    }
    int rc = MQTTClient_subscribe(s_client, topic, (int)qos);
    if (rc != MQTTCLIENT_SUCCESS) {
        EMBEDIQ_HAL_OBS_EMIT_ERROR(EMBEDIQ_HAL_SRC_MQTT, HAL_ERR_IO);
        return EMBEDIQ_ERR;
    }
    return EMBEDIQ_OK;
}

static embediq_err_t mqtt_unsubscribe(const char *topic)
{
    if (s_client && topic) { (void)MQTTClient_unsubscribe(s_client, topic); }
    return EMBEDIQ_OK;
}

static void mqtt_on_receive_stub(const char *t, const uint8_t *p, uint32_t l)
{
    (void)t; (void)p; (void)l;
    /* Actual callback stored via embediq_mqtt_set_on_receive() */
}

static const embediq_mqtt_ops_t g_posix_ops = {
    .version     = EMBEDIQ_MQTT_OPS_VERSION,
    .connect     = mqtt_connect,
    .disconnect  = mqtt_disconnect,
    .publish     = mqtt_publish,
    .subscribe   = mqtt_subscribe,
    .unsubscribe = mqtt_unsubscribe,
    .on_receive  = mqtt_on_receive_stub,
};

/* ---------------------------------------------------------------------------
 * Platform library init/deinit
 * ------------------------------------------------------------------------- */

static void hal_mqtt_posix_init(void)
{
    s_client    = NULL;
    s_connected = false;
#ifndef NDEBUG
    /* Route Paho internal trace to stdout for Debug builds. */
    MQTTClient_setTraceLevel(MQTTCLIENT_TRACE_MINIMUM);
    MQTTClient_setTraceCallback(paho_trace_cb);
#endif
    (void)embediq_mqtt_register_ops(&g_posix_ops);
}

static void hal_mqtt_posix_deinit(void)
{
    if (s_client) {
        if (s_connected) { (void)MQTTClient_disconnect(s_client, 0); }
        MQTTClient_destroy(&s_client);
        s_client    = NULL;
        s_connected = false;
    }
}

void hal_mqtt_posix_declare(void)
{
    embediq_platform_lib_declare(hal_mqtt_posix_init, hal_mqtt_posix_deinit);
}
