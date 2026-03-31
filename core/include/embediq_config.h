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
 * Dispatch thread defaults
 * ------------------------------------------------------------------------- */

/** Default stack size in bytes for per-FB dispatch threads. */
#define EMBEDIQ_DISPATCH_STACK_SIZE       4096u

/** Default OS thread priority for dispatch threads when FB config.priority is 0. */
#define EMBEDIQ_DISPATCH_DEFAULT_PRIORITY 2u

/* ---------------------------------------------------------------------------
 * Watchdog driver task
 * ------------------------------------------------------------------------- */

/** Stack size in bytes for the watchdog monitor task. */
#define EMBEDIQ_WDG_TASK_STACK_SIZE       4096u

/** OS thread priority for the watchdog monitor task.
 *  Lower than dispatch threads (EMBEDIQ_DISPATCH_DEFAULT_PRIORITY = 2) so
 *  watchdog scanning yields to normal FB work. */
#define EMBEDIQ_WDG_TASK_PRIORITY         1u

/* ---------------------------------------------------------------------------
 * NVM driver sizing
 *
 * Kept independent of EMBEDIQ_MSG_MAX_PAYLOAD and EMBEDIQ_MAX_ENDPOINTS so
 * increasing message payload size or endpoint count does not silently double
 * the NVM flash image and RAM cache.
 * ------------------------------------------------------------------------- */

/** Maximum byte length of an NVM key string (including NUL terminator). */
#define EMBEDIQ_NVM_KEY_SIZE              64u

/** Maximum byte length of an NVM value (raw bytes). */
#define EMBEDIQ_NVM_VAL_SIZE              64u

/** Maximum number of NVM key-value entries in the in-memory cache. */
#define EMBEDIQ_NVM_MAX_KEYS              64u

/* ---------------------------------------------------------------------------
 * Observatory verbosity level
 *
 *  Runtime verbosity gate — see EMBEDIQ_TRACE_LEVEL for the compile-time gate.
 *  0 = silent, 1 = standard, 2 = verbose — applied to compiled-in events.
 * ------------------------------------------------------------------------- */

#ifndef EMBEDIQ_OBS_LEVEL
#   ifdef EMBEDIQ_PLATFORM_HOST
#       define EMBEDIQ_OBS_LEVEL        1
#   else
#       define EMBEDIQ_OBS_LEVEL        0
#   endif
#endif

/* ---------------------------------------------------------------------------
 * Compile-time trace level
 *
 *  Controls which event families are compiled into the binary at all.
 *  This is a COMPILE-TIME gate — separate from EMBEDIQ_OBS_LEVEL which
 *  is the RUNTIME filter applied to what is already compiled in.
 *
 *  0 = Minimal    — FAULT + SYSTEM families only  (production MCU default)
 *  1 = Standard   — + MESSAGE + STATE families    (host/dev default)
 *  2 = Diagnostic — + RESOURCE + TIMING families  (profiling builds)
 *  3 = Deep Trace — + FUNCTION family             (deep debug only)
 * ------------------------------------------------------------------------- */

#ifndef EMBEDIQ_TRACE_LEVEL
#   ifdef EMBEDIQ_PLATFORM_HOST
#       define EMBEDIQ_TRACE_LEVEL      1
#   else
#       define EMBEDIQ_TRACE_LEVEL      0
#   endif
#endif

/* Per-family compile-time flags — derived from EMBEDIQ_TRACE_LEVEL unless
 * explicitly overridden before including this header.
 * FAULT and SYSTEM families are unconditional (always compiled in). */

#ifndef EMBEDIQ_TRACE_MESSAGE
#   define EMBEDIQ_TRACE_MESSAGE    ((EMBEDIQ_TRACE_LEVEL) >= 1)
#endif

#ifndef EMBEDIQ_TRACE_STATE
#   define EMBEDIQ_TRACE_STATE      ((EMBEDIQ_TRACE_LEVEL) >= 1)
#endif

#ifndef EMBEDIQ_TRACE_RESOURCE
#   define EMBEDIQ_TRACE_RESOURCE   ((EMBEDIQ_TRACE_LEVEL) >= 2)
#endif

#ifndef EMBEDIQ_TRACE_TIMING
#   define EMBEDIQ_TRACE_TIMING     ((EMBEDIQ_TRACE_LEVEL) >= 2)
#endif

#ifndef EMBEDIQ_TRACE_FUNCTION
#   define EMBEDIQ_TRACE_FUNCTION   ((EMBEDIQ_TRACE_LEVEL) >= 3)
#endif

/* ---------------------------------------------------------------------------
 * FB Metadata
 * ------------------------------------------------------------------------- */

/** Maximum byte length of a Functional Block safety class string,
 *  including the NUL terminator.
 *  Format: 'STD:LEVEL' — e.g. 'ISO26262:ASIL-B', 'IEC61508:SIL-2', 'NONE'.
 *  16 bytes covers all standard encodings with room for NUL. */
#define EMBEDIQ_FB_SAFETY_CLASS_LEN     16u

#ifdef __cplusplus
}
#endif

#endif /* EMBEDIQ_CONFIG_H */
