#define _POSIX_C_SOURCE 200809L

/*
 * tests/unit/test_fb_engine.c — Unit tests for core FB engine
 *
 * Covers every contractual behaviour of core/src/fb_engine.c.
 * Zero external test framework — stdlib only.
 * Each test exercises exactly one behaviour with one ASSERT.
 *
 * Run:      ./build/tests/unit/test_fb_engine
 * Expected: "All 17 tests passed. (0 failed)"
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

/* ---------------------------------------------------------------------------
 * F3 named constants
 * ------------------------------------------------------------------------- */

#define F3_MSG_PING          0x1430u  /* community range, unique to this file */
#define F3_MSG_BURST         0x1431u
#define F3_FAULT_REASON      0x55u
#define F3_WAIT_MS           200u
#define F3_GETSTATE_THREADS  4u
#define F3_GETSTATE_ITERS    20000u
#define F3_BURST_COUNT       50u
#define F3_BURST_SPACING_MS  2u
#define F3_BURST_WAIT_MS     500u
#define F3_API_FBS           3u

/* F9 (review R4) named constants */
#define F9_FAULT_SLEEP_MS    100u     /* fault_fn hold time: widens any overlap */
#define F9_REPORTERS         2u
#define F9_MSG_SUB           0x1432u  /* community range, unique to this file */

/* ---------------------------------------------------------------------------
 * Minimal test harness
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
 * F3 — faults on the FB thread, engine API, boot-order race
 * ======================================================================== */

/* Shared recorders. pthread_t values are published by the atomic flags that
 * guard them (seq_cst store/load gives the needed happens-before). */
static embediq_osal_atomic32_t g_f3_run_seen   = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);
static embediq_osal_atomic32_t g_f3_fault_seen = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);
static pthread_t               g_f3_run_thread;
static pthread_t               g_f3_fault_thread;
static uint32_t                g_f3_fault_reason;

static void f3_run_fn(EmbedIQ_FB_Handle_t fb, const void *msg, void *fb_data,
                      void *subfn_data)
{
    (void)fb; (void)msg; (void)fb_data; (void)subfn_data;
    g_f3_run_thread = pthread_self();
    embediq_osal_atomic32_store(&g_f3_run_seen, 1u);
}

static void f3_fault_fn(EmbedIQ_FB_Handle_t fb, void *fb_data, uint32_t reason)
{
    (void)fb; (void)fb_data;
    g_f3_fault_thread = pthread_self();
    g_f3_fault_reason = reason;
    embediq_osal_atomic32_store(&g_f3_fault_seen, 1u);
}

EMBEDIQ_SUBS(g_f3_ping_subs, F3_MSG_PING);

static const EmbedIQ_SubFn_Config_t g_f3_ping_subfn = {
    .name = "f3_ping", .init_order = 0,
    .subscriptions = g_f3_ping_subs, .subscription_count = 1,
    .run_fn = f3_run_fn,
};

static void f3_ping_init(EmbedIQ_FB_Handle_t h, void *d)
{
    (void)d;
    embediq_subfn_register(h, &g_f3_ping_subfn);
}

/* Spin (1 ms steps) until flag != 0 or ms elapse. Returns the final flag. */
static bool f3_wait_flag(embediq_osal_atomic32_t *flag, uint32_t ms)
{
    uint32_t t0 = embediq_osal_time_ms();
    while (embediq_osal_atomic32_load(flag) == 0u
           && (embediq_osal_time_ms() - t0) < ms) {
        embediq_osal_delay_ms(1u);
    }
    return embediq_osal_atomic32_load(flag) != 0u;
}

typedef struct { EmbedIQ_FB_Handle_t h; uint32_t reason; } f3_fault_req_t;

static void *f3_fault_reporter(void *arg)
{
    const f3_fault_req_t *r = (const f3_fault_req_t *)arg;
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
    embediq_osal_atomic32_store(&g_f3_run_seen, 0u);
    embediq_osal_atomic32_store(&g_f3_fault_seen, 0u);

    static const EmbedIQ_FB_Config_t k_src = {
        .name = "f3_src", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
    };
    static const EmbedIQ_FB_Config_t k_dst = {
        .name = "f3_dst", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
        .init_fn = f3_ping_init, .fault_fn = f3_fault_fn,
        .subscriptions = g_f3_ping_subs, .subscription_count = 1,
    };
    EmbedIQ_FB_Handle_t src = embediq_fb_register(&k_src);
    EmbedIQ_FB_Handle_t dst = embediq_fb_register(&k_dst);
    embediq_engine_boot();
    embediq_engine_dispatch_boot();

    /* Learn the dispatch thread's identity through a normal message. */
    EmbedIQ_Msg_t m;
    memset(&m, 0, sizeof(m));
    m.msg_id   = F3_MSG_PING;
    m.priority = EMBEDIQ_MSG_PRIORITY_NORMAL;
    embediq_publish(src, &m);
    bool ran = f3_wait_flag(&g_f3_run_seen, F3_WAIT_MS);

    pthread_t th;
    f3_fault_req_t req = { dst, F3_FAULT_REASON };
    pthread_create(&th, NULL, f3_fault_reporter, &req);
    pthread_join(th, NULL);
    bool faulted = f3_wait_flag(&g_f3_fault_seen, F3_WAIT_MS);

    bool ok = ran && faulted
              && pthread_equal(g_f3_fault_thread, g_f3_run_thread)
              && !pthread_equal(g_f3_fault_thread, th)
              && g_f3_fault_reason == F3_FAULT_REASON
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
    embediq_osal_atomic32_store(&g_f3_fault_seen, 0u);

    static const EmbedIQ_FB_Config_t k_cfg = {
        .name = "f3_nosub", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
        .fault_fn = f3_fault_fn,
    };
    EmbedIQ_FB_Handle_t h = embediq_fb_register(&k_cfg);
    embediq_engine_boot();
    embediq_engine_dispatch_boot();

    pthread_t th;
    f3_fault_req_t req = { h, F3_FAULT_REASON };
    pthread_create(&th, NULL, f3_fault_reporter, &req);
    pthread_join(th, NULL);   /* report_fault has returned */

    bool ok = embediq_osal_atomic32_load(&g_f3_fault_seen) == 1u
              && pthread_equal(g_f3_fault_thread, th)
              && embediq_fb_get_state(h) == EMBEDIQ_FB_STATE_FAULT;
    (void)embediq_engine_dispatch_shutdown();
    ASSERT(ok, "no-dispatch-thread FB: fault applied inline on the reporting thread");
}

/* ---------------------------------------------------------------------------
 * test_get_state_concurrent_tsan
 * Readers poll embediq_fb_get_state() while another thread reports faults.
 * No crash; under TSan, no report on the state field.
 * ------------------------------------------------------------------------- */

static EmbedIQ_FB_Handle_t     g_f3_state_fb   = NULL;
static embediq_osal_atomic32_t g_f3_state_stop = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);

static void *f3_state_reader(void *arg)
{
    (void)arg;
    uint32_t n = 0u;
    while (embediq_osal_atomic32_load(&g_f3_state_stop) == 0u && n < F3_GETSTATE_ITERS) {
        (void)embediq_fb_get_state(g_f3_state_fb);
        n++;
    }
    return NULL;
}

static void test_get_state_concurrent_tsan(void)
{
    fb_engine__reset();
    static const EmbedIQ_FB_Config_t k_cfg = {
        .name = "f3_state", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
    };
    g_f3_state_fb = embediq_fb_register(&k_cfg);
    embediq_engine_boot();
    embediq_osal_atomic32_store(&g_f3_state_stop, 0u);

    pthread_t rd[F3_GETSTATE_THREADS], wr;
    for (uint32_t i = 0u; i < F3_GETSTATE_THREADS; i++) {
        pthread_create(&rd[i], NULL, f3_state_reader, NULL);
    }
    f3_fault_req_t req = { g_f3_state_fb, F3_FAULT_REASON };
    pthread_create(&wr, NULL, f3_fault_reporter, &req);
    pthread_join(wr, NULL);
    pthread_create(&wr, NULL, f3_fault_reporter, &req);   /* report twice */
    pthread_join(wr, NULL);
    embediq_osal_atomic32_store(&g_f3_state_stop, 1u);
    for (uint32_t i = 0u; i < F3_GETSTATE_THREADS; i++) {
        pthread_join(rd[i], NULL);
    }
    ASSERT(embediq_fb_get_state(g_f3_state_fb) == EMBEDIQ_FB_STATE_FAULT,
           "concurrent get_state while faulting must be safe and end in FAULT");
}

/* ---------------------------------------------------------------------------
 * test_engine_fb_api_matches_registry
 * embediq_engine_fb_count / fb_config / fb_ep agree with what was registered.
 * ------------------------------------------------------------------------- */

static void test_engine_fb_api_matches_registry(void)
{
    fb_engine__reset();
    static const EmbedIQ_FB_Config_t k_cfgs[F3_API_FBS] = {
        { .name = "f3_api_a", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION },
        { .name = "f3_api_b", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION },
        { .name = "f3_api_c", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION },
    };
    EmbedIQ_FB_Handle_t h[F3_API_FBS];
    for (uint8_t i = 0u; i < F3_API_FBS; i++) h[i] = embediq_fb_register(&k_cfgs[i]);
    embediq_engine_boot();

    bool ok = embediq_engine_fb_count() == F3_API_FBS
              && embediq_engine_fb_config(F3_API_FBS) == NULL
              && embediq_engine_fb_ep(NULL) == 0xFFu;
    for (uint8_t i = 0u; ok && i < F3_API_FBS; i++) {
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

static EmbedIQ_FB_Handle_t     g_f3_burst_src = NULL;
static embediq_osal_atomic32_t g_f3_burst_rx  = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);
static pthread_t               g_f3_burst_th;

static void *f3_burst_publisher(void *arg)
{
    (void)arg;
    for (uint32_t i = 0u; i < F3_BURST_COUNT; i++) {
        EmbedIQ_Msg_t m;
        memset(&m, 0, sizeof(m));
        m.msg_id   = F3_MSG_BURST;
        m.priority = EMBEDIQ_MSG_PRIORITY_NORMAL;
        embediq_publish(g_f3_burst_src, &m);
        embediq_osal_delay_ms(F3_BURST_SPACING_MS);  /* stay under the NORMAL depth */
    }
    return NULL;
}

static void f3_burst_src_init(EmbedIQ_FB_Handle_t h, void *d)
{
    (void)d;
    g_f3_burst_src = h;
    pthread_create(&g_f3_burst_th, NULL, f3_burst_publisher, NULL);
}

static void f3_burst_rx_fn(EmbedIQ_FB_Handle_t fb, const void *msg, void *fb_data,
                           void *subfn_data)
{
    (void)fb; (void)msg; (void)fb_data; (void)subfn_data;
    (void)embediq_osal_atomic32_add(&g_f3_burst_rx, 1u);
}

EMBEDIQ_SUBS(g_f3_burst_subs, F3_MSG_BURST);

static const EmbedIQ_SubFn_Config_t g_f3_burst_subfn = {
    .name = "f3_burst_rx", .init_order = 0,
    .subscriptions = g_f3_burst_subs, .subscription_count = 1,
    .run_fn = f3_burst_rx_fn,
};

static void f3_burst_dst_init(EmbedIQ_FB_Handle_t h, void *d)
{
    (void)d;
    embediq_subfn_register(h, &g_f3_burst_subfn);
}

static void test_platform_thread_publish_during_boot_is_not_lost(void)
{
    fb_engine__reset();
    embediq_osal_atomic32_store(&g_f3_burst_rx, 0u);

    /* A boots first (PLATFORM) so its thread publishes while B is still booting. */
    static const EmbedIQ_FB_Config_t k_src = {
        .name = "f3_burst_src", .boot_phase = EMBEDIQ_BOOT_PHASE_PLATFORM,
        .init_fn = f3_burst_src_init,
    };
    static const EmbedIQ_FB_Config_t k_dst = {
        .name = "f3_burst_dst", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
        .init_fn = f3_burst_dst_init,
        .subscriptions = g_f3_burst_subs, .subscription_count = 1,
    };
    embediq_fb_register(&k_src);
    embediq_fb_register(&k_dst);
    embediq_engine_boot();
    embediq_engine_dispatch_boot();

    pthread_join(g_f3_burst_th, NULL);
    uint32_t t0 = embediq_osal_time_ms();
    while (embediq_osal_atomic32_load(&g_f3_burst_rx) < F3_BURST_COUNT
           && (embediq_osal_time_ms() - t0) < F3_BURST_WAIT_MS) {
        embediq_osal_delay_ms(1u);
    }
    uint32_t got = embediq_osal_atomic32_load(&g_f3_burst_rx);
    (void)embediq_engine_dispatch_shutdown();
    printf("      received %u / %u\n", (unsigned)got, (unsigned)F3_BURST_COUNT);
    ASSERT(got == F3_BURST_COUNT, "every message published during boot must be delivered");
}

/* ---------------------------------------------------------------------------
 * Entry point
 * ------------------------------------------------------------------------- */

/* ===========================================================================
 * F9 — review R4: fault_fn never runs concurrently with itself
 * ======================================================================== */

/* fault_fn that sleeps and tracks overlap with atomics only (reviewer probe). */
static embediq_osal_atomic32_t g_f9_active = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);
static embediq_osal_atomic32_t g_f9_peak   = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);
static embediq_osal_atomic32_t g_f9_count  = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);
static embediq_osal_atomic32_t g_f9_go     = EMBEDIQ_OSAL_ATOMIC32_INIT(0u);

static void f9_fault_fn(EmbedIQ_FB_Handle_t fb, void *fb_data, uint32_t reason)
{
    (void)fb; (void)fb_data; (void)reason;
    uint32_t active = embediq_osal_atomic32_add(&g_f9_active, 1u);   /* returns NEW value */
    uint32_t peak   = embediq_osal_atomic32_load(&g_f9_peak);
    while (active > peak && !embediq_osal_atomic32_cas(&g_f9_peak, peak, active)) {
        peak = embediq_osal_atomic32_load(&g_f9_peak);
    }
    embediq_osal_delay_ms(F9_FAULT_SLEEP_MS);
    (void)embediq_osal_atomic32_sub(&g_f9_active, 1u);
    (void)embediq_osal_atomic32_add(&g_f9_count, 1u);
}

static void *f9_reporter(void *arg)
{
    while (embediq_osal_atomic32_load(&g_f9_go) == 0u) { /* start together */ }
    embediq_fb_report_fault((EmbedIQ_FB_Handle_t)arg, F3_FAULT_REASON);
    return NULL;
}

/* Two threads report a fault on h at the same moment; returns the peak
 * fault_fn concurrency and stores the number of fault_fn runs in *count. */
static uint32_t f9_report_concurrently(EmbedIQ_FB_Handle_t h, uint32_t *count)
{
    embediq_osal_atomic32_store(&g_f9_active, 0u);
    embediq_osal_atomic32_store(&g_f9_peak,   0u);
    embediq_osal_atomic32_store(&g_f9_count,  0u);
    embediq_osal_atomic32_store(&g_f9_go,     0u);
    pthread_t th[F9_REPORTERS];
    for (uint32_t i = 0u; i < F9_REPORTERS; i++) {
        pthread_create(&th[i], NULL, f9_reporter, h);
    }
    embediq_osal_atomic32_store(&g_f9_go, 1u);
    for (uint32_t i = 0u; i < F9_REPORTERS; i++) {
        pthread_join(th[i], NULL);
    }
    *count = embediq_osal_atomic32_load(&g_f9_count);
    return embediq_osal_atomic32_load(&g_f9_peak);
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
        .name = "f9_nosub", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
        .fault_fn = f9_fault_fn,
    };
    EmbedIQ_FB_Handle_t h = embediq_fb_register(&k_cfg);
    embediq_engine_boot();
    embediq_engine_dispatch_boot();

    uint32_t count = 0u;
    uint32_t peak  = f9_report_concurrently(h, &count);
    (void)embediq_engine_dispatch_shutdown();
    printf("      peak=%u count=%u\n", (unsigned)peak, (unsigned)count);
    ASSERT(peak == 1u && count == F9_REPORTERS
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
    EMBEDIQ_SUBS(subs, F9_MSG_SUB);
    EmbedIQ_FB_Config_t cfg = {
        .name = "f9_predispatch", .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
        .fault_fn = f9_fault_fn,
        .subscriptions = subs, .subscription_count = 1,
    };
    EmbedIQ_FB_Handle_t h = embediq_fb_register(&cfg);
    embediq_engine_boot();

    uint32_t count = 0u;
    uint32_t peak  = f9_report_concurrently(h, &count);
    embediq_engine_dispatch_boot();
    int rc = embediq_engine_dispatch_shutdown();
    printf("      peak=%u count=%u shutdown=%d\n", (unsigned)peak, (unsigned)count, rc);
    ASSERT(peak == 1u && count == F9_REPORTERS && rc == 0
           && embediq_fb_get_state(h) == EMBEDIQ_FB_STATE_FAULT,
           "pre-dispatch FB: fault_fn applied twice, never concurrently");
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
    fb_engine__reset();

    printf("\nAll %d tests passed. (%d failed)\n",
           g_tests_run - g_tests_failed, g_tests_failed);
    return g_tests_failed > 0 ? 1 : 0;
}
