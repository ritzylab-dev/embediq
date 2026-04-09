#define _POSIX_C_SOURCE 200809L

/*
 * core/src/message_bus.c — Message bus: routing, 3-queue model, overflow policy
 *
 * Implements the contracts declared in core/include/embediq_bus.h.
 *
 * Responsibilities:
 *   1. message_bus_boot()  — build subscription table; create per-FB queues
 *   2. embediq_publish()   — route message to all subscribers; enforce overflow
 *   3. embediq_bus_resolve_id() — endpoint index → FB name
 *
 * Note: embediq_bus_resolve_name() (name → index) lives in fb_engine.c and is
 * already implemented there.  Both functions are declared in embediq_bus.h.
 *
 * SUBSCRIPTION TABLE:
 *   Flat sorted array of (msg_id, ep_id) pairs built once at message_bus_boot().
 *   Read-only after boot (v1: no dynamic subscription changes).
 *   Binary search for O(log n) lookup.
 *
 * QUEUE OVERFLOW POLICY (Architecture v8 §6.2.1 — BINDING):
 *   HIGH   full → block publisher   (embediq_osal_queue_send, UINT32_MAX timeout)
 *   NORMAL full → drop OLDEST msg, enqueue new, emit EMBEDIQ_OBS_EVT_QUEUE_DROP
 *   LOW    full → drop INCOMING msg,             emit EMBEDIQ_OBS_EVT_QUEUE_DROP
 *
 * NAMESPACE:
 *   msg_id 0x0000–0x03FF is reserved for core-system use.
 *   Application FBs publishing in this range get a host-only log warning.
 *   v1 does not block them — strict enforcement is future tooling.
 *
 * R-02: no malloc/free in this file. Queue memory is allocated inside the
 *       OSAL implementation (the approved R-02 exception site).
 *
 * Host/test-only API (EMBEDIQ_PLATFORM_HOST):
 *   message_bus__reset()        — clear routing state between test cases
 *   message_bus__test_recv()    — non-blocking queue recv (bypasses task loop)
 *   message_bus__drop_count()   — total drop events emitted since last reset
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "embediq_bus.h"
#include "embediq_msg.h"
#include "embediq_obs.h"
#include "embediq_osal.h"
#include "embediq_config.h"

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#ifdef EMBEDIQ_PLATFORM_HOST
#  include <stdio.h>
#endif

/* ---------------------------------------------------------------------------
 * Package-internal API imported from fb_engine.c (no public header).
 * These are non-static functions defined in fb_engine.c; extern declarations
 * here avoid introducing a private header.
 * ------------------------------------------------------------------------- */

extern uint8_t                     fb_engine__reg_count(void);
extern const EmbedIQ_FB_Config_t  *fb_engine__reg_config(uint8_t idx);
extern uint8_t                     fb_engine__get_ep_id(EmbedIQ_FB_Handle_t h);

/* Notify callback — registered by the engine, called after each enqueue. */
static embediq_notify_fn_t g_notify_fn = NULL;

void message_bus_set_notify_fn(embediq_notify_fn_t fn)
{
    g_notify_fn = fn;
}

/* ---------------------------------------------------------------------------
 * Subscription table — flat sorted array, built once at message_bus_boot()
 * ------------------------------------------------------------------------- */

typedef struct {
    uint16_t msg_id;
    uint8_t  ep_id;
} sub_entry_t;

static sub_entry_t g_subs[EMBEDIQ_MAX_SUBSCRIPTIONS];
static uint16_t    g_sub_count = 0;

/* ---------------------------------------------------------------------------
 * Per-endpoint queue set — one HIGH, one NORMAL, one LOW per registered FB
 * ------------------------------------------------------------------------- */

typedef struct {
    EmbedIQ_Queue_t *q[3];    /* indexed by EmbedIQ_Msg_Priority_t */
    uint16_t         cap[3];  /* capacity of each queue (items) — for depth % calc */
} ep_queues_t;

static ep_queues_t g_ep[EMBEDIQ_MAX_ENDPOINTS];
static uint8_t     g_ep_count   = 0;
static bool        g_bus_booted = false;

/* Drop event counter (host/test builds only). */
#ifdef EMBEDIQ_PLATFORM_HOST
static uint32_t g_drop_count = 0;
#endif

/* ---------------------------------------------------------------------------
 * Queue depth warning helper — XOBS-3
 *
 * Emits EMBEDIQ_OBS_EVT_BUS_QUEUE_DEPTH when fill % >= EMBEDIQ_QUEUE_WARN_THRESHOLD.
 * Gated by EMBEDIQ_OBS_EMIT_RESOURCE (EMBEDIQ_TRACE_RESOURCE must be enabled).
 * Division-by-zero safe: guard on cap > 0.
 * ------------------------------------------------------------------------- */
#define BUS_QUEUE_DEPTH_CHECK(q_, cap_, src_ep_, dst_ep_, prio_, msg_id_)       \
    do {                                                                          \
        if ((cap_) > 0u) {                                                        \
            uint8_t _pct = (uint8_t)(((uint32_t)embediq_osal_queue_count(q_)    \
                                      * 100u) / (uint32_t)(cap_));               \
            if (_pct >= (uint8_t)EMBEDIQ_QUEUE_WARN_THRESHOLD) {                 \
                EMBEDIQ_OBS_EMIT_RESOURCE(EMBEDIQ_OBS_EVT_BUS_QUEUE_DEPTH,       \
                                          (src_ep_), (dst_ep_),                  \
                                          _pct, (msg_id_));                       \
            }                                                                     \
        }                                                                         \
    } while (0)

/* ---------------------------------------------------------------------------
 * Internal helpers
 * ------------------------------------------------------------------------- */

/** Insertion sort for the subscription table — called once at boot. */
static void sub_sort(void)
{
    for (uint16_t i = 1u; i < g_sub_count; i++) {
        sub_entry_t key = g_subs[i];
        int j = (int)i - 1;
        while (j >= 0 && g_subs[j].msg_id > key.msg_id) {
            g_subs[j + 1] = g_subs[j];
            j--;
        }
        g_subs[j + 1] = key;
    }
}

/**
 * Binary search: return index of the first subscription entry whose msg_id
 * equals target.  Returns -1 if not found.
 */
static int sub_first(uint16_t target)
{
    int lo = 0, hi = (int)g_sub_count - 1, found = -1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        if (g_subs[mid].msg_id == target) {
            found = mid;
            hi    = mid - 1;   /* continue left to find the first occurrence */
        } else if (g_subs[mid].msg_id < target) {
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return found;
}

/* ---------------------------------------------------------------------------
 * Public: message_bus_boot()
 * ------------------------------------------------------------------------- */

void message_bus_boot(void)
{
    if (g_bus_booted) return;   /* idempotent */

    uint8_t n = fb_engine__reg_count();
    g_ep_count = n;

    /* Create three priority queues for each registered FB. */
    for (uint8_t i = 0u; i < n; i++) {
        g_ep[i].q[EMBEDIQ_MSG_PRIORITY_HIGH]    =
            embediq_osal_queue_create(EMBEDIQ_HIGH_QUEUE_DEPTH,   sizeof(EmbedIQ_Msg_t));
        g_ep[i].cap[EMBEDIQ_MSG_PRIORITY_HIGH]  = EMBEDIQ_HIGH_QUEUE_DEPTH;

        g_ep[i].q[EMBEDIQ_MSG_PRIORITY_NORMAL]  =
            embediq_osal_queue_create(EMBEDIQ_NORMAL_QUEUE_DEPTH, sizeof(EmbedIQ_Msg_t));
        g_ep[i].cap[EMBEDIQ_MSG_PRIORITY_NORMAL] = EMBEDIQ_NORMAL_QUEUE_DEPTH;

        g_ep[i].q[EMBEDIQ_MSG_PRIORITY_LOW]     =
            embediq_osal_queue_create(EMBEDIQ_LOW_QUEUE_DEPTH,    sizeof(EmbedIQ_Msg_t));
        g_ep[i].cap[EMBEDIQ_MSG_PRIORITY_LOW]   = EMBEDIQ_LOW_QUEUE_DEPTH;
    }

    /* Build subscription table from each FB's subscription list. */
    g_sub_count = 0u;
    for (uint8_t i = 0u; i < n; i++) {
        const EmbedIQ_FB_Config_t *cfg = fb_engine__reg_config(i);
        if (!cfg || !cfg->subscriptions) continue;
        for (uint8_t s = 0u; s < cfg->subscription_count; s++) {
            if (g_sub_count >= EMBEDIQ_MAX_SUBSCRIPTIONS) break;
            g_subs[g_sub_count].msg_id = cfg->subscriptions[s];
            g_subs[g_sub_count].ep_id  = i;
            g_sub_count++;
        }
    }

    sub_sort();

    g_bus_booted = true;
}

/* ---------------------------------------------------------------------------
 * Public: embediq_bus_resolve_id()
 * ------------------------------------------------------------------------- */

const char *embediq_bus_resolve_id(uint8_t endpoint_id)
{
    const EmbedIQ_FB_Config_t *cfg = fb_engine__reg_config(endpoint_id);
    if (!cfg) return NULL;
    return cfg->name;
}

/* ---------------------------------------------------------------------------
 * Public: embediq_publish()
 *
 * 1. Stamp source endpoint id from the publishing FB handle.
 * 2. Namespace check — warn on core-reserved IDs (0x0000–0x03FF), don't block.
 * 3. Lookup subscribers via binary search.
 * 4. For each subscriber, copy message into the appropriate priority queue.
 *    Overflow is handled per queue level (BINDING policy, AGENTS.md §2B).
 * ------------------------------------------------------------------------- */

void embediq_publish(EmbedIQ_FB_Handle_t fb, EmbedIQ_Msg_t *msg)
{
    if (!msg) return;

    /* Stamp source endpoint — convenience for sub-fns reading the envelope. */
    msg->source_endpoint_id = fb_engine__get_ep_id(fb);

    uint16_t msg_id = msg->msg_id;
    uint8_t  prio   = msg->priority;

    /* Clamp invalid priority to NORMAL. */
    if (prio > (uint8_t)EMBEDIQ_MSG_PRIORITY_LOW) {
        prio = (uint8_t)EMBEDIQ_MSG_PRIORITY_NORMAL;
    }

    /* Namespace warning: 0x0000–0x03FF is core-system reserved. */
#ifdef EMBEDIQ_PLATFORM_HOST
    if (msg_id <= 0x03FFu) {
        printf("[BUS] warn: msg_id 0x%04x is in core-reserved range "
               "(0x0000-0x03FF); publishing anyway per v1 policy\n", msg_id);
    }
#endif

    /* Find first subscriber entry via binary search. */
    int first = sub_first(msg_id);
    if (first < 0) return;   /* no subscribers — silent drop, no event */

    /* Deliver to each subscriber endpoint. */
    for (int i = first;
         i < (int)g_sub_count && g_subs[i].msg_id == msg_id;
         i++) {

        uint8_t ep = g_subs[i].ep_id;
        if (ep >= g_ep_count) continue;

        EmbedIQ_Queue_t *q = g_ep[ep].q[prio];
        if (!q) continue;

        /* Copy the message per-subscriber (v1: copy-by-value). */
        EmbedIQ_Msg_t copy = *msg;

        if (prio == (uint8_t)EMBEDIQ_MSG_PRIORITY_HIGH) {
            /* ---- HIGH: block until space is available ---- */
            embediq_osal_queue_send(q, &copy, UINT32_MAX);
            BUS_QUEUE_DEPTH_CHECK(q, g_ep[ep].cap[prio],
                                  msg->source_endpoint_id, ep, prio, msg_id);
            if (g_notify_fn) g_notify_fn(ep);

        } else if (prio == (uint8_t)EMBEDIQ_MSG_PRIORITY_NORMAL) {
            /* ---- NORMAL: drop OLDEST on overflow ---- */
            if (embediq_osal_queue_send(q, &copy, 0u) != EMBEDIQ_OK) {
                EmbedIQ_Msg_t oldest;
                embediq_osal_queue_recv(q, &oldest, 0u);  /* discard oldest */
                embediq_osal_queue_send(q, &copy,   0u);  /* enqueue new */
                embediq_obs_emit(EMBEDIQ_OBS_EVT_QUEUE_DROP,
                                 msg->source_endpoint_id, ep,
                                 prio, msg_id);
#ifdef EMBEDIQ_PLATFORM_HOST
                g_drop_count++;
#endif
            }
            BUS_QUEUE_DEPTH_CHECK(q, g_ep[ep].cap[prio],
                                  msg->source_endpoint_id, ep, prio, msg_id);
            if (g_notify_fn) g_notify_fn(ep);

        } else {
            /* ---- LOW: drop INCOMING on overflow ---- */
            if (embediq_osal_queue_send(q, &copy, 0u) == EMBEDIQ_OK) {
                BUS_QUEUE_DEPTH_CHECK(q, g_ep[ep].cap[prio],
                                      msg->source_endpoint_id, ep, prio, msg_id);
                if (g_notify_fn) g_notify_fn(ep);
            } else {
                embediq_obs_emit(EMBEDIQ_OBS_EVT_QUEUE_DROP,
                                 msg->source_endpoint_id, ep,
                                 prio, msg_id);
#ifdef EMBEDIQ_PLATFORM_HOST
                g_drop_count++;
#endif
            }
        }
    }
}

/* ---------------------------------------------------------------------------
 * Public: message_bus_recv_ep()
 *
 * Non-blocking receive used by the per-FB dispatch loop.
 * Returns true if a message was available; false if the queue was empty.
 * ------------------------------------------------------------------------- */

bool message_bus_recv_ep(uint8_t ep_id, uint8_t priority, EmbedIQ_Msg_t *out)
{
    if (ep_id >= g_ep_count || priority > 2u || !out) return false;
    EmbedIQ_Queue_t *q = g_ep[ep_id].q[priority];
    if (!q) return false;
    return embediq_osal_queue_recv(q, out, 0u) == EMBEDIQ_OK;
}

/* ---------------------------------------------------------------------------
 * Host / test-only API
 * ------------------------------------------------------------------------- */

#ifdef EMBEDIQ_PLATFORM_HOST

/**
 * Reset all bus state.  Destroys all per-FB queues before zeroing handles.
 */
void message_bus__reset(void)
{
    for (uint8_t i = 0u; i < g_ep_count; i++) {
        for (uint8_t p = 0u; p < 3u; p++) {
            embediq_osal_queue_destroy(g_ep[i].q[p]);
        }
    }
    memset(g_ep,   0, sizeof(g_ep));
    memset(g_subs, 0, sizeof(g_subs));
    g_sub_count   = 0u;
    g_ep_count    = 0u;
    g_bus_booted  = false;
    g_drop_count  = 0u;
    g_notify_fn   = NULL;
}

/** Return the currently registered notify callback (NULL if not yet set). */
embediq_notify_fn_t message_bus__get_notify_fn(void)
{
    return g_notify_fn;
}

/**
 * Non-blocking receive from an endpoint's queue at the given priority level.
 * Returns true and fills *out if a message was available, false if queue empty.
 * Used by unit tests to inspect queue contents without a running FB task.
 */
bool message_bus__test_recv(uint8_t ep_id, uint8_t prio, EmbedIQ_Msg_t *out)
{
    if (ep_id >= g_ep_count || prio > 2u) return false;
    EmbedIQ_Queue_t *q = g_ep[ep_id].q[prio];
    if (!q) return false;
    return embediq_osal_queue_recv(q, out, 0u) == EMBEDIQ_OK;
}

/** Return the total number of drop events emitted since last reset. */
uint32_t message_bus__drop_count(void)
{
    return g_drop_count;
}

#endif /* EMBEDIQ_PLATFORM_HOST */
