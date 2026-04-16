/*
 * fbs/services/fb_telemetry.c — Telemetry aggregation Service FB
 *
 * Subscribes to MSG_TELEMETRY_GAUGE / _COUNTER / _HISTOGRAM published by
 * any application FB; aggregates into a static table keyed by metric_id;
 * publishes one or more MSG_TELEMETRY_BATCH messages at window boundaries
 * (EMBEDIQ_TELEMETRY_WINDOW_SEC seconds of MSG_TIMER_1SEC ticks).
 *
 * Layer rules honoured (ARCHITECTURE.md §Layer 2 Service FB):
 *   - No HAL includes (platform-agnostic).
 *   - No OSAL / RTOS / POSIX threading calls (R-sub-03).
 *   - No blocking primitives in handlers (R-sub-03a).
 *   - No dynamic allocation (I-07, R-02) — all state in static arrays.
 *
 * Batch payload layout (packed; 8-byte header + N × 18-byte entries):
 *   header  — MSG_TELEMETRY_BATCH_Payload_t from generated catalog
 *   entries — EmbedIQ_Telemetry_Batch_Entry_t[N] packed after the header
 *             (public type in core/include/embediq_telemetry.h)
 *
 * Batch capacity: (EMBEDIQ_MSG_MAX_PAYLOAD - sizeof(batch header)) /
 * sizeof(EmbedIQ_Telemetry_Batch_Entry_t). With MAX_PAYLOAD=64 that is
 * 3 entries per message; tables larger than 3 active entries are split
 * across multiple MSG_TELEMETRY_BATCH messages, each with its own
 * entry_count.
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "fb_telemetry.h"
#include "embediq_subfn.h"
#include "embediq_bus.h"
#include "embediq_msg.h"
#include "embediq_telemetry.h"
#include "embediq_config.h"
#include "embediq_platform_msgs.h"

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>

/* ---------------------------------------------------------------------------
 * Internal types
 *
 * Public types live in core/include/embediq_telemetry.h:
 *   EmbedIQ_Telemetry_Batch_Entry_t   — packed 18-byte on-wire entry
 *   EmbedIQ_Telemetry_Entry_Type_t    — enum for the .type field
 * ------------------------------------------------------------------------- */

#define TEL_SLOT_FREE       0xFFFFu

#define TEL_FLT_MAX         3.402823466e+38f

typedef struct {
    uint16_t metric_id;      /* TEL_SLOT_FREE = unused slot */
    uint8_t  type;
    uint8_t  unit_id;
    uint32_t count;
    float    sum;
    float    min;
    float    max;
    float    last;
    uint32_t counter_total;
} fb_telemetry_entry_t;

typedef struct {
    char key[EMBEDIQ_TELEMETRY_TAG_LEN];
    char val[EMBEDIQ_TELEMETRY_TAG_LEN];
} TelTag_t;

/* Batch header as defined by generated/telemetry_msg_catalog.h. */
#define TEL_HDR_SIZE   (sizeof(MSG_TELEMETRY_BATCH_Payload_t))
#define TEL_ENTRY_SIZE EMBEDIQ_TELEMETRY_ENTRY_SIZE
#define TEL_ENTRIES_PER_BATCH \
    ((EMBEDIQ_MSG_MAX_PAYLOAD - TEL_HDR_SIZE) / TEL_ENTRY_SIZE)

/* ---------------------------------------------------------------------------
 * Module state (all static, R-02)
 * ------------------------------------------------------------------------- */

static fb_telemetry_entry_t s_table[EMBEDIQ_TELEMETRY_MAX_METRICS];
static TelTag_t             s_tags[EMBEDIQ_TELEMETRY_MAX_TAGS];
static uint8_t              s_tag_count;
static uint32_t             s_elapsed_s;
static uint32_t             s_window_start_s;
static EmbedIQ_FB_Handle_t  s_fb;

/* ---------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */

static void reset_table(void)
{
    for (uint32_t i = 0u; i < EMBEDIQ_TELEMETRY_MAX_METRICS; i++) {
        s_table[i].metric_id     = TEL_SLOT_FREE;
        s_table[i].count         = 0u;
        s_table[i].sum           = 0.0f;
        s_table[i].min           =  TEL_FLT_MAX;
        s_table[i].max           = -TEL_FLT_MAX;
        s_table[i].last          = 0.0f;
        s_table[i].counter_total = 0u;
    }
    s_elapsed_s      = 0u;
    s_window_start_s = 0u;  /* no wall clock in Service FB — see design note */
}

static fb_telemetry_entry_t *find_or_alloc(uint16_t metric_id,
                                           uint8_t  type,
                                           uint8_t  unit_id)
{
    /* Linear scan — MAX_METRICS is small (default 32). */
    for (uint32_t i = 0u; i < EMBEDIQ_TELEMETRY_MAX_METRICS; i++) {
        if (s_table[i].metric_id == metric_id) {
            return &s_table[i];
        }
    }
    for (uint32_t i = 0u; i < EMBEDIQ_TELEMETRY_MAX_METRICS; i++) {
        if (s_table[i].metric_id == TEL_SLOT_FREE) {
            s_table[i].metric_id     = metric_id;
            s_table[i].type          = type;
            s_table[i].unit_id       = unit_id;
            s_table[i].count         = 0u;
            s_table[i].sum           = 0.0f;
            s_table[i].min           =  TEL_FLT_MAX;
            s_table[i].max           = -TEL_FLT_MAX;
            s_table[i].last          = 0.0f;
            s_table[i].counter_total = 0u;
            return &s_table[i];
        }
    }
    return NULL;  /* table full — caller drops silently */
}

static void build_entry(const fb_telemetry_entry_t *src, EmbedIQ_Telemetry_Batch_Entry_t *dst)
{
    dst->metric_id = src->metric_id;
    dst->type      = src->type;
    dst->unit_id   = src->unit_id;
    dst->count     = (src->count > UINT16_MAX) ? UINT16_MAX
                                               : (uint16_t)src->count;
    float n = (src->count > 0u) ? (float)src->count : 1.0f;

    switch (src->type) {
    case EMBEDIQ_TELEMETRY_ENTRY_GAUGE:
        dst->value_a = src->sum / n;   /* avg  */
        dst->value_b = src->last;      /* last */
        dst->value_c = src->max;       /* max  */
        break;
    case EMBEDIQ_TELEMETRY_ENTRY_COUNTER:
        dst->value_a = (float)src->counter_total;
        dst->value_b = 0.0f;
        dst->value_c = 0.0f;
        break;
    case EMBEDIQ_TELEMETRY_ENTRY_HISTOGRAM:
    default:
        dst->value_a = src->sum / n;   /* mean */
        dst->value_b = src->min;
        dst->value_c = src->max;
        break;
    }
}

static void publish_batch(uint8_t fault_triggered)
{
    if (s_fb == NULL) {
        return;  /* test mode: no registered FB, skip bus publish */
    }

    /* Walk the table in groups of up to TEL_ENTRIES_PER_BATCH active slots.
     * Each group becomes one MSG_TELEMETRY_BATCH message. */
    uint32_t i = 0u;
    while (i < EMBEDIQ_TELEMETRY_MAX_METRICS) {
        EmbedIQ_Telemetry_Batch_Entry_t group[TEL_ENTRIES_PER_BATCH];
        uint8_t n = 0u;

        while (i < EMBEDIQ_TELEMETRY_MAX_METRICS && n < TEL_ENTRIES_PER_BATCH) {
            if (s_table[i].metric_id != TEL_SLOT_FREE && s_table[i].count > 0u) {
                build_entry(&s_table[i], &group[n]);
                n++;
            }
            i++;
        }

        if (n == 0u) {
            break;  /* nothing more to publish */
        }

        MSG_TELEMETRY_BATCH_Payload_t hdr;
        hdr.window_start_s = s_window_start_s;
        hdr.window_dur_s   = (uint16_t)EMBEDIQ_TELEMETRY_WINDOW_SEC;
        hdr.entry_count    = n;
        hdr.flags          = (fault_triggered != 0u) ? 0x01u : 0x00u;

        EmbedIQ_Msg_t m;
        memset(&m, 0, sizeof(m));
        m.msg_id   = MSG_TELEMETRY_BATCH;
        m.priority = EMBEDIQ_MSG_PRIORITY_NORMAL;

        memcpy(&m.payload[0], &hdr, TEL_HDR_SIZE);
        memcpy(&m.payload[TEL_HDR_SIZE], group, (size_t)n * TEL_ENTRY_SIZE);
        m.payload_len = (uint16_t)(TEL_HDR_SIZE + (size_t)n * TEL_ENTRY_SIZE);

        embediq_publish(s_fb, &m);
    }
}

/* ---------------------------------------------------------------------------
 * Package-internal aggregator helpers — callable from both the sub-function
 * handlers (production path) and the unit tests (EMBEDIQ_PLATFORM_HOST).
 *
 * These are not in fb_telemetry.h; declared extern in tests/unit/test_fb_telemetry.c.
 * ------------------------------------------------------------------------- */

void fb_telemetry__agg_gauge(uint16_t metric_id, float value, uint8_t unit_id)
{
    fb_telemetry_entry_t *e = find_or_alloc(metric_id, EMBEDIQ_TELEMETRY_ENTRY_GAUGE, unit_id);
    if (e == NULL) return;
    e->count++;
    e->sum  += value;
    if (value < e->min) e->min = value;
    if (value > e->max) e->max = value;
    e->last = value;
}

void fb_telemetry__agg_counter(uint16_t metric_id, uint32_t delta, uint8_t unit_id)
{
    fb_telemetry_entry_t *e = find_or_alloc(metric_id, EMBEDIQ_TELEMETRY_ENTRY_COUNTER, unit_id);
    if (e == NULL) return;
    e->count++;
    e->counter_total += delta;
}

void fb_telemetry__agg_histogram(uint16_t metric_id, float observation, uint8_t unit_id)
{
    fb_telemetry_entry_t *e = find_or_alloc(metric_id, EMBEDIQ_TELEMETRY_ENTRY_HISTOGRAM, unit_id);
    if (e == NULL) return;
    e->count++;
    e->sum += observation;
    if (observation < e->min) e->min = observation;
    if (observation > e->max) e->max = observation;
}

/* ---------------------------------------------------------------------------
 * Sub-function handlers — thin wrappers: extract payload, call aggregator
 * ------------------------------------------------------------------------- */

static void on_gauge(EmbedIQ_FB_Handle_t fb, const void *msg,
                     void *fb_data, void *subfn_data)
{
    (void)fb; (void)fb_data; (void)subfn_data;
    if (msg == NULL) return;
    const EmbedIQ_Msg_t *m = (const EmbedIQ_Msg_t *)msg;
    MSG_TELEMETRY_GAUGE_Payload_t p;
    memcpy(&p, m->payload, sizeof(p));
    fb_telemetry__agg_gauge(p.metric_id, p.value, p.unit_id);
}

static void on_counter(EmbedIQ_FB_Handle_t fb, const void *msg,
                       void *fb_data, void *subfn_data)
{
    (void)fb; (void)fb_data; (void)subfn_data;
    if (msg == NULL) return;
    const EmbedIQ_Msg_t *m = (const EmbedIQ_Msg_t *)msg;
    MSG_TELEMETRY_COUNTER_Payload_t p;
    memcpy(&p, m->payload, sizeof(p));
    fb_telemetry__agg_counter(p.metric_id, p.delta, p.unit_id);
}

static void on_histogram(EmbedIQ_FB_Handle_t fb, const void *msg,
                         void *fb_data, void *subfn_data)
{
    (void)fb; (void)fb_data; (void)subfn_data;
    if (msg == NULL) return;
    const EmbedIQ_Msg_t *m = (const EmbedIQ_Msg_t *)msg;
    MSG_TELEMETRY_HISTOGRAM_Payload_t p;
    memcpy(&p, m->payload, sizeof(p));
    fb_telemetry__agg_histogram(p.metric_id, p.observation, p.unit_id);
}

static void on_timer(EmbedIQ_FB_Handle_t fb, const void *msg,
                     void *fb_data, void *subfn_data)
{
    (void)fb; (void)msg; (void)fb_data; (void)subfn_data;
    s_elapsed_s++;
    if (s_elapsed_s >= EMBEDIQ_TELEMETRY_WINDOW_SEC) {
        publish_batch(0u);
        reset_table();
    }
}

/* ---------------------------------------------------------------------------
 * FB init — register four sub-functions (R-sub-08)
 * ------------------------------------------------------------------------- */

static void telemetry_init(EmbedIQ_FB_Handle_t fb, void *fb_data)
{
    (void)fb_data;
    s_fb = fb;

    static const uint16_t k_gauge_subs[]     = { MSG_TELEMETRY_GAUGE     };
    static const uint16_t k_counter_subs[]   = { MSG_TELEMETRY_COUNTER   };
    static const uint16_t k_histogram_subs[] = { MSG_TELEMETRY_HISTOGRAM };
    static const uint16_t k_timer_subs[]     = { MSG_TIMER_1SEC          };

    static EmbedIQ_SubFn_Config_t k_gauge_cfg = {
        .name               = "on_gauge",
        .init_order         = 1u,
        .run_fn             = on_gauge,
        .subscriptions      = k_gauge_subs,
        .subscription_count = 1u,
    };
    static EmbedIQ_SubFn_Config_t k_counter_cfg = {
        .name               = "on_counter",
        .init_order         = 2u,
        .run_fn             = on_counter,
        .subscriptions      = k_counter_subs,
        .subscription_count = 1u,
    };
    static EmbedIQ_SubFn_Config_t k_histogram_cfg = {
        .name               = "on_histogram",
        .init_order         = 3u,
        .run_fn             = on_histogram,
        .subscriptions      = k_histogram_subs,
        .subscription_count = 1u,
    };
    static EmbedIQ_SubFn_Config_t k_timer_cfg = {
        .name               = "on_timer",
        .init_order         = 4u,
        .run_fn             = on_timer,
        .subscriptions      = k_timer_subs,
        .subscription_count = 1u,
    };

    (void)embediq_subfn_register(fb, &k_gauge_cfg);
    (void)embediq_subfn_register(fb, &k_counter_cfg);
    (void)embediq_subfn_register(fb, &k_histogram_cfg);
    (void)embediq_subfn_register(fb, &k_timer_cfg);
}

/* ---------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

EMBEDIQ_SUBS(g_telemetry_subs,
    MSG_TELEMETRY_GAUGE,
    MSG_TELEMETRY_COUNTER,
    MSG_TELEMETRY_HISTOGRAM,
    MSG_TIMER_1SEC);

EMBEDIQ_PUBS(g_telemetry_pubs, MSG_TELEMETRY_BATCH);

EmbedIQ_FB_Handle_t fb_telemetry_register(void)
{
    /* First registration path: initialise accumulators and tags. */
    reset_table();
    s_tag_count = 0u;

    static const EmbedIQ_FB_Config_t k_cfg = {
        .name               = "fb_telemetry",
        .boot_phase         = EMBEDIQ_BOOT_PHASE_INFRASTRUCTURE,
        .init_fn            = telemetry_init,
        .subscriptions      = g_telemetry_subs,
        .subscription_count = 4u,
        .publications       = g_telemetry_pubs,
        .publication_count  = 1u,
    };
    return embediq_fb_register(&k_cfg);
}

void fb_telemetry_set_tag(const char *key, const char *value)
{
    if (key == NULL || value == NULL) return;
    if (s_tag_count >= EMBEDIQ_TELEMETRY_MAX_TAGS) return;

    TelTag_t *t = &s_tags[s_tag_count];
    (void)strncpy(t->key, key,   EMBEDIQ_TELEMETRY_TAG_LEN - 1u);
    (void)strncpy(t->val, value, EMBEDIQ_TELEMETRY_TAG_LEN - 1u);
    t->key[EMBEDIQ_TELEMETRY_TAG_LEN - 1u] = '\0';
    t->val[EMBEDIQ_TELEMETRY_TAG_LEN - 1u] = '\0';
    s_tag_count++;
}

/* ---------------------------------------------------------------------------
 * Test-only API — EMBEDIQ_PLATFORM_HOST only (I-05)
 * ------------------------------------------------------------------------- */

#ifdef EMBEDIQ_PLATFORM_HOST

void fb_telemetry__reset(void)
{
    reset_table();
    s_tag_count = 0u;
    /* s_fb left intact so production path (if registered) is unaffected */
}

bool fb_telemetry__get_entry(uint16_t metric_id, fb_telemetry_entry_t *out)
{
    if (out == NULL) return false;
    for (uint32_t i = 0u; i < EMBEDIQ_TELEMETRY_MAX_METRICS; i++) {
        if (s_table[i].metric_id == metric_id) {
            *out = s_table[i];
            return true;
        }
    }
    return false;
}

uint8_t fb_telemetry__active_count(void)
{
    uint8_t n = 0u;
    for (uint32_t i = 0u; i < EMBEDIQ_TELEMETRY_MAX_METRICS; i++) {
        if (s_table[i].metric_id != TEL_SLOT_FREE && s_table[i].count > 0u) {
            n++;
        }
    }
    return n;
}

void fb_telemetry__force_flush(uint8_t fault_triggered)
{
    publish_batch(fault_triggered);  /* no-op if s_fb == NULL */
    reset_table();
}

#endif /* EMBEDIQ_PLATFORM_HOST */
