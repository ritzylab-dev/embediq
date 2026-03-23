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
 * Must be called once, after embediq_engine_boot() completes.
 * Idempotent: safe to call multiple times, only first call has effect.
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

#ifdef __cplusplus
}
#endif

#endif /* EMBEDIQ_BUS_H */
