#define _POSIX_C_SOURCE 200809L

/*
 * tests/unit/test_fb_watchdog_boundaries.c — watchdog table-lock order and
 * resource failures, at the driver's boundary calls
 *
 * fbs/drivers/fb_watchdog.c is compiled into this executable with two calls
 * renamed (CMake source definitions on that file only):
 *   embediq_osal_mutex_lock -> wdg_boundary_mutex_lock
 *   hal_wdg_init            -> wdg_boundary_hal_init
 * The wrappers below delegate to the real functions. A wrapper can hold the
 * calling thread just before the real lock is taken (and signal that it is
 * held), fail one lock acquisition without locking, or fail one HAL
 * initialisation. The driver library is not linked, so the watchdog is
 * defined once: by this compiled copy.
 *
 * Cases:
 *   check-in starts first, locks second
 *                      a check-in is held before the table lock; the FB is
 *                      registered meanwhile; released, the check-in returns
 *                      OK — it observes the table as it is when it locks
 *   register starts first, locks second
 *                      a register is held before the table lock; a check-in
 *                      meanwhile returns ERR, NOT_REGISTERED; released, the
 *                      register returns OK and a check-in then returns OK
 *   lock acquisition fails
 *                      register (update), check-in and unregister each
 *                      return ERR with one UNAVAILABLE event naming the lock;
 *                      the entry keeps its timeout and its deadline
 *   HAL initialisation fails
 *                      boot with the real fb_watchdog: one UNAVAILABLE event
 *                      naming the HAL, no monitor, register refused as
 *                      NOT_READY
 *
 * Every wait is for an event (the held signal, a returned call); its time
 * bound is only a hang guard, and failing it fails the case.
 *
 * Run:      ./build/tests/unit/test_fb_watchdog_boundaries
 * Expected: "All N checks passed. (0 failed)"
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "embediq_fb.h"
#include "embediq_wdg.h"
#include "embediq_osal.h"
#include "embediq_obs.h"
#include "hal/hal_wdg.h"
#include "hal/hal_defs.h"

#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>     /* alarm */

/* Boundary wrappers: the compiled watchdog source calls these. */
embediq_err_t wdg_boundary_mutex_lock(EmbedIQ_Mutex_t *m, uint32_t timeout_ms);
int           wdg_boundary_hal_init(uint32_t timeout_ms);

/* Watchdog test hooks (compiled copy of fb_watchdog.c). */
extern void     wdg__init_state(void);
extern void     wdg__reset_service(void);
extern void     wdg__trigger_check(void);
extern uint32_t wdg__fault_count(void);
extern void     wdg__set_clock(uint32_t (*now_ms)(void));
extern uint32_t wdg__monitor_starts(void);
extern EmbedIQ_FB_Handle_t fb_watchdog_register(void);

/* Engine and Observatory test hooks. */
extern void fb_engine__reset(void);
extern void obs__reset(void);
extern bool obs__ring_read(uint32_t idx, EmbedIQ_Event_t *out);

/* Hang guard for every wait, and for the whole executable. */
#define WAIT_MS       2000u
#define HANG_GUARD_S  20u

static int g_checks = 0;
static int g_failed = 0;

#define CHECK(cond, msg)  do {                                                  \
    g_checks++;                                                                 \
    if (!(cond)) {                                                              \
        fprintf(stderr, "FAIL  %s: %s  %s:%d\n", __func__, (msg), __FILE__,     \
                __LINE__);                                                      \
        g_failed++;                                                             \
    } else {                                                                    \
        printf("PASS  %s: %s\n", __func__, (msg));                              \
    }                                                                           \
} while (0)

/* ---------------------------------------------------------------------------
 * Boundary wrappers
 *
 * g_gate guards the hold state. A thread marked with s_hold_me waits in the
 * lock wrapper, before the real lock, until released; it broadcasts that it
 * is held. The failure flags are armed and consumed under g_gate.
 * ------------------------------------------------------------------------- */

static pthread_mutex_t      g_gate = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t       g_cv   = PTHREAD_COND_INITIALIZER;
static bool                 g_held;
static bool                 g_released;
static bool                 g_fail_lock;
static bool                 g_fail_hal;
static _Thread_local bool   s_hold_me;

embediq_err_t wdg_boundary_mutex_lock(EmbedIQ_Mutex_t *m, uint32_t timeout_ms)
{
    pthread_mutex_lock(&g_gate);
    if (s_hold_me) {
        s_hold_me = false;
        g_held = true;
        pthread_cond_broadcast(&g_cv);
        while (!g_released) pthread_cond_wait(&g_cv, &g_gate);
    }
    bool fail = g_fail_lock;
    g_fail_lock = false;
    pthread_mutex_unlock(&g_gate);

    if (fail) return EMBEDIQ_ERR;
    return embediq_osal_mutex_lock(m, timeout_ms);
}

int wdg_boundary_hal_init(uint32_t timeout_ms)
{
    pthread_mutex_lock(&g_gate);
    bool fail = g_fail_hal;
    g_fail_hal = false;
    pthread_mutex_unlock(&g_gate);

    if (fail) return HAL_ERR_IO;
    return hal_wdg_init(timeout_ms);
}

static void gate_reset(void)
{
    pthread_mutex_lock(&g_gate);
    g_held = false;
    g_released = false;
    g_fail_lock = false;
    g_fail_hal = false;
    pthread_mutex_unlock(&g_gate);
}

/* Wait until a marked thread is held in the lock wrapper (hang guard only). */
static bool wait_held(void)
{
    struct timespec dl;
    clock_gettime(CLOCK_REALTIME, &dl);
    dl.tv_sec += WAIT_MS / 1000u;
    pthread_mutex_lock(&g_gate);
    int rc = 0;
    while (!g_held && rc != ETIMEDOUT) rc = pthread_cond_timedwait(&g_cv, &g_gate, &dl);
    bool held = g_held;
    pthread_mutex_unlock(&g_gate);
    return held;
}

static void release_held(void)
{
    pthread_mutex_lock(&g_gate);
    g_released = true;
    pthread_cond_broadcast(&g_cv);
    pthread_mutex_unlock(&g_gate);
}

static void arm_lock_failure(void)
{
    pthread_mutex_lock(&g_gate);
    g_fail_lock = true;
    pthread_mutex_unlock(&g_gate);
}

static void arm_hal_failure(void)
{
    pthread_mutex_lock(&g_gate);
    g_fail_hal = true;
    pthread_mutex_unlock(&g_gate);
}

/* ---------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */

#define ANY  (-1)

static uint32_t g_now_ms;
static uint32_t fake_now(void) { return g_now_ms; }

static EmbedIQ_FB_Handle_t app_fb(void)
{
    static const EmbedIQ_FB_Config_t cfg = {
        .name = "boundary_app", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
    };
    return embediq_fb_register(&cfg);
}

/* Fresh engine, empty Observatory ring, wrappers disarmed; with ready, the
 * service accepts calls (no monitor, scans by wdg__trigger_check()). */
static EmbedIQ_FB_Handle_t begin(bool ready)
{
    gate_reset();
    wdg__reset_service();
    fb_engine__reset();
    if (ready) wdg__init_state();
    obs__reset();
    embediq_obs_set_transport(EMBEDIQ_OBS_TRANSPORT_TCP);
    return app_fb();
}

static void end(void)
{
    gate_reset();
    wdg__reset_service();
    fb_engine__reset();
}

/* FAULT events with state_or_flag == state; src / msg_id matched unless ANY. */
static uint32_t fault_events(uint8_t state, int src, int msg_id)
{
    uint32_t n = 0u;
    EmbedIQ_Event_t e;
    for (uint32_t i = 0u; obs__ring_read(i, &e); i++) {
        if (e.event_type != EMBEDIQ_OBS_EVT_FAULT || e.state_or_flag != state) continue;
        if (src != ANY && e.source_fb_id != (uint8_t)src) continue;
        if (msg_id != ANY && e.msg_id != (uint16_t)msg_id) continue;
        n++;
    }
    return n;
}

/* One scan; returns the number of misses it reported. */
static uint32_t scan(void)
{
    uint32_t before = wdg__fault_count();
    wdg__trigger_check();
    return wdg__fault_count() - before;
}

typedef struct {
    EmbedIQ_FB_Handle_t fb;
    bool                checkin;   /* else register */
    embediq_err_t       rc;
} held_call_t;

static void *held_call(void *arg)
{
    held_call_t *c = arg;
    s_hold_me = true;
    c->rc = c->checkin ? embediq_wdg_checkin(c->fb)
                       : embediq_wdg_register(c->fb, 1000u);
    return NULL;
}

/* ---------------------------------------------------------------------------
 * Lock order
 * ------------------------------------------------------------------------- */

static void test_checkin_starts_first_locks_second(void)
{
    EmbedIQ_FB_Handle_t fb = begin(true);
    held_call_t c = { .fb = fb, .checkin = true, .rc = EMBEDIQ_ERR };

    pthread_t t;
    pthread_create(&t, NULL, held_call, &c);
    bool held = wait_held();
    embediq_err_t reg = held ? embediq_wdg_register(fb, 1000u) : EMBEDIQ_ERR;
    release_held();
    pthread_join(t, NULL);

    CHECK(held, "the check-in started and is held before the table lock");
    CHECK(reg == EMBEDIQ_OK, "register returns OK while the check-in is held");
    CHECK(c.rc == EMBEDIQ_OK, "released after the register returned: the check-in returns OK");
    CHECK(fault_events(EMBEDIQ_FAULT_WDG_NOT_REGISTERED, ANY, ANY) == 0u,
          "no NOT_REGISTERED event");
    end();
}

static void test_register_starts_first_locks_second(void)
{
    EmbedIQ_FB_Handle_t fb = begin(true);
    uint8_t ep = embediq_engine_fb_ep(fb);
    held_call_t r = { .fb = fb, .checkin = false, .rc = EMBEDIQ_ERR };

    pthread_t t;
    pthread_create(&t, NULL, held_call, &r);
    bool held = wait_held();
    embediq_err_t early = held ? embediq_wdg_checkin(fb) : EMBEDIQ_OK;
    uint32_t nr = fault_events(EMBEDIQ_FAULT_WDG_NOT_REGISTERED, ep, 0);
    release_held();
    pthread_join(t, NULL);
    embediq_err_t later = embediq_wdg_checkin(fb);

    CHECK(held, "the register started and is held before the table lock");
    CHECK(early == EMBEDIQ_ERR && nr == 1u,
          "a check-in meanwhile returns ERR with one NOT_REGISTERED event");
    CHECK(r.rc == EMBEDIQ_OK, "released: the register returns OK");
    CHECK(later == EMBEDIQ_OK, "a check-in after the register returned: OK");
    end();
}

/* ---------------------------------------------------------------------------
 * Resource failures
 * ------------------------------------------------------------------------- */

#define T0_MS          1000u
#define TIMEOUT_MS     100u

static void test_lock_acquisition_fails(void)
{
    EmbedIQ_FB_Handle_t fb = begin(true);
    uint8_t ep = embediq_engine_fb_ep(fb);
    wdg__set_clock(fake_now);
    g_now_ms = T0_MS;
    CHECK(embediq_wdg_register(fb, TIMEOUT_MS) == EMBEDIQ_OK, "registered at t0, timeout 100");

    /* Halfway through the window: a call that changed the table would move
     * or remove the deadline. */
    g_now_ms = T0_MS + 50u;
    static const char *const names[] = { "register (update to 500)", "check-in", "unregister" };
    for (int op = 0; op < 3; op++) {
        obs__reset();
        arm_lock_failure();
        embediq_err_t rc = op == 0 ? embediq_wdg_register(fb, 500u)
                         : op == 1 ? embediq_wdg_checkin(fb)
                                   : embediq_wdg_unregister(fb);
        char msg[160];
        snprintf(msg, sizeof(msg),
                 "%s: ERR with one UNAVAILABLE event naming the lock", names[op]);
        CHECK(rc == EMBEDIQ_ERR
              && fault_events(EMBEDIQ_FAULT_WDG_UNAVAILABLE, ep, EMBEDIQ_WDG_RESOURCE_LOCK) == 1u
              && fault_events(EMBEDIQ_FAULT_WDG_UNAVAILABLE, ANY, ANY) == 1u, msg);
    }

    g_now_ms = T0_MS + TIMEOUT_MS;
    CHECK(scan() == 0u, "at t0 + 100: no miss (the original window is not over)");
    g_now_ms = T0_MS + TIMEOUT_MS + 1u;
    CHECK(scan() == 1u, "at t0 + 101: exactly one miss (original timeout and deadline)");
    end();
}

static void test_hal_initialisation_fails(void)
{
    gate_reset();
    wdg__reset_service();
    fb_engine__reset();
    obs__reset();
    embediq_obs_set_transport(EMBEDIQ_OBS_TRANSPORT_TCP);
    EmbedIQ_FB_Handle_t wdg = fb_watchdog_register();
    EmbedIQ_FB_Handle_t fb  = app_fb();
    uint8_t wdg_ep = embediq_engine_fb_ep(wdg);
    uint8_t fb_ep  = embediq_engine_fb_ep(fb);

    arm_hal_failure();
    int boot = embediq_engine_boot();

    CHECK(boot == 0, "boot completes");
    CHECK(fault_events(EMBEDIQ_FAULT_WDG_UNAVAILABLE, wdg_ep, EMBEDIQ_WDG_RESOURCE_HAL) == 1u
          && fault_events(EMBEDIQ_FAULT_WDG_UNAVAILABLE, ANY, ANY) == 1u,
          "one UNAVAILABLE event naming the HAL");
    CHECK(wdg__monitor_starts() == 0u, "no monitor was started");
    CHECK(embediq_fb_get_state(wdg) == EMBEDIQ_FB_STATE_FAULT,
          "the watchdog FB reports its own fault");
    obs__reset();
    CHECK(embediq_wdg_register(fb, TIMEOUT_MS) == EMBEDIQ_ERR
          && fault_events(EMBEDIQ_FAULT_WDG_NOT_READY, fb_ep, 0) == 1u,
          "register returns ERR, NOT_READY");
    end();
}

/* ---------------------------------------------------------------------------
 * main
 * ------------------------------------------------------------------------- */

int main(void)
{
    alarm(HANG_GUARD_S);

    test_checkin_starts_first_locks_second();
    test_register_starts_first_locks_second();
    test_lock_acquisition_fails();
    test_hal_initialisation_fails();

    alarm(0u);
    printf("\n");
    if (g_failed == 0) {
        printf("All %d checks passed. (0 failed)\n", g_checks);
    } else {
        printf("%d of %d checks FAILED.\n", g_failed, g_checks);
    }
    return g_failed > 0 ? 1 : 0;
}
