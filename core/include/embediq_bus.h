/*
 * embediq_bus.h — Message bus contract
 *
 * Public API for publishing messages and resolving endpoint identities.
 * Declarations only — zero implementation in this file.
 *
 * I-09: This header does not include bridge headers; native bus compiles
 *       without bridge code when EMBEDIQ_BRIDGE is not defined.
 * R-03: C11.
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef EMBEDIQ_BUS_H
#define EMBEDIQ_BUS_H

#include "embediq_fb.h"
#include "embediq_msg.h"

#include <stdbool.h>

/* ---------------------------------------------------------------------------
 * Async bus post token
 *
 * An opaque token passed to async ops table callbacks. The token is the
 * ONLY argument permitted inside a callback — it physically prevents
 * callbacks from accessing FB state or calling OSAL primitives.
 *
 * Lifecycle: valid only for the duration of the callback invocation.
 * The only permitted operation on a token: pass it to embediq_bus_post_async().
 * ------------------------------------------------------------------------- */
typedef struct embediq_bus_token embediq_bus_token_t;

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
 * Publish API
 *
 * embediq_publish() is the ONLY way for an FB to send a message to another FB.
 * Direct cross-FB function calls are forbidden (R-01, R-sub-02).
 *
 * The bus copies msg by value into the recipient's priority queue
 * (v1 constraint: copy-by-value at every queue send).
 *
 * Queue overflow behaviour (AGENTS.md §2B):
 *   HIGH   full → caller blocks until space is available
 *   NORMAL full → oldest message dropped; observable event emitted
 *   LOW    full → new message dropped; observable event emitted
 * ------------------------------------------------------------------------- */

/**
 * Callback type invoked by the bus after a successful message enqueue.
 * ep_id is the destination endpoint that received the message.
 * Registered by the engine via message_bus_set_notify_fn().
 */
typedef void (*embediq_notify_fn_t)(uint8_t ep_id);

/** Register the engine notify callback. Called once inside embediq_engine_boot(). */
void message_bus_set_notify_fn(embediq_notify_fn_t fn);

/**
 * Build the subscription routing table and create per-FB priority queues.
 * Called automatically from embediq_engine_boot(). Do not call manually.
 * Safe to call again from tests (idempotent).
 */
void message_bus_boot(void);

/** Publish a message from fb onto the bus. msg is copied by value. */
void embediq_publish(EmbedIQ_FB_Handle_t fb, EmbedIQ_Msg_t *msg);

/* ---------------------------------------------------------------------------
 * Reserved endpoint sentinels (source_endpoint_id / target_endpoint_id)
 *
 *   0xFF  broadcast / N-A   — target "all subscribers"; "name not found"
 *   0xFE  test harness      — EMBEDIQ_TEST_SOURCE_ID (embediq_test.h)
 *   0xFD  framework         — EMBEDIQ_EP_FRAMEWORK: messages the engine
 *                             itself posts (e.g. MSG_FB_FAULT delivered to an
 *                             FB's own HIGH queue by embediq_fb_report_fault)
 * ------------------------------------------------------------------------- */
#define EMBEDIQ_EP_FRAMEWORK  ((uint8_t)0xFDu)

/* ---------------------------------------------------------------------------
 * Endpoint resolution
 *
 * These are primarily used by the test harness and Observatory.
 * Production FBs communicate by message ID — never by endpoint index directly.
 * ------------------------------------------------------------------------- */

/** Resolve an FB name string to its registered endpoint index.
 *  Returns 0xFF if the name is not found. */
uint8_t embediq_bus_resolve_name(const char *fb_name);

/** Resolve an endpoint index to the registered FB name string.
 *  Returns NULL if the endpoint index is not registered. */
const char *embediq_bus_resolve_id(uint8_t endpoint_id);

/**
 * Non-blocking receive from one priority queue of an endpoint.
 * priority: 0=HIGH, 1=NORMAL, 2=LOW.
 * Returns true and fills *out if a message was available; false if empty.
 * Used by the per-FB dispatch loop created by embediq_engine_dispatch_boot().
 */
bool message_bus_recv_ep(uint8_t ep_id, uint8_t priority, EmbedIQ_Msg_t *out);

/* ---------------------------------------------------------------------------
 * Async bus post — ISR-safe and callback-safe
 * ------------------------------------------------------------------------- */

/**
 * Return the async-post token for an FB. The token names the FB as the source
 * of messages posted with embediq_bus_post_async(). Tokens live in a static
 * per-endpoint table (no allocation): the same FB always gets the same
 * pointer, valid for the life of the process. Returns NULL for a NULL or
 * unregistered handle.
 * Calling context: boot thread or the FB's dispatch thread.
 */
embediq_bus_token_t *embediq_bus_token_for(EmbedIQ_FB_Handle_t fb);

/**
 * Callback-safe, never-blocking bus post — the bus entry point for HAL and
 * ops-table callbacks (timer ticks, stream HALs).
 *
 * The message is copied, its source_endpoint_id is taken from the token, and
 * it is routed to every subscriber with NON-BLOCKING delivery on every
 * priority:
 *   HIGH   full → not delivered to that subscriber; QUEUE_DROP observed;
 *                 the call returns EMBEDIQ_ERR (it never blocks)
 *   NORMAL full → oldest discarded, new enqueued (drop-oldest policy)
 *   LOW    full → new message discarded (drop-incoming policy)
 * Observatory events use embediq_obs_emit_from_isr() (lock-free ISR ring).
 * Dispatch threads are woken with an any-thread semaphore post.
 *
 * token: from embediq_bus_token_for().
 * msg:   message to post; copied by value.
 * Returns EMBEDIQ_OK unless a HIGH delivery failed (EMBEDIQ_ERR) or token/msg
 * is NULL (EMBEDIQ_ERR). NORMAL/LOW overflow follows the bus policy and is
 * counted and observed, not reported here.
 * Calling context: any thread or ISR. On POSIX the queues are mutex-backed;
 * on an MCU the OSAL maps the non-blocking sends to its from-ISR primitives.
 */
int32_t embediq_bus_post_async(embediq_bus_token_t *token, const EmbedIQ_Msg_t *msg);

/* ---------------------------------------------------------------------------
 * Bridge routing — Item 5
 *
 * Stamps the supplied virtual endpoint id as source_endpoint_id and routes
 * the message on the bus. Used by embediq_ext_fb.c on both socket and queue
 * transport paths so that External FB-originated messages carry a valid
 * source identity (0x40-0x7F virtual range) without occupying a real
 * endpoint slot.
 *
 * NOT wrapped in EMBEDIQ_PLATFORM_HOST — available on all targets. This
 * declaration is a bare function signature; it pulls in no bridge headers,
 * preserving I-09 (native bus compiles without bridge code when
 * EMBEDIQ_BRIDGE is not defined).
 *
 * @param virtual_ep_id  Virtual endpoint id (0x40-0x7F) to stamp into msg.
 * @param msg            Message to route. Modified in place: source_endpoint_id
 *                       is overwritten. Copied by value into subscriber queues
 *                       by the underlying routing path.
 * @return EMBEDIQ_OK on success, EMBEDIQ_ERR_INVALID if msg is NULL.
 */
int32_t embediq_bus_bridge_route(uint8_t virtual_ep_id, EmbedIQ_Msg_t *msg);

#ifdef __cplusplus
}
#endif

#endif /* EMBEDIQ_BUS_H */
