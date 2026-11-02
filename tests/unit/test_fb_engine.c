#define _POSIX_C_SOURCE 200809L

/*
 * tests/unit/test_fb_engine.c — Unit tests for core FB engine
 *
 * Covers every contractual behaviour of core/src/fb_engine.c.
 * Zero external test framework — stdlib only.
 * Each test exercises exactly one behaviour with one ASSERT.
 *
 * Run:      ./build/tests/unit/test_fb_engine
 * Expected: "All 32 tests passed. (0 failed)"
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "embediq_fb.h"
#include "embediq_subfn.h"
#include "embediq_bus.h"
#include "embediq_msg.h"
#include "embediq_osal.h"
#include "embediq_config.h"

#include <pthread.h>
#include <unistd.h>     /* alarm */
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

/* ---------------------------------------------------------------------------
 * Test-only API provided by fb_engine.c (EMBEDIQ_PLATFORM_HOST builds only)
 * ------------------------------------------------------------------------- */

extern void     fb_engine__reset(void);
extern uint32_t fb_engine__obs_event_count(void);
extern void     fb_engine__deliver_msg(EmbedIQ_FB_Handle_t handle,
                                       uint16_t msg_id, const void *msg);
extern bool     obs__ring_read(uint32_t idx, EmbedIQ_Event_t *out);
/* Weak fallback so this file links before fb_engine.c provides the hook
 * (TDD-first commit); the engine's strong definition replaces it. */
__attribute__((weak)) bool fb_engine__exec_lock_held(uint8_t ep) { (void)ep; return false; }
/* Host-only: called by report_fault between accepting a report and acting
 * on it, so a test can hold a reporter there. Weak no-op fallbacks as above. */
typedef void (*fb_engine_report_hook_t)(void);
__attribute__((weak)) void fb_engine__set_report_delay_hook(fb_engine_report_hook_t fn) { (void)fn; }
/* Host-only: total reports counted as inbox overflow since the last reset. */
__attribute__((weak)) uint32_t fb_engine__fault_overflow_total(void) { return 0u; }
/* Host-only OSAL failure injection (osal/posix): the nth task_create from
 * now returns NULL. */
__attribute__((weak)) void embediq_osal__fail_next_task_create(uint32_t nth) { (void)nth; }

#ifndef EMBEDIQ_FB_FAULT_INBOX_DEPTH
#  define EMBEDIQ_FB_FAULT_INBOX_DEPTH  EMBEDIQ_FB_FAULT_PENDING_DEPTH
#endif
#ifndef EMBEDIQ_FAULT_BOOT_ABORTED
#  define EMBEDIQ_FAULT_BOOT_ABORTED    0x82u
#endif
/* Host-only OSAL failure injection (osal/posix): the nth mutex_create from
 * now returns NULL. */
extern void     embediq_osal__fail_next_mutex_create(uint32_t nth);

/* ---------------------------------------------------------------------------
 * Fault delivery and engine API constants
 * ------------------------------------------------------------------------- */

#define MSG_TEST_PING          0x1430u  /* community range, unique to this file */
#define MSG_TEST_BURST         0x1431u
#define FAULT_REASON_TEST      0x55u
#define DISPATCH_WAIT_MS       200u
#define GETSTATE_THREADS       4u
#define GETSTATE_ITERS         20000u
#define BURST_COUNT            50u
#define BURST_SPACING_MS       2u
#define BURST_WAIT_MS          500u
#define API_TEST_FBS           3u

/* Inline fault serialisation constants */
#define FAULT_HOLD_LONG_MS     100u     /* fault_fn hold time: widens any overlap */
#define FAULT_REPORTERS        2u
#define MSG_TEST_SUB           0x1432u  /* community range, unique to this file */

/* Execution-ownership constants (init, dispatch, nested faults, boot resource) */
#define MSG_TEST_EXEC          0x1433u  /* community range, unique to this file */
#define GATE_MAX_MS            2000u    /* upper bound on any gate wait */
#define RUN_SILENT_SAMPLE_MS   100u     /* run_fn must stay silent this long */
#define INIT_BUSY_MS           50u      /* init_fn keeps using fb_data */
#define FAULT_HOLD_SHORT_MS    10u
#define FB_DATA_WORDS          2u
#define FAIL_NTH_MUTEX         2u       /* second lock allocation fails */
#define REASON_CAP             12u
#define NESTED_OVERFLOW_OUTER  10u
#define NESTED_OVERFLOW_EXTRA  2u       /* reports beyond the inbox depth */
#define DISPATCH_COST_MSGS     100000u
#define DISPATCH_COST_MAX_NS   2000u    /* < 2 us per dispatch over baseline */

/* Dispatch shutdown constants */
#define MSG_TEST_GATED           0x1434u  /* community range, unique to this file */
#define MSG_TEST_FILL            0x1435u
#define MSG_TEST_IDLE            0x1436u
#define FAULT_REASON_QUEUED      0x61u
#define FAULT_REASON_DURING      0x62u
#define FAULT_REASON_FULL_QUEUE  0x63u
#define FAULT_REASON_STORM       0x64u
#define SHUTDOWN_START_SETTLE_MS 50u      /* shutdown flag set before the late report */
#define STORM_REPORTERS          4u
#define STORM_RUN_MS             20u
#define STORM_NORMAL_MSGS        20u      /* below the NORMAL depth: none drop-oldest */
#define SHUTDOWN_MAX_MS          500u
#define SHUTDOWN_HANG_GUARD_S    2u       /* alarm(): a hang fails the binary */
#define REPORT_RETURN_WAIT_MS    500u     /* well inside the handler's gate bound */

/* Fault inbox constants */
#define MSG_TEST_CROSS           0x1437u  /* community range, unique to this file */
#define MSG_TEST_HOLD            0x1438u
#define FAULT_REASON_BEFORE_INIT 0x65u
#define FAULT_REASON_DELAYED     0x66u
#define FAULT_REASON_CROSS_FIRST 0x67u
#define FAULT_REASON_CROSS_OTHER 0x68u
#define FAULT_REASON_OUTER       0x69u
#define FAULT_REASON_ON_HELD     0x6Au
#define FAULT_REASON_OVERFLOW    0x6Bu
#define FAULT_REASON_ABORTED     0x6Cu
#define FAULT_REASON_NO_THREAD   0x6Du
#define MSG_TEST_NO_THREAD       0x1439u
#define CROSS_FBS                2u
#define CROSS_COMPLETE_MAX_MS    500u
#define OTHER_FB_REPORT_MAX_US   10000u   /* a report on a busy FB returns within 10 ms */
#define OVERFLOW_REPORTERS       6u
#define OVERFLOW_REASONS_EACH    3u
#define OVERFLOW_REPORTS         (OVERFLOW_REPORTERS * OVERFLOW_REASONS_EACH)

/* ---------------------------------------------------------------------------
 * Minimal test harness. Every test joins the reporter and platform threads it
 * starts before the next fb_engine__reset(): no reporter may be alive at reset.
 * ------------------------------------------------------------------------- */

static int g_tests_run    = 0;
static int g_tests_failed = 0;

#define ASSERT(cond, msg)  do {                                                \
    g_tests_run++;                                                             \
    if (!(cond)) {                                                             \
        fprintf(stderr, "FAIL  %-56s  %s:%d\n", __func__, __FILE__, __LINE__);\
        g_tests_failed++;                                                      \
    } else {                                                                   \
        printf("PASS  %s\n", __func__);                                        \
    }                                                                          \
} while (0)

/* ---------------------------------------------------------------------------
 * test_fb_register_stores_config
 * embediq_fb_register() must return a non-NULL handle.
 * ------------------------------------------------------------------------- */

static void test_fb_register_stores_config(void)
{
    fb_engine__reset();
    EmbedIQ_FB_Config_t cfg = {
        .name       = "fb_test",
        .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
    };
    EmbedIQ_FB_Handle_t h = embediq_fb_register(&cfg);
    ASSERT(h != NULL, "register must return non-NULL handle");
}

/* ---------------------------------------------------------------------------
 * test_boot_phase_order_platform_before_infrastructure
 * Phase 1 (PLATFORM) init_fn must be called before Phase 2 (INFRASTRUCTURE).
 * FBs are registered in reverse order to verify the engine reorders by phase.
 * ------------------------------------------------------------------------- */

static int g_ppi_plat_order  = -1;
static int g_ppi_infra_order = -1;
static int g_ppi_call_count  = 0;

static void ppi_plat_init(EmbedIQ_FB_Handle_t fb, void *data)
{
    (void)fb; (void)data;
    g_ppi_plat_order = g_ppi_call_count++;
}

static void ppi_infra_init(EmbedIQ_FB_Handle_t fb, void *data)
{
    (void)fb; (void)data;
    g_ppi_infra_order = g_ppi_call_count++;
}

static void test_boot_phase_order_platform_before_infrastructure(void)
{
    fb_engine__reset();
    g_ppi_plat_order = g_ppi_infra_order = -1;
    g_ppi_call_count = 0;

    /* Register INFRASTRUCTURE first, PLATFORM second — engine must reorder. */
    EmbedIQ_FB_Config_t infra_cfg = {
        .name = "fb_infra_ppi", .init_fn = ppi_infra_init,
        .boot_phase = EMBEDIQ_BOOT_PHASE_INFRASTRUCTURE,
    };
    EmbedIQ_FB_Config_t plat_cfg = {
        .name = "fb_plat_ppi", .init_fn = ppi_plat_init,
        .boot_phase = EMBEDIQ_BOOT_PHASE_PLATFORM,
    };
    embediq_fb_register(&infra_cfg);
    embediq_fb_register(&plat_cfg);
    embediq_engine_boot();

    ASSERT(g_ppi_plat_order < g_ppi_infra_order,
           "PLATFORM (phase 1) must init before INFRASTRUCTURE (phase 2)");
}

/* ---------------------------------------------------------------------------
 * test_boot_phase_order_infrastructure_before_application
 * Phase 2 (INFRASTRUCTURE) must init before Phase 3 (APPLICATION).
 * ------------------------------------------------------------------------- */

static int g_pia_app_order   = -1;
static int g_pia_infra_order = -1;
static int g_pia_call_count  = 0;

static void pia_app_init(EmbedIQ_FB_Handle_t fb, void *data)
{
    (void)fb; (void)data;
    g_pia_app_order = g_pia_call_count++;
}

static void pia_infra_init(EmbedIQ_FB_Handle_t fb, void *data)
{
    (void)fb; (void)data;
    g_pia_infra_order = g_pia_call_count++;
}

static void test_boot_phase_order_infrastructure_before_application(void)
{
    fb_engine__reset();
    g_pia_app_order = g_pia_infra_order = -1;
    g_pia_call_count = 0;

    /* Register APPLICATION first, INFRASTRUCTURE second — engine must reorder. */
    EmbedIQ_FB_Config_t app_cfg = {
        .name = "fb_app_pia", .init_fn = pia_app_init,
        .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
    };
    EmbedIQ_FB_Config_t infra_cfg = {
        .name = "fb_infra_pia", .init_fn = pia_infra_init,
        .boot_phase = EMBEDIQ_BOOT_PHASE_INFRASTRUCTURE,
    };
    embediq_fb_register(&app_cfg);
    embediq_fb_register(&infra_cfg);
    embediq_engine_boot();

    ASSERT(g_pia_infra_order < g_pia_app_order,
           "INFRASTRUCTURE (phase 2) must init before APPLICATION (phase 3)");
}

/* ---------------------------------------------------------------------------
 * test_depends_on_topological_sort
 * FB_B depends on FB_A → FB_A must init first regardless of registration order.
 * FB_B is registered before FB_A to force the engine to reorder.
 * ------------------------------------------------------------------------- */

static int g_topo_order[2] = {-1, -1};
static int g_topo_count    = 0;

static void topo_a_init(EmbedIQ_FB_Handle_t fb, void *data)
{
    (void)fb; (void)data;
    g_topo_order[g_topo_count++] = 0;   /* marks A */
}

static void topo_b_init(EmbedIQ_FB_Handle_t fb, void *data)
{
    (void)fb; (void)data;
    g_topo_order[g_topo_count++] = 1;   /* marks B */
}

static void test_depends_on_topological_sort(void)
{
    fb_engine__reset();
    g_topo_order[0] = g_topo_order[1] = -1;
    g_topo_count = 0;

    static const char *b_deps[] = {"fb_topo_a"};

    /* Register B (depends on A) before A — engine must init A first. */
    EmbedIQ_FB_Config_t b_cfg = {
        .name          = "fb_topo_b",
        .init_fn       = topo_b_init,
        .boot_phase    = EMBEDIQ_BOOT_PHASE_APPLICATION,
        .depends_on    = b_deps,
        .depends_count = 1,
    };
    EmbedIQ_FB_Config_t a_cfg = {
        .name       = "fb_topo_a",
        .init_fn    = topo_a_init,
        .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
    };
    embediq_fb_register(&b_cfg);
    embediq_fb_register(&a_cfg);
    int rc = embediq_engine_boot();

    ASSERT(rc == 0 && g_topo_order[0] == 0 && g_topo_order[1] == 1,
           "boot must succeed and init FB_A (0) before FB_B (1)");
}

/* ---------------------------------------------------------------------------
 * test_cycle_detection_returns_fault
 * A depends on B, B depends on A → embediq_engine_boot() must return -1.
 * ------------------------------------------------------------------------- */

static void test_cycle_detection_returns_fault(void)
{
    fb_engine__reset();

    static const char *a_deps[] = {"fb_cycle_b"};
    static const char *b_deps[] = {"fb_cycle_a"};

    EmbedIQ_FB_Config_t a_cfg = {
        .name          = "fb_cycle_a",
        .boot_phase    = EMBEDIQ_BOOT_PHASE_APPLICATION,
        .depends_on    = a_deps,
        .depends_count = 1,
    };
    EmbedIQ_FB_Config_t b_cfg = {
        .name          = "fb_cycle_b",
        .boot_phase    = EMBEDIQ_BOOT_PHASE_APPLICATION,
        .depends_on    = b_deps,
        .depends_count = 1,
    };
    embediq_fb_register(&a_cfg);
    embediq_fb_register(&b_cfg);
    int rc = embediq_engine_boot();

    ASSERT(rc != 0, "boot must fail when dependency cycle A→B→A is detected");
}

/* ---------------------------------------------------------------------------
 * test_missing_dep_returns_fault
 * An FB depends on "fb_missing" (not registered) → boot must return -1.
 * ------------------------------------------------------------------------- */

static void test_missing_dep_returns_fault(void)
{
    fb_engine__reset();

    static const char *deps[] = {"fb_missing"};
    EmbedIQ_FB_Config_t cfg = {
        .name          = "fb_has_missing_dep",
        .boot_phase    = EMBEDIQ_BOOT_PHASE_APPLICATION,
        .depends_on    = deps,
        .depends_count = 1,
    };
    embediq_fb_register(&cfg);
    int rc = embediq_engine_boot();

    ASSERT(rc != 0, "boot must fail when a declared dependency is not registered");
}

/* ---------------------------------------------------------------------------
 * test_report_fault_changes_state
 * After embediq_fb_report_fault(), state must be EMBEDIQ_FB_STATE_FAULT.
 * ------------------------------------------------------------------------- */

static void test_report_fault_changes_state(void)
{
    fb_engine__reset();

    EmbedIQ_FB_Config_t cfg = {
        .name       = "fb_fault_test",
        .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
    };
    EmbedIQ_FB_Handle_t h = embediq_fb_register(&cfg);
    embediq_engine_boot();

    embediq_fb_report_fault(h, 0xDEADu);

    ASSERT(embediq_fb_get_state(h) == EMBEDIQ_FB_STATE_FAULT,
           "state must be EMBEDIQ_FB_STATE_FAULT after report_fault");
}

/* ---------------------------------------------------------------------------
 * test_subfn_dispatch_in_registration_order
 * Two sub-fns for the same message: the one with init_order=2 is registered
 * first, init_order=1 second.  Dispatch must call init_order=1 before
 * init_order=2 (R-sub-ORDER: execution order = init_order ascending).
 * ------------------------------------------------------------------------- */

static int g_disp_seq[4];
static int g_disp_seq_count = 0;

static void sfn_first__run(EmbedIQ_FB_Handle_t fb, const void *msg,
                            void *fb_data, void *subfn_data)
{
    (void)fb; (void)msg; (void)fb_data; (void)subfn_data;
    g_disp_seq[g_disp_seq_count++] = 1;
}

static void sfn_second__run(EmbedIQ_FB_Handle_t fb, const void *msg,
                             void *fb_data, void *subfn_data)
{
    (void)fb; (void)msg; (void)fb_data; (void)subfn_data;
    g_disp_seq[g_disp_seq_count++] = 2;
}

static void dispatch_init(EmbedIQ_FB_Handle_t fb, void *data)
{
    (void)data;
    static const uint16_t subs[] = {0x0042u};

    /* init_order=2 registered FIRST, init_order=1 SECOND.
     * Engine must sort by init_order, not insertion order. */
    static const EmbedIQ_SubFn_Config_t sf2 = {
        .name               = "sfn_second",
        .init_order         = 2,
        .run_fn             = sfn_second__run,
        .subscriptions      = subs,
        .subscription_count = 1,
    };
    static const EmbedIQ_SubFn_Config_t sf1 = {
        .name               = "sfn_first",
        .init_order         = 1,
        .run_fn             = sfn_first__run,
        .subscriptions      = subs,
        .subscription_count = 1,
    };
    embediq_subfn_register(fb, &sf2);   /* init_order=2 — registered first */
    embediq_subfn_register(fb, &sf1);   /* init_order=1 — registered second */
}

static void test_subfn_dispatch_in_registration_order(void)
{
    fb_engine__reset();
    g_disp_seq_count = 0;

    EmbedIQ_FB_Config_t cfg = {
        .name       = "fb_dispatch",
        .init_fn    = dispatch_init,
        .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
    };
    EmbedIQ_FB_Handle_t h = embediq_fb_register(&cfg);
    embediq_engine_boot();

    fb_engine__deliver_msg(h, 0x0042u, NULL);

    ASSERT(g_disp_seq_count == 2 &&
           g_disp_seq[0] == 1 &&
           g_disp_seq[1] == 2,
           "sub-fns must be dispatched in ascending init_order (R-sub-ORDER)");
}

/* ---------------------------------------------------------------------------
 * test_lifecycle_events_emitted_to_observatory
 * Booting one FB must emit at least 2 LIFECYCLE events:
 *   IDLE → INITIALISING and INITIALISING → RUNNING.
 * ------------------------------------------------------------------------- */

static void test_lifecycle_events_emitted_to_observatory(void)
{
    fb_engine__reset();

    EmbedIQ_FB_Config_t cfg = {
        .name       = "fb_obs_test",
        .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
    };
    embediq_fb_register(&cfg);
    embediq_engine_boot();

    ASSERT(fb_engine__obs_event_count() >= 2u,
           "at least 2 lifecycle events must be emitted during boot");
}

/* ---------------------------------------------------------------------------
 * test_max_endpoint_overflow_returns_null
 * Registering more than EMBEDIQ_MAX_ENDPOINTS FBs must return NULL.
 * ------------------------------------------------------------------------- */

static void test_max_endpoint_overflow_returns_null(void)
{
    fb_engine__reset();

    EmbedIQ_FB_Config_t cfg = {
        .name       = "fb_overflow",
        .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
    };

    for (int i = 0; i < EMBEDIQ_MAX_ENDPOINTS; i++) {
        EmbedIQ_FB_Handle_t h = embediq_fb_register(&cfg);
        (void)h;
    }

    EmbedIQ_FB_Handle_t overflow = embediq_fb_register(&cfg);
    ASSERT(overflow == NULL,
           "registering beyond EMBEDIQ_MAX_ENDPOINTS must return NULL");
}

/* ===========================================================================
 * Faults on the FB thread, engine API, boot-order race
 * ======================================================================== */

/* Shared recorders. pthread_t values are published by the atomic flags that
 * guard them (seq_cst store/load gives the needed happens-before). */
static embediq_osal_atomic32_t g_engine_run_seen   = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);
static embediq_osal_atomic32_t g_engine_fault_seen = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);
static pthread_t               g_engine_run_thread;
static pthread_t               g_engine_fault_thread;
static uint32_t                g_engine_fault_reason;

static void engine_run_fn(EmbedIQ_FB_Handle_t fb, const void *msg, void *fb_data,
                          void *subfn_data)
{
    (void)fb; (void)msg; (void)fb_data; (void)subfn_data;
    g_engine_run_thread = pthread_self();
    embediq_osal_atomic32_store(&g_engine_run_seen, 1u);
}

static void engine_fault_fn(EmbedIQ_FB_Handle_t fb, void *fb_data, uint32_t reason)
{
    (void)fb; (void)fb_data;
    g_engine_fault_thread = pthread_self();
    g_engine_fault_reason = reason;
    embediq_osal_atomic32_store(&g_engine_fault_seen, 1u);
}

EMBEDIQ_SUBS(g_engine_ping_subs, MSG_TEST_PING);

static const EmbedIQ_SubFn_Config_t g_engine_ping_subfn = {
    .name = "engine_ping", .init_order = 0,
    .subscriptions = g_engine_ping_subs, .subscription_count = 1,
    .run_fn = engine_run_fn,
};

static void engine_ping_init(EmbedIQ_FB_Handle_t h, void *d)
{
    (void)d;
    embediq_subfn_register(h, &g_engine_ping_subfn);
}

/* Spin (1 ms steps) until flag != 0 or ms elapse. Returns the final flag. */
static bool engine_wait_flag(embediq_osal_atomic32_t *flag, uint32_t ms)
{
    uint32_t t0 = embediq_osal_time_ms();
    while (embediq_osal_atomic32_load(flag) == 0u
           && (embediq_osal_time_ms() - t0) < ms) {
        embediq_osal_delay_ms(1u);
    }
    return embediq_osal_atomic32_load(flag) != 0u;
}

typedef struct { EmbedIQ_FB_Handle_t h; uint32_t reason; } engine_fault_req_t;

static void *engine_fault_reporter(void *arg)
{
    const engine_fault_req_t *r = (const engine_fault_req_t *)arg;
    embediq_fb_report_fault(r->h, r->reason);
    return NULL;
}

/* ---------------------------------------------------------------------------
 * test_report_fault_from_foreign_thread_applies_on_fb_thread
 * A fault reported from another thread runs fault_fn on the FB's own
 * dispatch thread (the thread its run_fn runs on), with the reason intact.
 * ------------------------------------------------------------------------- */

static void test_report_fault_from_foreign_thread_applies_on_fb_thread(void)
{
    fb_engine__reset();
    embediq_osal_atomic32_store(&g_engine_run_seen, 0u);
    embediq_osal_atomic32_store(&g_engine_fault_seen, 0u);

    static const EmbedIQ_FB_Config_t k_src = {
        .name = "engine_src", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
    };
    static const EmbedIQ_FB_Config_t k_dst = {
        .name = "engine_dst", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
        .init_fn = engine_ping_init, .fault_fn = engine_fault_fn,
        .subscriptions = g_engine_ping_subs, .subscription_count = 1,
    };
    EmbedIQ_FB_Handle_t src = embediq_fb_register(&k_src);
    EmbedIQ_FB_Handle_t dst = embediq_fb_register(&k_dst);
    embediq_engine_boot();
    embediq_engine_dispatch_boot();

    /* Learn the dispatch thread's identity through a normal message. */
    EmbedIQ_Msg_t m;
    memset(&m, 0, sizeof(m));
    m.msg_id   = MSG_TEST_PING;
    m.priority = EMBEDIQ_MSG_PRIORITY_NORMAL;
    embediq_publish(src, &m);
    bool ran = engine_wait_flag(&g_engine_run_seen, DISPATCH_WAIT_MS);

    pthread_t th;
    engine_fault_req_t req = { dst, FAULT_REASON_TEST };
    pthread_create(&th, NULL, engine_fault_reporter, &req);
    pthread_join(th, NULL);
    bool faulted = engine_wait_flag(&g_engine_fault_seen, DISPATCH_WAIT_MS);

    bool ok = ran && faulted
              && pthread_equal(g_engine_fault_thread, g_engine_run_thread)
              && !pthread_equal(g_engine_fault_thread, th)
              && g_engine_fault_reason == FAULT_REASON_TEST
              && embediq_fb_get_state(dst) == EMBEDIQ_FB_STATE_FAULT;
    (void)embediq_engine_dispatch_shutdown();
    ASSERT(ok, "fault_fn must run on the FB's dispatch thread with reason 0x55");
}

/* ---------------------------------------------------------------------------
 * test_report_fault_inline_for_fb_without_dispatch_thread
 * An FB with no subscriptions has no dispatch thread: the fault is applied
 * on the reporting thread before report_fault returns.
 * ------------------------------------------------------------------------- */

static void test_report_fault_inline_for_fb_without_dispatch_thread(void)
{
    fb_engine__reset();
    embediq_osal_atomic32_store(&g_engine_fault_seen, 0u);

    static const EmbedIQ_FB_Config_t k_cfg = {
        .name = "engine_nosub", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
        .fault_fn = engine_fault_fn,
    };
    EmbedIQ_FB_Handle_t h = embediq_fb_register(&k_cfg);
    embediq_engine_boot();
    embediq_engine_dispatch_boot();

    pthread_t th;
    engine_fault_req_t req = { h, FAULT_REASON_TEST };
    pthread_create(&th, NULL, engine_fault_reporter, &req);
    pthread_join(th, NULL);   /* report_fault has returned */

    bool ok = embediq_osal_atomic32_load(&g_engine_fault_seen) == 1u
              && pthread_equal(g_engine_fault_thread, th)
              && embediq_fb_get_state(h) == EMBEDIQ_FB_STATE_FAULT;
    (void)embediq_engine_dispatch_shutdown();
    ASSERT(ok, "no-dispatch-thread FB: fault applied inline on the reporting thread");
}

/* ---------------------------------------------------------------------------
 * test_get_state_concurrent_tsan
 * Readers poll embediq_fb_get_state() while another thread reports faults.
 * No crash; under TSan, no report on the state field.
 * ------------------------------------------------------------------------- */

static EmbedIQ_FB_Handle_t     g_engine_state_fb   = NULL;
static embediq_osal_atomic32_t g_engine_state_stop = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);

static void *engine_state_reader(void *arg)
{
    (void)arg;
    uint32_t n = 0u;
    while (embediq_osal_atomic32_load(&g_engine_state_stop) == 0u && n < GETSTATE_ITERS) {
        (void)embediq_fb_get_state(g_engine_state_fb);
        n++;
    }
    return NULL;
}

static void test_get_state_concurrent_tsan(void)
{
    fb_engine__reset();
    static const EmbedIQ_FB_Config_t k_cfg = {
        .name = "engine_state", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
    };
    g_engine_state_fb = embediq_fb_register(&k_cfg);
    embediq_engine_boot();
    embediq_osal_atomic32_store(&g_engine_state_stop, 0u);

    pthread_t rd[GETSTATE_THREADS], wr;
    for (uint32_t i = 0u; i < GETSTATE_THREADS; i++) {
        pthread_create(&rd[i], NULL, engine_state_reader, NULL);
    }
    engine_fault_req_t req = { g_engine_state_fb, FAULT_REASON_TEST };
    pthread_create(&wr, NULL, engine_fault_reporter, &req);
    pthread_join(wr, NULL);
    pthread_create(&wr, NULL, engine_fault_reporter, &req);   /* report twice */
    pthread_join(wr, NULL);
    embediq_osal_atomic32_store(&g_engine_state_stop, 1u);
    for (uint32_t i = 0u; i < GETSTATE_THREADS; i++) {
        pthread_join(rd[i], NULL);
    }
    ASSERT(embediq_fb_get_state(g_engine_state_fb) == EMBEDIQ_FB_STATE_FAULT,
           "concurrent get_state while faulting must be safe and end in FAULT");
}

/* ---------------------------------------------------------------------------
 * test_engine_fb_api_matches_registry
 * embediq_engine_fb_count / fb_config / fb_ep agree with what was registered.
 * ------------------------------------------------------------------------- */

static void test_engine_fb_api_matches_registry(void)
{
    fb_engine__reset();
    static const EmbedIQ_FB_Config_t k_cfgs[API_TEST_FBS] = {
        { .name = "engine_api_a", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION },
        { .name = "engine_api_b", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION },
        { .name = "engine_api_c", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION },
    };
    EmbedIQ_FB_Handle_t h[API_TEST_FBS];
    for (uint8_t i = 0u; i < API_TEST_FBS; i++) h[i] = embediq_fb_register(&k_cfgs[i]);
    embediq_engine_boot();

    bool ok = embediq_engine_fb_count() == API_TEST_FBS
              && embediq_engine_fb_config(API_TEST_FBS) == NULL
              && embediq_engine_fb_ep(NULL) == 0xFFu;
    for (uint8_t i = 0u; ok && i < API_TEST_FBS; i++) {
        const EmbedIQ_FB_Config_t *c = embediq_engine_fb_config(i);
        uint8_t ep = embediq_engine_fb_ep(h[i]);
        ok = c != NULL && strcmp(c->name, k_cfgs[i].name) == 0
             && ep == i && ep == embediq_bus_resolve_name(k_cfgs[i].name);
    }
    ASSERT(ok, "engine API must agree with the registered handles");
}

/* ---------------------------------------------------------------------------
 * test_platform_thread_publish_during_boot_is_not_lost
 * FB A's init_fn starts a thread that publishes immediately — while boot is
 * still running — and keeps publishing. FB B receives every message. Under
 * TSan nothing races on the notify callback or the dispatch semaphores.
 * ------------------------------------------------------------------------- */

static EmbedIQ_FB_Handle_t     g_engine_burst_src = NULL;
static embediq_osal_atomic32_t g_engine_burst_rx  = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);
static pthread_t               g_engine_burst_th;

static void *engine_burst_publisher(void *arg)
{
    (void)arg;
    for (uint32_t i = 0u; i < BURST_COUNT; i++) {
        EmbedIQ_Msg_t m;
        memset(&m, 0, sizeof(m));
        m.msg_id   = MSG_TEST_BURST;
        m.priority = EMBEDIQ_MSG_PRIORITY_NORMAL;
        embediq_publish(g_engine_burst_src, &m);
        embediq_osal_delay_ms(BURST_SPACING_MS);  /* stay under the NORMAL depth */
    }
    return NULL;
}

static void engine_burst_src_init(EmbedIQ_FB_Handle_t h, void *d)
{
    (void)d;
    g_engine_burst_src = h;
    pthread_create(&g_engine_burst_th, NULL, engine_burst_publisher, NULL);
}

static void engine_burst_rx_fn(EmbedIQ_FB_Handle_t fb, const void *msg, void *fb_data,
                               void *subfn_data)
{
    (void)fb; (void)msg; (void)fb_data; (void)subfn_data;
    (void)embediq_osal_atomic32_add(&g_engine_burst_rx, 1u);
}

EMBEDIQ_SUBS(g_engine_burst_subs, MSG_TEST_BURST);

static const EmbedIQ_SubFn_Config_t g_engine_burst_subfn = {
    .name = "engine_burst_rx", .init_order = 0,
    .subscriptions = g_engine_burst_subs, .subscription_count = 1,
    .run_fn = engine_burst_rx_fn,
};

static void engine_burst_dst_init(EmbedIQ_FB_Handle_t h, void *d)
{
    (void)d;
    embediq_subfn_register(h, &g_engine_burst_subfn);
}

static void test_platform_thread_publish_during_boot_is_not_lost(void)
{
    fb_engine__reset();
    embediq_osal_atomic32_store(&g_engine_burst_rx, 0u);

    /* A boots first (PLATFORM) so its thread publishes while B is still booting. */
    static const EmbedIQ_FB_Config_t k_src = {
        .name = "engine_burst_src", .boot_phase = EMBEDIQ_BOOT_PHASE_PLATFORM,
        .init_fn = engine_burst_src_init,
    };
    static const EmbedIQ_FB_Config_t k_dst = {
        .name = "engine_burst_dst", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
        .init_fn = engine_burst_dst_init,
        .subscriptions = g_engine_burst_subs, .subscription_count = 1,
    };
    embediq_fb_register(&k_src);
    embediq_fb_register(&k_dst);
    embediq_engine_boot();
    embediq_engine_dispatch_boot();

    pthread_join(g_engine_burst_th, NULL);
    uint32_t t0 = embediq_osal_time_ms();
    while (embediq_osal_atomic32_load(&g_engine_burst_rx) < BURST_COUNT
           && (embediq_osal_time_ms() - t0) < BURST_WAIT_MS) {
        embediq_osal_delay_ms(1u);
    }
    uint32_t got = embediq_osal_atomic32_load(&g_engine_burst_rx);
    (void)embediq_engine_dispatch_shutdown();
    printf("      received %u / %u\n", (unsigned)got, (unsigned)BURST_COUNT);
    ASSERT(got == BURST_COUNT, "every message published during boot must be delivered");
}

/* ---------------------------------------------------------------------------
 * Entry point
 * ------------------------------------------------------------------------- */

/* ===========================================================================
 * fault_fn never runs concurrently with itself (review R4)
 * ======================================================================== */

/* fault_fn that sleeps and tracks overlap with atomics only (reviewer probe). */
static embediq_osal_atomic32_t g_inline_active = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);
static embediq_osal_atomic32_t g_inline_peak   = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);
static embediq_osal_atomic32_t g_inline_count  = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);
static embediq_osal_atomic32_t g_inline_go     = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);

static void inline_fault_fn(EmbedIQ_FB_Handle_t fb, void *fb_data, uint32_t reason)
{
    (void)fb; (void)fb_data; (void)reason;
    uint32_t active = embediq_osal_atomic32_add(&g_inline_active, 1u);   /* returns NEW value */
    uint32_t peak   = embediq_osal_atomic32_load(&g_inline_peak);
    while (active > peak && !embediq_osal_atomic32_cas(&g_inline_peak, peak, active)) {
        peak = embediq_osal_atomic32_load(&g_inline_peak);
    }
    embediq_osal_delay_ms(FAULT_HOLD_LONG_MS);
    (void)embediq_osal_atomic32_sub(&g_inline_active, 1u);
    (void)embediq_osal_atomic32_add(&g_inline_count, 1u);
}

static void *inline_reporter(void *arg)
{
    while (embediq_osal_atomic32_load(&g_inline_go) == 0u) { /* start together */ }
    embediq_fb_report_fault((EmbedIQ_FB_Handle_t)arg, FAULT_REASON_TEST);
    return NULL;
}

/* Two threads report a fault on h at the same moment; returns the peak
 * fault_fn concurrency and stores the number of fault_fn runs in *count. */
static uint32_t inline_report_concurrently(EmbedIQ_FB_Handle_t h, uint32_t *count)
{
    embediq_osal_atomic32_store(&g_inline_active, 0u);
    embediq_osal_atomic32_store(&g_inline_peak,   0u);
    embediq_osal_atomic32_store(&g_inline_count,  0u);
    embediq_osal_atomic32_store(&g_inline_go,     0u);
    pthread_t th[FAULT_REPORTERS];
    for (uint32_t i = 0u; i < FAULT_REPORTERS; i++) {
        pthread_create(&th[i], NULL, inline_reporter, h);
    }
    embediq_osal_atomic32_store(&g_inline_go, 1u);
    for (uint32_t i = 0u; i < FAULT_REPORTERS; i++) {
        pthread_join(th[i], NULL);
    }
    *count = embediq_osal_atomic32_load(&g_inline_count);
    return embediq_osal_atomic32_load(&g_inline_peak);
}

/* ---------------------------------------------------------------------------
 * test_fault_fn_never_concurrent_for_non_dispatch_fb
 * FB with no subscriptions (no dispatch thread): two concurrent reporters.
 * fault_fn runs twice, never overlapping.
 * ------------------------------------------------------------------------- */

static void test_fault_fn_never_concurrent_for_non_dispatch_fb(void)
{
    fb_engine__reset();
    static const EmbedIQ_FB_Config_t k_cfg = {
        .name = "inline_nosub", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
        .fault_fn = inline_fault_fn,
    };
    EmbedIQ_FB_Handle_t h = embediq_fb_register(&k_cfg);
    embediq_engine_boot();
    embediq_engine_dispatch_boot();

    uint32_t count = 0u;
    uint32_t peak  = inline_report_concurrently(h, &count);
    (void)embediq_engine_dispatch_shutdown();
    printf("      peak=%u count=%u\n", (unsigned)peak, (unsigned)count);
    ASSERT(peak == 1u && count == FAULT_REPORTERS
           && embediq_fb_get_state(h) == EMBEDIQ_FB_STATE_FAULT,
           "non-dispatch FB: fault_fn applied twice, never concurrently");
}

/* ---------------------------------------------------------------------------
 * test_fault_fn_never_concurrent_before_dispatch_boot
 * Subscribing FB, engine booted, dispatch NOT started: two concurrent
 * reporters (platform threads started in init_fn can do this). fault_fn runs
 * twice, never overlapping; dispatch then starts and stops cleanly.
 * ------------------------------------------------------------------------- */

static void test_fault_fn_never_concurrent_before_dispatch_boot(void)
{
    fb_engine__reset();
    EMBEDIQ_SUBS(subs, MSG_TEST_SUB);
    EmbedIQ_FB_Config_t cfg = {
        .name = "inline_predispatch", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
        .fault_fn = inline_fault_fn,
        .subscriptions = subs, .subscription_count = 1,
    };
    EmbedIQ_FB_Handle_t h = embediq_fb_register(&cfg);
    embediq_engine_boot();

    uint32_t count = 0u;
    uint32_t peak  = inline_report_concurrently(h, &count);
    embediq_engine_dispatch_boot();
    int rc = embediq_engine_dispatch_shutdown();
    printf("      peak=%u count=%u shutdown=%d\n", (unsigned)peak, (unsigned)count, rc);
    ASSERT(peak == 1u && count == FAULT_REPORTERS && rc == 0
           && embediq_fb_get_state(h) == EMBEDIQ_FB_STATE_FAULT,
           "pre-dispatch FB: fault_fn applied twice, never concurrently");
}

/* ===========================================================================
 * One per-FB execution lock across the lifecycle (review #182 N1–N3)
 * ======================================================================== */

/* Gate helpers: spin in 1 ms steps until flag != 0 or ms elapse. */
static bool exec_wait(embediq_osal_atomic32_t *flag, uint32_t ms)
{
    uint32_t t0 = embediq_osal_time_ms();
    while (embediq_osal_atomic32_load(flag) == 0u
           && (embediq_osal_time_ms() - t0) < ms) {
        embediq_osal_delay_ms(1u);
    }
    return embediq_osal_atomic32_load(flag) != 0u;
}

/* ---------------------------------------------------------------------------
 * test_dispatch_waits_for_inflight_inline_fault  (N1-a)
 * A pre-dispatch inline fault_fn is held on a gate; dispatch then starts and
 * a subscribed message is published. run_fn must not run while fault_fn is in
 * flight, and must run once it returns.
 * ------------------------------------------------------------------------- */

static embediq_osal_atomic32_t g_exec_in_fault  = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);
static embediq_osal_atomic32_t g_exec_gate      = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);
static embediq_osal_atomic32_t g_exec_run_seen  = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);
static embediq_osal_atomic32_t g_exec_overlap   = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);

static void exec_gated_fault_fn(EmbedIQ_FB_Handle_t fb, void *d, uint32_t r)
{
    (void)fb; (void)d; (void)r;
    embediq_osal_atomic32_store(&g_exec_in_fault, 1u);
    (void)exec_wait(&g_exec_gate, GATE_MAX_MS);
    embediq_osal_atomic32_store(&g_exec_in_fault, 0u);
}

static void exec_run_fn(EmbedIQ_FB_Handle_t fb, const void *msg, void *d, void *s)
{
    (void)fb; (void)msg; (void)d; (void)s;
    if (embediq_osal_atomic32_load(&g_exec_in_fault) != 0u) {
        (void)embediq_osal_atomic32_add(&g_exec_overlap, 1u);
    }
    embediq_osal_atomic32_store(&g_exec_run_seen, 1u);
}

EMBEDIQ_SUBS(g_exec_subs, MSG_TEST_EXEC);

static const EmbedIQ_SubFn_Config_t g_exec_subfn = {
    .name = "exec_run", .init_order = 0,
    .subscriptions = g_exec_subs, .subscription_count = 1,
    .run_fn = exec_run_fn,
};

static void exec_subfn_init(EmbedIQ_FB_Handle_t h, void *d)
{
    (void)d;
    embediq_subfn_register(h, &g_exec_subfn);
}

static void *exec_reporter(void *h)
{
    embediq_fb_report_fault((EmbedIQ_FB_Handle_t)h, FAULT_REASON_TEST);
    return NULL;
}

static void test_dispatch_waits_for_inflight_inline_fault(void)
{
    fb_engine__reset();
    embediq_obs_set_transport(EMBEDIQ_OBS_TRANSPORT_NULL);
    embediq_osal_atomic32_store(&g_exec_in_fault, 0u);
    embediq_osal_atomic32_store(&g_exec_gate, 0u);
    embediq_osal_atomic32_store(&g_exec_run_seen, 0u);
    embediq_osal_atomic32_store(&g_exec_overlap, 0u);

    static const EmbedIQ_FB_Config_t k_cfg = {
        .name = "exec_startup", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
        .init_fn = exec_subfn_init, .fault_fn = exec_gated_fault_fn,
        .subscriptions = g_exec_subs, .subscription_count = 1,
    };
    EmbedIQ_FB_Handle_t h = embediq_fb_register(&k_cfg);
    embediq_engine_boot();

    pthread_t th;
    pthread_create(&th, NULL, exec_reporter, h);
    bool entered = exec_wait(&g_exec_in_fault, GATE_MAX_MS);

    embediq_engine_dispatch_boot();
    EmbedIQ_Msg_t m;
    memset(&m, 0, sizeof(m));
    m.msg_id   = MSG_TEST_EXEC;
    m.priority = (uint8_t)EMBEDIQ_MSG_PRIORITY_NORMAL;
    embediq_publish(h, &m);

    embediq_osal_delay_ms(RUN_SILENT_SAMPLE_MS);   /* fault_fn still gated */
    uint32_t ran_while_gated = embediq_osal_atomic32_load(&g_exec_run_seen);

    embediq_osal_atomic32_store(&g_exec_gate, 1u);
    pthread_join(th, NULL);
    bool ran_after = exec_wait(&g_exec_run_seen, GATE_MAX_MS);
    (void)embediq_engine_dispatch_shutdown();

    uint32_t overlap = embediq_osal_atomic32_load(&g_exec_overlap);
    printf("      entered=%d ran_while_gated=%u ran_after=%d overlap=%u\n",
           (int)entered, (unsigned)ran_while_gated, (int)ran_after, (unsigned)overlap);
    ASSERT(entered && ran_while_gated == 0u && ran_after && overlap == 0u,
           "first dispatch waits for an in-flight pre-dispatch fault_fn");
}

/* ---------------------------------------------------------------------------
 * test_fault_during_init_fn_is_serialised_and_not_overwritten  (N1-b/c)
 * init_fn starts a platform thread that reports a fault at once, then keeps
 * touching fb_data for 50 ms. fault_fn must not overlap init_fn, must run
 * after init_fn returns, and the final state must be FAULT, not RUNNING.
 * ------------------------------------------------------------------------- */

static embediq_osal_atomic32_t g_exec_init_done      = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);
static embediq_osal_atomic32_t g_exec_fault_active   = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);
static embediq_osal_atomic32_t g_exec_init_overlap   = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);
static embediq_osal_atomic32_t g_exec_fault_ran      = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);
static embediq_osal_atomic32_t g_exec_fault_in_init  = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);
static pthread_t               g_exec_plat_thread;
static uint32_t                g_exec_fb_data[FB_DATA_WORDS];

static void exec_init_fault_fn(EmbedIQ_FB_Handle_t fb, void *d, uint32_t r)
{
    (void)fb; (void)r;
    embediq_osal_atomic32_store(&g_exec_fault_active, 1u);
    if (embediq_osal_atomic32_load(&g_exec_init_done) == 0u) {
        embediq_osal_atomic32_store(&g_exec_fault_in_init, 1u);
    }
    ((uint32_t *)d)[0]++;                    /* touches the same fb_data */
    embediq_osal_delay_ms(FAULT_HOLD_SHORT_MS);
    embediq_osal_atomic32_store(&g_exec_fault_ran, 1u);
    embediq_osal_atomic32_store(&g_exec_fault_active, 0u);
}

static void exec_init_with_platform_thread(EmbedIQ_FB_Handle_t h, void *d)
{
    pthread_create(&g_exec_plat_thread, NULL, exec_reporter, h);
    uint32_t t0 = embediq_osal_time_ms();
    while ((embediq_osal_time_ms() - t0) < INIT_BUSY_MS) {
        ((uint32_t *)d)[1]++;                /* init_fn using fb_data */
        if (embediq_osal_atomic32_load(&g_exec_fault_active) != 0u) {
            embediq_osal_atomic32_store(&g_exec_init_overlap, 1u);
        }
        embediq_osal_delay_ms(1u);
    }
    embediq_osal_atomic32_store(&g_exec_init_done, 1u);
}

static void test_fault_during_init_fn_is_serialised_and_not_overwritten(void)
{
    fb_engine__reset();
    embediq_obs_set_transport(EMBEDIQ_OBS_TRANSPORT_NULL);
    embediq_osal_atomic32_store(&g_exec_init_done, 0u);
    embediq_osal_atomic32_store(&g_exec_fault_active, 0u);
    embediq_osal_atomic32_store(&g_exec_init_overlap, 0u);
    embediq_osal_atomic32_store(&g_exec_fault_ran, 0u);
    embediq_osal_atomic32_store(&g_exec_fault_in_init, 0u);
    memset(g_exec_fb_data, 0, sizeof(g_exec_fb_data));

    static const EmbedIQ_FB_Config_t k_cfg = {
        .name = "exec_plat", .boot_phase = EMBEDIQ_BOOT_PHASE_PLATFORM,
        .init_fn = exec_init_with_platform_thread, .fault_fn = exec_init_fault_fn,
        .fb_data = g_exec_fb_data,
    };
    EmbedIQ_FB_Handle_t h = embediq_fb_register(&k_cfg);
    int rc = embediq_engine_boot();
    pthread_join(g_exec_plat_thread, NULL);

    uint32_t ov  = embediq_osal_atomic32_load(&g_exec_init_overlap);
    uint32_t in  = embediq_osal_atomic32_load(&g_exec_fault_in_init);
    uint32_t ran = embediq_osal_atomic32_load(&g_exec_fault_ran);
    EmbedIQ_FB_State_t st = embediq_fb_get_state(h);
    printf("      boot=%d overlap=%u fault_during_init=%u fault_ran=%u state=%d (FAULT=%d)\n",
           rc, (unsigned)ov, (unsigned)in, (unsigned)ran, (int)st,
           (int)EMBEDIQ_FB_STATE_FAULT);
    ASSERT(rc == 0 && ov == 0u && in == 0u && ran == 1u
           && st == EMBEDIQ_FB_STATE_FAULT,
           "fault reported during init_fn: serialised after it, FAULT not overwritten");
}

/* ---------------------------------------------------------------------------
 * test_boot_fails_when_exec_lock_allocation_fails  (N2)
 * The second per-FB lock allocation returns NULL: boot fails, emits a FAULT
 * event with EMBEDIQ_FAULT_BOOT_RESOURCE, and runs no init_fn.
 * ------------------------------------------------------------------------- */

static uint32_t g_exec_init_runs = 0u;
static void exec_count_init(EmbedIQ_FB_Handle_t h, void *d)
{
    (void)h; (void)d;
    g_exec_init_runs++;
}

static void test_boot_fails_when_exec_lock_allocation_fails(void)
{
    fb_engine__reset();   /* transport STDOUT: events land in the ring */
    g_exec_init_runs = 0u;
    static const EmbedIQ_FB_Config_t k_a = {
        .name = "exec_res_a", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
        .init_fn = exec_count_init,
    };
    static const EmbedIQ_FB_Config_t k_b = {
        .name = "exec_res_b", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
        .init_fn = exec_count_init,
    };
    static const EmbedIQ_FB_Config_t k_c = {
        .name = "exec_res_c", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
        .init_fn = exec_count_init,
    };
    embediq_fb_register(&k_a);
    embediq_fb_register(&k_b);
    embediq_fb_register(&k_c);

    embediq_osal__fail_next_mutex_create(FAIL_NTH_MUTEX);
    int rc = embediq_engine_boot();
    embediq_osal__fail_next_mutex_create(0u);   /* disarm */

    uint32_t res_faults = 0u;
    EmbedIQ_Event_t e;
    for (uint32_t i = 0u; obs__ring_read(i, &e); i++) {
        if (e.event_type == EMBEDIQ_OBS_EVT_FAULT
            && e.state_or_flag == (uint8_t)EMBEDIQ_FAULT_BOOT_RESOURCE) {
            res_faults++;
        }
    }
    printf("      boot_rc=%d boot_resource_faults=%u init_runs=%u\n",
           rc, (unsigned)res_faults, (unsigned)g_exec_init_runs);
    ASSERT(rc != 0 && res_faults == 1u && g_exec_init_runs == 0u,
           "exec-lock allocation failure: boot fails with a FAULT event, no init_fn runs");
    fb_engine__reset();
}

/* ---------------------------------------------------------------------------
 * test_nested_fault_reports_all_delivered_in_order  (N3)
 * fault_fn(1) reports 2 and 3 → delivered [1, 2, 3]. Then fault_fn(10)
 * reports DEPTH + 2 reasons → DEPTH applied in order, 2 counted as dropped
 * and reported in one EMBEDIQ_FAULT_PENDING_OVERFLOW event (msg_id 2).
 * ------------------------------------------------------------------------- */

static uint32_t g_exec_reasons[REASON_CAP];
static uint32_t g_exec_reason_n = 0u;

static void exec_nested_fault_fn(EmbedIQ_FB_Handle_t fb, void *d, uint32_t r)
{
    (void)d;
    if (g_exec_reason_n < REASON_CAP) g_exec_reasons[g_exec_reason_n++] = r;
    if (r == 1u) {
        embediq_fb_report_fault(fb, 2u);
        embediq_fb_report_fault(fb, 3u);
    } else if (r == NESTED_OVERFLOW_OUTER) {
        for (uint32_t i = 0u; i < EMBEDIQ_FB_FAULT_INBOX_DEPTH + NESTED_OVERFLOW_EXTRA; i++) {
            embediq_fb_report_fault(fb, NESTED_OVERFLOW_OUTER + 1u + i);
        }
    }
}

static void test_nested_fault_reports_all_delivered_in_order(void)
{
    fb_engine__reset();   /* transport STDOUT: events land in the ring */
    static const EmbedIQ_FB_Config_t k_cfg = {
        .name = "exec_nested", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
        .fault_fn = exec_nested_fault_fn,
    };
    EmbedIQ_FB_Handle_t h = embediq_fb_register(&k_cfg);
    embediq_engine_boot();

    g_exec_reason_n = 0u;
    embediq_fb_report_fault(h, 1u);
    bool first = g_exec_reason_n == 3u && g_exec_reasons[0] == 1u
                 && g_exec_reasons[1] == 2u && g_exec_reasons[2] == 3u;
    uint32_t n_first = g_exec_reason_n;

    g_exec_reason_n = 0u;
    embediq_fb_report_fault(h, NESTED_OVERFLOW_OUTER);
    bool second = g_exec_reason_n == 1u + EMBEDIQ_FB_FAULT_INBOX_DEPTH
                  && g_exec_reasons[0] == NESTED_OVERFLOW_OUTER;
    for (uint32_t i = 1u; second && i < g_exec_reason_n; i++) {
        second = g_exec_reasons[i] == NESTED_OVERFLOW_OUTER + i;
    }

    uint32_t ovf_events = 0u, ovf_count = 0u;
    EmbedIQ_Event_t e;
    for (uint32_t i = 0u; obs__ring_read(i, &e); i++) {
        if (e.event_type == EMBEDIQ_OBS_EVT_FAULT
            && e.state_or_flag == (uint8_t)EMBEDIQ_FAULT_PENDING_OVERFLOW) {
            ovf_events++;
            ovf_count = e.msg_id;
        }
    }
    printf("      first: n=%u in_order=%d | overflow: applied=%u in_order=%d "
           "overflow_events=%u dropped=%u\n",
           (unsigned)n_first, (int)first, (unsigned)g_exec_reason_n, (int)second,
           (unsigned)ovf_events, (unsigned)ovf_count);
    ASSERT(first && second && ovf_events == 1u && ovf_count == NESTED_OVERFLOW_EXTRA,
           "nested fault reports: queued in order, overflow counted and reported");
}

/* ---------------------------------------------------------------------------
 * test_run_fn_cost_under_exec_lock
 * 100,000 messages dispatched through the engine path (which runs run_fn
 * under the FB's execution lock — asserted from inside run_fn) cost less than
 * DISPATCH_COST_MAX_NS more per message than calling run_fn directly.
 * ------------------------------------------------------------------------- */

static uint32_t g_exec_cost_calls  = 0u;
static uint32_t g_exec_cost_locked = 0u;
static uint8_t  g_exec_cost_ep     = 0u;

static void exec_cost_run_fn(EmbedIQ_FB_Handle_t fb, const void *msg, void *d, void *s)
{
    (void)fb; (void)msg; (void)d; (void)s;
    g_exec_cost_calls++;
    if (fb_engine__exec_lock_held(g_exec_cost_ep)) g_exec_cost_locked++;
}

static const EmbedIQ_SubFn_Config_t g_exec_cost_subfn = {
    .name = "exec_cost", .init_order = 0,
    .subscriptions = g_exec_subs, .subscription_count = 1,
    .run_fn = exec_cost_run_fn,
};

static void exec_cost_init(EmbedIQ_FB_Handle_t h, void *d)
{
    (void)d;
    embediq_subfn_register(h, &g_exec_cost_subfn);
}

static void test_run_fn_cost_under_exec_lock(void)
{
    fb_engine__reset();
    embediq_obs_set_transport(EMBEDIQ_OBS_TRANSPORT_NULL);
    static const EmbedIQ_FB_Config_t k_cfg = {
        .name = "exec_cost", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
        .init_fn = exec_cost_init,
        .subscriptions = g_exec_subs, .subscription_count = 1,
    };
    EmbedIQ_FB_Handle_t h = embediq_fb_register(&k_cfg);
    embediq_engine_boot();
    g_exec_cost_ep = embediq_engine_fb_ep(h);
    uint8_t payload[EMBEDIQ_MSG_MAX_PAYLOAD];
    memset(payload, 0, sizeof(payload));

    g_exec_cost_calls = 0u;
    uint32_t t0 = embediq_osal_time_us();
    for (uint32_t i = 0u; i < DISPATCH_COST_MSGS; i++) {
        exec_cost_run_fn(h, payload, NULL, NULL);          /* baseline: direct call */
    }
    uint32_t base_us = embediq_osal_time_us() - t0;

    g_exec_cost_calls = 0u; g_exec_cost_locked = 0u;
    t0 = embediq_osal_time_us();
    for (uint32_t i = 0u; i < DISPATCH_COST_MSGS; i++) {
        fb_engine__deliver_msg(h, MSG_TEST_EXEC, payload);      /* engine dispatch path */
    }
    uint32_t lock_us = embediq_osal_time_us() - t0;

    uint32_t over_ns = (lock_us > base_us)
        ? (uint32_t)(((uint64_t)(lock_us - base_us) * 1000u) / DISPATCH_COST_MSGS) : 0u;
    printf("      base=%u us engine=%u us overhead=%u ns/msg locked=%u/%u\n",
           (unsigned)base_us, (unsigned)lock_us, (unsigned)over_ns,
           (unsigned)g_exec_cost_locked, (unsigned)g_exec_cost_calls);
    ASSERT(g_exec_cost_calls == DISPATCH_COST_MSGS && g_exec_cost_locked == DISPATCH_COST_MSGS
           && over_ns < DISPATCH_COST_MAX_NS,
           "run_fn under the exec lock: every dispatch locked, overhead < 2 us/msg");
}

/* ===========================================================================
 * Faults across dispatch shutdown; report_fault never waits for capacity
 * ======================================================================== */

/* Gated FB: run_fn for MSG_TEST_GATED blocks until g_sd_gate; MSG_TEST_FILL
 * is counted; fault_fn counts calls and records the first call's thread. */
static embediq_osal_atomic32_t g_sd_in_run       = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);
static embediq_osal_atomic32_t g_sd_gate         = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);
static embediq_osal_atomic32_t g_sd_fill_rx      = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);
static embediq_osal_atomic32_t g_sd_fault_calls  = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);
static embediq_osal_atomic32_t g_sd_fault_reason = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);
static embediq_osal_atomic32_t g_sd_idle_faults  = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);
static pthread_t               g_sd_fault_thread;
static pthread_t               g_sd_run_thread;

static void sd_reset_recorders(void)
{
    embediq_osal_atomic32_store(&g_sd_in_run, 0u);
    embediq_osal_atomic32_store(&g_sd_gate, 0u);
    embediq_osal_atomic32_store(&g_sd_fill_rx, 0u);
    embediq_osal_atomic32_store(&g_sd_fault_calls, 0u);
    embediq_osal_atomic32_store(&g_sd_fault_reason, 0u);
    embediq_osal_atomic32_store(&g_sd_idle_faults, 0u);
}

static void sd_gated_run_fn(EmbedIQ_FB_Handle_t fb, const void *msg, void *d, void *s)
{
    (void)fb; (void)msg; (void)d; (void)s;
    g_sd_run_thread = pthread_self();
    embediq_osal_atomic32_store(&g_sd_in_run, 1u);
    (void)exec_wait(&g_sd_gate, GATE_MAX_MS);
}

static void sd_fill_run_fn(EmbedIQ_FB_Handle_t fb, const void *msg, void *d, void *s)
{
    (void)fb; (void)msg; (void)d; (void)s;
    (void)embediq_osal_atomic32_add(&g_sd_fill_rx, 1u);
}

/* Runs under the FB's execution lock: never concurrently with itself. */
static void sd_fault_fn(EmbedIQ_FB_Handle_t fb, void *d, uint32_t reason)
{
    (void)fb; (void)d;
    if (embediq_osal_atomic32_load(&g_sd_fault_calls) == 0u) {
        g_sd_fault_thread = pthread_self();
        embediq_osal_atomic32_store(&g_sd_fault_reason, reason);
    }
    (void)embediq_osal_atomic32_add(&g_sd_fault_calls, 1u);
}

static void sd_idle_fault_fn(EmbedIQ_FB_Handle_t fb, void *d, uint32_t reason)
{
    (void)fb; (void)d; (void)reason;
    (void)embediq_osal_atomic32_add(&g_sd_idle_faults, 1u);
}

EMBEDIQ_SUBS(g_sd_gated_subs, MSG_TEST_GATED);
EMBEDIQ_SUBS(g_sd_fill_subs,  MSG_TEST_FILL);
EMBEDIQ_SUBS(g_sd_all_subs,   MSG_TEST_GATED, MSG_TEST_FILL);
EMBEDIQ_SUBS(g_sd_idle_subs,  MSG_TEST_IDLE);

static const EmbedIQ_SubFn_Config_t g_sd_gated_subfn = {
    .name = "sd_gated", .init_order = 0,
    .subscriptions = g_sd_gated_subs, .subscription_count = 1,
    .run_fn = sd_gated_run_fn,
};
static const EmbedIQ_SubFn_Config_t g_sd_fill_subfn = {
    .name = "sd_fill", .init_order = 1,
    .subscriptions = g_sd_fill_subs, .subscription_count = 1,
    .run_fn = sd_fill_run_fn,
};

static void sd_gated_init(EmbedIQ_FB_Handle_t h, void *d)
{
    (void)d;
    embediq_subfn_register(h, &g_sd_gated_subfn);
    embediq_subfn_register(h, &g_sd_fill_subfn);
}

static const EmbedIQ_FB_Config_t k_sd_gated = {
    .name = "sd_gated", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
    .init_fn = sd_gated_init, .fault_fn = sd_fault_fn,
    .subscriptions = g_sd_all_subs, .subscription_count = 2,
};
static const EmbedIQ_FB_Config_t k_sd_idle = {
    .name = "sd_idle", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
    .fault_fn = sd_idle_fault_fn,
    .subscriptions = g_sd_idle_subs, .subscription_count = 1,
};
static const EmbedIQ_FB_Config_t k_sd_src = {
    .name = "sd_src", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
};

static void sd_publish(EmbedIQ_FB_Handle_t src, uint16_t id, uint8_t prio)
{
    EmbedIQ_Msg_t m;
    memset(&m, 0, sizeof(m));
    m.msg_id   = id;
    m.priority = prio;
    embediq_publish(src, &m);
}

typedef struct { EmbedIQ_FB_Handle_t h; uint32_t reason; } sd_report_t;

static void *sd_reporter(void *arg)
{
    const sd_report_t *r = (const sd_report_t *)arg;
    embediq_fb_report_fault(r->h, r->reason);
    return NULL;
}

static void *sd_shutdowner(void *arg)
{
    (void)arg;
    (void)embediq_engine_dispatch_shutdown();
    return NULL;
}

/* ---------------------------------------------------------------------------
 * test_fault_queued_before_shutdown_is_applied
 * (1) A fault queued while the FB's run_fn is busy, followed at once by
 *     dispatch_shutdown, is applied before shutdown returns.
 * (2) A fault reported on another FB while that shutdown is in progress
 *     (its dispatch thread has already stopped) is applied, not left queued.
 * The settle delay only orders the scenario (shutdown flag set before the
 * second report); the assertions hold in every interleaving.
 * ------------------------------------------------------------------------- */

static void test_fault_queued_before_shutdown_is_applied(void)
{
    alarm(SHUTDOWN_HANG_GUARD_S);
    fb_engine__reset();
    embediq_obs_set_transport(EMBEDIQ_OBS_TRANSPORT_NULL);
    sd_reset_recorders();

    EmbedIQ_FB_Handle_t gated = embediq_fb_register(&k_sd_gated);
    EmbedIQ_FB_Handle_t idle  = embediq_fb_register(&k_sd_idle);
    EmbedIQ_FB_Handle_t src   = embediq_fb_register(&k_sd_src);
    embediq_engine_boot();
    embediq_engine_dispatch_boot();

    sd_publish(src, MSG_TEST_GATED, (uint8_t)EMBEDIQ_MSG_PRIORITY_NORMAL);
    bool in_run = exec_wait(&g_sd_in_run, GATE_MAX_MS);

    /* (1) queued while run_fn is busy */
    pthread_t th;
    sd_report_t queued = { gated, FAULT_REASON_QUEUED };
    pthread_create(&th, NULL, sd_reporter, &queued);
    pthread_join(th, NULL);

    /* (2) shutdown starts; run_fn still gated keeps it in progress */
    pthread_t sd;
    pthread_create(&sd, NULL, sd_shutdowner, NULL);
    embediq_osal_delay_ms(SHUTDOWN_START_SETTLE_MS);
    embediq_fb_report_fault(idle, FAULT_REASON_DURING);

    embediq_osal_atomic32_store(&g_sd_gate, 1u);
    pthread_join(sd, NULL);
    alarm(0u);

    uint32_t calls  = embediq_osal_atomic32_load(&g_sd_fault_calls);
    uint32_t reason = embediq_osal_atomic32_load(&g_sd_fault_reason);
    uint32_t idle_n = embediq_osal_atomic32_load(&g_sd_idle_faults);
    printf("      in_run=%d queued_fault_calls=%u reason=0x%x during_shutdown_calls=%u\n",
           (int)in_run, (unsigned)calls, (unsigned)reason, (unsigned)idle_n);
    ASSERT(in_run && calls == 1u && reason == FAULT_REASON_QUEUED
           && embediq_fb_get_state(gated) == EMBEDIQ_FB_STATE_FAULT
           && idle_n == 1u && embediq_fb_get_state(idle) == EMBEDIQ_FB_STATE_FAULT,
           "faults queued before or reported during shutdown are applied once");
}

static embediq_osal_atomic32_t g_sd_report_done = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);

static void *sd_flagged_reporter(void *arg)
{
    const sd_report_t *r = (const sd_report_t *)arg;
    embediq_fb_report_fault(r->h, r->reason);
    embediq_osal_atomic32_store(&g_sd_report_done, 1u);
    return NULL;
}

/* ---------------------------------------------------------------------------
 * test_report_fault_never_waits_for_queue_capacity
 * run_fn holds the FB (gated) and its HIGH queue is at capacity. A reporter
 * returns at once, while run_fn is still gated: it waits neither for queue
 * capacity nor for the FB. fault_fn then runs exactly once, on the FB's own
 * dispatch thread, after run_fn returns.
 * ------------------------------------------------------------------------- */

static void test_report_fault_never_waits_for_queue_capacity(void)
{
    alarm(SHUTDOWN_HANG_GUARD_S);
    fb_engine__reset();
    embediq_obs_set_transport(EMBEDIQ_OBS_TRANSPORT_NULL);
    sd_reset_recorders();
    embediq_osal_atomic32_store(&g_sd_report_done, 0u);

    EmbedIQ_FB_Handle_t gated = embediq_fb_register(&k_sd_gated);
    EmbedIQ_FB_Handle_t src   = embediq_fb_register(&k_sd_src);
    embediq_engine_boot();
    embediq_engine_dispatch_boot();

    sd_publish(src, MSG_TEST_GATED, (uint8_t)EMBEDIQ_MSG_PRIORITY_NORMAL);
    bool in_run = exec_wait(&g_sd_in_run, GATE_MAX_MS);
    for (uint32_t i = 0u; i < EMBEDIQ_HIGH_QUEUE_DEPTH; i++) {
        sd_publish(src, MSG_TEST_FILL, (uint8_t)EMBEDIQ_MSG_PRIORITY_HIGH);
    }

    pthread_t th;
    sd_report_t full = { gated, FAULT_REASON_FULL_QUEUE };
    pthread_create(&th, NULL, sd_flagged_reporter, &full);
    bool returned_while_gated = exec_wait(&g_sd_report_done, REPORT_RETURN_WAIT_MS);
    uint32_t calls_while_gated = embediq_osal_atomic32_load(&g_sd_fault_calls);

    embediq_osal_atomic32_store(&g_sd_gate, 1u);
    pthread_join(th, NULL);
    bool ran = exec_wait(&g_sd_fault_calls, GATE_MAX_MS);
    (void)embediq_engine_dispatch_shutdown();
    alarm(0u);

    bool on_dispatch = ran && pthread_equal(g_sd_fault_thread, g_sd_run_thread)
                       && !pthread_equal(g_sd_fault_thread, th);
    uint32_t calls = embediq_osal_atomic32_load(&g_sd_fault_calls);
    printf("      in_run=%d returned_while_gated=%d calls_while_gated=%u calls=%u "
           "on_dispatch_thread=%d\n", (int)in_run, (int)returned_while_gated,
           (unsigned)calls_while_gated, (unsigned)calls, (int)on_dispatch);
    ASSERT(in_run && returned_while_gated && calls_while_gated == 0u && calls == 1u
           && on_dispatch && embediq_fb_get_state(gated) == EMBEDIQ_FB_STATE_FAULT,
           "full HIGH queue, busy FB: reporter returns at once; FB's thread executes");
}

/* ---------------------------------------------------------------------------
 * test_dispatch_shutdown_returns_under_reporter_storm
 * Four threads report faults in a tight loop while NORMAL messages are
 * queued; dispatch_shutdown returns within 500 ms. Every report is either
 * executed once or counted as inbox overflow, and every NORMAL message was
 * either delivered or counted in the one QUEUE_DROP
 * (EMBEDIQ_QUEUE_DROP_AT_SHUTDOWN) event.
 * Runs last: on a hang the alarm ends the binary.
 * ------------------------------------------------------------------------- */

static embediq_osal_atomic32_t g_storm_go      = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);
static embediq_osal_atomic32_t g_storm_stop    = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);
static embediq_osal_atomic32_t g_storm_reports = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);

static void *storm_reporter(void *h)
{
    uint32_t n = 0u;
    while (embediq_osal_atomic32_load(&g_storm_go) == 0u) { /* start together */ }
    while (embediq_osal_atomic32_load(&g_storm_stop) == 0u) {
        embediq_fb_report_fault((EmbedIQ_FB_Handle_t)h, FAULT_REASON_STORM);
        n++;
    }
    (void)embediq_osal_atomic32_add(&g_storm_reports, n);
    return NULL;
}

static void test_dispatch_shutdown_returns_under_reporter_storm(void)
{
    alarm(SHUTDOWN_HANG_GUARD_S);
    fb_engine__reset();
    embediq_obs_set_transport(EMBEDIQ_OBS_TRANSPORT_TCP);   /* ring only, no output */
    sd_reset_recorders();
    embediq_osal_atomic32_store(&g_storm_go, 0u);
    embediq_osal_atomic32_store(&g_storm_stop, 0u);
    embediq_osal_atomic32_store(&g_storm_reports, 0u);
    uint32_t overflow_before = fb_engine__fault_overflow_total();

    EmbedIQ_FB_Handle_t fb  = embediq_fb_register(&k_sd_gated);
    EmbedIQ_FB_Handle_t src = embediq_fb_register(&k_sd_src);
    embediq_engine_boot();
    embediq_engine_dispatch_boot();
    uint8_t ep = embediq_engine_fb_ep(fb);

    pthread_t th[STORM_REPORTERS];
    for (uint32_t i = 0u; i < STORM_REPORTERS; i++) {
        pthread_create(&th[i], NULL, storm_reporter, fb);
    }
    embediq_osal_atomic32_store(&g_storm_go, 1u);
    for (uint32_t i = 0u; i < STORM_NORMAL_MSGS; i++) {
        sd_publish(src, MSG_TEST_FILL, (uint8_t)EMBEDIQ_MSG_PRIORITY_NORMAL);
    }
    embediq_osal_delay_ms(STORM_RUN_MS);

    uint32_t t0 = embediq_osal_time_ms();
    (void)embediq_engine_dispatch_shutdown();
    uint32_t shutdown_ms = embediq_osal_time_ms() - t0;

    embediq_osal_atomic32_store(&g_storm_stop, 1u);
    for (uint32_t i = 0u; i < STORM_REPORTERS; i++) {
        pthread_join(th[i], NULL);
    }
    alarm(0u);

    uint32_t drop_events = 0u, dropped = 0u;
    EmbedIQ_Event_t e;
    for (uint32_t i = 0u; obs__ring_read(i, &e); i++) {
        if (e.event_type == EMBEDIQ_OBS_EVT_QUEUE_DROP
            && e.state_or_flag == EMBEDIQ_QUEUE_DROP_AT_SHUTDOWN && e.target_fb_id == ep) {
            drop_events++;
            dropped = e.msg_id;
        }
    }
    uint32_t reports   = embediq_osal_atomic32_load(&g_storm_reports);
    uint32_t calls     = embediq_osal_atomic32_load(&g_sd_fault_calls);
    uint32_t delivered = embediq_osal_atomic32_load(&g_sd_fill_rx);
    uint32_t overflow  = fb_engine__fault_overflow_total() - overflow_before;
    printf("      shutdown=%u ms reports=%u fault_fn=%u overflow=%u normal: delivered=%u "
           "dropped=%u (events=%u)\n", (unsigned)shutdown_ms, (unsigned)reports,
           (unsigned)calls, (unsigned)overflow, (unsigned)delivered, (unsigned)dropped,
           (unsigned)drop_events);
    ASSERT(shutdown_ms < SHUTDOWN_MAX_MS && calls + overflow == reports && calls > 0u
           && drop_events <= 1u && delivered + dropped == STORM_NORMAL_MSGS,
           "shutdown bounded under a reporter storm; executed + overflow == reported");
}

/* ===========================================================================
 * Fault inbox: execution only by the FB's owner, never before init_fn,
 * never a wait on another FB's execution lock
 * ======================================================================== */

/* ---------------------------------------------------------------------------
 * test_fault_before_init_runs_after_init_fn
 * A platform-phase FB's init_fn reports a fault on an application-phase FB
 * that has not been initialised yet. fault_fn runs exactly once, after the
 * target's init_fn has returned; the final state is FAULT and the target is
 * never observed RUNNING.
 * ------------------------------------------------------------------------- */

static EmbedIQ_FB_Handle_t     g_bi_target      = NULL;
static embediq_osal_atomic32_t g_bi_seq         = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);
static embediq_osal_atomic32_t g_bi_init_at     = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);
static embediq_osal_atomic32_t g_bi_fault_at    = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);
static embediq_osal_atomic32_t g_bi_fault_calls = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);
static embediq_osal_atomic32_t g_bi_fault_early = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);

static void bi_target_init(EmbedIQ_FB_Handle_t h, void *d)
{
    (void)h; (void)d;
    embediq_osal_atomic32_store(&g_bi_init_at, embediq_osal_atomic32_add(&g_bi_seq, 1u));
}

static void bi_target_fault(EmbedIQ_FB_Handle_t h, void *d, uint32_t r)
{
    (void)h; (void)d; (void)r;
    if (embediq_osal_atomic32_load(&g_bi_init_at) == 0u) {
        (void)embediq_osal_atomic32_add(&g_bi_fault_early, 1u);   /* resources not ready */
    }
    embediq_osal_atomic32_store(&g_bi_fault_at, embediq_osal_atomic32_add(&g_bi_seq, 1u));
    (void)embediq_osal_atomic32_add(&g_bi_fault_calls, 1u);
}

static void bi_platform_init(EmbedIQ_FB_Handle_t h, void *d)
{
    (void)h; (void)d;
    embediq_fb_report_fault(g_bi_target, FAULT_REASON_BEFORE_INIT);
}

static void test_fault_before_init_runs_after_init_fn(void)
{
    fb_engine__reset();
    embediq_obs_set_transport(EMBEDIQ_OBS_TRANSPORT_TCP);   /* ring only, no output */
    embediq_osal_atomic32_store(&g_bi_seq, 0u);
    embediq_osal_atomic32_store(&g_bi_init_at, 0u);
    embediq_osal_atomic32_store(&g_bi_fault_at, 0u);
    embediq_osal_atomic32_store(&g_bi_fault_calls, 0u);
    embediq_osal_atomic32_store(&g_bi_fault_early, 0u);

    static const EmbedIQ_FB_Config_t k_target = {
        .name = "bi_target", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
        .init_fn = bi_target_init, .fault_fn = bi_target_fault,
    };
    static const EmbedIQ_FB_Config_t k_platform = {
        .name = "bi_platform", .boot_phase = EMBEDIQ_BOOT_PHASE_PLATFORM,
        .init_fn = bi_platform_init,
    };
    g_bi_target = embediq_fb_register(&k_target);
    (void)embediq_fb_register(&k_platform);
    int rc = embediq_engine_boot();

    uint8_t  ep = embediq_engine_fb_ep(g_bi_target);
    uint32_t running_seen = 0u;
    EmbedIQ_Event_t e;
    for (uint32_t i = 0u; obs__ring_read(i, &e); i++) {
        if (e.event_type == EMBEDIQ_OBS_EVT_LIFECYCLE && e.source_fb_id == ep
            && e.state_or_flag == (uint8_t)EMBEDIQ_FB_STATE_RUNNING) {
            running_seen++;
        }
    }
    uint32_t calls    = embediq_osal_atomic32_load(&g_bi_fault_calls);
    uint32_t early    = embediq_osal_atomic32_load(&g_bi_fault_early);
    uint32_t init_at  = embediq_osal_atomic32_load(&g_bi_init_at);
    uint32_t fault_at = embediq_osal_atomic32_load(&g_bi_fault_at);
    EmbedIQ_FB_State_t st = embediq_fb_get_state(g_bi_target);
    printf("      boot=%d calls=%u before_init=%u order: init=%u fault=%u running_seen=%u "
           "state=%d\n", rc, (unsigned)calls, (unsigned)early, (unsigned)init_at,
           (unsigned)fault_at, (unsigned)running_seen, (int)st);
    ASSERT(rc == 0 && calls == 1u && early == 0u && init_at != 0u && fault_at > init_at
           && running_seen == 0u && st == EMBEDIQ_FB_STATE_FAULT,
           "fault reported before init: fault_fn runs once, after init_fn; never RUNNING");
}

/* ---------------------------------------------------------------------------
 * test_delayed_reporter_fault_is_executed_before_shutdown_returns
 * A reporter is held between acceptance and its follow-up action (host test
 * hook) while dispatch_shutdown runs. Shutdown returns without waiting for
 * it, and the fault has already been executed — once — before the
 * reporter's report_fault returns. A further dispatch_boot / shutdown cycle
 * does not execute it again.
 * ------------------------------------------------------------------------- */

static embediq_osal_atomic32_t g_dl_paused   = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);
static embediq_osal_atomic32_t g_dl_release  = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);
static embediq_osal_atomic32_t g_dl_returned = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);
static embediq_osal_atomic32_t g_dl_calls    = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);
static embediq_osal_atomic32_t g_dl_late     = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);

static void dl_hold_reporter(void)
{
    embediq_osal_atomic32_store(&g_dl_paused, 1u);
    (void)exec_wait(&g_dl_release, GATE_MAX_MS);
}

static void dl_fault_fn(EmbedIQ_FB_Handle_t h, void *d, uint32_t r)
{
    (void)h; (void)d; (void)r;
    if (embediq_osal_atomic32_load(&g_dl_returned) != 0u) {
        (void)embediq_osal_atomic32_add(&g_dl_late, 1u);
    }
    (void)embediq_osal_atomic32_add(&g_dl_calls, 1u);
}

static void *dl_reporter(void *h)
{
    embediq_fb_report_fault((EmbedIQ_FB_Handle_t)h, FAULT_REASON_DELAYED);
    embediq_osal_atomic32_store(&g_dl_returned, 1u);
    return NULL;
}

static void test_delayed_reporter_fault_is_executed_before_shutdown_returns(void)
{
    alarm(SHUTDOWN_HANG_GUARD_S);
    fb_engine__reset();
    embediq_obs_set_transport(EMBEDIQ_OBS_TRANSPORT_NULL);
    embediq_osal_atomic32_store(&g_dl_paused, 0u);
    embediq_osal_atomic32_store(&g_dl_release, 0u);
    embediq_osal_atomic32_store(&g_dl_returned, 0u);
    embediq_osal_atomic32_store(&g_dl_calls, 0u);
    embediq_osal_atomic32_store(&g_dl_late, 0u);

    static const EmbedIQ_FB_Config_t k_cfg = {
        .name = "dl_subscriber", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
        .fault_fn = dl_fault_fn,
        .subscriptions = g_sd_idle_subs, .subscription_count = 1,
    };
    EmbedIQ_FB_Handle_t h = embediq_fb_register(&k_cfg);
    embediq_engine_boot();
    embediq_engine_dispatch_boot();

    fb_engine__set_report_delay_hook(dl_hold_reporter);
    pthread_t th;
    pthread_create(&th, NULL, dl_reporter, h);
    bool paused = exec_wait(&g_dl_paused, GATE_MAX_MS);

    uint32_t t0 = embediq_osal_time_ms();
    (void)embediq_engine_dispatch_shutdown();
    uint32_t shutdown_ms = embediq_osal_time_ms() - t0;
    uint32_t at_shutdown = embediq_osal_atomic32_load(&g_dl_calls);

    embediq_osal_atomic32_store(&g_dl_release, 1u);
    pthread_join(th, NULL);
    fb_engine__set_report_delay_hook(NULL);
    uint32_t at_return = embediq_osal_atomic32_load(&g_dl_calls);

    embediq_engine_dispatch_boot();
    (void)embediq_engine_dispatch_shutdown();
    alarm(0u);
    uint32_t after_restart = embediq_osal_atomic32_load(&g_dl_calls);
    uint32_t late          = embediq_osal_atomic32_load(&g_dl_late);

    printf("      reporter_held=%d shutdown=%u ms executed_at_shutdown_return=%u "
           "at_reporter_return=%u after_restart=%u after_reporter_returned=%u\n",
           (int)paused, (unsigned)shutdown_ms, (unsigned)at_shutdown, (unsigned)at_return,
           (unsigned)after_restart, (unsigned)late);
    ASSERT(paused && at_shutdown == 1u && at_return == 1u && after_restart == 1u
           && late == 0u && embediq_fb_get_state(h) == EMBEDIQ_FB_STATE_FAULT,
           "delayed reporter: fault executed once, before shutdown and report_fault return");
}

/* ---------------------------------------------------------------------------
 * test_cross_faults_between_dispatch_fbs_with_full_queues_complete
 * Two subscribed FBs, each with a dispatch thread, hold their fault_fns on a
 * gate while both HIGH queues are filled to capacity; released, each
 * fault_fn reports a fault on the other. Both complete within 500 ms, each
 * FB executes both reasons, and dispatch_shutdown returns.
 * Runs last: on a deadlock the alarm ends the binary.
 * ------------------------------------------------------------------------- */

static EmbedIQ_FB_Handle_t     g_cx_fb[CROSS_FBS];
static embediq_osal_atomic32_t g_cx_entered  = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);
static embediq_osal_atomic32_t g_cx_release  = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);
static embediq_osal_atomic32_t g_cx_finished = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);
static embediq_osal_atomic32_t g_cx_other[CROSS_FBS];

static void cx_fault_fn(EmbedIQ_FB_Handle_t h, void *d, uint32_t r)
{
    (void)d;
    uint32_t self = (h == g_cx_fb[0]) ? 0u : 1u;
    if (r == FAULT_REASON_CROSS_FIRST) {
        (void)embediq_osal_atomic32_add(&g_cx_entered, 1u);
        (void)exec_wait(&g_cx_release, GATE_MAX_MS);
        embediq_fb_report_fault(g_cx_fb[1u - self], FAULT_REASON_CROSS_OTHER);
        (void)embediq_osal_atomic32_add(&g_cx_finished, 1u);
    } else if (r == FAULT_REASON_CROSS_OTHER) {
        (void)embediq_osal_atomic32_add(&g_cx_other[self], 1u);
    }
}

static bool cx_wait_count(embediq_osal_atomic32_t *a, uint32_t n, uint32_t ms)
{
    uint32_t t0 = embediq_osal_time_ms();
    while (embediq_osal_atomic32_load(a) < n && (embediq_osal_time_ms() - t0) < ms) {
        embediq_osal_delay_ms(1u);
    }
    return embediq_osal_atomic32_load(a) >= n;
}

EMBEDIQ_SUBS(g_cx_subs, MSG_TEST_CROSS);

static void test_cross_faults_between_dispatch_fbs_with_full_queues_complete(void)
{
    alarm(SHUTDOWN_HANG_GUARD_S);
    fb_engine__reset();
    embediq_obs_set_transport(EMBEDIQ_OBS_TRANSPORT_NULL);
    embediq_osal_atomic32_store(&g_cx_entered, 0u);
    embediq_osal_atomic32_store(&g_cx_release, 0u);
    embediq_osal_atomic32_store(&g_cx_finished, 0u);
    for (uint32_t i = 0u; i < CROSS_FBS; i++) {
        embediq_osal_atomic32_store(&g_cx_other[i], 0u);
    }

    static const EmbedIQ_FB_Config_t k_a = {
        .name = "cx_a", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
        .fault_fn = cx_fault_fn, .subscriptions = g_cx_subs, .subscription_count = 1,
    };
    static const EmbedIQ_FB_Config_t k_b = {
        .name = "cx_b", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
        .fault_fn = cx_fault_fn, .subscriptions = g_cx_subs, .subscription_count = 1,
    };
    g_cx_fb[0] = embediq_fb_register(&k_a);
    g_cx_fb[1] = embediq_fb_register(&k_b);
    EmbedIQ_FB_Handle_t src = embediq_fb_register(&k_sd_src);
    embediq_engine_boot();
    embediq_engine_dispatch_boot();

    embediq_fb_report_fault(g_cx_fb[0], FAULT_REASON_CROSS_FIRST);
    embediq_fb_report_fault(g_cx_fb[1], FAULT_REASON_CROSS_FIRST);
    bool entered = cx_wait_count(&g_cx_entered, CROSS_FBS, GATE_MAX_MS);
    for (uint32_t i = 0u; i < EMBEDIQ_HIGH_QUEUE_DEPTH; i++) {
        sd_publish(src, MSG_TEST_CROSS, (uint8_t)EMBEDIQ_MSG_PRIORITY_HIGH);   /* both queues */
    }

    uint32_t t0 = embediq_osal_time_ms();
    embediq_osal_atomic32_store(&g_cx_release, 1u);
    bool finished = cx_wait_count(&g_cx_finished, CROSS_FBS, CROSS_COMPLETE_MAX_MS);
    bool other_a  = cx_wait_count(&g_cx_other[0], 1u, CROSS_COMPLETE_MAX_MS);
    bool other_b  = cx_wait_count(&g_cx_other[1], 1u, CROSS_COMPLETE_MAX_MS);
    uint32_t complete_ms = embediq_osal_time_ms() - t0;
    int rc = embediq_engine_dispatch_shutdown();
    alarm(0u);

    printf("      entered=%u completed=%u other_reason_executed: a=%u b=%u in %u ms "
           "shutdown=%d\n", (unsigned)embediq_osal_atomic32_load(&g_cx_entered),
           (unsigned)embediq_osal_atomic32_load(&g_cx_finished),
           (unsigned)embediq_osal_atomic32_load(&g_cx_other[0]),
           (unsigned)embediq_osal_atomic32_load(&g_cx_other[1]), (unsigned)complete_ms, rc);
    ASSERT(entered && finished && other_a && other_b && complete_ms < CROSS_COMPLETE_MAX_MS
           && rc == 0,
           "mutual reports from fault_fns with full HIGH queues complete; no deadlock");
}

/* ---------------------------------------------------------------------------
 * test_report_from_fault_fn_never_blocks_on_other_fb
 * FB A's fault_fn (running under A's execution lock) reports a fault on FB
 * B while B's code is running on another thread (a gated handler). A's
 * fault_fn returns within 10 ms; B's fault executes once, on the thread
 * running B's handler, when that handler returns.
 * ------------------------------------------------------------------------- */

static EmbedIQ_FB_Handle_t     g_ob_b          = NULL;
static embediq_osal_atomic32_t g_ob_in_hold    = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);
static embediq_osal_atomic32_t g_ob_gate       = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);
static embediq_osal_atomic32_t g_ob_report_us  = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);
static embediq_osal_atomic32_t g_ob_b_calls    = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);
static embediq_osal_atomic32_t g_ob_b_early    = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);
static pthread_t               g_ob_holder_thread;
static pthread_t               g_ob_b_fault_thread;

static void ob_a_fault_fn(EmbedIQ_FB_Handle_t h, void *d, uint32_t r)
{
    (void)h; (void)d; (void)r;
    uint32_t t0 = embediq_osal_time_us();
    embediq_fb_report_fault(g_ob_b, FAULT_REASON_ON_HELD);
    embediq_osal_atomic32_store(&g_ob_report_us, embediq_osal_time_us() - t0);
}

static void ob_b_fault_fn(EmbedIQ_FB_Handle_t h, void *d, uint32_t r)
{
    (void)h; (void)d; (void)r;
    if (embediq_osal_atomic32_load(&g_ob_gate) == 0u) {
        (void)embediq_osal_atomic32_add(&g_ob_b_early, 1u);   /* overlapped the handler */
    }
    g_ob_b_fault_thread = pthread_self();
    (void)embediq_osal_atomic32_add(&g_ob_b_calls, 1u);
}

static void ob_hold_run_fn(EmbedIQ_FB_Handle_t fb, const void *msg, void *d, void *s)
{
    (void)fb; (void)msg; (void)d; (void)s;
    embediq_osal_atomic32_store(&g_ob_in_hold, 1u);
    (void)exec_wait(&g_ob_gate, GATE_MAX_MS);
}

EMBEDIQ_SUBS(g_ob_hold_subs, MSG_TEST_HOLD);

static const EmbedIQ_SubFn_Config_t g_ob_hold_subfn = {
    .name = "ob_hold", .init_order = 0,
    .subscriptions = g_ob_hold_subs, .subscription_count = 1,
    .run_fn = ob_hold_run_fn,
};

static void ob_b_init(EmbedIQ_FB_Handle_t h, void *d)
{
    (void)d;
    embediq_subfn_register(h, &g_ob_hold_subfn);
}

static void *ob_holder(void *h)
{
    uint8_t payload[EMBEDIQ_MSG_MAX_PAYLOAD];
    memset(payload, 0, sizeof(payload));
    g_ob_holder_thread = pthread_self();
    fb_engine__deliver_msg((EmbedIQ_FB_Handle_t)h, MSG_TEST_HOLD, payload);
    return NULL;
}

static void test_report_from_fault_fn_never_blocks_on_other_fb(void)
{
    fb_engine__reset();
    embediq_obs_set_transport(EMBEDIQ_OBS_TRANSPORT_NULL);
    embediq_osal_atomic32_store(&g_ob_in_hold, 0u);
    embediq_osal_atomic32_store(&g_ob_gate, 0u);
    embediq_osal_atomic32_store(&g_ob_report_us, 0u);
    embediq_osal_atomic32_store(&g_ob_b_calls, 0u);
    embediq_osal_atomic32_store(&g_ob_b_early, 0u);

    static const EmbedIQ_FB_Config_t k_a = {
        .name = "ob_a", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
        .fault_fn = ob_a_fault_fn,
    };
    static const EmbedIQ_FB_Config_t k_b = {
        .name = "ob_b", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
        .init_fn = ob_b_init, .fault_fn = ob_b_fault_fn,
    };
    EmbedIQ_FB_Handle_t a = embediq_fb_register(&k_a);
    g_ob_b = embediq_fb_register(&k_b);
    embediq_engine_boot();

    pthread_t holder;
    pthread_create(&holder, NULL, ob_holder, g_ob_b);
    bool held = exec_wait(&g_ob_in_hold, GATE_MAX_MS);

    embediq_fb_report_fault(a, FAULT_REASON_OUTER);   /* A's fault_fn runs here */
    uint32_t report_us   = embediq_osal_atomic32_load(&g_ob_report_us);
    uint32_t b_while_held = embediq_osal_atomic32_load(&g_ob_b_calls);

    embediq_osal_atomic32_store(&g_ob_gate, 1u);
    pthread_join(holder, NULL);
    uint32_t b_calls = embediq_osal_atomic32_load(&g_ob_b_calls);
    bool on_holder   = b_calls == 1u && pthread_equal(g_ob_b_fault_thread, holder);

    printf("      held=%d report_on_busy_fb=%u us b_executed_while_held=%u b_calls=%u "
           "on_holder_thread=%d\n", (int)held, (unsigned)report_us, (unsigned)b_while_held,
           (unsigned)b_calls, (int)on_holder);
    ASSERT(held && report_us < OTHER_FB_REPORT_MAX_US && b_while_held == 0u
           && on_holder && embediq_osal_atomic32_load(&g_ob_b_early) == 0u
           && embediq_fb_get_state(g_ob_b) == EMBEDIQ_FB_STATE_FAULT,
           "fault_fn reporting on a busy FB returns at once; the holder executes it");
}

/* ---------------------------------------------------------------------------
 * test_inbox_overflow_counted_once
 * The FB's fault_fn is held on a gate by the first report; 17 more reports
 * arrive from six threads (18 in all). After release fault_fn has run
 * 1 + EMBEDIQ_FB_FAULT_INBOX_DEPTH times, and exactly one PENDING_OVERFLOW
 * event counts the other 18 - 1 - EMBEDIQ_FB_FAULT_INBOX_DEPTH.
 * ------------------------------------------------------------------------- */

static embediq_osal_atomic32_t g_ov_entered = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);
static embediq_osal_atomic32_t g_ov_gate    = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);
static embediq_osal_atomic32_t g_ov_calls   = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);

static void ov_fault_fn(EmbedIQ_FB_Handle_t h, void *d, uint32_t r)
{
    (void)h; (void)d; (void)r;
    if (embediq_osal_atomic32_add(&g_ov_calls, 1u) == 1u) {
        embediq_osal_atomic32_store(&g_ov_entered, 1u);
        (void)exec_wait(&g_ov_gate, GATE_MAX_MS);
    }
}

typedef struct { EmbedIQ_FB_Handle_t h; uint32_t n; } ov_job_t;

static void *ov_reporter(void *arg)
{
    const ov_job_t *j = (const ov_job_t *)arg;
    for (uint32_t i = 0u; i < j->n; i++) {
        embediq_fb_report_fault(j->h, FAULT_REASON_OVERFLOW);
    }
    return NULL;
}

static void test_inbox_overflow_counted_once(void)
{
    alarm(SHUTDOWN_HANG_GUARD_S * 2u);   /* the unfixed engine holds a reporter up to the gate bound */
    fb_engine__reset();
    embediq_obs_set_transport(EMBEDIQ_OBS_TRANSPORT_TCP);   /* ring only, no output */
    embediq_osal_atomic32_store(&g_ov_entered, 0u);
    embediq_osal_atomic32_store(&g_ov_gate, 0u);
    embediq_osal_atomic32_store(&g_ov_calls, 0u);

    static const EmbedIQ_FB_Config_t k_cfg = {
        .name = "ov_subscriber", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
        .fault_fn = ov_fault_fn,
        .subscriptions = g_sd_idle_subs, .subscription_count = 1,
    };
    EmbedIQ_FB_Handle_t h = embediq_fb_register(&k_cfg);
    embediq_engine_boot();
    embediq_engine_dispatch_boot();
    uint8_t ep = embediq_engine_fb_ep(h);

    embediq_fb_report_fault(h, FAULT_REASON_OVERFLOW);   /* executes, then holds */
    bool entered = exec_wait(&g_ov_entered, GATE_MAX_MS);

    pthread_t th[OVERFLOW_REPORTERS];
    ov_job_t  jobs[OVERFLOW_REPORTERS];
    for (uint32_t i = 0u; i < OVERFLOW_REPORTERS; i++) {
        jobs[i].h = h;
        jobs[i].n = (i == 0u) ? OVERFLOW_REASONS_EACH - 1u : OVERFLOW_REASONS_EACH;
        pthread_create(&th[i], NULL, ov_reporter, &jobs[i]);
    }
    for (uint32_t i = 0u; i < OVERFLOW_REPORTERS; i++) {
        pthread_join(th[i], NULL);
    }
    embediq_osal_atomic32_store(&g_ov_gate, 1u);
    (void)embediq_engine_dispatch_shutdown();   /* executes everything still latched */
    alarm(0u);

    uint32_t events = 0u, counted = 0u;
    EmbedIQ_Event_t e;
    for (uint32_t i = 0u; obs__ring_read(i, &e); i++) {
        if (e.event_type == EMBEDIQ_OBS_EVT_FAULT && e.target_fb_id == ep
            && e.state_or_flag == (uint8_t)EMBEDIQ_FAULT_PENDING_OVERFLOW) {
            events++;
            counted = e.msg_id;
        }
    }
    uint32_t calls = embediq_osal_atomic32_load(&g_ov_calls);
    printf("      entered=%d reports=%u executed=%u overflow_events=%u counted=%u\n",
           (int)entered, (unsigned)OVERFLOW_REPORTS, (unsigned)calls, (unsigned)events,
           (unsigned)counted);
    ASSERT(entered && calls == 1u + EMBEDIQ_FB_FAULT_INBOX_DEPTH && events == 1u
           && counted == OVERFLOW_REPORTS - 1u - EMBEDIQ_FB_FAULT_INBOX_DEPTH,
           "inbox overflow: 1 + depth executed, the rest counted in one event");
}

/* ---------------------------------------------------------------------------
 * test_fault_latched_during_failed_boot_is_reported_not_executed
 * A platform-phase FB's init_fn reports a fault on an application-phase FB;
 * boot then fails (dependency cycle in the application phase). The latched
 * fault is never executed: one BOOT_ABORTED event for that FB counts it, and
 * a dispatch_boot afterwards creates no dispatch thread (detected through
 * the OSAL task-create failure seam: an armed failure stays unconsumed).
 * ------------------------------------------------------------------------- */

static EmbedIQ_FB_Handle_t     g_ab_target = NULL;
static embediq_osal_atomic32_t g_ab_faults = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);

static void ab_fault_fn(EmbedIQ_FB_Handle_t h, void *d, uint32_t r)
{
    (void)h; (void)d; (void)r;
    (void)embediq_osal_atomic32_add(&g_ab_faults, 1u);
}

static void ab_platform_init(EmbedIQ_FB_Handle_t h, void *d)
{
    (void)h; (void)d;
    embediq_fb_report_fault(g_ab_target, FAULT_REASON_ABORTED);
}

static void ab_probe_task(void *arg) { (void)arg; }

static void test_fault_latched_during_failed_boot_is_reported_not_executed(void)
{
    fb_engine__reset();
    embediq_obs_set_transport(EMBEDIQ_OBS_TRANSPORT_TCP);   /* ring only, no output */
    embediq_osal_atomic32_store(&g_ab_faults, 0u);

    static const char *const k_dep_b[] = { "ab_cycle_b" };
    static const char *const k_dep_a[] = { "ab_cycle_a" };
    static const EmbedIQ_FB_Config_t k_a = {
        .name = "ab_cycle_a", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
        .fault_fn = ab_fault_fn, .depends_on = k_dep_b, .depends_count = 1,
        .subscriptions = g_sd_idle_subs, .subscription_count = 1,
    };
    static const EmbedIQ_FB_Config_t k_b = {
        .name = "ab_cycle_b", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
        .fault_fn = ab_fault_fn, .depends_on = k_dep_a, .depends_count = 1,
        .subscriptions = g_sd_idle_subs, .subscription_count = 1,
    };
    static const EmbedIQ_FB_Config_t k_platform = {
        .name = "ab_platform", .boot_phase = EMBEDIQ_BOOT_PHASE_PLATFORM,
        .init_fn = ab_platform_init,
    };
    g_ab_target = embediq_fb_register(&k_a);
    (void)embediq_fb_register(&k_b);
    (void)embediq_fb_register(&k_platform);
    int rc = embediq_engine_boot();
    uint8_t ep = embediq_engine_fb_ep(g_ab_target);

    embediq_osal__fail_next_task_create(1u);
    embediq_engine_dispatch_boot();
    EmbedIQ_Task_t *probe = embediq_osal_task_create("ab_probe", ab_probe_task, NULL,
                                                     EMBEDIQ_DISPATCH_DEFAULT_PRIORITY,
                                                     EMBEDIQ_DISPATCH_STACK_SIZE);
    embediq_osal__fail_next_task_create(0u);
    bool no_thread = probe == NULL;     /* the armed failure reached this create */
    if (probe) embediq_osal_task_join(probe);
    (void)embediq_engine_dispatch_shutdown();

    uint32_t events = 0u, counted = 0u;
    EmbedIQ_Event_t e;
    for (uint32_t i = 0u; obs__ring_read(i, &e); i++) {
        if (e.event_type == EMBEDIQ_OBS_EVT_FAULT && e.target_fb_id == ep
            && e.state_or_flag == (uint8_t)EMBEDIQ_FAULT_BOOT_ABORTED) {
            events++;
            counted = e.msg_id;
        }
    }
    uint32_t faults = embediq_osal_atomic32_load(&g_ab_faults);
    printf("      boot=%d fault_fn_runs=%u boot_aborted_events=%u counted=%u "
           "dispatch_threads_created=%d\n", rc, (unsigned)faults, (unsigned)events,
           (unsigned)counted, (int)!no_thread);
    ASSERT(rc != 0 && faults == 0u && events == 1u && counted == 1u && no_thread,
           "failed boot: latched fault counted in one BOOT_ABORTED event, never executed");
}

/* ---------------------------------------------------------------------------
 * test_dispatch_boot_after_task_create_failure_keeps_owner_none
 * The FB's dispatch thread cannot be created. Its faults are still executed:
 * a report after dispatch_boot runs fault_fn on the reporting thread before
 * report_fault returns.
 * ------------------------------------------------------------------------- */

static embediq_osal_atomic32_t g_nt_faults = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);
static pthread_t               g_nt_fault_thread;

static void nt_fault_fn(EmbedIQ_FB_Handle_t h, void *d, uint32_t r)
{
    (void)h; (void)d; (void)r;
    g_nt_fault_thread = pthread_self();
    (void)embediq_osal_atomic32_add(&g_nt_faults, 1u);
}

EMBEDIQ_SUBS(g_nt_subs, MSG_TEST_NO_THREAD);

static void test_dispatch_boot_after_task_create_failure_keeps_owner_none(void)
{
    fb_engine__reset();
    embediq_obs_set_transport(EMBEDIQ_OBS_TRANSPORT_NULL);
    embediq_osal_atomic32_store(&g_nt_faults, 0u);

    static const EmbedIQ_FB_Config_t k_cfg = {
        .name = "nt_subscriber", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
        .fault_fn = nt_fault_fn, .subscriptions = g_nt_subs, .subscription_count = 1,
    };
    EmbedIQ_FB_Handle_t h = embediq_fb_register(&k_cfg);
    embediq_engine_boot();

    embediq_osal__fail_next_task_create(1u);    /* its dispatch thread */
    embediq_engine_dispatch_boot();
    embediq_osal__fail_next_task_create(0u);

    embediq_fb_report_fault(h, FAULT_REASON_NO_THREAD);
    uint32_t at_return = embediq_osal_atomic32_load(&g_nt_faults);
    bool here = at_return == 1u && pthread_equal(g_nt_fault_thread, pthread_self());
    int rc = embediq_engine_dispatch_shutdown();

    printf("      executed_before_return=%u on_calling_thread=%d shutdown=%d\n",
           (unsigned)at_return, (int)here, rc);
    ASSERT(here && rc == 0 && embediq_fb_get_state(h) == EMBEDIQ_FB_STATE_FAULT,
           "no dispatch thread after a create failure: the reporter executes the fault");
}

int main(void)
{
    test_fb_register_stores_config();
    test_boot_phase_order_platform_before_infrastructure();
    test_boot_phase_order_infrastructure_before_application();
    test_depends_on_topological_sort();
    test_cycle_detection_returns_fault();
    test_missing_dep_returns_fault();
    test_report_fault_changes_state();
    test_subfn_dispatch_in_registration_order();
    test_lifecycle_events_emitted_to_observatory();
    test_max_endpoint_overflow_returns_null();
    test_report_fault_from_foreign_thread_applies_on_fb_thread();
    test_report_fault_inline_for_fb_without_dispatch_thread();
    test_get_state_concurrent_tsan();
    test_engine_fb_api_matches_registry();
    test_platform_thread_publish_during_boot_is_not_lost();
    test_fault_fn_never_concurrent_for_non_dispatch_fb();
    test_fault_fn_never_concurrent_before_dispatch_boot();
    test_dispatch_waits_for_inflight_inline_fault();
    test_fault_during_init_fn_is_serialised_and_not_overwritten();
    test_boot_fails_when_exec_lock_allocation_fails();
    test_nested_fault_reports_all_delivered_in_order();
    test_run_fn_cost_under_exec_lock();
    test_fault_queued_before_shutdown_is_applied();
    test_report_fault_never_waits_for_queue_capacity();
    test_fault_before_init_runs_after_init_fn();
    test_report_from_fault_fn_never_blocks_on_other_fb();
    test_inbox_overflow_counted_once();
    test_fault_latched_during_failed_boot_is_reported_not_executed();
    test_dispatch_boot_after_task_create_failure_keeps_owner_none();
    test_delayed_reporter_fault_is_executed_before_shutdown_returns();
    test_dispatch_shutdown_returns_under_reporter_storm();   /* alarm-bounded */
    test_cross_faults_between_dispatch_fbs_with_full_queues_complete();   /* last: alarm-bounded */
    fb_engine__reset();

    printf("\nAll %d tests passed. (%d failed)\n",
           g_tests_run - g_tests_failed, g_tests_failed);
    return g_tests_failed > 0 ? 1 : 0;
}
