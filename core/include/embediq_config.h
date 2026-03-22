/*
 * embediq_config.h — EmbedIQ Sizing Parameters
 *
 * Single source of truth for every framework size constant.
 * No numeric literals for these values may appear anywhere else in the
 * codebase; tools/validator.py enforces this at build time (Invariant I-08).
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef EMBEDIQ_CONFIG_H
#define EMBEDIQ_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
 * Endpoint routing
 * ------------------------------------------------------------------------- */

/** Maximum number of registered message endpoints across all Functional Blocks. */
#define EMBEDIQ_MAX_ENDPOINTS           64

/* ---------------------------------------------------------------------------
 * Message payload
 * ------------------------------------------------------------------------- */

/** Maximum byte size of a fixed-length message payload.
 *  v1 constraint: payloads are copy-by-value; no variable-length or zero-copy. */
#define EMBEDIQ_MSG_MAX_PAYLOAD         64

/* ---------------------------------------------------------------------------
 * Per-FB priority queues  (HIGH / NORMAL / LOW — three queues per FB)
 *
 *  HIGH   full → sender blocks (intentional; HIGH publishers must be rare).
 *  NORMAL full → oldest message dropped; observable event emitted.
 *  LOW    full → new message dropped; observable event emitted.
 * ------------------------------------------------------------------------- */

/** Depth of the HIGH-priority queue for each Functional Block. */
#define EMBEDIQ_HIGH_QUEUE_DEPTH        16

/** Depth of the NORMAL-priority queue for each Functional Block. */
#define EMBEDIQ_NORMAL_QUEUE_DEPTH      32

/** Depth of the LOW-priority queue for each Functional Block. */
#define EMBEDIQ_LOW_QUEUE_DEPTH         16

/* ---------------------------------------------------------------------------
 * Sub-function dispatch
 * ------------------------------------------------------------------------- */

/** Maximum number of sub-functions that can be registered per Functional Block. */
#define EMBEDIQ_MAX_SUBFNS_PER_FB       16

/* ---------------------------------------------------------------------------
 * Observatory ring buffer
 * ------------------------------------------------------------------------- */

/** Depth (number of events) of the Observatory ring buffer.
 *  Must be a power of 2 for efficient modulo masking. */
#define EMBEDIQ_OBS_RING_DEPTH          256

/* ---------------------------------------------------------------------------
 * Message bus subscription table
 * ------------------------------------------------------------------------- */

/** Maximum total subscription entries across all registered Functional Blocks.
 *  Limits the flat (msg_id, ep_id) table built at message_bus_boot().
 *  512 supports 64 FBs × ~8 subscriptions each — sufficient for v1. */
#define EMBEDIQ_MAX_SUBSCRIPTIONS       512

/* ---------------------------------------------------------------------------
 * Boot dependency resolution
 * ------------------------------------------------------------------------- */

/** Maximum number of boot-phase depends_on entries per Functional Block. */
#define EMBEDIQ_MAX_BOOT_DEPS           8

/* ---------------------------------------------------------------------------
 * Observatory verbosity level
 *
 *  0 = silent  — MCU default; minimal runtime overhead.
 *  1 = normal  — host/Linux default; human-readable event stream.
 *  2 = verbose — debug builds only.
 * ------------------------------------------------------------------------- */

#ifndef EMBEDIQ_OBS_LEVEL
#   ifdef EMBEDIQ_PLATFORM_HOST
#       define EMBEDIQ_OBS_LEVEL        1
#   else
#       define EMBEDIQ_OBS_LEVEL        0
#   endif
#endif

#ifdef __cplusplus
}
#endif

#endif /* EMBEDIQ_CONFIG_H */
