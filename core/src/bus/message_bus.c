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

/* Drop event counter (host/test builds only). Atomic: incremented from
 * every publishing thread. */
#ifdef EMBEDIQ_PLATFORM_HOST
static embediq_osal_atomic32_t g_drop_count = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);
#endif

/* Async-post tokens — one static slot per endpoint (R-02: no allocation).
 * Opaque to callers; the token is the only thing a callback may hold.
 * Ownership: written by message_bus_boot() only (boot thread, before any
 * dispatch or platform thread exists; message_bus__reset() refills it in
 * host tests); read-only after boot. embediq_bus_token_for() and
 * embediq_bus_post_async() only read it (review R2). */
struct embediq_bus_token {
    uint8_t source_ep;
};
static struct embediq_bus_token g_tokens[EMBEDIQ_MAX_ENDPOINTS];

static void tokens_fill(void)
{
    for (uint8_t i = 0u; i < (uint8_t)EMBEDIQ_MAX_ENDPOINTS; i++) {
        g_tokens[i].source_ep = i;
    }
}

/* ---------------------------------------------------------------------------
 * Queue depth warning helper — XOBS-3
 *
 * Emits EMBEDIQ_OBS_EVT_BUS_QUEUE_DEPTH when fill % >= EMBEDIQ_QUEUE_WARN_THRESHOLD.
 * Gated like EMBEDIQ_OBS_EMIT_RESOURCE (EMBEDIQ_TRACE_RESOURCE must be enabled).
 * async_ selects the emit path (bus_obs_emit): the async post path records
 * through the lock-free ISR ring, never the thread-path emit (review R1).
 * Division-by-zero safe: guard on cap > 0.
 * ------------------------------------------------------------------------- */
#if EMBEDIQ_TRACE_RESOURCE
#  define BUS_OBS_EMIT_RESOURCE(async_, evt_, src_, dst_, state_, msg_id_) \
     bus_obs_emit((async_), (evt_), (src_), (dst_), (state_), (msg_id_))
#else
#  define BUS_OBS_EMIT_RESOURCE(async_, evt_, src_, dst_, state_, msg_id_) ((void)0)
#endif

#define BUS_QUEUE_DEPTH_CHECK(async_, q_, cap_, src_ep_, dst_ep_, prio_, msg_id_) \
    do {                                                                          \
        if ((cap_) > 0u) {                                                        \
            uint8_t _pct = (uint8_t)(((uint32_t)embediq_osal_queue_count(q_)    \
                                      * 100u) / (uint32_t)(cap_));               \
            if (_pct >= (uint8_t)EMBEDIQ_QUEUE_WARN_THRESHOLD) {                 \
                BUS_OBS_EMIT_RESOURCE((async_), EMBEDIQ_OBS_EVT_BUS_QUEUE_DEPTH, \
                                      (src_ep_), (dst_ep_), _pct, (msg_id_));    \
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

    uint8_t n = embediq_engine_fb_count();
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
        const EmbedIQ_FB_Config_t *cfg = embediq_engine_fb_config(i);
        if (!cfg || !cfg->subscriptions) continue;
        for (uint8_t s = 0u; s < cfg->subscription_count; s++) {
            if (g_sub_count >= EMBEDIQ_MAX_SUBSCRIPTIONS) break;
            g_subs[g_sub_count].msg_id = cfg->subscriptions[s];
            g_subs[g_sub_count].ep_id  = i;
            g_sub_count++;
        }
    }

    sub_sort();
    tokens_fill();   /* the only writer of g_tokens (R2) */

    g_bus_booted = true;
}

/* ---------------------------------------------------------------------------
 * Public: embediq_bus_resolve_id()
 * ------------------------------------------------------------------------- */

const char *embediq_bus_resolve_id(uint8_t endpoint_id)
{
    const EmbedIQ_FB_Config_t *cfg = embediq_engine_fb_config(endpoint_id);
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

/* ---------------------------------------------------------------------------
 * route_message_ex() — private routing helper
 *
 * Contains all subscription lookup, queue enqueue, and overflow policy logic.
 * msg must have source_endpoint_id already stamped by the caller.
 *
 * async == false — embediq_publish() / bus_inject() / bridge route:
 *   HIGH blocks until space; Observatory via embediq_obs_emit().
 * async == true  — embediq_bus_post_async() (HAL/ops-table callbacks):
 *   no send waits for capacity: HIGH full = not delivered + drop observed +
 *   EMBEDIQ_ERR; every Observatory record (MSG_TX, QUEUE_DROP, XOBS-3 depth,
 *   and the OSAL obligation macros reached through the queue sends) goes
 *   through embediq_obs_emit_from_isr() (lock-free ISR ring); no printf;
 *   no transport I/O.
 * Both: NORMAL full drops the oldest atomically (one queue-lock operation);
 *       LOW full drops the incoming message; drops are counted atomically.
 * Returns EMBEDIQ_OK, or EMBEDIQ_ERR if any async HIGH delivery failed.
 * ------------------------------------------------------------------------- */

static void bus_obs_emit(bool async, uint8_t evt, uint8_t src, uint8_t dst,
                         uint8_t prio, uint16_t msg_id)
{
    if (async) embediq_obs_emit_from_isr(evt, src, dst, prio, msg_id);
    else       embediq_obs_emit(evt, src, dst, prio, msg_id);
}

static void count_drop(void)
{
#ifdef EMBEDIQ_PLATFORM_HOST
    (void)embediq_osal_atomic32_add(&g_drop_count, 1u);
#endif
}

static int32_t route_message_ex(EmbedIQ_Msg_t *msg, bool async)
{
    uint16_t msg_id = msg->msg_id;
    uint8_t  prio   = msg->priority;
    int32_t  result = EMBEDIQ_OK;

    /* Clamp invalid priority to NORMAL. */
    if (prio > (uint8_t)EMBEDIQ_MSG_PRIORITY_LOW) {
        prio = (uint8_t)EMBEDIQ_MSG_PRIORITY_NORMAL;
    }

    /* Namespace warning: 0x0000–0x03FF is core-system reserved. */
#ifdef EMBEDIQ_PLATFORM_HOST
    if (!async && msg_id <= 0x03FFu) {
        printf("[BUS] warn: msg_id 0x%04x is in core-reserved range "
               "(0x0000-0x03FF); publishing anyway per v1 policy\n", msg_id);
    }
#endif

    /* MSG_TX tap — level-3 (trace) event, once per routed message (T2-6). */
    if (embediq_obs_level_at_least(3u)) {
        bus_obs_emit(async, EMBEDIQ_OBS_EVT_MSG_TX, msg->source_endpoint_id,
                     0xFFu, prio, msg_id);
    }

    /* Find first subscriber entry via binary search. */
    int first = sub_first(msg_id);
    if (first < 0) return EMBEDIQ_OK;   /* no subscribers — silent drop, no event */

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
            if (!async) {
                /* ---- HIGH: block until space is available ---- */
                embediq_osal_queue_send(q, &copy, UINT32_MAX);
                BUS_QUEUE_DEPTH_CHECK(false, q, g_ep[ep].cap[prio],
                                      msg->source_endpoint_id, ep, prio, msg_id);
                if (g_notify_fn) g_notify_fn(ep);
            } else if (embediq_osal_queue_send(q, &copy, 0u) == EMBEDIQ_OK) {
                /* ---- HIGH, async: never wait for capacity ---- */
                BUS_QUEUE_DEPTH_CHECK(true, q, g_ep[ep].cap[prio],
                                      msg->source_endpoint_id, ep, prio, msg_id);
                if (g_notify_fn) g_notify_fn(ep);
            } else {
                bus_obs_emit(true, EMBEDIQ_OBS_EVT_QUEUE_DROP,
                             msg->source_endpoint_id, ep, prio, msg_id);
                count_drop();
                result = EMBEDIQ_ERR;
            }

        } else if (prio == (uint8_t)EMBEDIQ_MSG_PRIORITY_NORMAL) {
            /* ---- NORMAL: drop OLDEST on overflow — one atomic queue op ---- */
            bool dropped = false;
            (void)embediq_osal_queue_send_replace_oldest(q, &copy, &dropped);  /* q != NULL: cannot fail */
            if (dropped) {
                bus_obs_emit(async, EMBEDIQ_OBS_EVT_QUEUE_DROP,
                             msg->source_endpoint_id, ep, prio, msg_id);
                count_drop();
            }
            BUS_QUEUE_DEPTH_CHECK(async, q, g_ep[ep].cap[prio],
                                  msg->source_endpoint_id, ep, prio, msg_id);
            if (g_notify_fn) g_notify_fn(ep);

        } else {
            /* ---- LOW: drop INCOMING on overflow ---- */
            if (embediq_osal_queue_send(q, &copy, 0u) == EMBEDIQ_OK) {
                BUS_QUEUE_DEPTH_CHECK(async, q, g_ep[ep].cap[prio],
                                      msg->source_endpoint_id, ep, prio, msg_id);
                if (g_notify_fn) g_notify_fn(ep);
            } else {
                bus_obs_emit(async, EMBEDIQ_OBS_EVT_QUEUE_DROP,
                             msg->source_endpoint_id, ep, prio, msg_id);
                count_drop();
            }
        }
    }
    return result;
}

static void route_message(EmbedIQ_Msg_t *msg)
{
    (void)route_message_ex(msg, false);   /* blocking path returns OK by construction */
}

/* ---------------------------------------------------------------------------
 * message_bus_post_ep() — package-internal (fb_engine.c)
 *
 * Enqueue msg directly onto one endpoint's priority queue, bypassing the
 * subscription table, then wake its dispatch thread. Used by the engine to
 * deliver MSG_FB_FAULT to the faulted FB only. Blocking send (UINT32_MAX),
 * as on the HIGH publish path.
 * Returns EMBEDIQ_OK, or EMBEDIQ_ERR_INVALID if ep/prio/queue is unusable.
 * ------------------------------------------------------------------------- */

embediq_err_t message_bus_post_ep(uint8_t ep, uint8_t prio, const EmbedIQ_Msg_t *msg)
{
    if (!msg || ep >= g_ep_count || prio > (uint8_t)EMBEDIQ_MSG_PRIORITY_LOW) {
        return EMBEDIQ_ERR_INVALID;
    }
    EmbedIQ_Queue_t *q = g_ep[ep].q[prio];
    if (!q) return EMBEDIQ_ERR_INVALID;

    EmbedIQ_Msg_t copy = *msg;
    embediq_err_t rc = embediq_osal_queue_send(q, &copy, UINT32_MAX);
    if (rc != EMBEDIQ_OK) return rc;
    if (g_notify_fn) g_notify_fn(ep);
    return EMBEDIQ_OK;
}

void embediq_publish(EmbedIQ_FB_Handle_t fb, EmbedIQ_Msg_t *msg)
{
    if (!msg) return;
    msg->source_endpoint_id = embediq_engine_fb_ep(fb);
    route_message(msg);
}

/* ---------------------------------------------------------------------------
 * embediq_bus_token_for() / embediq_bus_post_async() — T2-1
 * ------------------------------------------------------------------------- */

embediq_bus_token_t *embediq_bus_token_for(EmbedIQ_FB_Handle_t fb)
{
    uint8_t ep = embediq_engine_fb_ep(fb);
    if (ep >= EMBEDIQ_MAX_ENDPOINTS) return NULL;   /* NULL handle → 0xFF */
    return &g_tokens[ep];   /* read-only lookup: filled by message_bus_boot() */
}

int32_t embediq_bus_post_async(embediq_bus_token_t *token, const EmbedIQ_Msg_t *msg)
{
    if (!token || !msg) return EMBEDIQ_ERR;
    EmbedIQ_Msg_t copy = *msg;
    copy.source_endpoint_id = token->source_ep;
    return route_message_ex(&copy, true);
}

/* ---------------------------------------------------------------------------
 * embediq_bus_bridge_route() — Item 5
 *
 * Stamps a caller-supplied virtual endpoint id (0x40-0x7F per Item 5 Topic 3D)
 * as source_endpoint_id and routes the message through the same private
 * pipeline as embediq_publish(). Used by embediq_ext_fb.c to inject
 * External-FB-originated messages onto the bus without occupying a real
 * endpoint slot in g_ep[]/g_registry[].
 *
 * NOT inside any #ifdef EMBEDIQ_PLATFORM_HOST guard — this is production code
 * that must be available on RTOS builds for queue transport as well. The
 * function does not include any bridge header (I-09): it relies only on the
 * already-static route_message() helper in this same translation unit.
 * ------------------------------------------------------------------------- */
embediq_err_t embediq_bus_bridge_route(uint8_t virtual_ep_id,
                                       EmbedIQ_Msg_t *msg)
{
    if (msg == NULL) {
        return EMBEDIQ_ERR_INVALID;
    }
    msg->source_endpoint_id = virtual_ep_id;
    route_message(msg);
    return EMBEDIQ_OK;
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

#include "embediq_test.h"   /* for EMBEDIQ_TEST_SOURCE_ID */

/*
 * bus_inject — test-only message injection with sentinel source ID.
 * See embediq_test.h for API contract.
 */
void bus_inject(EmbedIQ_Msg_t *msg)
{
    msg->source_endpoint_id = EMBEDIQ_TEST_SOURCE_ID;
    route_message(msg);
}

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
    embediq_osal_atomic32_store(&g_drop_count, 0u);
    tokens_fill();
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
    return embediq_osal_atomic32_load(&g_drop_count);
}

#endif /* EMBEDIQ_PLATFORM_HOST */
