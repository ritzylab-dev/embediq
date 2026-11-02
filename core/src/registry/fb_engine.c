#define _POSIX_C_SOURCE 200809L

/*
 * core/src/fb_engine.c — Functional Block engine
 *
 * Implements the complete FB lifecycle against the contracts declared in:
 *   core/include/embediq_fb.h
 *   core/include/embediq_subfn.h
 *   core/include/embediq_obs.h
 *
 * Responsibilities:
 *   1. embediq_fb_register()   — static registry, up to EMBEDIQ_MAX_ENDPOINTS
 *   2. embediq_engine_boot()   — phase sort (1→4), topo sort within phase,
 *                                cycle/missing-dep detection, init_fn dispatch
 *   3. embediq_fb_report_fault() — accepts the reason into the FB's fault
 *                                inbox; the FB's owner (boot thread, dispatch
 *                                thread, or the reporter) executes it: FAULT
 *                                state + Observatory event + fault_fn
 *   4. embediq_subfn_register() — stored sorted by init_order (I-10 enforced)
 *   5. Per-FB execution lock (exec_lock) — held by whichever thread runs an
 *      FB's code: init_fn, each run_fn dispatch, each fault execution
 *   6. embediq_engine_dispatch_shutdown() — bounded; latched faults are
 *      executed, queued messages discarded and reported
 *
 * Fault delivery. No deadlock: the only blocking acquisition of an execution
 * lock is by a thread that holds none; a thread that holds one takes others
 * by trylock only; a cycle needs every member to hold a lock and wait for
 * another, so none can form. No lost fault: every inbox push, owner read and
 * owner change is inside the OSAL critical section, and each owner drains
 * after the change that made it the owner. Nothing runs before init_fn: the
 * owner stays BOOT until the boot thread, holding the lock, flips it after.
 * No stale executor: a thread acts on an owner it read earlier, but a drain
 * pass consults the owner again inside the critical section before its
 * budget and before every reason, and executes only in a role that owner
 * allows (DISPATCH: the dispatch thread; NONE: any other thread). A thread
 * whose owner read went stale — boot failed, or dispatch_boot handed the FB
 * to its dispatch thread — can still win the lock but drains nothing: it
 * leaves the inbox to the new owner, which is woken (DISPATCH) or counts or
 * executes it (BOOT).
 * Nothing is stranded: every release of an execution lock is followed by a
 * check of the inbox. A push that lands before the check is seen by it; a
 * push after the check finds the lock free and executes itself, or is
 * refused only because a new holder exists — which runs the same
 * release-then-check loop. Each extra iteration consumes a reason pushed
 * during the previous pass, so the loop ends when reports stop.
 * Liveness: no thread waits on a timer; each drain pass is bounded by the
 * count latched when it starts and executes one or finds none;
 * dispatch_shutdown drains each FB with the same release-then-check loop as
 * any executor, acquiring only by trylock; it never waits for an execution
 * lock, and like every executor it terminates when reports on that FB stop.
 * A pass is bounded, a storm is
 * not: under unbounded reports on one FB its fault_fn keeps being invoked by
 * whichever executor holds the lock, and a fault_fn that blocks holds up the
 * thread executing it (shutdown waits for that dispatch thread to join).
 * The execution-lock argument excludes lock cycles only. A blocking HIGH
 * publish from FB code is a bus-capacity wait, governed by R-sub-33 and the
 * Message System's HIGH-queue rules, not by this protocol.
 * R-02: no malloc/free. All storage is static.
 * I-10: sub-fn registration is gated by the registering flag, which is true
 *       only during the parent FB's init_fn call.
 * R-sub-ORDER: sub-fns stored in ascending init_order via insertion sort at
 *              registration time — dispatch is always deterministic.
 *
 * Host/test-only API (EMBEDIQ_PLATFORM_HOST):
 *   fb_engine__reset()           — clear registry between test cases
 *   fb_engine__obs_event_count() — inspect emitted event count
 *   fb_engine__deliver_msg()     — direct sub-fn dispatch (bypasses queue)
 *   fb_engine__set_report_delay_hook() — hold a reporter after acceptance
 *   fb_engine__fault_overflow_total()  — reports counted as inbox overflow
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "embediq_fb.h"
#include "embediq_subfn.h"
#include "embediq_obs.h"
#include "embediq_bus.h"
#include "embediq_osal.h"
#include "embediq_config.h"
#include "embediq_platform.h"
#include "embediq_msg.h"

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>

/* ---------------------------------------------------------------------------
 * Private struct definitions
 *
 * EmbedIQ_FB_s is opaque to all callers. Only fb_engine.c may dereference it.
 * ------------------------------------------------------------------------- */

struct EmbedIQ_FB_s {
    EmbedIQ_FB_Config_t    config;
    embediq_osal_atomic32_t state;        /* EmbedIQ_FB_State_t; atomic: read from any thread */
    uint8_t                ep_idx;
    uint8_t                subfn_count;
    /* Sub-fns kept sorted by init_order via insertion sort at registration. */
    EmbedIQ_SubFn_Config_t subfns[EMBEDIQ_MAX_SUBFNS_PER_FB];
    bool                   registering;   /* I-10: true only during init_fn */
    /* Execution ownership: exec_lock is held by whichever thread runs this
     * FB's code — init_fn (boot thread), each run_fn dispatch and each fault
     * execution — so no two of them ever overlap. A required boot resource:
     * created first in embediq_engine_boot(), boot fails if it cannot be.
     * Destroyed by fb_engine__reset(). */
    EmbedIQ_Mutex_t       *exec_lock;
    /* Fault inbox: reasons accepted by embediq_fb_report_fault() awaiting
     * execution by the FB's owner (fault_owner_t). Every field below is read
     * and written only inside the OSAL critical section. */
    uint32_t               inbox[EMBEDIQ_FB_FAULT_INBOX_DEPTH];
    uint8_t                inbox_head;
    uint8_t                inbox_count;
    uint32_t               inbox_dropped;   /* reports not accepted; the event clamps */
    uint8_t                fault_owner;
};

/* Who executes an FB's latched faults. */
typedef enum {
    FAULT_OWNER_BOOT     = 0,   /* until init_fn has returned: latch only   */
    FAULT_OWNER_NONE     = 1,   /* no dispatch thread: the reporter executes */
    FAULT_OWNER_DISPATCH = 2,   /* the FB's dispatch thread executes         */
} fault_owner_t;

/* The role in which a thread holding an FB's execution lock drains its
 * inbox. A drain executes only when the current owner allows the role. */
typedef enum {
    EXEC_ROLE_DISPATCH = 0,   /* the FB's dispatch thread: owner DISPATCH   */
    EXEC_ROLE_REPORTER = 1,   /* any other thread (reporter, the boot thread
                                 after its flip, shutdown): owner NONE     */
} exec_role_t;

/* ---------------------------------------------------------------------------
 * Static registry — R-02: no malloc
 * ------------------------------------------------------------------------- */

static EmbedIQ_FB_t  g_registry[EMBEDIQ_MAX_ENDPOINTS];
static uint8_t       g_reg_count  = 0;
static bool          g_booted     = false;

/* Computed by embediq_engine_boot(): global indices in init sequence order. */
static uint8_t       g_boot_order[EMBEDIQ_MAX_ENDPOINTS];
static uint8_t       g_boot_count = 0;

/* ---------------------------------------------------------------------------
 * Per-FB dispatch thread state (populated by embediq_engine_boot)
 * ------------------------------------------------------------------------- */

/* Forward declaration — defined after embediq_engine_boot(). */
void embediq_engine_notify_fb(uint8_t ep_id);

typedef struct {
    EmbedIQ_FB_t   *fb;
    uint8_t         ep_id;
    embediq_sem_t   sem;        /* posted by bus on every successful enqueue */
    EmbedIQ_Task_t *task;       /* handle stored for pthread_join on shutdown */
    uint32_t        discarded;  /* messages the thread discarded at shutdown;
                                   written by it before exit, read after join */
} dispatch_arg_t;

static dispatch_arg_t           g_dispatch_args[EMBEDIQ_MAX_ENDPOINTS];

/* Dispatch lifecycle flags. Fault delivery does not read them: it follows
 * each FB's fault_owner, changed inside the critical section. */
static embediq_osal_atomic32_t  g_dispatch_running     = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);
static embediq_osal_atomic32_t  g_dispatch_shutdown    = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);

/* Upper bound on messages one shutdown drain pops for an FB: every queue full. */
#define SHUTDOWN_DRAIN_MAX  ((uint32_t)EMBEDIQ_HIGH_QUEUE_DEPTH                 \
                             + (uint32_t)EMBEDIQ_NORMAL_QUEUE_DEPTH             \
                             + (uint32_t)EMBEDIQ_LOW_QUEUE_DEPTH)

/* tl_exec_held[ep]: this thread holds that FB's exec_lock (it is running
 * that FB's code). A fault reported for such an FB from inside its own code
 * is only latched, not re-locked (no self-deadlock), and executed when that
 * code returns. tl_exec_depth: how many execution locks this thread holds;
 * non-zero means it may take another only by trylock.
 * Host: C11 _Thread_local. An MCU OSAL must provide equivalent per-task
 * storage (e.g. FreeRTOS thread-local storage pointers). */
static _Thread_local bool    tl_exec_held[EMBEDIQ_MAX_ENDPOINTS];
static _Thread_local uint8_t tl_exec_depth = 0u;

#ifdef EMBEDIQ_PLATFORM_HOST
/* Test hook: called by report_fault between accepting a reason and acting on
 * it. Read and written inside the critical section. */
typedef void (*fb_engine_report_hook_t)(void);
static fb_engine_report_hook_t  s_report_hook = NULL;
static embediq_osal_atomic32_t  g_fault_overflow_total = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);
#endif

static void fb_state_set(EmbedIQ_FB_t *fb, EmbedIQ_FB_State_t st)
{
    embediq_osal_atomic32_store(&fb->state, (uint32_t)st);
}

/* ---------------------------------------------------------------------------
 * Public: embediq_fb_register()
 * ------------------------------------------------------------------------- */

EmbedIQ_FB_Handle_t embediq_fb_register(const EmbedIQ_FB_Config_t *config)
{
    if (!config)                              return NULL;
    if (g_booted)                             return NULL;
    if (g_reg_count >= EMBEDIQ_MAX_ENDPOINTS) return NULL;

    EmbedIQ_FB_t *fb = &g_registry[g_reg_count];
    fb->config       = *config;
    fb_state_set(fb, EMBEDIQ_FB_STATE_IDLE);
    fb->ep_idx       = g_reg_count;
    fb->subfn_count  = 0;
    fb->registering  = false;
    embediq_osal_critical_enter();
    fb->inbox_head    = 0u;
    fb->inbox_count   = 0u;
    fb->inbox_dropped = 0u;
    fb->fault_owner   = (uint8_t)FAULT_OWNER_BOOT;
    embediq_osal_critical_exit();
    g_reg_count++;
    return (EmbedIQ_FB_Handle_t)fb;
}

/* ---------------------------------------------------------------------------
 * Public: embediq_fb_get_state()
 * ------------------------------------------------------------------------- */

EmbedIQ_FB_State_t embediq_fb_get_state(EmbedIQ_FB_Handle_t handle)
{
    const EmbedIQ_FB_t *fb = (const EmbedIQ_FB_t *)handle;
    if (!fb) return EMBEDIQ_FB_STATE_IDLE;
    return (EmbedIQ_FB_State_t)embediq_osal_atomic32_load(&fb->state);
}

/* ---------------------------------------------------------------------------
 * Per-FB execution lock and fault inbox
 * ------------------------------------------------------------------------- */

/* Execute one fault: state, Observatory LIFECYCLE event, fault_fn.
 * Caller holds fb->exec_lock, not the critical section. */
static void fault_apply_one_locked(EmbedIQ_FB_t *fb, uint32_t reason)
{
    fb_state_set(fb, EMBEDIQ_FB_STATE_FAULT);
    embediq_obs_emit(EMBEDIQ_OBS_EVT_LIFECYCLE, fb->ep_idx, 0xFFu,
                     (uint8_t)EMBEDIQ_FB_STATE_FAULT, 0u);
    if (fb->config.fault_fn) {
        fb->config.fault_fn((EmbedIQ_FB_Handle_t)fb, fb->config.fb_data, reason);
    }
}

/* Whether owner allows a drain in role. Called inside the critical section. */
static bool owner_allows(uint8_t owner, exec_role_t role)
{
    return (role == EXEC_ROLE_DISPATCH && owner == (uint8_t)FAULT_OWNER_DISPATCH)
        || (role == EXEC_ROLE_REPORTER && owner == (uint8_t)FAULT_OWNER_NONE);
}

/* One drain pass: execute, in acceptance order, at most the number of
 * reasons latched when the pass starts (<= EMBEDIQ_FB_FAULT_INBOX_DEPTH);
 * reasons the fault_fns latch meanwhile wait for the next pass. Then report
 * any overflow once. The owner is checked against role with the budget and
 * again before every reason: if it stops allowing the role, the pass stops
 * and leaves the inbox and the overflow count to the new owner. Caller holds
 * fb->exec_lock, not the critical section. */
static void fault_drain_pass_locked(EmbedIQ_FB_t *fb, exec_role_t role)
{
    embediq_osal_critical_enter();
    uint8_t budget = owner_allows(fb->fault_owner, role) ? fb->inbox_count : 0u;
    embediq_osal_critical_exit();

    for (uint8_t n = 0u; n < budget; n++) {
        bool     have   = false;
        uint32_t reason = 0u;
        embediq_osal_critical_enter();
        if (owner_allows(fb->fault_owner, role) && fb->inbox_count > 0u) {
            reason         = fb->inbox[fb->inbox_head];
            fb->inbox_head = (uint8_t)((fb->inbox_head + 1u) % EMBEDIQ_FB_FAULT_INBOX_DEPTH);
            fb->inbox_count--;
            have = true;
        }
        embediq_osal_critical_exit();
        if (!have) break;
        fault_apply_one_locked(fb, reason);
    }

    embediq_osal_critical_enter();
    uint32_t dropped = 0u;
    if (owner_allows(fb->fault_owner, role)) {
        dropped           = fb->inbox_dropped;
        fb->inbox_dropped = 0u;
    }
    embediq_osal_critical_exit();
    if (dropped > 0u) {
        embediq_obs_emit(EMBEDIQ_OBS_EVT_FAULT, 0xFFu, fb->ep_idx,
                         (uint8_t)EMBEDIQ_FAULT_PENDING_OVERFLOW,
                         (uint16_t)(dropped > UINT16_MAX ? UINT16_MAX : dropped));
#ifdef EMBEDIQ_PLATFORM_HOST
        (void)embediq_osal_atomic32_add(&g_fault_overflow_total, dropped);
#endif
    }
}

/* Missing execution lock: boot guarantees one exists after it has run, so
 * this is a programming error (FB code never runs unlocked). */
static bool exec_lock_exists(EmbedIQ_FB_t *fb)
{
    if (fb->exec_lock) return true;
    embediq_obs_emit(EMBEDIQ_OBS_EVT_FAULT, 0xFFu, fb->ep_idx,
                     (uint8_t)EMBEDIQ_FAULT_BOOT_RESOURCE, 0u);
    return false;
}

static void exec_mark_held(EmbedIQ_FB_t *fb)
{
    tl_exec_held[fb->ep_idx] = true;
    tl_exec_depth++;
}

/* Take fb's execution lock, blocking. Only for a thread that holds no other
 * execution lock (see the file header). Returns false — and runs nothing —
 * if the lock does not exist. */
static bool exec_enter(EmbedIQ_FB_t *fb)
{
    if (!exec_lock_exists(fb)) return false;
    (void)embediq_osal_mutex_lock(fb->exec_lock, UINT32_MAX);
    exec_mark_held(fb);
    return true;
}

/* Take fb's execution lock only if it is free; never waits. */
static bool exec_try(EmbedIQ_FB_t *fb)
{
    if (!exec_lock_exists(fb)) return false;
    if (!embediq_osal_mutex_try(fb->exec_lock)) return false;
    exec_mark_held(fb);
    return true;
}

static void exec_release(EmbedIQ_FB_t *fb)
{
    tl_exec_held[fb->ep_idx] = false;
    tl_exec_depth--;
    embediq_osal_mutex_unlock(fb->exec_lock);
}

/* Drain pass in role, release, check — repeated until a check that
 * follows a release finds the inbox empty. A reporter whose trylock was
 * refused while this thread held the lock relies on that check: under NONE
 * a REPORTER-role thread takes the lock again (trylock only) and runs
 * another pass; refused, a new holder exists and runs this same loop. Under
 * DISPATCH it wakes the dispatch thread; under BOOT the reason is the boot
 * thread's (executed after init_fn, or counted by a failed boot). */
static void exec_exit(EmbedIQ_FB_t *fb, exec_role_t role)
{
    for (;;) {
        fault_drain_pass_locked(fb, role);
        exec_release(fb);

        embediq_osal_critical_enter();
        bool    pending = fb->inbox_count > 0u;
        uint8_t owner   = fb->fault_owner;
        embediq_osal_critical_exit();
        if (!pending) return;

        if (owner == (uint8_t)FAULT_OWNER_DISPATCH) {
            embediq_sem_post(g_dispatch_args[fb->ep_idx].sem);
            return;
        }
        if (owner != (uint8_t)FAULT_OWNER_NONE || role != EXEC_ROLE_REPORTER
            || !exec_try(fb)) {
            return;
        }
    }
}

/* Change who executes fb's faults, only if from_owner is the current owner
 * (so dispatch_boot never takes over an FB whose init_fn has not run).
 * Inside the critical section: every report sees the old owner or the new. */
static void fault_owner_change(EmbedIQ_FB_t *fb, fault_owner_t from_owner,
                               fault_owner_t to_owner)
{
    embediq_osal_critical_enter();
    if (fb->fault_owner == (uint8_t)from_owner) {
        fb->fault_owner = (uint8_t)to_owner;
    }
    embediq_osal_critical_exit();
}

/* ---------------------------------------------------------------------------
 * Public: embediq_fb_report_fault()
 *
 * Accept the reason (constant time, inside the critical section), then act
 * by the owner read in the same section: BOOT — nothing (boot executes after
 * init_fn); DISPATCH — wake the dispatch thread; NONE — execute here under
 * the execution lock: blocking if this thread holds no execution lock, by
 * trylock otherwise (refused: the holder executes it on release). The owner
 * read here may be stale by the time the lock is held; the drain pass
 * checks it again and executes only while it is still NONE.
 * ------------------------------------------------------------------------- */

void embediq_fb_report_fault(EmbedIQ_FB_Handle_t handle, uint32_t reason)
{
    EmbedIQ_FB_t *fb = (EmbedIQ_FB_t *)handle;
    if (!fb) return;

    uint8_t ep = fb->ep_idx;

    embediq_osal_critical_enter();
    if (fb->inbox_count < (uint8_t)EMBEDIQ_FB_FAULT_INBOX_DEPTH) {
        fb->inbox[(fb->inbox_head + fb->inbox_count) % EMBEDIQ_FB_FAULT_INBOX_DEPTH] = reason;
        fb->inbox_count++;
    } else if (fb->inbox_dropped < UINT32_MAX) {
        fb->inbox_dropped++;
    }
    uint8_t owner = fb->fault_owner;
#ifdef EMBEDIQ_PLATFORM_HOST
    fb_engine_report_hook_t hook = s_report_hook;
#endif
    embediq_osal_critical_exit();

#ifdef EMBEDIQ_PLATFORM_HOST
    if (hook) hook();
#endif

    /* This thread is running fb's code: exec_exit executes it on return. */
    if (tl_exec_held[ep]) return;

    if (owner == (uint8_t)FAULT_OWNER_DISPATCH) {
        embediq_sem_post(g_dispatch_args[ep].sem);
        return;
    }
    if (owner != (uint8_t)FAULT_OWNER_NONE) return;   /* BOOT: latched */

    if (tl_exec_depth > 0u) {
        /* Holding another FB's lock: never wait for this one. Refused: the
         * holder executes it on release. */
        if (!exec_try(fb)) return;
    } else if (!exec_enter(fb)) {
        return;
    }
    exec_exit(fb, EXEC_ROLE_REPORTER);
}

/* ---------------------------------------------------------------------------
 * Public: embediq_bus_resolve_name()
 * ------------------------------------------------------------------------- */

uint8_t embediq_bus_resolve_name(const char *name)
{
    if (!name) return 0xFFu;
    for (uint8_t i = 0; i < g_reg_count; i++) {
        if (g_registry[i].config.name &&
            strcmp(g_registry[i].config.name, name) == 0) {
            return i;
        }
    }
    return 0xFFu;
}

/* ---------------------------------------------------------------------------
 * Public: embediq_subfn_register() — I-10 enforced via registering flag
 *
 * Sub-fns are inserted in ascending init_order so that dispatch_msg_to_fb
 * can iterate forward for guaranteed deterministic (R-sub-ORDER) execution.
 * ------------------------------------------------------------------------- */

int embediq_subfn_register(EmbedIQ_FB_Handle_t handle,
                            const EmbedIQ_SubFn_Config_t *config)
{
    EmbedIQ_FB_t *fb = (EmbedIQ_FB_t *)handle;
    if (!fb || !config)                        return -1;
    if (!fb->registering)                      return -1;  /* I-10 */
    if (fb->subfn_count >= EMBEDIQ_MAX_SUBFNS_PER_FB) return -1;

    /* Insertion sort: find position that maintains ascending init_order. */
    uint8_t pos = fb->subfn_count;
    while (pos > 0 &&
           fb->subfns[pos - 1u].init_order > config->init_order) {
        fb->subfns[pos] = fb->subfns[pos - 1u];
        pos--;
    }
    fb->subfns[pos] = *config;
    fb->subfn_count++;
    return 0;
}

/* ---------------------------------------------------------------------------
 * Message dispatch — called by the runtime task loop and the test harness
 *
 * Iterates sub-fns in stored order (ascending init_order, R-sub-ORDER).
 * For each sub-fn whose subscription list contains msg_id, calls run_fn.
 * ------------------------------------------------------------------------- */

static void dispatch_msg_to_fb(EmbedIQ_FB_t *fb, uint16_t msg_id,
                                const void *msg)
{
    for (uint8_t i = 0; i < fb->subfn_count; i++) {
        EmbedIQ_SubFn_Config_t *sf = &fb->subfns[i];
        if (!sf->run_fn) continue;
        for (uint8_t s = 0; s < sf->subscription_count; s++) {
            if (sf->subscriptions[s] == msg_id) {
                sf->run_fn((EmbedIQ_FB_Handle_t)fb, msg,
                           fb->config.fb_data, sf->subfn_data);
                break;
            }
        }
    }
}

/* ---------------------------------------------------------------------------
 * Boot internals — phase sort + topological sort + cycle/missing dep checks
 * ------------------------------------------------------------------------- */

typedef enum { COLOR_WHITE = 0, COLOR_GRAY = 1, COLOR_BLACK = 2 } topo_color_t;

/** Linear scan for a registered FB by name. Returns index or -1. */
static int find_fb_by_name(const char *name)
{
    for (int i = 0; i < (int)g_reg_count; i++) {
        if (g_registry[i].config.name &&
            strcmp(g_registry[i].config.name, name) == 0) {
            return i;
        }
    }
    return -1;
}

/**
 * DFS for topological sort within one boot phase.
 *
 * phase_set[i] == true when global registry index i belongs to the current
 * phase.  Dependencies outside the current phase are already initialised and
 * are silently skipped.
 *
 * Returns false if a cycle is detected (GRAY node reached from GRAY ancestor).
 */
static bool topo_dfs(uint8_t idx, topo_color_t *colors, const bool *phase_set,
                     uint8_t *result, uint8_t *result_count)
{
    colors[idx] = COLOR_GRAY;

    for (int d = 0; d < (int)g_registry[idx].config.depends_count; d++) {
        const char *dep_name = g_registry[idx].config.depends_on[d];
        int dep_idx = find_fb_by_name(dep_name);

        /* Dependency in a previous phase — already initialised, skip. */
        if (dep_idx < 0 || !phase_set[dep_idx]) continue;

        if (colors[dep_idx] == COLOR_GRAY)  return false;  /* cycle detected */
        if (colors[dep_idx] == COLOR_WHITE) {
            if (!topo_dfs((uint8_t)dep_idx, colors, phase_set,
                          result, result_count)) {
                return false;
            }
        }
    }

    colors[idx] = COLOR_BLACK;
    result[(*result_count)++] = idx;
    return true;
}

/* Boot failed: no FB code starts from here. Every FB returns to BOOT (latch
 * only); its latched reports are discarded and counted in one BOOT_ABORTED
 * event. A reason already taken from the inbox when boot fails is executed
 * to completion on the thread that took it and is not included in the
 * BOOT_ABORTED count; reports accepted after the BOOT_ABORTED event are
 * latched; they are
 * counted at the next boot failure, executed after init on a retried boot,
 * or discarded by reset. */
static int boot_abort(void)
{
    for (uint8_t i = 0u; i < g_reg_count; i++) {
        EmbedIQ_FB_t *fb = &g_registry[i];
        embediq_osal_critical_enter();
        uint32_t latched  = (fb->inbox_dropped > UINT32_MAX - fb->inbox_count)
                            ? UINT32_MAX : fb->inbox_dropped + fb->inbox_count;
        fb->inbox_head    = 0u;
        fb->inbox_count   = 0u;
        fb->inbox_dropped = 0u;
        fb->fault_owner   = (uint8_t)FAULT_OWNER_BOOT;
        embediq_osal_critical_exit();
        if (latched > 0u) {
            embediq_obs_emit(EMBEDIQ_OBS_EVT_FAULT, 0xFFu, fb->ep_idx,
                             (uint8_t)EMBEDIQ_FAULT_BOOT_ABORTED,
                             (uint16_t)(latched > UINT16_MAX ? UINT16_MAX : latched));
        }
    }
    return -1;
}

/* ---------------------------------------------------------------------------
 * Public: embediq_engine_boot()
 * ------------------------------------------------------------------------- */

int embediq_engine_boot(void)
{
    if (g_booted) return 0;   /* idempotent */

    /* Observatory transport mutex — before any init_fn can emit. */
    embediq_obs_init();

    /* Per-FB execution locks FIRST — a required boot resource. Before
     * platform libs, queues, semaphores, notify_fn and any init_fn: if one
     * cannot be allocated, nothing has run and boot fails cleanly. */
    for (uint8_t i = 0u; i < g_reg_count; i++) {
        if (g_registry[i].exec_lock) continue;   /* boot may be retried */
        g_registry[i].exec_lock = embediq_osal_mutex_create();
        if (!g_registry[i].exec_lock) {
            embediq_obs_emit(EMBEDIQ_OBS_EVT_FAULT, 0xFFu, g_registry[i].ep_idx,
                             (uint8_t)EMBEDIQ_FAULT_BOOT_RESOURCE, 0u);
            for (uint8_t k = 0u; k < g_reg_count; k++) {
                embediq_osal_mutex_destroy(g_registry[k].exec_lock);   /* NULL-safe */
                g_registry[k].exec_lock = NULL;
            }
            return boot_abort();
        }
    }

    g_boot_count = 0;

    /* Initialise all declared platform libraries (e.g. HAL ops tables) before
     * any Phase-1 FB runs. Platform libs are registered via
     * embediq_platform_lib_declare() in main() before embediq_engine_boot().
     * This call invokes every registered init_fn in declaration order so that
     * ops tables are populated when any FB's init_fn queries them.
     * I-PL: platform libs are the seam between HAL implementations and
     * framework services — they must be ready before Phase 1. */
    embediq_platform_lib_init_all();

    /* Boot the message bus first: builds subscription table and creates
     * per-FB priority queues before any Phase-1 FB tasks start running.
     * This must happen before embediq_publish() is called by any FB thread. */
    message_bus_boot();

    /* --- Step 1: verify all dependencies exist (global, pre-phase) --- */
    for (uint8_t i = 0; i < g_reg_count; i++) {
        for (int d = 0; d < (int)g_registry[i].config.depends_count; d++) {
            const char *dep = g_registry[i].config.depends_on[d];
            if (find_fb_by_name(dep) < 0) {
                embediq_obs_emit(EMBEDIQ_OBS_EVT_FAULT,
                                 g_registry[i].ep_idx, 0xFFu,
                                 (uint8_t)EMBEDIQ_FB_STATE_FAULT, 0u);
                return boot_abort();   /* FAULT: dependency not registered */
            }
        }
    }

    /* Create per-FB dispatch semaphores and install the bus notify callback
     * BEFORE any init_fn runs: a Platform FB's init_fn may start a thread
     * that publishes immediately (fb_timer), and that publish must find the
     * notify pointer and the target's semaphore already in place (TSan-found
     * boot-order race). embediq_engine_dispatch_boot() starts the threads that
     * block on these semaphores. Only subscribing FBs get one. */
    for (uint8_t i = 0u; i < g_reg_count; i++) {
        if (g_registry[i].config.subscription_count == 0u) continue;
        g_dispatch_args[i].fb    = &g_registry[i];
        g_dispatch_args[i].ep_id = g_registry[i].ep_idx;
        g_dispatch_args[i].sem   = embediq_sem_create(0u, UINT32_MAX);
    }
    message_bus_set_notify_fn(embediq_engine_notify_fb);

    /* --- Step 2: process each boot phase in order 1 → 2 → 3 → 4 --- */
    static const EmbedIQ_BootPhase_t k_phases[] = {
        EMBEDIQ_BOOT_PHASE_PLATFORM,
        EMBEDIQ_BOOT_PHASE_INFRASTRUCTURE,
        EMBEDIQ_BOOT_PHASE_APPLICATION,
        EMBEDIQ_BOOT_PHASE_BRIDGE,
    };

    for (int p = 0; p < (int)(sizeof(k_phases) / sizeof(k_phases[0])); p++) {
        EmbedIQ_BootPhase_t phase = k_phases[p];

        /* Collect all FBs belonging to this phase. */
        uint8_t phase_indices[EMBEDIQ_MAX_ENDPOINTS];
        uint8_t phase_count = 0;
        bool    phase_set[EMBEDIQ_MAX_ENDPOINTS];
        memset(phase_set, 0, sizeof(phase_set));

        for (uint8_t i = 0; i < g_reg_count; i++) {
            if (g_registry[i].config.boot_phase == phase) {
                phase_indices[phase_count++] = i;
                phase_set[i] = true;
            }
        }

        if (phase_count == 0) continue;

        /* Topological sort within this phase via DFS post-order. */
        topo_color_t colors[EMBEDIQ_MAX_ENDPOINTS];
        memset(colors, 0, sizeof(colors));   /* COLOR_WHITE == 0 */

        uint8_t sorted[EMBEDIQ_MAX_ENDPOINTS];
        uint8_t sorted_count = 0;

        for (uint8_t j = 0; j < phase_count; j++) {
            uint8_t idx = phase_indices[j];
            if (colors[idx] == COLOR_WHITE) {
                if (!topo_dfs(idx, colors, phase_set, sorted, &sorted_count)) {
                    /* Cycle detected within this phase. */
                    embediq_obs_emit(EMBEDIQ_OBS_EVT_FAULT, 0xFFu, 0xFFu,
                                     (uint8_t)EMBEDIQ_FB_STATE_FAULT, 0u);
                    return boot_abort();
                }
            }
        }

        /* --- Step 3: call init_fn for each FB in topo-sorted order --- */
        for (uint8_t j = 0; j < sorted_count; j++) {
            uint8_t       idx = sorted[j];
            EmbedIQ_FB_t *fb  = &g_registry[idx];

            /* init_fn runs under the FB's execution lock. Faults reported on
             * the FB until init_fn returns — before boot reached it, from a
             * platform thread started in init_fn, or by init_fn itself — are
             * only latched (owner BOOT). */
            fault_owner_change(fb, FAULT_OWNER_NONE, FAULT_OWNER_BOOT);   /* retried boot */
            (void)exec_enter(fb);   /* exists: created at the top of boot */

            /* Transition IDLE → INITIALISING (state is kept on a boot retry). */
            if (embediq_osal_atomic32_cas(&fb->state, (uint32_t)EMBEDIQ_FB_STATE_IDLE,
                                          (uint32_t)EMBEDIQ_FB_STATE_INITIALISING)) {
                embediq_obs_emit(EMBEDIQ_OBS_EVT_LIFECYCLE, fb->ep_idx, 0xFFu,
                                 (uint8_t)EMBEDIQ_FB_STATE_INITIALISING, 0u);
            }

            /* Call init_fn — sub-fn registration is allowed during this call. */
            if (fb->config.init_fn) {
                fb->registering = true;
                fb->config.init_fn((EmbedIQ_FB_Handle_t)fb, fb->config.fb_data);
                fb->registering = false;
            }

            /* Owner BOOT → NONE first, then execute what was latched: a
             * report after the flip finds this thread holding the lock and
             * is executed by exec_exit below. A fault accepted before or
             * during init_fn is executed before the FB is marked RUNNING. A
             * report accepted in the closing window — after that drain and
             * before the boot thread releases the lock — is executed
             * immediately after; the trace may then show RUNNING followed by
             * FAULT for that FB. The state never returns to RUNNING. */
            fault_owner_change(fb, FAULT_OWNER_BOOT, FAULT_OWNER_NONE);
            fault_drain_pass_locked(fb, EXEC_ROLE_REPORTER);

            /* Transition INITIALISING → RUNNING, only if no fault was executed
             * (FAULT is never overwritten), before the unlock. */
            if (embediq_osal_atomic32_cas(&fb->state, (uint32_t)EMBEDIQ_FB_STATE_INITIALISING,
                                          (uint32_t)EMBEDIQ_FB_STATE_RUNNING)) {
                embediq_obs_emit(EMBEDIQ_OBS_EVT_LIFECYCLE, fb->ep_idx, 0xFFu,
                                 (uint8_t)EMBEDIQ_FB_STATE_RUNNING, 0u);
            }
            exec_exit(fb, EXEC_ROLE_REPORTER);

            g_boot_order[g_boot_count++] = idx;
        }
    }

    g_booted = true;
    return 0;
}

/* ---------------------------------------------------------------------------
 * Public: engine registry API (declared in embediq_fb.h) — replaces the former
 * undeclared fb_engine__* accessors used by message_bus.c and Layer 2 (T2-7).
 * ------------------------------------------------------------------------- */

uint8_t embediq_engine_fb_count(void)
{
    return g_reg_count;
}

const EmbedIQ_FB_Config_t *embediq_engine_fb_config(uint8_t idx)
{
    if (idx >= g_reg_count) return NULL;
    return &g_registry[idx].config;
}

uint8_t embediq_engine_fb_ep(EmbedIQ_FB_Handle_t handle)
{
    const EmbedIQ_FB_t *fb = (const EmbedIQ_FB_t *)handle;
    if (!fb) return 0xFFu;
    return fb->ep_idx;
}

/* ---------------------------------------------------------------------------
 * Public: embediq_engine_notify_fb()
 *
 * Called by message_bus.c after every successful message enqueue.
 * Posts the semaphore of the target FB's dispatch thread so it wakes
 * immediately instead of polling.  Safe to call from any thread.
 * ------------------------------------------------------------------------- */

void embediq_engine_notify_fb(uint8_t ep_id)
{
    if (ep_id >= EMBEDIQ_MAX_ENDPOINTS) return;
    if (!g_dispatch_args[ep_id].sem)    return;
    embediq_sem_post_from_isr(g_dispatch_args[ep_id].sem);
}

/* ---------------------------------------------------------------------------
 * Per-FB dispatch loop
 *
 * One instance runs per FB that has subscriptions.
 * Blocks on the FB's dispatch semaphore (posted by the bus after each
 * enqueue) then drains available messages before blocking again, testing the
 * shutdown flag before every message so shutdown is bounded by one message,
 * not by how fast others keep posting. On exit it drains what is left
 * (drain_at_shutdown). Zero CPU burn when idle — no polling.
 * ------------------------------------------------------------------------- */

/* Pop the next message (HIGH, then NORMAL, then LOW) into *msg. */
static bool recv_next(uint8_t ep, EmbedIQ_Msg_t *msg, uint8_t *prio)
{
    for (uint8_t p = 0u; p < 3u; p++) {
        if (message_bus_recv_ep(ep, p, msg)) {
            *prio = p;
            return true;
        }
    }
    return false;
}

/* Dispatch one queued message. Returns false when all queues are empty. */
static bool dispatch_next(EmbedIQ_FB_t *fb, uint8_t ep)
{
    EmbedIQ_Msg_t msg;
    uint8_t       rx_prio = 0u;
    if (!recv_next(ep, &msg, &rx_prio)) return false;

    /* MSG_RX tap — level-3 (trace) event. */
    if (embediq_obs_level_at_least(3u)) {
        embediq_obs_emit(EMBEDIQ_OBS_EVT_MSG_RX, msg.source_endpoint_id,
                         ep, rx_prio, msg.msg_id);
    }
    /* One message under the FB's execution lock; released between messages
     * so other owners of the lock can interleave. */
    if (!exec_enter(fb)) return true;
    dispatch_msg_to_fb(fb, msg.msg_id, msg.payload);
    exec_exit(fb, EXEC_ROLE_DISPATCH);   /* executes faults latched meanwhile */
    return true;
}

/* Empty ep's queues at dispatch shutdown: every message is discarded and
 * counted. Bounded by SHUTDOWN_DRAIN_MAX pops. Returns the number discarded. */
static uint32_t drain_at_shutdown(uint8_t ep)
{
    uint32_t      discarded = 0u;
    EmbedIQ_Msg_t msg;
    uint8_t       prio = 0u;
    for (uint32_t n = 0u; n < SHUTDOWN_DRAIN_MAX && recv_next(ep, &msg, &prio); n++) {
        discarded++;
    }
    return discarded;
}

static void fb_dispatch_loop(void *arg)
{
    dispatch_arg_t *da = (dispatch_arg_t *)arg;
    EmbedIQ_FB_t   *fb = da->fb;
    uint8_t         ep = da->ep_id;

    while (embediq_osal_atomic32_load(&g_dispatch_running) != 0u
           && embediq_osal_atomic32_load(&g_dispatch_shutdown) == 0u) {
        /* Block until the bus signals that at least one message is queued,
         * a fault was latched, or dispatch_shutdown posts the semaphore. */
        embediq_sem_wait(da->sem);

        /* Latched faults first, then queued messages, testing the shutdown
         * flag before each one. */
        if (exec_enter(fb)) exec_exit(fb, EXEC_ROLE_DISPATCH);
        while (embediq_osal_atomic32_load(&g_dispatch_shutdown) == 0u
               && dispatch_next(fb, ep)) {
        }
    }

    /* Shutdown: discard the queued messages; execute latched faults. */
    da->discarded = drain_at_shutdown(ep);
    if (exec_enter(fb)) exec_exit(fb, EXEC_ROLE_DISPATCH);
}

/* ---------------------------------------------------------------------------
 * Public: embediq_engine_dispatch_boot()
 * ------------------------------------------------------------------------- */

void embediq_engine_dispatch_boot(void)
{
    if (!g_booted) return;   /* a failed boot hands no FB to a dispatch thread */
    if (embediq_osal_atomic32_load(&g_dispatch_running) != 0u) return;   /* idempotent */

    embediq_osal_atomic32_store(&g_dispatch_running, 1u);

    for (uint8_t i = 0u; i < g_reg_count; i++) {
        EmbedIQ_FB_t *fb = &g_registry[i];

        /* Only create dispatch tasks for FBs that receive messages.
         * Semaphore was already created in embediq_engine_boot(). */
        if (fb->config.subscription_count == 0u) continue;

        uint8_t prio = fb->config.priority;
        if (prio == 0u) prio = EMBEDIQ_DISPATCH_DEFAULT_PRIORITY;

        g_dispatch_args[i].task =
            embediq_osal_task_create(fb->config.name,
                                     fb_dispatch_loop,
                                     &g_dispatch_args[i],
                                     prio,
                                     EMBEDIQ_DISPATCH_STACK_SIZE);
        /* No thread (the OSAL observes the failure): the owner stays NONE,
         * so reporters keep executing this FB's faults. */
        if (!g_dispatch_args[i].task) continue;

        /* The dispatch thread owns fault execution from here; the post
         * covers anything latched before its first wait. */
        fault_owner_change(fb, FAULT_OWNER_NONE, FAULT_OWNER_DISPATCH);
        embediq_sem_post(g_dispatch_args[i].sem);
    }
}

/* ---------------------------------------------------------------------------
 * Public: embediq_engine_dispatch_shutdown()
 *
 * 1. Set the shutdown flag.
 * 2. Post each FB's semaphore once and join every dispatch thread; each exits
 *    within one message, discards its queued messages and executes its
 *    latched faults.
 * 3. Per dispatch FB: owner DISPATCH → NONE, then execute the faults
 *    latched for each FB whose execution lock it can take. A fault being
 *    executed by a reporting thread at that moment completes on that
 *    thread, and reasons it leaves latched are executed by that thread's own
 *    release-check loop; shutdown never waits for an execution lock. A later
 *    report is executed by its reporter. Discard what was queued since the thread's drain and
 *    report all discarded messages as one QUEUE_DROP event per FB.
 * 4. Clear running, then shutdown.
 * ------------------------------------------------------------------------- */

int embediq_engine_dispatch_shutdown(void)
{
    if (embediq_osal_atomic32_load(&g_dispatch_running) == 0u) return 0;   /* idempotent */

    /* 1. */
    embediq_osal_atomic32_store(&g_dispatch_shutdown, 1u);

    /* 2. */
    for (uint8_t i = 0u; i < EMBEDIQ_MAX_ENDPOINTS; i++) {
        if (g_dispatch_args[i].sem) {
            embediq_sem_post(g_dispatch_args[i].sem);
        }
    }
    for (uint8_t i = 0u; i < EMBEDIQ_MAX_ENDPOINTS; i++) {
        if (g_dispatch_args[i].task) {
            embediq_osal_task_join(g_dispatch_args[i].task);
            g_dispatch_args[i].task = NULL;
        }
    }

    /* 3. */
    for (uint8_t i = 0u; i < EMBEDIQ_MAX_ENDPOINTS; i++) {
        EmbedIQ_FB_t *fb = g_dispatch_args[i].fb;
        if (!g_dispatch_args[i].sem || !fb) continue;
        fault_owner_change(fb, FAULT_OWNER_DISPATCH, FAULT_OWNER_NONE);
        if (exec_try(fb)) exec_exit(fb, EXEC_ROLE_REPORTER);
        uint32_t discarded = g_dispatch_args[i].discarded + drain_at_shutdown(i);
        g_dispatch_args[i].discarded = 0u;
        if (discarded > 0u) {
            embediq_obs_emit(EMBEDIQ_OBS_EVT_QUEUE_DROP, 0xFFu, i,
                             (uint8_t)EMBEDIQ_QUEUE_DROP_AT_SHUTDOWN,
                             (uint16_t)(discarded > UINT16_MAX ? UINT16_MAX : discarded));
        }
    }

    /* 4. */
    embediq_osal_atomic32_store(&g_dispatch_running, 0u);
    embediq_osal_atomic32_store(&g_dispatch_shutdown, 0u);
    return 0;
}

/* ---------------------------------------------------------------------------
 * Host / test-only API
 *
 * Guarded by EMBEDIQ_PLATFORM_HOST so these symbols are absent from MCU
 * production builds (Invariant I-05: zero test symbols in production binary).
 * ------------------------------------------------------------------------- */

#ifdef EMBEDIQ_PLATFORM_HOST

/* Observatory package-internal API — implemented in observatory/obs.c */
extern void     obs__reset(void);
extern uint32_t obs__event_count(void);

/* message_bus__reset() is defined in message_bus.c; extern decl here avoids
 * a private header.  Safe to call even if message_bus_boot() never ran. */
extern void message_bus__reset(void);

/** Reset all engine state — must be called between test cases.
 *  Precondition: no reporter or platform thread started by the test is still
 *  alive (every test joins its threads first); a report racing the reset
 *  would touch a registry being cleared. */
void fb_engine__reset(void)
{
    /* If dispatch threads are running, shut them down cleanly first.
     * This handles tests that call dispatch_boot without dispatch_shutdown. */
    if (embediq_osal_atomic32_load(&g_dispatch_running) != 0u) {
        embediq_engine_dispatch_shutdown();
    }

    /* Destroy per-FB dispatch semaphores (created in embediq_engine_boot).
     * Safe to call: embediq_sem_destroy(NULL) is a no-op. */
    for (uint8_t i = 0u; i < EMBEDIQ_MAX_ENDPOINTS; i++) {
        if (g_dispatch_args[i].sem) {
            embediq_sem_destroy(g_dispatch_args[i].sem);
        }
        embediq_osal_mutex_destroy(g_registry[i].exec_lock);   /* NULL-safe */
    }
    memset(g_registry,      0, sizeof(g_registry));
    memset(g_boot_order,    0, sizeof(g_boot_order));
    memset(g_dispatch_args, 0, sizeof(g_dispatch_args));
    g_reg_count        = 0;
    g_boot_count       = 0;
    g_booted           = false;
    embediq_osal_atomic32_store(&g_dispatch_running, 0u);
    embediq_osal_atomic32_store(&g_fault_overflow_total, 0u);
    message_bus__reset();
    obs__reset();
}

/** Return the total number of Observatory events emitted since last reset. */
uint32_t fb_engine__obs_event_count(void)
{
    return obs__event_count();
}

/** Directly dispatch a message to an FB's sub-fns, bypassing the queue.
 *  Used by unit tests to verify dispatch order without OSAL task infrastructure. */
void fb_engine__deliver_msg(EmbedIQ_FB_Handle_t handle, uint16_t msg_id,
                             const void *msg)
{
    EmbedIQ_FB_t *fb = (EmbedIQ_FB_t *)handle;
    if (!fb) return;
    if (tl_exec_held[fb->ep_idx]) {          /* already running this FB's code */
        dispatch_msg_to_fb(fb, msg_id, msg);
        return;
    }
    if (!exec_enter(fb)) return;             /* same rule as the dispatch loop */
    dispatch_msg_to_fb(fb, msg_id, msg);
    exec_exit(fb, EXEC_ROLE_REPORTER);
}

/** True when the calling thread holds ep's execution lock (test use only). */
bool fb_engine__exec_lock_held(uint8_t ep)
{
    if (ep >= EMBEDIQ_MAX_ENDPOINTS) return false;
    return tl_exec_held[ep];
}

/** Install (fn) or remove (NULL) the hook report_fault calls between
 *  accepting a reason and acting on it (test use only). */
void fb_engine__set_report_delay_hook(fb_engine_report_hook_t fn)
{
    embediq_osal_critical_enter();
    s_report_hook = fn;
    embediq_osal_critical_exit();
}

/** Reports counted as fault-inbox overflow since the last reset (test use only). */
uint32_t fb_engine__fault_overflow_total(void)
{
    return embediq_osal_atomic32_load(&g_fault_overflow_total);
}

/** Return the dispatch semaphore for ep_id, or NULL if none (no subscriptions). */
embediq_sem_t fb_engine__dispatch_sem(uint8_t ep_id)
{
    if (ep_id >= EMBEDIQ_MAX_ENDPOINTS) return NULL;
    return g_dispatch_args[ep_id].sem;
}

#endif /* EMBEDIQ_PLATFORM_HOST */
