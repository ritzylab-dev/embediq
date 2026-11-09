/*
 * fbs/drivers/fb_watchdog.c — Watchdog Driver FB (portable)
 *
 * Health-token model: callers register an FB handle with a timeout, then
 * periodically call embediq_wdg_checkin().  A background monitor thread
 * runs every 100 ms and checks that every registered FB has checked in
 * within its declared timeout window.
 *
 *   If ALL checked in:
 *     -> hal_wdg_kick() to reset the hardware watchdog
 *     -> embediq_obs_emit(EMBEDIQ_OBS_EVT_OVERFLOW, ...) if ring buffer full
 *   If ANY missed:
 *     -> embediq_obs_emit(EMBEDIQ_OBS_EVT_FAULT, ...)
 *     -> embediq_fb_report_fault(<missed FB>, WDG_REASON_MISSED_CHECKIN)
 *        — the missed check-in is the monitored FB's fault, not the watchdog's
 *     -> publish MSG_WDG_MISSED_CHECKIN with the missed FB's endpoint index
 *
 * On startup the FB publishes MSG_WDG_RESET_REASON.  On a host build the
 * reset reason is always RESET_NORMAL (0).
 *
 * Slot capacity: EMBEDIQ_MAX_ENDPOINTS entries (static, R-02).
 * All slot access is protected by a single mutex. The monitor scan decides
 * under the mutex and acts (emit, fault report, publish) after releasing it.
 *
 * Boot phase: EMBEDIQ_BOOT_PHASE_INFRASTRUCTURE (2)
 *
 * Public API:
 *   fb_watchdog_register()         — register FB with framework
 *   embediq_wdg_register(fb, ms)   — start tracking an FB
 *   embediq_wdg_checkin(fb)        — record a live heartbeat
 *   embediq_wdg_unregister(fb)     — stop tracking an FB
 *
 * Test-only API (EMBEDIQ_TEST_HOOKS):
 *   wdg__init_state()    — reset slots and counters (does not start task)
 *   wdg__trigger_check() — run one monitor scan synchronously
 *   wdg__fault_count()   — total missed-checkin faults since last init
 *   wdg__stop_monitor()  — stop the monitor task and wait for it to exit
 *
 * Zero POSIX headers.  R-02: no malloc in this file.
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "embediq_platform_msgs.h"
#include "embediq_fb.h"
#include "embediq_bus.h"
#include "embediq_msg.h"
#include "embediq_osal.h"
#include "embediq_config.h"
#include "embediq_obs.h"
#include "hal/hal_wdg.h"

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* ---------------------------------------------------------------------------
 * Internal types
 * ------------------------------------------------------------------------- */

#define WDG_MAX_SLOTS  EMBEDIQ_MAX_ENDPOINTS

/* Reason codes for embediq_fb_report_fault(). */
#define WDG_REASON_MISSED_CHECKIN  1u
#define WDG_REASON_RESET_DETECTED  2u

typedef struct {
    EmbedIQ_FB_Handle_t handle;
    uint32_t            timeout_ms;
    uint32_t            last_checkin_us;
    bool                active;
} wdg_slot_t;

/* One missed check-in, found under the mutex and acted on after unlock. */
typedef struct {
    EmbedIQ_FB_Handle_t handle;
    uint32_t            timeout_ms;
} wdg_miss_t;

/* ---------------------------------------------------------------------------
 * Module state (static, R-02)
 * ------------------------------------------------------------------------- */

static wdg_slot_t          g_slots[WDG_MAX_SLOTS];
static uint8_t             g_slot_count  = 0u;
static EmbedIQ_Mutex_t    *g_mutex       = NULL;
static EmbedIQ_Task_t     *g_task        = NULL;
static EmbedIQ_FB_Handle_t g_wdg_fb      = NULL;
static embediq_osal_atomic32_t g_running = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);
static uint32_t            g_kick_seq    = 0u;

#ifdef EMBEDIQ_TEST_HOOKS
static uint32_t            g_fault_count = 0u;
#endif

/* ---------------------------------------------------------------------------
 * Internal: one monitor scan cycle
 * Called by the background task every 100 ms, and by wdg__trigger_check().
 *
 * Calling context: the monitor task (a platform thread), or the test thread
 * via wdg__trigger_check(). Two phases:
 *   1. Under g_mutex: find every slot past its deadline and copy it into a
 *      stack array (WDG_MAX_SLOTS entries).
 *   2. After unlocking: emit, report each miss as the monitored FB's fault and
 *      publish MSG_WDG_MISSED_CHECKIN.
 * Nothing in phase 2 runs with g_mutex held. A HIGH publish into a full queue
 * blocks only this scan, never a check-in, and a monitored FB whose fault_fn
 * calls embediq_wdg_checkin() cannot deadlock against the monitor. The fault
 * report runs the monitored FB's fault_fn under its execution lock: on its
 * dispatch thread when it has one (a HIGH message), otherwise inline here
 * after any in-flight init_fn/run_fn/fault_fn of that FB.
 * ------------------------------------------------------------------------- */

static void monitor_scan(void)
{
    wdg_miss_t misses[WDG_MAX_SLOTS];
    uint8_t    n_miss = 0u;
    uint32_t   now_us = embediq_osal_time_us();

    /* Phase 1 — decide under the lock. */
    if (embediq_osal_mutex_lock(g_mutex, UINT32_MAX) != EMBEDIQ_OK) return;

    for (uint8_t i = 0u; i < g_slot_count; i++) {
        if (!g_slots[i].active) continue;

        /* uint32_t unsigned sub wraps correctly for intervals < ~35 min
         * (2^31 µs); max watchdog timeout is well under this. */
        uint32_t elapsed_us = now_us - g_slots[i].last_checkin_us;
        uint32_t timeout_us = g_slots[i].timeout_ms * 1000u;

        if (elapsed_us > timeout_us) {
            misses[n_miss].handle     = g_slots[i].handle;
            misses[n_miss].timeout_ms = g_slots[i].timeout_ms;
            n_miss++;
#ifdef EMBEDIQ_TEST_HOOKS
            g_fault_count++;
#endif
        }
    }

    embediq_osal_mutex_unlock(g_mutex);

    /* Phase 2 — act after unlock. */
    for (uint8_t i = 0u; i < n_miss; i++) {
        uint8_t ep = embediq_engine_fb_ep(misses[i].handle);

        /* Observatory fault event */
        embediq_obs_emit(EMBEDIQ_OBS_EVT_FAULT,
                         ep, 0xFFu, 0u, MSG_WDG_MISSED_CHECKIN);

        /* The missed check-in is the monitored FB's fault. */
        embediq_fb_report_fault(misses[i].handle, WDG_REASON_MISSED_CHECKIN);

        /* Publish MSG_WDG_MISSED_CHECKIN (payload byte 0 = missed ep). */
        if (g_wdg_fb) {
            EmbedIQ_Msg_t m;
            memset(&m, 0, sizeof(m));
            m.msg_id      = MSG_WDG_MISSED_CHECKIN;
            m.priority    = EMBEDIQ_MSG_PRIORITY_HIGH;
            m.payload[0]  = ep;
            m.payload_len = 1u;
            embediq_publish(g_wdg_fb, &m);
        }

#ifdef EMBEDIQ_PLATFORM_HOST
        embediq_obs_emit(EMBEDIQ_OBS_EVT_FAULT,
                         ep, 0xFFu, misses[i].timeout_ms,
                         MSG_WDG_MISSED_CHECKIN);
#endif
    }

    bool all_ok = n_miss == 0u;
    if (all_ok) {
        hal_wdg_kick();
        g_kick_seq++;
        /* XOBS-4: Emit TIMING heartbeat on every successful kick.
         * Gated by EMBEDIQ_TRACE_TIMING — expands to ((void)0) when
         * EMBEDIQ_TRACE_LEVEL < 2, so zero overhead in production builds.
         * g_wdg_fb is NULL when wdg__trigger_check() is called from
         * tests that bypass wdg_init() — guard with 0xFFu sentinel. */
        {
            uint8_t const wdg_src_id = g_wdg_fb
                                       ? embediq_engine_fb_ep(g_wdg_fb)
                                       : 0xFFu;
            EMBEDIQ_OBS_EMIT_TIMING(EMBEDIQ_OBS_EVT_WDG_CHECKIN,
                                    wdg_src_id, 0xFFu,
                                    (uint8_t)(g_kick_seq & 0xFFu), 0u);
        }
    }
}

/* ---------------------------------------------------------------------------
 * Background monitor task
 *
 * Calling context: platform thread created by wdg_init(). Runs one two-phase
 * monitor_scan() every 100 ms until g_running is cleared.
 * ------------------------------------------------------------------------- */

static void monitor_task(void *arg)
{
    (void)arg;
    while (embediq_osal_atomic32_load(&g_running) != 0u) {
        embediq_osal_delay_ms(100u);
        if (embediq_osal_atomic32_load(&g_running) != 0u) {
            monitor_scan();
        }
    }
}

/* ---------------------------------------------------------------------------
 * FB init / exit (called by embediq_engine_boot)
 * ------------------------------------------------------------------------- */

static void wdg_init(EmbedIQ_FB_Handle_t fb, void *fb_data)
{
    (void)fb_data;
    g_wdg_fb = fb;

    /* Create mutex if not already created (allows test-bypass path too). */
    if (!g_mutex) {
        g_mutex = embediq_osal_mutex_create();
    }

    /* Initialise the HAL watchdog with a generous timeout. */
    hal_wdg_init(5000u);

    /* Publish reset reason (NORMAL = 0 on host). */
    EmbedIQ_Msg_t m;
    memset(&m, 0, sizeof(m));
    m.msg_id      = MSG_WDG_RESET_REASON;
    m.priority    = EMBEDIQ_MSG_PRIORITY_NORMAL;
    m.payload[0]  = 0u;   /* RESET_NORMAL */
    m.payload_len = 1u;
    embediq_publish(g_wdg_fb, &m);

    /* Start background monitor. */
    embediq_osal_atomic32_store(&g_running, 1u);
    g_task = embediq_osal_task_create("fb_wdg_mon", monitor_task, NULL,
                                     EMBEDIQ_WDG_TASK_PRIORITY,
                                     EMBEDIQ_WDG_TASK_STACK_SIZE);
}

static void wdg_exit(EmbedIQ_FB_Handle_t fb, void *fb_data)
{
    (void)fb; (void)fb_data;
    embediq_osal_atomic32_store(&g_running, 0u);
    hal_wdg_deinit();
}

/* ---------------------------------------------------------------------------
 * Public: fb_watchdog_register()
 * Call before embediq_engine_boot().
 * ------------------------------------------------------------------------- */

EMBEDIQ_PUBS(g_wdg_pubs,
    MSG_WDG_MISSED_CHECKIN,
    MSG_WDG_RESET_REASON);

EmbedIQ_FB_Handle_t fb_watchdog_register(void)
{
    static const EmbedIQ_FB_Config_t k_cfg = {
        .name              = "fb_watchdog",
        .boot_phase        = EMBEDIQ_BOOT_PHASE_INFRASTRUCTURE,
        .init_fn           = wdg_init,
        .exit_fn           = wdg_exit,
        .publications      = g_wdg_pubs,
        .publication_count = 2u,
    };
    return embediq_fb_register(&k_cfg);
}

/* ---------------------------------------------------------------------------
 * Public watchdog API
 * ------------------------------------------------------------------------- */

/**
 * Register an FB for health monitoring.
 * The FB's last-checkin timestamp is initialised to now, granting a full
 * timeout_ms window before the first check fires.
 */
void embediq_wdg_register(EmbedIQ_FB_Handle_t fb, uint32_t timeout_ms)
{
    if (!fb || timeout_ms == 0u) return;

    if (!g_mutex) {
        g_mutex = embediq_osal_mutex_create();
    }

    embediq_osal_mutex_lock(g_mutex, UINT32_MAX);

    /* Check if already registered. */
    for (uint8_t i = 0u; i < g_slot_count; i++) {
        if (g_slots[i].handle == fb && g_slots[i].active) {
            g_slots[i].timeout_ms      = timeout_ms;
            g_slots[i].last_checkin_us = embediq_osal_time_us();
            embediq_osal_mutex_unlock(g_mutex);
            return;
        }
    }

    /* Find a free slot. */
    if (g_slot_count < WDG_MAX_SLOTS) {
        g_slots[g_slot_count].handle          = fb;
        g_slots[g_slot_count].timeout_ms      = timeout_ms;
        g_slots[g_slot_count].last_checkin_us = embediq_osal_time_us();
        g_slots[g_slot_count].active          = true;
        g_slot_count++;
    }

    embediq_osal_mutex_unlock(g_mutex);
}

/** Record a heartbeat for the given FB. */
void embediq_wdg_checkin(EmbedIQ_FB_Handle_t fb)
{
    if (!fb || !g_mutex) return;

    embediq_osal_mutex_lock(g_mutex, UINT32_MAX);

    for (uint8_t i = 0u; i < g_slot_count; i++) {
        if (g_slots[i].handle == fb && g_slots[i].active) {
            g_slots[i].last_checkin_us = embediq_osal_time_us();
            break;
        }
    }

    embediq_osal_mutex_unlock(g_mutex);
}

/** Remove an FB from watchdog tracking. */
void embediq_wdg_unregister(EmbedIQ_FB_Handle_t fb)
{
    if (!fb || !g_mutex) return;

    embediq_osal_mutex_lock(g_mutex, UINT32_MAX);

    for (uint8_t i = 0u; i < g_slot_count; i++) {
        if (g_slots[i].handle == fb && g_slots[i].active) {
            /* Shift remaining slots down to keep array contiguous. */
            for (uint8_t j = i; j < g_slot_count - 1u; j++) {
                g_slots[j] = g_slots[j + 1u];
            }
            memset(&g_slots[g_slot_count - 1u], 0, sizeof(wdg_slot_t));
            g_slot_count--;
            break;
        }
    }

    embediq_osal_mutex_unlock(g_mutex);
}

/* ---------------------------------------------------------------------------
 * Test-only API — EMBEDIQ_TEST_HOOKS only
 * ------------------------------------------------------------------------- */

#ifdef EMBEDIQ_TEST_HOOKS

/**
 * Reset all watchdog state.
 * Does NOT start the monitor task — tests call wdg__trigger_check() directly.
 * Creates the mutex if it has not been created yet.
 */
void wdg__init_state(void)
{
    embediq_osal_atomic32_store(&g_running, 0u);
    g_slot_count  = 0u;
    g_kick_seq    = 0u;
    g_fault_count = 0u;
    g_wdg_fb      = NULL;
    memset(g_slots, 0, sizeof(g_slots));

    if (!g_mutex) {
        g_mutex = embediq_osal_mutex_create();
    }
}

/** Synchronously run one monitor scan cycle (bypasses 100 ms timer). */
void wdg__trigger_check(void)
{
    monitor_scan();
}

/** Total missed-checkin faults detected since last wdg__init_state(). */
uint32_t wdg__fault_count(void)
{
    return g_fault_count;
}

/** Stop the monitor task started by wdg_init() and wait for it to exit.
 *  Call from the thread that booted the engine. */
void wdg__stop_monitor(void)
{
    embediq_osal_atomic32_store(&g_running, 0u);
    if (g_task != NULL) {
        embediq_osal_task_join(g_task);
        g_task = NULL;
    }
}

#endif /* EMBEDIQ_TEST_HOOKS */
