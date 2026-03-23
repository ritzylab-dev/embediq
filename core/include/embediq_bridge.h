/*
 * embediq_bridge.h — External FB bridge contract
 *
 * Allows external processes (Python scripts, Node.js agents, EmbedIQ Studio)
 * to act as Functional Blocks on the message bus. External FBs connect via
 * a unix socket or TCP endpoint and exchange EmbedIQ_Msg_t frames.
 *
 * Phase and status:
 *   Implementation target: Phase 3 (AGENTS.md §3 Shell 3 — Ecosystem).
 *   Contract frozen: Phase 2 (this header).
 *   No bridge implementation may be included in Phase 1 or Phase 2 builds
 *   unless EMBEDIQ_BRIDGE is defined (I-09 invariant).
 *
 * Design notes:
 *   - The bridge daemon runs as a separate OS process or thread.
 *   - EmbedIQ_Msg_t frames are serialised over the transport unchanged
 *     (copy-by-value, fixed size — v1 constraint).
 *   - Authentication is NOT provided in v1 (dev mode only — AGENTS.md §9).
 *   - Reliable delivery (ack/retry) is NOT provided in v1.
 *
 * I-01: Compiles standalone with zero OSAL or BSP dependencies.
 * I-09: This header does not include bridge implementation headers.
 *       Native bus compiles without bridge code when EMBEDIQ_BRIDGE is not defined.
 * R-03: C11. Fixed-width types from <stdint.h> only.
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef EMBEDIQ_BRIDGE_H
#define EMBEDIQ_BRIDGE_H

#include "embediq_osal.h"  /* embediq_err_t */
#include "embediq_msg.h"   /* EmbedIQ_Msg_t */

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
 * Bridge transport operations table
 *
 * The bridge daemon implements this table. The framework calls into it to
 * send messages outbound; the daemon calls on_receive() for inbound messages.
 * ------------------------------------------------------------------------- */

typedef struct {
    /**
     * Connect to the bridge endpoint.
     *
     * @param endpoint  NUL-terminated address string.
     *                  Unix socket: "/tmp/embediq.sock"
     *                  TCP:         "127.0.0.1:9999"
     * @return EMBEDIQ_OK if connected and ready, EMBEDIQ_ERR otherwise.
     */
    embediq_err_t (*connect)(const char *endpoint);

    /** Disconnect from the bridge endpoint and release resources. */
    embediq_err_t (*disconnect)(void);

    /**
     * Send an outbound message to an external FB.
     * Message is serialised and transmitted over the transport.
     *
     * @param msg  Pointer to the message to send. Copied before return.
     * @return EMBEDIQ_OK on success, EMBEDIQ_ERR if not connected or send fails.
     */
    embediq_err_t (*send)(const EmbedIQ_Msg_t *msg);

    /**
     * Callback invoked by the transport when a message arrives from an
     * external FB. Registered by the framework at bridge init time.
     * Do NOT call this directly.
     *
     * @param msg  Pointer to the received message. Valid only during callback.
     */
    void (*on_receive)(const EmbedIQ_Msg_t *msg);
} embediq_bridge_ops_t;

/* ---------------------------------------------------------------------------
 * Registration
 * ------------------------------------------------------------------------- */

/**
 * Register the bridge transport operations table.
 * Call once during EMBEDIQ_BOOT_PHASE_BRIDGE (Phase 4).
 * Has no effect and returns EMBEDIQ_ERR if EMBEDIQ_BRIDGE is not defined.
 *
 * @param ops  Pointer to a statically allocated ops table. Must remain valid
 *             for the lifetime of the process.
 * @return EMBEDIQ_OK on success, EMBEDIQ_ERR if ops is NULL or bridge not built.
 */
embediq_err_t embediq_bridge_register_ops(const embediq_bridge_ops_t *ops);

/* Implementation target: Phase 3. Contract frozen: Phase 2. */

#ifdef __cplusplus
}
#endif

#endif /* EMBEDIQ_BRIDGE_H */
