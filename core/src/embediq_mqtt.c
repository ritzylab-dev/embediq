/*
 * core/src/embediq_mqtt.c — MQTT ops table registration store
 *
 * Implements embediq_mqtt_register_ops() + embediq_mqtt_ops_get()
 * (declared in core/include/ops/embediq_mqtt.h).
 *
 * Also stores the on_receive callback registered by fb_cloud_mqtt.
 * hal_mqtt_posix.c calls embediq_mqtt_on_receive_call() when Paho delivers
 * a message — this avoids const-casting the ops table.
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ops/embediq_mqtt.h"

static const embediq_mqtt_ops_t *s_ops       = NULL;
static void (*s_on_receive)(const char *,
                              const uint8_t *,
                              uint32_t)       = NULL;

embediq_err_t embediq_mqtt_register_ops(const embediq_mqtt_ops_t *ops)
{
    if (!ops || ops->version < EMBEDIQ_MQTT_OPS_VERSION) {
        return EMBEDIQ_ERR_INVALID;
    }
    s_ops = ops;
    return EMBEDIQ_OK;
}

const embediq_mqtt_ops_t *embediq_mqtt_ops_get(void)
{
    return s_ops;
}

void embediq_mqtt_set_on_receive(
    void (*cb)(const char *, const uint8_t *, uint32_t))
{
    s_on_receive = cb;
}

void embediq_mqtt_on_receive_call(const char    *topic,
                                   const uint8_t *payload,
                                   uint32_t       len)
{
    if (s_on_receive) {
        s_on_receive(topic, payload, len);
    }
}
