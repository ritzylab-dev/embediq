#define _POSIX_C_SOURCE 200809L

/*
 * tests/unit/test_fb_engine.c — Unit tests for core FB engine
 *
 * Covers every contractual behaviour of core/src/fb_engine.c.
 * Zero external test framework — stdlib only.
 * Each test exercises exactly one behaviour with one ASSERT.
 *
 * Run:      ./build/tests/unit/test_fb_engine
 * Expected: "All 10 tests passed. (0 failed)"
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "embediq_fb.h"
#include "embediq_subfn.h"
#include "embediq_config.h"

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

/* ---------------------------------------------------------------------------
 * Entry point
 * ------------------------------------------------------------------------- */

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

    printf("\nAll %d tests passed. (%d failed)\n",
           g_tests_run - g_tests_failed, g_tests_failed);
    return g_tests_failed > 0 ? 1 : 0;
}
