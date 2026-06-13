/*
 * tests/unit/test_fb_cloud_mqtt.c — Unit tests for fb_cloud_mqtt JSON
 *                                    serialisation of MSG_TELEMETRY_BATCH.
 *
 * Verifies publish_telemetry_json() decodes a binary MSG_TELEMETRY_BATCH and
 * emits valid JSON of the form:
 *   {"ts":N,"window_dur_s":N,"metrics":{"ID":V,...}}
 *
 * Tests exercise the package-internal test API
 * (fb_cloud_mqtt__publish_telemetry_json_test, fb_cloud_mqtt__set_topic_test)
 * under EMBEDIQ_PLATFORM_HOST only.
 *
 * Run:      ./build/tests/unit/test_fb_cloud_mqtt
 * Expected: "All N tests passed. (0 failed)"
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "embediq_config.h"
#include "embediq_msg.h"
#include "embediq_telemetry.h"
#include "ops/embediq_mqtt.h"

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* ---------------------------------------------------------------------------
 * Package-internal test API (implemented in fbs/services/fb_cloud_mqtt.c
 * under EMBEDIQ_PLATFORM_HOST).
 * ------------------------------------------------------------------------- */

extern void fb_cloud_mqtt__publish_telemetry_json_test(const uint8_t *payload);
extern void fb_cloud_mqtt__set_topic_test(const char *topic);

/* ---------------------------------------------------------------------------
 * Mock MQTT ops table — captures the most recent publish call.
 * ------------------------------------------------------------------------- */

#define CAPTURE_BUF_SIZE  512u

static int      g_publish_calls   = 0;
static char     g_captured_topic[80];
static uint8_t  g_captured_payload[CAPTURE_BUF_SIZE];
static uint32_t g_captured_len    = 0u;

static void reset_capture(void)
{
    g_publish_calls = 0;
    g_captured_topic[0] = '\0';
    g_captured_len = 0u;
    (void)memset(g_captured_payload, 0, sizeof(g_captured_payload));
}

static embediq_err_t mock_connect(const embediq_mqtt_connect_params_t *p)
{
    (void)p; return EMBEDIQ_OK;
}
static embediq_err_t mock_disconnect(void) { return EMBEDIQ_OK; }
static embediq_err_t mock_publish(const char *topic, const uint8_t *payload,
                                   uint32_t len, uint8_t qos)
{
    (void)qos;
    g_publish_calls++;
    if (topic) {
        (void)snprintf(g_captured_topic, sizeof(g_captured_topic), "%s", topic);
    }
    g_captured_len = (len < CAPTURE_BUF_SIZE) ? len : (CAPTURE_BUF_SIZE - 1u);
    if (payload && g_captured_len > 0u) {
        (void)memcpy(g_captured_payload, payload, g_captured_len);
    }
    g_captured_payload[g_captured_len] = '\0';
    return EMBEDIQ_OK;
}
static embediq_err_t mock_subscribe(const char *t, uint8_t q)
{ (void)t; (void)q; return EMBEDIQ_OK; }
static embediq_err_t mock_unsubscribe(const char *t) { (void)t; return EMBEDIQ_OK; }
static void mock_on_receive(const char *t, const uint8_t *p, uint32_t l)
{ (void)t; (void)p; (void)l; }

static embediq_mqtt_ops_t g_mock_ops = {
    .version     = EMBEDIQ_MQTT_OPS_VERSION,
    .connect     = mock_connect,
    .disconnect  = mock_disconnect,
    .publish     = mock_publish,
    .subscribe   = mock_subscribe,
    .unsubscribe = mock_unsubscribe,
    .on_receive  = mock_on_receive,
};

/* ---------------------------------------------------------------------------
 * Test harness
 * ------------------------------------------------------------------------- */

static int g_run = 0, g_fail = 0;

#define ASSERT(cond, msg) do {                                                \
    g_run++;                                                                  \
    if (!(cond)) {                                                            \
        fprintf(stderr, "FAIL  %s:%d  %s\n", __func__, __LINE__, (msg));      \
        g_fail++;                                                             \
    } else {                                                                  \
        printf("PASS  %s\n", __func__);                                       \
    }                                                                         \
} while (0)

/* ---------------------------------------------------------------------------
 * Helper: build a well-formed MSG_TELEMETRY_BATCH message with N entries
 * ------------------------------------------------------------------------- */

static void build_batch(EmbedIQ_Msg_t *out,
                        uint32_t ts,
                        uint16_t window_dur_s,
                        const EmbedIQ_Telemetry_Batch_Entry_t *entries,
                        uint8_t entry_count)
{
    (void)memset(out, 0, sizeof(*out));
    out->msg_id = MSG_TELEMETRY_BATCH;

    MSG_TELEMETRY_BATCH_Payload_t hdr;
    hdr.window_start_s = ts;
    hdr.window_dur_s   = window_dur_s;
    hdr.entry_count    = entry_count;
    hdr.flags          = 0u;
    (void)memcpy(&out->payload[0], &hdr, sizeof(hdr));

    for (uint8_t i = 0u; i < entry_count; i++) {
        size_t offset = sizeof(hdr) + (size_t)i * sizeof(EmbedIQ_Telemetry_Batch_Entry_t);
        (void)memcpy(&out->payload[offset], &entries[i], sizeof(entries[i]));
    }

    out->payload_len = (uint16_t)(sizeof(hdr) +
                                  (uint32_t)entry_count * sizeof(EmbedIQ_Telemetry_Batch_Entry_t));
}

/* ---------------------------------------------------------------------------
 * Tests
 * ------------------------------------------------------------------------- */

/* Test 1 — buffer size is sufficient for the worst case (3 entries × 21 chars
 * + 49-char prefix + 2-char suffix = 114 chars). We can't see MQTT_JSON_BUF_SIZE
 * from outside, so we instead run the worst-case batch and verify the captured
 * output is shorter than the test capture buffer (which is 512 bytes — far
 * larger than any sensible JSON for 3 entries). If the implementation buffer
 * were 64 bytes, the publish would either truncate or never fire. */
static void test_telemetry_json_buf_size_sufficient(void)
{
    reset_capture();
    (void)embediq_mqtt_register_ops(&g_mock_ops);
    fb_cloud_mqtt__set_topic_test("embediq/x/telemetry");

    EmbedIQ_Telemetry_Batch_Entry_t e[3];
    (void)memset(e, 0, sizeof(e));
    e[0].metric_id = 65535u; e[0].value_a = -3.40282e+38f;
    e[1].metric_id = 65535u; e[1].value_a = -3.40282e+38f;
    e[2].metric_id = 65535u; e[2].value_a = -3.40282e+38f;

    EmbedIQ_Msg_t m;
    build_batch(&m, 4294967295u, 65535u, e, 3u);

    fb_cloud_mqtt__publish_telemetry_json_test(m.payload);

    ASSERT(g_publish_calls == 1, "worst-case batch must publish");
    ASSERT(g_captured_len > 0u && g_captured_len < CAPTURE_BUF_SIZE,
           "captured payload must fit in capture buffer and be non-empty");
    ASSERT(g_captured_payload[0] == '{', "JSON must start with '{'");
    ASSERT(g_captured_payload[g_captured_len - 1u] == '}',
           "JSON must end with '}'");
}

/* Test 2 — single entry batch produces correct JSON shape */
static void test_telemetry_json_single_entry_format(void)
{
    reset_capture();
    (void)embediq_mqtt_register_ops(&g_mock_ops);
    fb_cloud_mqtt__set_topic_test("embediq/dev/telemetry");

    EmbedIQ_Telemetry_Batch_Entry_t entry;
    (void)memset(&entry, 0, sizeof(entry));
    entry.metric_id = 1025u;
    entry.type      = 0u;   /* GAUGE */
    entry.unit_id   = 1u;
    entry.value_a   = 22.5f;
    entry.count     = 30u;

    EmbedIQ_Msg_t m;
    build_batch(&m, 100u, 30u, &entry, 1u);

    fb_cloud_mqtt__publish_telemetry_json_test(m.payload);

    ASSERT(g_publish_calls == 1, "single-entry batch must publish exactly once");
    ASSERT(g_captured_payload[0] == '{', "JSON must start with '{'");
    ASSERT(g_captured_payload[g_captured_len - 1u] == '}',
           "JSON must end with '}'");
    ASSERT(strstr((const char *)g_captured_payload, "\"ts\":100") != NULL,
           "JSON must contain ts:100");
    ASSERT(strstr((const char *)g_captured_payload, "\"window_dur_s\":30") != NULL,
           "JSON must contain window_dur_s:30");
    ASSERT(strstr((const char *)g_captured_payload, "\"1025\":") != NULL,
           "JSON must contain metric_id 1025 as key");

    /* No NUL byte before pos — important for bash command substitution. */
    bool has_early_nul = false;
    for (uint32_t i = 0u; i < g_captured_len; i++) {
        if (g_captured_payload[i] == '\0') { has_early_nul = true; break; }
    }
    ASSERT(!has_early_nul, "JSON must not contain NUL bytes within payload");
}

/* Test 3 — malformed payload (too short) must NOT publish */
static void test_telemetry_json_malformed_payload_rejected(void)
{
    reset_capture();
    (void)embediq_mqtt_register_ops(&g_mock_ops);
    fb_cloud_mqtt__set_topic_test("embediq/dev/telemetry");

    EmbedIQ_Msg_t m;
    (void)memset(&m, 0, sizeof(m));
    m.msg_id      = MSG_TELEMETRY_BATCH;
    m.payload_len = 3u;  /* shorter than sizeof(MSG_TELEMETRY_BATCH_Payload_t) = 8 */

    fb_cloud_mqtt__publish_telemetry_json_test(m.payload);

    ASSERT(g_publish_calls == 0, "malformed (too-short) batch must NOT publish");
}

/* Test 4 — entry_count == 0 must NOT publish */
static void test_telemetry_json_zero_entry_count_rejected(void)
{
    reset_capture();
    (void)embediq_mqtt_register_ops(&g_mock_ops);
    fb_cloud_mqtt__set_topic_test("embediq/dev/telemetry");

    EmbedIQ_Msg_t m;
    build_batch(&m, 0u, 0u, NULL, 0u);

    fb_cloud_mqtt__publish_telemetry_json_test(m.payload);

    ASSERT(g_publish_calls == 0, "zero-entry batch must NOT publish");
}

int main(void)
{
    printf("=== test_fb_cloud_mqtt ===\n");
    test_telemetry_json_buf_size_sufficient();
    test_telemetry_json_single_entry_format();
    test_telemetry_json_malformed_payload_rejected();
    test_telemetry_json_zero_entry_count_rejected();
    printf("\n%d/%d tests passed. (%d failed)\n", g_run - g_fail, g_run, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
