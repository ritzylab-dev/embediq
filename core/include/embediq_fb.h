/*
 * embediq_fb.h — Functional Block contract
 *
 * Core Layer contract for registering and operating Functional Blocks (FBs).
 * Declarations only — zero implementation in this file.
 *
 * I-01: Compiles standalone with zero OSAL or BSP dependencies.
 * R-03: C11. Fixed-width types from <stdint.h> only.
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef EMBEDIQ_FB_H
#define EMBEDIQ_FB_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
 * Opaque FB handle — implementation lives in core/src/registry/
 * ------------------------------------------------------------------------- */

/** Opaque Functional Block type. Callers hold pointers only. */
typedef struct EmbedIQ_FB_s EmbedIQ_FB_t;

/** Handle used by all public FB API calls. */
typedef void *EmbedIQ_FB_Handle_t;

/* ---------------------------------------------------------------------------
 * Lifecycle states
 * ------------------------------------------------------------------------- */

typedef enum {
    EMBEDIQ_FB_STATE_IDLE         = 0,
    EMBEDIQ_FB_STATE_INITIALISING,
    EMBEDIQ_FB_STATE_RUNNING,
    EMBEDIQ_FB_STATE_FAULT,
    EMBEDIQ_FB_STATE_STOPPING,
    EMBEDIQ_FB_STATE_STOPPED
} EmbedIQ_FB_State_t;

/* ---------------------------------------------------------------------------
 * Boot phases (AGENTS.md §2C)
 *
 *  Phase 1: Hardware peripherals (fb_uart, fb_timer, fb_gpio)
 *  Phase 2: Services consumed by application FBs (fb_nvm, fb_watchdog)
 *  Phase 3: Application / developer FBs — default if boot_phase not declared
 *  Phase 4: External FBs, Studio bridge connections
 *
 * Rule: a Phase 2 FB with depends_on pointing to a Phase 3 FB = BOOT FAULT.
 * ------------------------------------------------------------------------- */

typedef enum {
    EMBEDIQ_BOOT_PHASE_PLATFORM       = 1,
    EMBEDIQ_BOOT_PHASE_INFRASTRUCTURE = 2,
    EMBEDIQ_BOOT_PHASE_APPLICATION    = 3,
    EMBEDIQ_BOOT_PHASE_BRIDGE         = 4
} EmbedIQ_BootPhase_t;

/* ---------------------------------------------------------------------------
 * Engine fault codes — state_or_flag of an EMBEDIQ_OBS_EVT_FAULT event the
 * engine emits itself. Kept above the EmbedIQ_FB_State_t values (0–5), which
 * the existing boot FAULT events carry in the same field.
 * ------------------------------------------------------------------------- */

/** Boot failed: a required per-FB resource (the FB execution lock) could not
 *  be allocated. source_fb_id = 0xFF, target_fb_id = the FB's endpoint. */
#define EMBEDIQ_FAULT_BOOT_RESOURCE     0x80u

/** Fault reports on one FB exceeded EMBEDIQ_FB_FAULT_INBOX_DEPTH pending
 *  reasons; msg_id = number of reports counted and not executed, saturating
 *  at 65535. One event per drain pass that observed dropped reports. */
#define EMBEDIQ_FAULT_PENDING_OVERFLOW  0x81u

/** Boot failed with fault reports latched on this FB; they are not executed
 *  (no FB code runs after a failed boot). source_fb_id = 0xFF,
 *  target_fb_id = the FB's endpoint, msg_id = number of reports discarded,
 *  saturating at 65535. */
#define EMBEDIQ_FAULT_BOOT_ABORTED      0x82u

/* ---------------------------------------------------------------------------
 * Subscription / publication array macros
 *
 * ALWAYS use these macros. Never use compound literals.
 * Compound literals have automatic storage duration; the pointer becomes
 * invalid after fb_init() returns, causing silent memory corruption.
 * (CODING_RULES.md R-sub-01)
 * ------------------------------------------------------------------------- */

/** Declare a static const subscription array for use in EmbedIQ_FB_Config_t. */
#define EMBEDIQ_SUBS(name, ...) \
    static const uint16_t name[] = { __VA_ARGS__ }

/** Declare a static const publication array for use in EmbedIQ_FB_Config_t. */
#define EMBEDIQ_PUBS(name, ...) \
    static const uint16_t name[] = { __VA_ARGS__ }

/* ---------------------------------------------------------------------------
 * FB callback types
 * ------------------------------------------------------------------------- */

/** Called once by the framework after all sub-function registrations complete. */
typedef void (*embediq_fb_init_fn)(EmbedIQ_FB_Handle_t fb, void *fb_data);

/** Called on the FB's dedicated thread; drives message dispatch. */
typedef void (*embediq_fb_run_fn)(EmbedIQ_FB_Handle_t fb, void *fb_data);

/** Called once during ordered shutdown before the FB thread is stopped. */
typedef void (*embediq_fb_exit_fn)(EmbedIQ_FB_Handle_t fb, void *fb_data);

/**
 * Called when the FB enters FAULT state. reason is a caller-defined code.
 * Executes only by the FB's owner: the boot thread after init_fn has
 * returned, the dispatch thread when one runs, otherwise the reporting
 * thread (or the thread that last ran this FB's code); always under the FB's
 * execution lock (see "Execution ownership" below); never concurrently with
 * init_fn, run_fn or itself; never before init_fn has returned. Reasons
 * execute in report order, including reports the FB makes on itself from
 * inside its own fault_fn, init_fn or run_fn. Runs on the executing thread
 * (dispatch thread, boot thread, or the reporter); a blocking call inside it
 * blocks that thread. Publish from fault_fn with embediq_bus_post_async()
 * (R-sub-33).
 */
typedef void (*embediq_fb_fault_fn)(EmbedIQ_FB_Handle_t fb, void *fb_data,
                                    uint32_t reason);

/* ---------------------------------------------------------------------------
 * FB configuration — passed to embediq_fb_register()
 *
 * All pointer fields that are not used should be set to NULL.
 * boot_phase defaults to APPLICATION if not explicitly set.
 * ------------------------------------------------------------------------- */

typedef struct {
    const char             *name;               /**< Unique FB name (NUL-terminated). */
    uint8_t                 priority;            /**< OS thread priority. */
    uint16_t                stack_size;          /**< Thread stack size in bytes. */
    embediq_fb_init_fn      init_fn;             /**< Optional. NULL = no init. */
    embediq_fb_run_fn       run_fn;              /**< Optional. NULL = message-driven only. */
    embediq_fb_exit_fn      exit_fn;             /**< Optional. NULL = no cleanup. */
    embediq_fb_fault_fn     fault_fn;            /**< Optional. NULL = silent fault. */
    void                   *fb_data;             /**< Caller-owned context pointer. */
    const uint16_t         *subscriptions;       /**< MSG_IDs this FB receives. */
    uint8_t                 subscription_count;  /**< Length of subscriptions[]. */
    const uint16_t         *publications;        /**< MSG_IDs this FB may publish. */
    uint8_t                 publication_count;   /**< Length of publications[]. */
    const char * const     *depends_on;          /**< Names of FBs that must start first. */
    uint8_t                 depends_count;        /**< Length of depends_on[]. */
    EmbedIQ_BootPhase_t     boot_phase;          /**< Boot ordering phase. */
} EmbedIQ_FB_Config_t;

/* ---------------------------------------------------------------------------
 * Public API — implemented in core/src/registry/
 * ------------------------------------------------------------------------- */

/** Register an FB with the framework. Returns a handle, or NULL on error. */
EmbedIQ_FB_Handle_t embediq_fb_register(const EmbedIQ_FB_Config_t *config);

/**
 * Transition the FB to FAULT state with a diagnostic reason code.
 *
 * The reason is accepted into the FB's framework-owned fault inbox; the FB's
 * owner then executes it — state change to FAULT, Observatory LIFECYCLE
 * event, fault_fn — under the FB's execution lock.
 *
 * Calling context: any thread except an ISR, including from inside another
 * FB's handlers. The report is accepted in constant time and never waits for
 * queue capacity. Execution: by the dispatch thread when one runs; by the
 * boot thread after init_fn when reported before or during init; otherwise
 * on the calling thread — which waits for the FB's current handler to finish
 * if it holds no execution lock, and does not wait at all (the holder
 * executes on exit) if it does. The FB's fault_fn then runs on the calling
 * thread; a fault_fn that blocks on the bus blocks that thread. Order among
 * reporters is acceptance order per FB; beyond EMBEDIQ_FB_FAULT_INBOX_DEPTH
 * pending reasons the report is counted, and one
 * EMBEDIQ_FAULT_PENDING_OVERFLOW event is emitted per drain pass that
 * observed dropped reports; the FB enters FAULT when its first pending
 * reason executes.
 */
void embediq_fb_report_fault(EmbedIQ_FB_Handle_t fb, uint32_t reason);

/**
 * Return the current lifecycle state of an FB (atomic read).
 * Calling context: any thread.
 */
EmbedIQ_FB_State_t embediq_fb_get_state(EmbedIQ_FB_Handle_t fb);

/* ---------------------------------------------------------------------------
 * Engine registry API — read-only views of the static FB registry.
 * Replaces the former undeclared fb_engine__* accessors (T2-7).
 * ------------------------------------------------------------------------- */

/**
 * Number of registered FBs.
 * Calling context: any thread; read-only after embediq_engine_boot().
 */
uint8_t embediq_engine_fb_count(void);

/**
 * Config of the FB at registry index idx, or NULL if idx is out of range.
 * Calling context: any thread; read-only after embediq_engine_boot().
 */
const EmbedIQ_FB_Config_t *embediq_engine_fb_config(uint8_t idx);

/**
 * Endpoint index of an FB handle, or 0xFF for a NULL handle.
 * Calling context: any thread; read-only after embediq_engine_boot().
 */
uint8_t embediq_engine_fb_ep(EmbedIQ_FB_Handle_t handle);

/* embediq_bus_resolve_name() is declared in embediq_bus.h */

/* ---------------------------------------------------------------------------
 * Execution ownership
 *
 * Every FB has one framework-owned execution lock, held by whichever thread
 * runs that FB's code; the FB author never sees it:
 *   init      — embediq_engine_boot() holds it around IDLE → INITIALISING,
 *               init_fn, execution of the faults latched until then, and the
 *               conditional INITIALISING → RUNNING. A fault accepted before
 *               or during init_fn is executed before the FB is marked
 *               RUNNING. A report accepted in the closing window — after
 *               that drain and before the boot thread releases the lock —
 *               is executed immediately after; the trace may then show
 *               RUNNING followed by FAULT for that FB. The state never
 *               returns to RUNNING.
 *   pre-dispatch — a fault reported before embediq_engine_dispatch_boot()
 *               (or for an FB without subscriptions) is executed on the
 *               reporting thread under the lock.
 *   dispatch  — the dispatch thread holds it for one message at a time, and
 *               to execute latched faults, never while blocked on its
 *               semaphore; the first dispatch therefore waits for any
 *               in-flight fault_fn on another thread.
 *   shutdown  — embediq_engine_dispatch_shutdown() stops the dispatch thread
 *               between messages; latched faults are executed under the
 *               lock. exit_fn is not invoked by the engine in v1.
 * A thread that already holds one FB's execution lock never waits for
 * another's: it tries it, and if it is busy leaves the fault latched for the
 * holder, which executes it on release.
 * Cost: one uncontended mutex lock/unlock per dispatched message (tens of ns
 * on POSIX). Boot fails if a per-FB execution lock cannot be allocated.
 * ------------------------------------------------------------------------- */

/**
 * Boot the framework: sort all registered FBs by phase (1→2→3→4), resolve
 * depends_on order within each phase via topological sort, then call each
 * FB's init_fn in the computed order.  Also boots the message bus so that
 * queues exist before any Phase-1 FB tasks start publishing.
 *
 * Must be called exactly once, after all embediq_fb_register() calls are done.
 * Returns 0 on success. Returns -1 and emits an Observatory FAULT event if a
 * dependency cycle or missing dependency is detected, or if a per-FB
 * execution lock cannot be allocated (EMBEDIQ_FAULT_BOOT_RESOURCE; no init_fn
 * has run). On failure, latched fault reports are not executed; one
 * EMBEDIQ_FAULT_BOOT_ABORTED event per FB carries the count. A fault_fn
 * already executing when boot fails runs to completion. Reports accepted
 * after the BOOT_ABORTED event are latched; they are counted at the next
 * boot failure, executed after init on a retried boot, or discarded by
 * reset.
 * Calling context: boot thread only.
 */
int embediq_engine_boot(void);

/**
 * Create one per-FB dispatch pthread for every FB that has subscriptions.
 * Each thread reads from the FB's priority queues (HIGH > NORMAL > LOW) and
 * calls the matching sub-function dispatch loop.
 *
 * Must be called after a successful embediq_engine_boot(); does nothing if
 * boot failed or has not run. If a dispatch thread cannot be created, that
 * FB's faults keep being executed by their reporters.
 * Idempotent: safe to call multiple times, only first call has effect.
 *
 * Note: The integration test harness does NOT call this function; it drives
 * FBs directly via fb_engine__deliver_msg() instead.
 */
void embediq_engine_dispatch_boot(void);

/**
 * Signal all dispatch threads to exit and join them.
 *
 * Sets the shutdown flag, posts each FB's dispatch semaphore once to unblock
 * any waiting thread, then joins every dispatch thread; each stops within one
 * message. Executes every latched fault before returning; discards queued
 * messages and reports the count as one QUEUE_DROP event per FB (state
 * EMBEDIQ_QUEUE_DROP_AT_SHUTDOWN). Never waits for concurrent fault
 * reporters. Afterwards faults are executed on the reporting thread until
 * dispatch_boot is called again.
 *
 * Safe to call multiple times (idempotent).
 * Call before OTA restart or clean process exit.
 *
 * Returns 0 on success (including when no threads were running), -1 on error.
 * Calling context: the thread that called embediq_engine_dispatch_boot().
 */
int embediq_engine_dispatch_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* EMBEDIQ_FB_H */
