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
 *   3. embediq_fb_report_fault() — FAULT state + Observatory event + fault_fn
 *   4. embediq_subfn_register() — stored sorted by init_order (I-10 enforced)
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
    EmbedIQ_FB_State_t     state;
    uint8_t                ep_idx;
    uint8_t                subfn_count;
    /* Sub-fns kept sorted by init_order via insertion sort at registration. */
    EmbedIQ_SubFn_Config_t subfns[EMBEDIQ_MAX_SUBFNS_PER_FB];
    bool                   registering;   /* I-10: true only during init_fn */
};

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
    embediq_sem_t   sem;    /* posted by bus on every successful enqueue */
    EmbedIQ_Task_t *task;   /* handle stored for pthread_join on shutdown */
} dispatch_arg_t;

static dispatch_arg_t    g_dispatch_args[EMBEDIQ_MAX_ENDPOINTS];
static volatile bool     g_dispatch_running  = false;
static volatile bool     g_dispatch_shutdown = false;

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
    fb->state        = EMBEDIQ_FB_STATE_IDLE;
    fb->ep_idx       = g_reg_count;
    fb->subfn_count  = 0;
    fb->registering  = false;
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
    return fb->state;
}

/* ---------------------------------------------------------------------------
 * Public: embediq_fb_report_fault()
 * ------------------------------------------------------------------------- */

void embediq_fb_report_fault(EmbedIQ_FB_Handle_t handle, uint32_t reason)
{
    EmbedIQ_FB_t *fb = (EmbedIQ_FB_t *)handle;
    if (!fb) return;

    fb->state = EMBEDIQ_FB_STATE_FAULT;
    embediq_obs_emit(EMBEDIQ_OBS_EVT_LIFECYCLE, fb->ep_idx, 0xFFu,
                     (uint8_t)EMBEDIQ_FB_STATE_FAULT, 0u);

    if (fb->config.fault_fn) {
        fb->config.fault_fn(handle, fb->config.fb_data, reason);
    }
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

/* ---------------------------------------------------------------------------
 * Public: embediq_engine_boot()
 * ------------------------------------------------------------------------- */

int embediq_engine_boot(void)
{
    if (g_booted) return 0;   /* idempotent */

    g_boot_count = 0;

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
                return -1;   /* FAULT: dependency not registered */
            }
        }
    }

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
                    return -1;
                }
            }
        }

        /* --- Step 3: call init_fn for each FB in topo-sorted order --- */
        for (uint8_t j = 0; j < sorted_count; j++) {
            uint8_t       idx = sorted[j];
            EmbedIQ_FB_t *fb  = &g_registry[idx];

            /* Transition IDLE → INITIALISING */
            fb->state = EMBEDIQ_FB_STATE_INITIALISING;
            embediq_obs_emit(EMBEDIQ_OBS_EVT_LIFECYCLE, fb->ep_idx, 0xFFu,
                             (uint8_t)EMBEDIQ_FB_STATE_INITIALISING, 0u);

            /* Call init_fn — sub-fn registration is allowed during this call. */
            if (fb->config.init_fn) {
                fb->registering = true;
                fb->config.init_fn((EmbedIQ_FB_Handle_t)fb, fb->config.fb_data);
                fb->registering = false;
            }

            /* Transition INITIALISING → RUNNING */
            fb->state = EMBEDIQ_FB_STATE_RUNNING;
            embediq_obs_emit(EMBEDIQ_OBS_EVT_LIFECYCLE, fb->ep_idx, 0xFFu,
                             (uint8_t)EMBEDIQ_FB_STATE_RUNNING, 0u);

            g_boot_order[g_boot_count++] = idx;
        }
    }

    /* Create per-FB dispatch semaphores now that all FBs are fully init'd.
     * embediq_engine_dispatch_boot() will start threads that block on these. */
    for (uint8_t i = 0u; i < g_reg_count; i++) {
        if (g_registry[i].config.subscription_count == 0u) continue;
        g_dispatch_args[i].fb    = &g_registry[i];
        g_dispatch_args[i].ep_id = g_registry[i].ep_idx;
        g_dispatch_args[i].sem   = embediq_sem_create(0u, UINT32_MAX);
    }

    /* Register the notify callback so the bus can wake dispatch threads. */
    message_bus_set_notify_fn(embediq_engine_notify_fb);

    g_booted = true;
    return 0;
}

/* ---------------------------------------------------------------------------
 * Package-internal accessors — used by message_bus.c to read the registry.
 * Not declared in any public header; message_bus.c uses extern declarations.
 * These are required at runtime (not test-only) so no EMBEDIQ_PLATFORM_HOST guard.
 * ------------------------------------------------------------------------- */

/** Return the number of FBs currently registered. */
uint8_t fb_engine__reg_count(void)
{
    return g_reg_count;
}

/** Return a pointer to the config for registry index idx, or NULL if out of range. */
const EmbedIQ_FB_Config_t *fb_engine__reg_config(uint8_t idx)
{
    if (idx >= g_reg_count) return NULL;
    return &g_registry[idx].config;
}

/** Return the endpoint index for a given FB handle, or 0xFF on NULL. */
uint8_t fb_engine__get_ep_id(EmbedIQ_FB_Handle_t handle)
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
 * enqueue) then drains all available messages before blocking again.
 * Zero CPU burn when idle — no polling.
 * ------------------------------------------------------------------------- */

static void fb_dispatch_loop(void *arg)
{
    dispatch_arg_t *da = (dispatch_arg_t *)arg;
    EmbedIQ_FB_t   *fb = da->fb;
    uint8_t         ep = da->ep_id;
    EmbedIQ_Msg_t   msg;

    while (g_dispatch_running) {
        /* Block until the bus signals that at least one message is queued
         * or dispatch_shutdown posts the semaphore to wake this thread. */
        embediq_sem_wait(da->sem);

        /* Shutdown path: exit the loop cleanly without processing more msgs. */
        if (g_dispatch_shutdown) break;

        /* Drain all queued messages before blocking again. */
        bool got;
        do {
            got = false;
            for (uint8_t p = 0u; p < 3u && !got; p++) {
                got = message_bus_recv_ep(ep, p, &msg);
            }
            if (got) {
                dispatch_msg_to_fb(fb, msg.msg_id, msg.payload);
            }
        } while (got);
    }
}

/* ---------------------------------------------------------------------------
 * Public: embediq_engine_dispatch_boot()
 * ------------------------------------------------------------------------- */

void embediq_engine_dispatch_boot(void)
{
    if (g_dispatch_running) return;   /* idempotent */

    g_dispatch_running = true;

    for (uint8_t i = 0u; i < g_reg_count; i++) {
        EmbedIQ_FB_t *fb = &g_registry[i];

        /* Only create dispatch tasks for FBs that receive messages.
         * Semaphore was already created in embediq_engine_boot(). */
        if (fb->config.subscription_count == 0u) continue;

        g_dispatch_args[i].task =
            embediq_osal_task_create(fb->config.name,
                                     fb_dispatch_loop,
                                     &g_dispatch_args[i],
                                     2u, 4096u);
    }
}

/* ---------------------------------------------------------------------------
 * Public: embediq_engine_dispatch_shutdown()
 *
 * 1. Set the shutdown flag so dispatch loops exit after their next sem_wait.
 * 2. Post each FB's semaphore once to unblock any thread currently waiting.
 * 3. Join every dispatch thread — returns only when all have exited.
 * 4. Clear the shutdown flag and g_dispatch_running so dispatch_boot() can
 *    be called again for a clean restart (idempotent re-boot support).
 * ------------------------------------------------------------------------- */

int embediq_engine_dispatch_shutdown(void)
{
    if (!g_dispatch_running) return 0;   /* idempotent — nothing to stop */

    /* Signal all dispatch loops to exit after their next wake-up. */
    g_dispatch_shutdown = true;

    /* Post each semaphore once to unblock any thread blocked in sem_wait. */
    for (uint8_t i = 0u; i < EMBEDIQ_MAX_ENDPOINTS; i++) {
        if (g_dispatch_args[i].sem) {
            embediq_sem_post(g_dispatch_args[i].sem);
        }
    }

    /* Join every thread — no busy-wait, no polling. */
    for (uint8_t i = 0u; i < EMBEDIQ_MAX_ENDPOINTS; i++) {
        if (g_dispatch_args[i].task) {
            embediq_osal_task_join(g_dispatch_args[i].task);
            g_dispatch_args[i].task = NULL;
        }
    }

    /* Reset flags so dispatch_boot() can restart cleanly. */
    g_dispatch_shutdown = false;
    g_dispatch_running  = false;
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

/** Reset all engine state — must be called between test cases. */
void fb_engine__reset(void)
{
    /* If dispatch threads are running, shut them down cleanly first.
     * This handles tests that call dispatch_boot without dispatch_shutdown. */
    if (g_dispatch_running) {
        embediq_engine_dispatch_shutdown();
    }

    /* Destroy per-FB dispatch semaphores (created in embediq_engine_boot).
     * Safe to call: embediq_sem_destroy(NULL) is a no-op. */
    for (uint8_t i = 0u; i < EMBEDIQ_MAX_ENDPOINTS; i++) {
        if (g_dispatch_args[i].sem) {
            embediq_sem_destroy(g_dispatch_args[i].sem);
        }
    }
    memset(g_registry,      0, sizeof(g_registry));
    memset(g_boot_order,    0, sizeof(g_boot_order));
    memset(g_dispatch_args, 0, sizeof(g_dispatch_args));
    g_reg_count        = 0;
    g_boot_count       = 0;
    g_booted           = false;
    g_dispatch_running = false;
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
    dispatch_msg_to_fb(fb, msg_id, msg);
}

/** Return the dispatch semaphore for ep_id, or NULL if none (no subscriptions). */
embediq_sem_t fb_engine__dispatch_sem(uint8_t ep_id)
{
    if (ep_id >= EMBEDIQ_MAX_ENDPOINTS) return NULL;
    return g_dispatch_args[ep_id].sem;
}

#endif /* EMBEDIQ_PLATFORM_HOST */
