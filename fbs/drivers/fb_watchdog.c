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
 *   If ANY missed:
 *     -> embediq_obs_emit(EMBEDIQ_OBS_EVT_FAULT, ...)
 *     -> embediq_fb_report_fault(<missed FB>, WDG_REASON_MISSED_CHECKIN)
 *        — the missed check-in is the monitored FB's fault, not the watchdog's
 *     -> publish MSG_WDG_MISSED_CHECKIN with the missed FB's endpoint index
 *
 * The public contract (calling context, units, results, observations) is in
 * core/include/embediq_wdg.h; this file includes it, so every definition is
 * checked against it.
 *
 * Service readiness:
 *   g_ready is the only thing a public call reads before it holds the table
 *   lock. wdg_init() prepares the lock, the HAL watchdog and one monitor on
 *   the boot thread, and only then stores g_ready = 1 (a sequentially
 *   consistent OSAL atomic): a call that reads 1 also sees the lock and the
 *   watchdog FB handle. Until then every call returns EMBEDIQ_ERR and touches
 *   nothing. No public call creates anything.
 *   g_mutex, g_wdg_fb and g_task are written only by the boot thread (or a
 *   test hook) while g_ready is 0 or while no monitor is owned.
 *
 * Initializer run again (a boot retry after a later FB failed): a monitor is
 * already owned, so nothing is created and nothing the monitor or a caller
 * reads is rewritten. After a failed preparation the next run starts again,
 * reusing a table lock an earlier attempt created.
 *
 * Observatory events emitted by the service (embediq_wdg.h codes):
 *   EMBEDIQ_OBS_EVT_FAULT, state EMBEDIQ_FAULT_WDG_UNAVAILABLE, source = this
 *   FB, msg_id = EMBEDIQ_WDG_RESOURCE_* — preparation failed; the FB then
 *   reports its own fault (WDG_REASON_SERVICE_UNAVAILABLE).
 *   EMBEDIQ_OBS_EVT_FAULT, state EMBEDIQ_FAULT_WDG_* — a rejected call, one
 *   per call, emitted after the table lock is released.
 *
 * Slot capacity: EMBEDIQ_MAX_ENDPOINTS entries (static, R-02) — one per FB
 * handle the engine can hand out. All slot access is protected by a single
 * mutex. The monitor scan decides under the mutex and acts (emit, fault
 * report, publish) after releasing it.
 *
 * Boot phase: EMBEDIQ_BOOT_PHASE_INFRASTRUCTURE (2)
 *
 * Public API:
 *   fb_watchdog_register()         — register FB with framework
 *   embediq_wdg_register(fb, ms)   — start (or update) tracking an FB
 *   embediq_wdg_checkin(fb)        — record a live heartbeat
 *   embediq_wdg_unregister(fb)     — stop tracking an FB
 *
 * Test-only API (EMBEDIQ_TEST_HOOKS):
 *   wdg__init_state()      — test service state: ready, no monitor
 *   wdg__reset_service()   — the state of a fresh process
 *   wdg__trigger_check()   — run one monitor scan synchronously
 *   wdg__fault_count()     — total missed-checkin faults since last reset
 *   wdg__stop_monitor()    — not ready; stop and join the monitor task
 *   wdg__set_clock()       — replace the millisecond clock
 *   wdg__set_scan_hook()   — run a function between selection and report
 *   wdg__set_capacity()    — accept at most n entries
 *   wdg__monitor_starts()  — monitor tasks created since the last reset
 *
 * Zero POSIX headers.  R-02: no malloc in this file.
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "embediq_wdg.h"
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
#define WDG_REASON_MISSED_CHECKIN       1u
#define WDG_REASON_RESET_DETECTED       2u
#define WDG_REASON_SERVICE_UNAVAILABLE  3u   /* the watchdog FB could not prepare its service */

/* HAL watchdog timeout. The driver feeds the HAL watchdog only at the end of
 * a scan in which every registered FB was healthy; a scan that reports a
 * miss, or a scan delayed because the HIGH publish of a miss is waiting for
 * queue space, does not feed it. On a target with a real hardware watchdog
 * enabled that timer may therefore expire while feeding has stopped. The
 * POSIX HAL simulates the timer and performs no reset. */
#define WDG_HAL_TIMEOUT_MS  5000u

/* Endpoint value meaning "no FB" in an Observatory event. */
#define WDG_EP_NONE  0xFFu

typedef struct {
    EmbedIQ_FB_Handle_t handle;
    uint32_t            timeout_ms;
    uint32_t            last_checkin_ms;
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
static embediq_osal_atomic32_t g_ready   = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);
static uint32_t            g_kick_seq    = 0u;

#ifdef EMBEDIQ_TEST_HOOKS
/* Test instrumentation. Each is written by a test hook only while no
 * monitor task runs, or (g_fault_count) under the table lock. */
static uint32_t            g_fault_count = 0u;
static uint32_t          (*s_wdg_clock)(void)   = embediq_osal_time_ms;
static void              (*s_wdg_scan_hook)(void) = NULL;
static uint8_t             s_wdg_capacity       = WDG_MAX_SLOTS;
static uint32_t            s_wdg_monitor_starts = 0u;
#endif

/* ---------------------------------------------------------------------------
 * Internal helpers
 * ------------------------------------------------------------------------- */

static uint32_t now_ms(void)
{
#ifdef EMBEDIQ_TEST_HOOKS
    return s_wdg_clock();
#else
    return embediq_osal_time_ms();
#endif
}

static uint8_t capacity(void)
{
#ifdef EMBEDIQ_TEST_HOOKS
    return s_wdg_capacity;
#else
    return WDG_MAX_SLOTS;
#endif
}

static bool service_ready(void)
{
    return embediq_osal_atomic32_load(&g_ready) != 0u;
}

/* Index of fb's active slot, or -1. Caller holds g_mutex. */
static int find_slot(EmbedIQ_FB_Handle_t fb)
{
    for (uint8_t i = 0u; i < g_slot_count; i++) {
        if (g_slots[i].handle == fb && g_slots[i].active) return (int)i;
    }
    return -1;
}

/* One FAULT event for a rejected call; then EMBEDIQ_ERR. Called with no lock
 * held. The source endpoint comes from embediq_engine_fb_ep(), which returns
 * 0xFF for NULL and reads the endpoint of a valid framework handle; any other
 * pointer is outside the contract (see embediq_wdg.h). */
static embediq_err_t reject(EmbedIQ_FB_Handle_t fb, uint8_t code, uint16_t resource)
{
    uint8_t target = WDG_EP_NONE;
    if (service_ready() && g_wdg_fb != NULL) {
        target = embediq_engine_fb_ep(g_wdg_fb);
    }
    EMBEDIQ_OBS_EMIT_FAULT(EMBEDIQ_OBS_EVT_FAULT, embediq_engine_fb_ep(fb), target,
                           code, resource);
    return EMBEDIQ_ERR;
}

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
 * A miss selected in phase 1 is reported in phase 2 even if the entry was
 * unregistered, re-registered or checked in between the two.
 * ------------------------------------------------------------------------- */

static void monitor_scan(void)
{
    wdg_miss_t misses[WDG_MAX_SLOTS];
    uint8_t    n_miss = 0u;

    /* Phase 1 — decide under the lock. No lock, or none taken: no scan. */
    if (g_mutex == NULL) return;
    if (embediq_osal_mutex_lock(g_mutex, UINT32_MAX) != EMBEDIQ_OK) return;

    uint32_t now = now_ms();
    for (uint8_t i = 0u; i < g_slot_count; i++) {
        if (!g_slots[i].active) continue;

        /* Unsigned subtraction is wrap-correct while the true gap is below
         * UINT32_MAX / 2 ms — the supported timeout range (embediq_wdg.h). */
        uint32_t elapsed_ms = now - g_slots[i].last_checkin_ms;

        if (elapsed_ms > g_slots[i].timeout_ms) {
            misses[n_miss].handle     = g_slots[i].handle;
            misses[n_miss].timeout_ms = g_slots[i].timeout_ms;
            n_miss++;
#ifdef EMBEDIQ_TEST_HOOKS
            g_fault_count++;
#endif
        }
    }

    embediq_osal_mutex_unlock(g_mutex);

#ifdef EMBEDIQ_TEST_HOOKS
    if (s_wdg_scan_hook != NULL) s_wdg_scan_hook();
#endif

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

/* Preparation failed: observe it at the watchdog's own granularity (the
 * OSAL/HAL layer's own event, if any, is not suppressed), then report the
 * watchdog FB's own fault. g_ready stays 0. */
static void wdg_init_failed(EmbedIQ_FB_Handle_t fb, uint16_t resource)
{
    EMBEDIQ_OBS_EMIT_FAULT(EMBEDIQ_OBS_EVT_FAULT, embediq_engine_fb_ep(fb), WDG_EP_NONE,
                           EMBEDIQ_FAULT_WDG_UNAVAILABLE, resource);
    embediq_fb_report_fault(fb, WDG_REASON_SERVICE_UNAVAILABLE);
}

static void wdg_init(EmbedIQ_FB_Handle_t fb, void *fb_data)
{
    (void)fb_data;

    /* Run again on a boot retry with a monitor already owned: it may be
     * reading g_wdg_fb and the table — create nothing, rewrite nothing, and
     * do not publish the reset reason a second time. */
    if (g_task != NULL) return;

    g_wdg_fb = fb;

    /* The table lock; one left by an earlier failed attempt is reused. */
    if (g_mutex == NULL) {
        g_mutex = embediq_osal_mutex_create();
        if (g_mutex == NULL) {
            wdg_init_failed(fb, EMBEDIQ_WDG_RESOURCE_MUTEX);
            return;
        }
    }

    /* The HAL watchdog. The POSIX simulation cannot fail. */
    if (hal_wdg_init(WDG_HAL_TIMEOUT_MS) != HAL_OK) {
        wdg_init_failed(fb, EMBEDIQ_WDG_RESOURCE_HAL);
        return;
    }

    /* One monitor. */
    embediq_osal_atomic32_store(&g_running, 1u);
    g_task = embediq_osal_task_create("fb_wdg_mon", monitor_task, NULL,
                                     EMBEDIQ_WDG_TASK_PRIORITY,
                                     EMBEDIQ_WDG_TASK_STACK_SIZE);
    if (g_task == NULL) {
        embediq_osal_atomic32_store(&g_running, 0u);
        hal_wdg_deinit();
        wdg_init_failed(fb, EMBEDIQ_WDG_RESOURCE_MONITOR);
        return;
    }
#ifdef EMBEDIQ_TEST_HOOKS
    s_wdg_monitor_starts++;
#endif

    /* Ready: every field a call reads is in place. */
    embediq_osal_atomic32_store(&g_ready, 1u);

    /* Publish reset reason (NORMAL = 0 on host). */
    EmbedIQ_Msg_t m;
    memset(&m, 0, sizeof(m));
    m.msg_id      = MSG_WDG_RESET_REASON;
    m.priority    = EMBEDIQ_MSG_PRIORITY_NORMAL;
    m.payload[0]  = 0u;   /* RESET_NORMAL */
    m.payload_len = 1u;
    embediq_publish(g_wdg_fb, &m);
}

static void wdg_exit(EmbedIQ_FB_Handle_t fb, void *fb_data)
{
    (void)fb; (void)fb_data;
    embediq_osal_atomic32_store(&g_ready, 0u);
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
 * Public watchdog API (embediq_wdg.h)
 *
 * Common shape: argument checks; readiness; take the table lock (the table is
 * untouched if that fails); change the table; release the lock; then, for a
 * failure only, emit one event and return EMBEDIQ_ERR.
 * ------------------------------------------------------------------------- */

embediq_err_t embediq_wdg_register(EmbedIQ_FB_Handle_t fb, uint32_t timeout_ms)
{
    if (fb == NULL
        || timeout_ms < EMBEDIQ_WDG_TIMEOUT_MS_MIN
        || timeout_ms > EMBEDIQ_WDG_TIMEOUT_MS_MAX) {
        return reject(fb, EMBEDIQ_FAULT_WDG_INVALID_ARG, 0u);
    }
    if (!service_ready()) {
        return reject(fb, EMBEDIQ_FAULT_WDG_NOT_READY, 0u);
    }
    if (embediq_osal_mutex_lock(g_mutex, UINT32_MAX) != EMBEDIQ_OK) {
        return reject(fb, EMBEDIQ_FAULT_WDG_UNAVAILABLE, EMBEDIQ_WDG_RESOURCE_LOCK);
    }

    uint8_t code = 0u;
    int     idx  = find_slot(fb);
    if (idx >= 0) {
        /* Update: replace the timeout and restart the window. */
        g_slots[idx].timeout_ms      = timeout_ms;
        g_slots[idx].last_checkin_ms = now_ms();
    } else if (g_slot_count >= capacity()) {
        code = EMBEDIQ_FAULT_WDG_TABLE_FULL;
    } else {
        g_slots[g_slot_count].handle          = fb;
        g_slots[g_slot_count].timeout_ms      = timeout_ms;
        g_slots[g_slot_count].last_checkin_ms = now_ms();
        g_slots[g_slot_count].active          = true;
        g_slot_count++;
    }

    embediq_osal_mutex_unlock(g_mutex);
    return code == 0u ? EMBEDIQ_OK : reject(fb, code, 0u);
}

embediq_err_t embediq_wdg_checkin(EmbedIQ_FB_Handle_t fb)
{
    if (fb == NULL) {
        return reject(fb, EMBEDIQ_FAULT_WDG_INVALID_ARG, 0u);
    }
    if (!service_ready()) {
        return reject(fb, EMBEDIQ_FAULT_WDG_NOT_READY, 0u);
    }
    if (embediq_osal_mutex_lock(g_mutex, UINT32_MAX) != EMBEDIQ_OK) {
        return reject(fb, EMBEDIQ_FAULT_WDG_UNAVAILABLE, EMBEDIQ_WDG_RESOURCE_LOCK);
    }

    int idx = find_slot(fb);
    if (idx >= 0) {
        g_slots[idx].last_checkin_ms = now_ms();
    }

    embediq_osal_mutex_unlock(g_mutex);
    return idx >= 0 ? EMBEDIQ_OK : reject(fb, EMBEDIQ_FAULT_WDG_NOT_REGISTERED, 0u);
}

embediq_err_t embediq_wdg_unregister(EmbedIQ_FB_Handle_t fb)
{
    if (fb == NULL) {
        return reject(fb, EMBEDIQ_FAULT_WDG_INVALID_ARG, 0u);
    }
    if (!service_ready()) {
        return reject(fb, EMBEDIQ_FAULT_WDG_NOT_READY, 0u);
    }
    if (embediq_osal_mutex_lock(g_mutex, UINT32_MAX) != EMBEDIQ_OK) {
        return reject(fb, EMBEDIQ_FAULT_WDG_UNAVAILABLE, EMBEDIQ_WDG_RESOURCE_LOCK);
    }

    int idx = find_slot(fb);
    if (idx >= 0) {
        /* Shift remaining slots down to keep the array contiguous. */
        for (uint8_t j = (uint8_t)idx; j < g_slot_count - 1u; j++) {
            g_slots[j] = g_slots[j + 1u];
        }
        memset(&g_slots[g_slot_count - 1u], 0, sizeof(wdg_slot_t));
        g_slot_count--;
    }

    embediq_osal_mutex_unlock(g_mutex);
    return idx >= 0 ? EMBEDIQ_OK : reject(fb, EMBEDIQ_FAULT_WDG_NOT_REGISTERED, 0u);
}

/* ---------------------------------------------------------------------------
 * Test-only API — EMBEDIQ_TEST_HOOKS only
 *
 * Calling context: the test thread. Each hook that changes the service is
 * called while no other thread calls the watchdog, and while no monitor
 * task runs unless it stops that task itself.
 * ------------------------------------------------------------------------- */

#ifdef EMBEDIQ_TEST_HOOKS

/** Stop the monitor task started by wdg_init() and wait for it to exit.
 *  The service is not ready from the first step. Call from the thread that
 *  booted the engine. */
void wdg__stop_monitor(void)
{
    embediq_osal_atomic32_store(&g_ready, 0u);
    embediq_osal_atomic32_store(&g_running, 0u);
    if (g_task != NULL) {
        embediq_osal_task_join(g_task);
        g_task = NULL;
    }
}

/**
 * The state of a fresh process: no monitor (stopped and joined), not ready,
 * no table lock, empty table, counters cleared, default clock, no scan hook,
 * full capacity, no watchdog FB handle.
 */
void wdg__reset_service(void)
{
    wdg__stop_monitor();
    embediq_osal_mutex_destroy(g_mutex);   /* NULL-safe */
    g_mutex              = NULL;
    g_wdg_fb             = NULL;
    g_slot_count         = 0u;
    g_kick_seq           = 0u;
    g_fault_count        = 0u;
    s_wdg_clock          = embediq_osal_time_ms;
    s_wdg_scan_hook      = NULL;
    s_wdg_capacity       = WDG_MAX_SLOTS;
    s_wdg_monitor_starts = 0u;
    memset(g_slots, 0, sizeof(g_slots));
}

/**
 * Test service state: calls are accepted and scans are driven by
 * wdg__trigger_check(); production readiness is set only by wdg_init().
 * wdg__reset_service(), then create the table lock, then ready. No monitor.
 */
void wdg__init_state(void)
{
    wdg__reset_service();
    g_mutex = embediq_osal_mutex_create();
    if (g_mutex != NULL) {
        embediq_osal_atomic32_store(&g_ready, 1u);
    }
}

/** Synchronously run one monitor scan cycle (bypasses 100 ms timer). */
void wdg__trigger_check(void)
{
    monitor_scan();
}

/** Total missed-checkin faults detected since the last reset. */
uint32_t wdg__fault_count(void)
{
    return g_fault_count;
}

/** Replace the millisecond clock the table and the scan read; NULL restores
 *  embediq_osal_time_ms. */
void wdg__set_clock(uint32_t (*clock_ms)(void))
{
    s_wdg_clock = clock_ms != NULL ? clock_ms : embediq_osal_time_ms;
}

/** Called by monitor_scan() after it released the table lock and before it
 *  reports what it selected; NULL clears. */
void wdg__set_scan_hook(void (*after_snapshot)(void))
{
    s_wdg_scan_hook = after_snapshot;
}

/** The table accepts at most n entries (n <= WDG_MAX_SLOTS); 0 restores
 *  WDG_MAX_SLOTS. */
void wdg__set_capacity(uint8_t n)
{
    s_wdg_capacity = (n == 0u || n > WDG_MAX_SLOTS) ? (uint8_t)WDG_MAX_SLOTS : n;
}

/** Monitor tasks wdg_init() has created since the last reset. */
uint32_t wdg__monitor_starts(void)
{
    return s_wdg_monitor_starts;
}

#endif /* EMBEDIQ_TEST_HOOKS */
