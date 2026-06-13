/*
 * fbs/services/fb_cloud_mqtt.c — Cloud MQTT transport Service FB
 *
 * Platform-independent Service FB (Layer 2). Owns the MQTT FSM, reconnect
 * policy, cloud outage buffering, and bus↔MQTT message routing.
 *
 * Zero platform headers. boundary_checker.py CI enforces this.
 * Phase A: plain MQTT port 1883.
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "embediq_subfn.h"
#include "embediq_bus.h"
#include "embediq_msg.h"
#include "embediq_cfg.h"
#include "embediq_config.h"
#include "embediq_obs.h"
#include "embediq_platform_msgs.h"
#include "embediq_telemetry.h"    /* MSG_TELEMETRY_BATCH, EmbedIQ_Telemetry_Batch_Entry_t */
#include "ops/embediq_mqtt.h"

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

/* ---------------------------------------------------------------------------
 * Module state — static allocation, no malloc (R-02)
 * ------------------------------------------------------------------------- */

static embediq_mqtt_state_t s_state         = EMBEDIQ_MQTT_STATE_DISCONNECTED;
static EmbedIQ_FB_Handle_t  s_fb            = NULL;  /* EmbedIQ_FB_Handle_t = void* */
static uint32_t             s_backoff_sec   = 0u;
static uint32_t             s_countdown_sec = 0u;

/* Ring buffer */
static EmbedIQ_Msg_t  s_ring[EMBEDIQ_MQTT_RING_BUFFER_SIZE];
static uint16_t       s_ring_head  = 0u;
static uint16_t       s_ring_tail  = 0u;
static uint16_t       s_ring_count = 0u;

/* Config strings — read from NVM, held for duration of connect() call.
 * Sized to EMBEDIQ_NVM_VAL_SIZE (matches config.iq str[63] + NUL). */
static char s_host[EMBEDIQ_NVM_VAL_SIZE];
static char s_client_id[EMBEDIQ_NVM_VAL_SIZE];
static char s_username[EMBEDIQ_NVM_VAL_SIZE];
static char s_password[EMBEDIQ_NVM_VAL_SIZE];

/* Derived: "embediq/" (8) + client_id (EMBEDIQ_NVM_VAL_SIZE-1) + "/telemetry" (10) + NUL (1) */
#define MQTT_TOPIC_MAX  (EMBEDIQ_NVM_VAL_SIZE + 18u)
static char s_will_topic[MQTT_TOPIC_MAX];
static char s_telemetry_topic[MQTT_TOPIC_MAX];

static const uint8_t k_will_payload[] = "{\"online\":false}";

/* ---------------------------------------------------------------------------
 * JSON serialisation for MSG_TELEMETRY_BATCH payloads.
 *
 * The cloud bridge (app/bridge.py) calls json.loads() on all telemetry
 * messages. Publishing raw binary causes a ValueError → message dropped.
 * This buffer holds the serialised JSON form before each MQTT publish.
 *
 * Buffer size derivation (first-principles):
 *   Prefix:  {"ts":4294967295,"window_dur_s":65535,"metrics":{ = 49 chars
 *   Entries: TEL_ENTRIES_PER_BATCH = (EMBEDIQ_MSG_MAX_PAYLOAD - 8) / 18 = 3
 *   Per-entry max: "65535":-3.40282e+38, = 21 chars (metric_id max 65535,
 *                  %.6g of -FLT_MAX = -3.40282e+38 = 12 chars, comma = 1)
 *   Last entry has no comma: 20 chars
 *   Suffix:  }} = 2 chars
 *   Total:   49 + 21 + 21 + 20 + 2 = 113 chars max
 *   Buffer:  256 bytes → 143-byte safety margin. Derived, not guessed.
 * ------------------------------------------------------------------------- */
#define MQTT_JSON_BUF_SIZE  256u
_Static_assert(MQTT_JSON_BUF_SIZE >= 114u,
               "MQTT_JSON_BUF_SIZE insufficient for worst-case telemetry batch JSON");
static char s_json_buf[MQTT_JSON_BUF_SIZE];

/* ---------------------------------------------------------------------------
 * Ring buffer helpers
 * ------------------------------------------------------------------------- */

static void ring_push(const EmbedIQ_Msg_t *m)
{
    if (s_ring_count >= (uint16_t)EMBEDIQ_MQTT_RING_BUFFER_SIZE) {
        /* Drop oldest */
        s_ring_tail  = (uint16_t)((s_ring_tail + 1u) % EMBEDIQ_MQTT_RING_BUFFER_SIZE);
        s_ring_count--;
    }
    s_ring[s_ring_head] = *m;
    s_ring_head  = (uint16_t)((s_ring_head + 1u) % EMBEDIQ_MQTT_RING_BUFFER_SIZE);
    s_ring_count++;
}

static bool ring_pop(EmbedIQ_Msg_t *out)
{
    if (s_ring_count == 0u) { return false; }
    *out        = s_ring[s_ring_tail];
    s_ring_tail = (uint16_t)((s_ring_tail + 1u) % EMBEDIQ_MQTT_RING_BUFFER_SIZE);
    s_ring_count--;
    return true;
}

/* ---------------------------------------------------------------------------
 * Build params from NVM config and try connect
 * ------------------------------------------------------------------------- */

/* Forward decl — publish_telemetry_json is defined later but called by
 * attempt_connect()'s ring-buffer flush path.
 * Takes a pointer to the MSG_TELEMETRY_BATCH payload bytes (header + entries).
 * The dispatcher passes payload bytes (not the Msg envelope) to sub-fn handlers —
 * see core/src/registry/fb_engine.c:459 and examples/thermostat/fb_temp_controller.c. */
static void publish_telemetry_json(const uint8_t *payload);

static void attempt_connect(void)
{
    const embediq_mqtt_ops_t *ops = embediq_mqtt_ops_get();
    if (!ops) { return; }

    (void)embediq_cfg_get_str("mqtt.host",      s_host,      sizeof(s_host),      "");
    (void)embediq_cfg_get_str("mqtt.client_id", s_client_id, sizeof(s_client_id), "");
    (void)embediq_cfg_get_str("mqtt.username",  s_username,  sizeof(s_username),  "");
    (void)embediq_cfg_get_str("mqtt.password",  s_password,  sizeof(s_password),  "");
    uint32_t ka   = embediq_cfg_get_u32("mqtt.keepalive_sec", 60u);
    uint32_t port = embediq_cfg_get_u32("mqtt.port", 1883u);

    if (s_host[0] == '\0' || s_client_id[0] == '\0') { return; }

    (void)snprintf(s_will_topic,      sizeof(s_will_topic),
                   "embediq/%s/status",    s_client_id);
    (void)snprintf(s_telemetry_topic, sizeof(s_telemetry_topic),
                   "embediq/%s/telemetry", s_client_id);

    embediq_mqtt_connect_params_t p;
    (void)memset(&p, 0, sizeof(p));
    p.host             = s_host;
    p.port             = (uint16_t)(port & 0xFFFFu);
    p.client_id        = s_client_id;
    p.username         = (s_username[0] != '\0') ? s_username  : NULL;
    p.password         = (s_password[0] != '\0') ? s_password  : NULL;
    p.keepalive_sec    = (uint16_t)(ka & 0xFFFFu);
    p.clean_session    = true;
    p.connect_timeout_ms = EMBEDIQ_MQTT_CONNECT_TIMEOUT_MS;
    p.will_topic       = s_will_topic;
    p.will_payload     = k_will_payload;
    p.will_payload_len = (uint32_t)(sizeof(k_will_payload) - 1u);
    p.will_qos         = 1u;
    p.will_retain      = true;

    embediq_err_t rc = ops->connect(&p);
    if (rc == EMBEDIQ_OK) {
        s_state         = EMBEDIQ_MQTT_STATE_CONNECTED;
        s_backoff_sec   = 0u;
        s_countdown_sec = 0u;

        (void)ops->subscribe("embediq/+/state/desired", 1u);
        (void)ops->subscribe("embediq/+/cmd",           1u);

        /* Publish CONNECTED event */
        EmbedIQ_Msg_t ev;
        (void)memset(&ev, 0, sizeof(ev));
        ev.msg_id = MSG_MQTT_CONNECTED;
        embediq_publish(s_fb, &ev);

        /* Publish online status to MQTT broker.
         * Cloud OSS bridge reads embediq/{client_id}/status and marks
         * the device online in its database. Will payload ({"online":false})
         * is published automatically by the broker on ungraceful disconnect.
         *
         * Payload buffer size derivation:
         *   "{\"online\":true}" = 15 chars + NUL = 16 bytes
         *   Well within EMBEDIQ_MSG_MAX_PAYLOAD (64). */
        if (s_will_topic[0] != '\0') {
            static const uint8_t k_online_payload[] = "{\"online\":true}";
            (void)ops->publish(s_will_topic,
                               k_online_payload,
                               (uint32_t)(sizeof(k_online_payload) - 1u),
                               1u);  /* QoS 1 — retain = false (bridge handles) */
        }

        /* Flush ring buffer — serialise each buffered batch to JSON */
        EmbedIQ_Msg_t buffered;
        while (ring_pop(&buffered)) {
            publish_telemetry_json(buffered.payload);
        }
    } else {
        s_state = EMBEDIQ_MQTT_STATE_RECONNECTING;
        if (s_backoff_sec == 0u) {
            s_backoff_sec = EMBEDIQ_MQTT_RECONNECT_BASE_MS / 1000u;
            if (s_backoff_sec == 0u) { s_backoff_sec = 2u; }
        }
        s_countdown_sec = s_backoff_sec;

        EmbedIQ_Msg_t ev;
        (void)memset(&ev, 0, sizeof(ev));
        ev.msg_id = MSG_MQTT_DISCONNECTED;
        embediq_publish(s_fb, &ev);
    }
}

/* ---------------------------------------------------------------------------
 * on_receive callback — registered with embediq_mqtt_set_on_receive()
 * Called from hal_mqtt_posix background thread.
 * embediq_publish() is thread-safe (mutex-protected queue).
 * ------------------------------------------------------------------------- */

static void on_mqtt_receive(const char    *topic,
                             const uint8_t *payload,
                             uint32_t       len)
{
    if (!s_fb || !topic) { return; }

    EmbedIQ_Msg_t msg;
    (void)memset(&msg, 0, sizeof(msg));

    if (strstr(topic, "/cmd") != NULL) {
        msg.msg_id      = MSG_MQTT_CMD_RX;
        msg.priority    = 0u;
        if (payload && len > 0u && len <= (uint32_t)EMBEDIQ_MSG_MAX_PAYLOAD) {
            (void)memcpy(msg.payload, payload, len);
            msg.payload_len = (uint16_t)len;
        }
        embediq_publish(s_fb, &msg);
    } else if (strstr(topic, "/state/desired") != NULL) {
        msg.msg_id   = MSG_CFG_RELOAD;
        msg.priority = 0u;
        embediq_publish(s_fb, &msg);
    }
}

/* ---------------------------------------------------------------------------
 * Sub-function handlers
 * All signatures MUST match embediq_subfn_run_fn:
 *   void (*)(EmbedIQ_FB_Handle_t fb, const void *msg,
 *            void *fb_data, void *subfn_data)
 * ------------------------------------------------------------------------- */

static void sf_timer(EmbedIQ_FB_Handle_t fb, const void *msg,
                     void *fb_data, void *subfn_data)
{
    (void)fb; (void)msg; (void)fb_data; (void)subfn_data;
    if (s_state != EMBEDIQ_MQTT_STATE_RECONNECTING) { return; }
    if (s_countdown_sec > 0u) { s_countdown_sec--; return; }
    attempt_connect();
    if (s_state == EMBEDIQ_MQTT_STATE_RECONNECTING) {
        s_backoff_sec *= 2u;
        uint32_t ceil_sec = EMBEDIQ_MQTT_MAX_RECONNECT_INTERVAL_MS / 1000u;
        if (ceil_sec == 0u) { ceil_sec = 300u; }
        if (s_backoff_sec > ceil_sec) { s_backoff_sec = ceil_sec; }
        s_countdown_sec = s_backoff_sec;
    }
}

/* Decode one MSG_TELEMETRY_BATCH message from the bus, serialise to JSON, and
 * publish to the telemetry MQTT topic.
 *
 * JSON schema: {"ts":N,"window_dur_s":N,"metrics":{"ID":V,...}}
 *   ts            — window_start_s (seconds; 0 when no wall clock is available)
 *   window_dur_s  — window duration in seconds
 *   metrics keys  — decimal string of metric_id (uint16)
 *   metrics values — value_a (primary value: avg for gauge, total for counter,
 *                    mean for histogram)
 *
 * Returns without publishing if:
 *   - m is NULL or payload is too short to hold a valid batch header
 *   - entry_count is 0 or payload is too short to hold the declared entries
 *   - snprintf truncation is detected at any stage
 *   - ops or topic not available
 */
static void publish_telemetry_json(const uint8_t *payload)
{
    if (payload == NULL) {
        return;
    }

    /* Decode header with memcpy — payload bytes are uint8_t, alignment not guaranteed. */
    MSG_TELEMETRY_BATCH_Payload_t hdr;
    (void)memcpy(&hdr, payload, sizeof(hdr));

    if (hdr.entry_count == 0u) {
        return;
    }

    /* Serialise prefix. */
    int pos = snprintf(s_json_buf, sizeof(s_json_buf),
                       "{\"ts\":%lu,\"window_dur_s\":%u,\"metrics\":{",
                       (unsigned long)hdr.window_start_s,
                       (unsigned)hdr.window_dur_s);
    if (pos < 0 || (uint32_t)pos >= (uint32_t)sizeof(s_json_buf)) {
        return;
    }

    /* Serialise each entry. */
    for (uint8_t i = 0u; i < hdr.entry_count; i++) {
        EmbedIQ_Telemetry_Batch_Entry_t entry;
        size_t offset = sizeof(MSG_TELEMETRY_BATCH_Payload_t) +
                        (size_t)i * sizeof(EmbedIQ_Telemetry_Batch_Entry_t);
        (void)memcpy(&entry, payload + offset, sizeof(entry));

        const char *sep = (i < (hdr.entry_count - 1u)) ? "," : "";
        int w = snprintf(s_json_buf + pos,
                         sizeof(s_json_buf) - (size_t)pos,
                         "\"%u\":%.6g%s",
                         (unsigned)entry.metric_id,
                         (double)entry.value_a,
                         sep);
        if (w < 0 || ((uint32_t)pos + (uint32_t)w) >= (uint32_t)sizeof(s_json_buf)) {
            return;
        }
        pos += w;
    }

    /* Close JSON object. */
    int wc = snprintf(s_json_buf + pos,
                      sizeof(s_json_buf) - (size_t)pos,
                      "}}");
    if (wc < 0 || ((uint32_t)pos + (uint32_t)wc) >= (uint32_t)sizeof(s_json_buf)) {
        return;
    }
    pos += wc;

    /* Publish JSON. */
    const embediq_mqtt_ops_t *ops = embediq_mqtt_ops_get();
    if (ops != NULL && s_telemetry_topic[0] != '\0') {
        (void)ops->publish(s_telemetry_topic,
                           (const uint8_t *)s_json_buf,
                           (uint32_t)pos, 0u);
    }
}

static void sf_telemetry(EmbedIQ_FB_Handle_t fb, const void *msg,
                          void *fb_data, void *subfn_data)
{
    (void)fb; (void)fb_data; (void)subfn_data;
    if (!msg) { return; }

    /* The dispatcher passes a pointer to the payload bytes (NOT the Msg envelope).
     * See core/src/registry/fb_engine.c:459 (`dispatch_msg_to_fb(fb, msg.msg_id,
     * msg.payload)`) and the convention used in examples/thermostat/fb_temp_controller.c
     * and tests/integration/test_thermostat_observable.c which passes &TempReading_t. */
    const uint8_t *payload = (const uint8_t *)msg;

    if (s_state == EMBEDIQ_MQTT_STATE_CONNECTED &&
        s_telemetry_topic[0] != '\0') {
        publish_telemetry_json(payload);
    } else {
        /* Offline: stash the batch in the ring so we can flush on reconnect.
         * Compute payload_len from the batch header's entry_count and synthesise
         * a Msg envelope for ring_push. */
        MSG_TELEMETRY_BATCH_Payload_t hdr;
        (void)memcpy(&hdr, payload, sizeof(hdr));
        if (hdr.entry_count == 0u) { return; }

        uint32_t plen = (uint32_t)sizeof(hdr) +
                        (uint32_t)hdr.entry_count *
                        (uint32_t)sizeof(EmbedIQ_Telemetry_Batch_Entry_t);
        if (plen > (uint32_t)EMBEDIQ_MSG_MAX_PAYLOAD) { return; }

        EmbedIQ_Msg_t buffered;
        (void)memset(&buffered, 0, sizeof(buffered));
        buffered.msg_id      = MSG_TELEMETRY_BATCH;
        buffered.payload_len = (uint16_t)plen;
        (void)memcpy(buffered.payload, payload, plen);
        ring_push(&buffered);
    }
}

static void sf_cfg_reload(EmbedIQ_FB_Handle_t fb, const void *msg,
                           void *fb_data, void *subfn_data)
{
    (void)fb; (void)msg; (void)fb_data; (void)subfn_data;
    const embediq_mqtt_ops_t *ops = embediq_mqtt_ops_get();
    if (ops && s_state == EMBEDIQ_MQTT_STATE_CONNECTED) {
        (void)ops->disconnect();
    }
    s_state         = EMBEDIQ_MQTT_STATE_DISCONNECTED;
    s_backoff_sec   = 0u;
    s_countdown_sec = 0u;
    attempt_connect();
}

/* ---------------------------------------------------------------------------
 * FB init — register sub-functions (follows fb_telemetry pattern exactly)
 * Signature: void (*init_fn)(EmbedIQ_FB_Handle_t fb, void *fb_data)
 * ------------------------------------------------------------------------- */

static void cloud_mqtt_init(EmbedIQ_FB_Handle_t fb, void *fb_data)
{
    (void)fb_data;
    s_fb = fb;

    embediq_mqtt_set_on_receive(on_mqtt_receive);

    static const uint16_t k_timer_subs[]     = { MSG_TIMER_1SEC       };
    static const uint16_t k_telemetry_subs[] = { MSG_TELEMETRY_BATCH  };
    static const uint16_t k_cfg_subs[]       = { MSG_CFG_RELOAD       };

    static EmbedIQ_SubFn_Config_t k_timer_cfg = {
        .name               = "on_timer",
        .init_order         = 1u,
        .run_fn             = sf_timer,
        .subscriptions      = k_timer_subs,
        .subscription_count = 1u,
    };
    static EmbedIQ_SubFn_Config_t k_telemetry_cfg = {
        .name               = "on_telemetry",
        .init_order         = 2u,
        .run_fn             = sf_telemetry,
        .subscriptions      = k_telemetry_subs,
        .subscription_count = 1u,
    };
    static EmbedIQ_SubFn_Config_t k_cfg_reload_cfg = {
        .name               = "on_cfg_reload",
        .init_order         = 3u,
        .run_fn             = sf_cfg_reload,
        .subscriptions      = k_cfg_subs,
        .subscription_count = 1u,
    };

    (void)embediq_subfn_register(fb, &k_timer_cfg);
    (void)embediq_subfn_register(fb, &k_telemetry_cfg);
    (void)embediq_subfn_register(fb, &k_cfg_reload_cfg);

    attempt_connect();
}

/* ---------------------------------------------------------------------------
 * Public registration (follows fb_telemetry_register pattern)
 * ------------------------------------------------------------------------- */

EMBEDIQ_SUBS(g_cloud_mqtt_subs,
             MSG_TIMER_1SEC,
             MSG_TELEMETRY_BATCH,
             MSG_CFG_RELOAD);

EmbedIQ_FB_Handle_t fb_cloud_mqtt_register(void)
{
    static const EmbedIQ_FB_Config_t k_cfg = {
        .name               = "fb_cloud_mqtt",
        .boot_phase         = EMBEDIQ_BOOT_PHASE_INFRASTRUCTURE,
        .init_fn            = cloud_mqtt_init,
        .subscriptions      = g_cloud_mqtt_subs,
        .subscription_count = 3u,
    };
    return embediq_fb_register(&k_cfg);
}

#ifdef EMBEDIQ_PLATFORM_HOST
/* ---------------------------------------------------------------------------
 * Test-only accessors (host/test builds only — never linked into production)
 * Exposes publish_telemetry_json() and a setter for the topic so the unit
 * test (tests/unit/test_fb_cloud_mqtt.c) can drive the JSON code path without
 * boot/connect machinery.
 * ------------------------------------------------------------------------- */
void fb_cloud_mqtt__publish_telemetry_json_test(const uint8_t *payload)
{
    publish_telemetry_json(payload);
}

void fb_cloud_mqtt__set_topic_test(const char *topic)
{
    if (topic == NULL) { s_telemetry_topic[0] = '\0'; return; }
    (void)snprintf(s_telemetry_topic, sizeof(s_telemetry_topic), "%s", topic);
}
#endif /* EMBEDIQ_PLATFORM_HOST */
