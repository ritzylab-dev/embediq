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

/** Called when the FB enters FAULT state. reason is a caller-defined code. */
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

/** Transition the FB to FAULT state with a diagnostic reason code. */
void embediq_fb_report_fault(EmbedIQ_FB_Handle_t fb, uint32_t reason);

/** Return the current lifecycle state of an FB. */
EmbedIQ_FB_State_t embediq_fb_get_state(EmbedIQ_FB_Handle_t fb);

/* embediq_bus_resolve_name() is declared in embediq_bus.h */

/**
 * Boot the framework: sort all registered FBs by phase (1→2→3→4), resolve
 * depends_on order within each phase via topological sort, then call each
 * FB's init_fn in the computed order.  Also boots the message bus so that
 * queues exist before any Phase-1 FB tasks start publishing.
 *
 * Must be called exactly once, after all embediq_fb_register() calls are done.
 * Returns 0 on success. Returns -1 and emits an Observatory FAULT event if a
 * dependency cycle or missing dependency is detected.
 */
int embediq_engine_boot(void);

/**
 * Create one per-FB dispatch pthread for every FB that has subscriptions.
 * Each thread reads from the FB's priority queues (HIGH > NORMAL > LOW) and
 * calls the matching sub-function dispatch loop.
 *
 * Must be called after embediq_engine_boot().
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
 * any waiting thread, then calls pthread_join() on every dispatch thread.
 * Returns only after all threads have exited — no polling, no busy-wait.
 *
 * Safe to call multiple times (idempotent).
 * Call before OTA restart or clean process exit.
 *
 * Returns 0 on success (including when no threads were running), -1 on error.
 */
int embediq_engine_dispatch_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* EMBEDIQ_FB_H */
