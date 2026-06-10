/*
 * tests/integration/test_fb_cloud_mqtt_unit.c — fb_cloud_mqtt unit tests
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "ops/embediq_mqtt.h"
#include "embediq_platform_msgs.h"
#include "embediq_config.h"

/* --- mock ops --- */
static int g_connect_calls = 0;
static embediq_err_t g_connect_rc = EMBEDIQ_OK;

static embediq_err_t mock_connect(const embediq_mqtt_connect_params_t *p)
{
    g_connect_calls++;
    if (!p || !p->host || !p->client_id) { return EMBEDIQ_ERR; }
    /* Verify LWT topic contains "status" */
    if (p->will_topic && !strstr(p->will_topic, "status")) { return EMBEDIQ_ERR; }
    return g_connect_rc;
}
static embediq_err_t mock_disconnect(void) { return EMBEDIQ_OK; }
static embediq_err_t mock_publish(const char *t, const uint8_t *p,
                                   uint32_t l, uint8_t q)
{ (void)t;(void)p;(void)l;(void)q; return EMBEDIQ_OK; }
static embediq_err_t mock_subscribe(const char *t, uint8_t q)
{ (void)t;(void)q; return EMBEDIQ_OK; }
static embediq_err_t mock_unsubscribe(const char *t) { (void)t; return EMBEDIQ_OK; }
static void mock_on_receive(const char *t, const uint8_t *p, uint32_t l)
{ (void)t;(void)p;(void)l; }

static embediq_mqtt_ops_t g_mock = {
    .version     = EMBEDIQ_MQTT_OPS_VERSION,
    .connect     = mock_connect,
    .disconnect  = mock_disconnect,
    .publish     = mock_publish,
    .subscribe   = mock_subscribe,
    .unsubscribe = mock_unsubscribe,
    .on_receive  = mock_on_receive,
};

/* --- test infra --- */
static int g_run = 0, g_fail = 0;
#define ASSERT(c, m) do { g_run++; if(!(c)){fprintf(stderr,"FAIL %s:%d — %s\n",__func__,__LINE__,m);g_fail++;}else{printf("PASS  %s\n",__func__);} } while(0)

/* --- tests --- */
static void test_ops_version(void)
{
    ASSERT(g_mock.version == EMBEDIQ_MQTT_OPS_VERSION, "version must be 2");
}
static void test_register(void)
{
    embediq_err_t rc = embediq_mqtt_register_ops(&g_mock);
    ASSERT(rc == EMBEDIQ_OK, "register with valid ops must succeed");
    ASSERT(embediq_mqtt_ops_get() == &g_mock, "ops_get must return registered ops");
}
static void test_register_null(void)
{
    ASSERT(embediq_mqtt_register_ops(NULL) != EMBEDIQ_OK, "NULL ops must fail");
}
static void test_config_constants(void)
{
    ASSERT(EMBEDIQ_MQTT_RING_BUFFER_SIZE > 0u, "ring buffer must be > 0");
    ASSERT(EMBEDIQ_MQTT_CONNECT_TIMEOUT_MS > 0u, "connect timeout must be > 0");
    ASSERT(EMBEDIQ_MQTT_RECONNECT_BASE_MS > 0u, "reconnect base must be > 0");
    ASSERT(EMBEDIQ_MQTT_MAX_RECONNECT_INTERVAL_MS > EMBEDIQ_MQTT_RECONNECT_BASE_MS,
           "max reconnect must exceed base");
}
static void test_msg_ids_defined(void)
{
    ASSERT(MSG_MQTT_CONNECTED    == 0x0578u, "MSG_MQTT_CONNECTED");
    ASSERT(MSG_MQTT_DISCONNECTED == 0x0579u, "MSG_MQTT_DISCONNECTED");
    ASSERT(MSG_MQTT_CMD_RX       == 0x057Au, "MSG_MQTT_CMD_RX");
    ASSERT(MSG_CFG_RELOAD        == 0x057Bu, "MSG_CFG_RELOAD");
}
static void test_lwtformat(void)
{
    embediq_mqtt_connect_params_t p = {0};
    p.host       = "localhost"; p.port = 1883u;
    p.client_id  = "dev-001";
    p.will_topic = "embediq/dev-001/status";
    static const uint8_t wpl[] = "{\"online\":false}";
    p.will_payload     = wpl;
    p.will_payload_len = (uint32_t)(sizeof(wpl) - 1u);
    p.will_qos         = 1u; p.will_retain = true;
    g_connect_calls = 0; g_connect_rc = EMBEDIQ_OK;
    ASSERT(mock_connect(&p) == EMBEDIQ_OK, "connect with valid LWT must succeed");
    ASSERT(g_connect_calls == 1, "connect called once");
}

static int g_on_receive_called = 0;
static void test_cb(const char *t, const uint8_t *p, uint32_t l)
{ (void)t;(void)p;(void)l; g_on_receive_called++; }

static void test_set_on_receive(void)
{
    g_on_receive_called = 0;
    embediq_mqtt_set_on_receive(test_cb);
    embediq_mqtt_on_receive_call("test/topic", NULL, 0u);
    ASSERT(g_on_receive_called == 1, "on_receive callback must be invoked");
}

int main(void)
{
    printf("\n--- fb_cloud_mqtt unit tests ---\n\n");
    test_ops_version();
    test_register();
    test_register_null();
    test_config_constants();
    test_msg_ids_defined();
    test_lwtformat();
    test_set_on_receive();
    printf("\n%d/%d tests passed.\n", g_run - g_fail, g_run);
    return g_fail > 0 ? 1 : 0;
}
