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
 * ISR-safe and callback-safe bus post.
 *
 * The ONLY function permitted inside an async ops table callback.
 * On FreeRTOS: maps to xQueueSendFromISR().
 * On POSIX host: maps to embediq_publish() with mutex guard.
 *
 * token: must be the token received in the callback — not a stored copy.
 * msg:   message to post; copied by value.
 * Returns EMBEDIQ_OK on success, EMBEDIQ_ERR if queue is full.
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
