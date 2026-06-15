#define _POSIX_C_SOURCE 200809L   /* setenv() visibility on Linux */

/*
 * tests/unit/test_fb_provisioning.c — Unit tests for fb_provisioning Service FB
 *
 * Drives the device-identity gatekeeper startup sequence and sub-functions
 * through package-internal test accessors (fb_provisioning__*_test) under
 * EMBEDIQ_PLATFORM_HOST only — no dispatch threads, no engine boot.
 *
 * A mock embediq_provisioning_ops_t is registered to control identity_check /
 * fleet_config_check / factory_reset outcomes. The real embediq_cfg + fb_nvm
 * stack is used (EMBEDIQ_NVM_PATH points at /tmp) so NVM effects are verified
 * behaviourally (read the value back), never by mocking NVM.
 *
 * Run:      ./build/tests/unit/test_fb_provisioning
 * Expected: "All N tests passed. (0 failed)"
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "embediq_config.h"
#include "embediq_cfg.h"
#include "embediq_nvm.h"
#include "embediq_platform_msgs.h"
#include "ops/embediq_provisioning.h"
#include "fb_provisioning.h"
#include "provisioning_msg_catalog.h"

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>   /* setenv */

#define TEST_NVM_PATH  "/tmp/embediq_prov_test.bin"

/* ---------------------------------------------------------------------------
 * Package-internal test API (implemented in fbs/services/fb_provisioning.c
 * under EMBEDIQ_PLATFORM_HOST). Drives the FB logic without the engine/bus.
 * ------------------------------------------------------------------------- */

extern void     fb_provisioning__run_startup_test(void);
extern void     fb_provisioning__sf_connected_test(void);
extern void     fb_provisioning__sf_cmd_rx_test(const uint8_t *payload, uint32_t len);
extern void     fb_provisioning__test_reset(void);
extern int      fb_provisioning__test_pub_count(void);
extern uint16_t fb_provisioning__test_pub_id(int idx);
extern uint8_t  fb_provisioning__test_pub_status(int idx);
extern int      fb_provisioning__test_fault_count(void);

/* fb_nvm host-only state reset (declared in fbs/drivers/fb_nvm.c). */
extern void     nvm__init_state(void);

/* ---------------------------------------------------------------------------
 * Mock provisioning ops table — outcomes controlled per test via globals.
 * ------------------------------------------------------------------------- */

static embediq_err_t g_mock_identity_rc   = EMBEDIQ_OK;
static const char   *g_mock_identity_cn   = "test-device-001";
static embediq_err_t g_mock_fleet_rc      = EMBEDIQ_OK;
static embediq_err_t g_mock_factory_rc    = EMBEDIQ_OK;
static int           g_mock_factory_calls = 0;

static embediq_err_t mock_identity_check(embediq_provisioning_ctx_t *ctx)
{
    if (g_mock_identity_rc == EMBEDIQ_OK && ctx != NULL) {
        (void)snprintf(ctx->device_id, sizeof(ctx->device_id), "%s",
                       g_mock_identity_cn);
        ctx->identity_valid = 1u;
    } else if (ctx != NULL) {
        ctx->identity_valid = 0u;
    }
    return g_mock_identity_rc;
}

static embediq_err_t mock_fleet_config_check(embediq_provisioning_ctx_t *ctx)
{
    (void)ctx;
    return g_mock_fleet_rc;
}

/* Representative factory_reset: clears the same NVM keys the real POSIX HAL
 * clears, so the FB-level behaviour (keys gone after the command) is testable. */
static embediq_err_t mock_factory_reset(embediq_provisioning_ctx_t *ctx)
{
    (void)ctx;
    g_mock_factory_calls++;
    (void)embediq_nvm_delete("mqtt.client_id");
    (void)embediq_nvm_delete("prov.state");
    (void)embediq_nvm_delete("mqtt.host");
    (void)embediq_nvm_delete("mqtt.port");
    (void)embediq_nvm_delete("mqtt.username");
    (void)embediq_nvm_delete("mqtt.password");
    (void)embediq_nvm_flush();
    return g_mock_factory_rc;
}

static const embediq_provisioning_ops_t g_mock_ops = {
    .version            = EMBEDIQ_PROV_OPS_VERSION,
    .identity_check     = mock_identity_check,
    .fleet_config_check = mock_fleet_config_check,
    .factory_reset      = mock_factory_reset,
};

/* ---------------------------------------------------------------------------
 * Minimal test harness (pattern follows test_fb_telemetry.c)
 * ------------------------------------------------------------------------- */

static int g_tests_run    = 0;
static int g_tests_failed = 0;

#define ASSERT(cond, msg)  do {                                                 \
    g_tests_run++;                                                              \
    if (!(cond)) {                                                              \
        fprintf(stderr, "FAIL  %-56s  %s:%d  %s\n",                             \
                __func__, __FILE__, __LINE__, (msg));                           \
        g_tests_failed++;                                                       \
    } else {                                                                    \
        printf("PASS  %s  %s\n", __func__, (msg));                             \
    }                                                                           \
} while (0)

/* Return index of the first captured publish with the given msg_id, or -1. */
static int find_pub(uint16_t msg_id)
{
    int n = fb_provisioning__test_pub_count();
    for (int i = 0; i < n; i++) {
        if (fb_provisioning__test_pub_id(i) == msg_id) {
            return i;
        }
    }
    return -1;
}

/* Fresh state before each test: clean NVM, reset capture, register mock ops,
 * restore default mock outcomes. */
static void setup(void)
{
    setenv("EMBEDIQ_NVM_PATH", TEST_NVM_PATH, 1);
    (void)remove(TEST_NVM_PATH);
    nvm__init_state();

    g_mock_identity_rc   = EMBEDIQ_OK;
    g_mock_identity_cn   = "test-device-001";
    g_mock_fleet_rc      = EMBEDIQ_OK;
    g_mock_factory_rc    = EMBEDIQ_OK;
    g_mock_factory_calls = 0;

    embediq_provisioning_ops_register(&g_mock_ops);
    fb_provisioning__test_reset();
}

/* ---------------------------------------------------------------------------
 * TEST 1 — identity OK writes mqtt.client_id from the cert CN
 * ------------------------------------------------------------------------- */
static void test_provisioning_identity_ok_writes_client_id(void)
{
    setup();
    g_mock_identity_cn = "test-device-001";

    fb_provisioning__run_startup_test();

    char id[EMBEDIQ_NVM_VAL_SIZE] = {0};
    (void)embediq_cfg_get_str("mqtt.client_id", id, sizeof(id), "");
    ASSERT(strcmp(id, "test-device-001") == 0,
           "mqtt.client_id written from cert CN");

    int idx = find_pub(MSG_PROV_IDENTITY_READY);
    ASSERT(idx >= 0, "MSG_PROV_IDENTITY_READY published");
    ASSERT(fb_provisioning__test_pub_status(idx) == 0u,
           "IDENTITY_READY status == 0 on success");
}

/* ---------------------------------------------------------------------------
 * TEST 2 — identity failure publishes status != 0 and reports a fault
 * ------------------------------------------------------------------------- */
static void test_provisioning_identity_fail_publishes_status_nonzero(void)
{
    setup();
    g_mock_identity_rc = EMBEDIQ_ERR;

    fb_provisioning__run_startup_test();

    int idx = find_pub(MSG_PROV_IDENTITY_READY);
    ASSERT(idx >= 0, "MSG_PROV_IDENTITY_READY published on identity failure");
    ASSERT(fb_provisioning__test_pub_status(idx) != 0u,
           "IDENTITY_READY status != 0 on failure");
    ASSERT(fb_provisioning__test_fault_count() >= 1,
           "embediq_fb_report_fault() called on identity failure");
}

/* ---------------------------------------------------------------------------
 * TEST 3 — identity OK + fleet OK publishes CONFIG_RECEIVED and CFG_RELOAD
 * ------------------------------------------------------------------------- */
static void test_provisioning_fleet_ok_publishes_cfg_reload(void)
{
    setup();
    g_mock_fleet_rc = EMBEDIQ_OK;

    fb_provisioning__run_startup_test();

    ASSERT(find_pub(MSG_PROV_CONFIG_RECEIVED) >= 0,
           "MSG_PROV_CONFIG_RECEIVED published when fleet config present");
    ASSERT(find_pub(MSG_CFG_RELOAD) >= 0,
           "MSG_CFG_RELOAD published when fleet config present");
}

/* ---------------------------------------------------------------------------
 * TEST 4 — identity OK + fleet missing: no CFG_RELOAD, stays MANUFACTURED
 * ------------------------------------------------------------------------- */
static void test_provisioning_fleet_missing_no_cfg_reload(void)
{
    setup();
    g_mock_fleet_rc = EMBEDIQ_ERR;

    fb_provisioning__run_startup_test();

    ASSERT(find_pub(MSG_CFG_RELOAD) < 0,
           "MSG_CFG_RELOAD NOT published when fleet config absent");

    int idx = find_pub(MSG_PROV_IDENTITY_READY);
    ASSERT(idx >= 0 && fb_provisioning__test_pub_status(idx) == 0u,
           "IDENTITY_READY status == 0 (identity still valid)");

    char st[EMBEDIQ_NVM_VAL_SIZE] = {0};
    (void)embediq_cfg_get_str("prov.state", st, sizeof(st), "");
    ASSERT(strcmp(st, "1") == 0, "prov.state stays MANUFACTURED (1)");
}

/* ---------------------------------------------------------------------------
 * TEST 5 — factory_reset via sf_cmd_rx clears provisioning NVM keys
 * ------------------------------------------------------------------------- */
static void test_provisioning_factory_reset_clears_nvm_keys(void)
{
    setup();
    /* Pre-populate provisioning keys. */
    (void)embediq_cfg_set_str("mqtt.client_id", "old-device");
    (void)embediq_cfg_set_str("prov.state", "2");
    (void)embediq_nvm_flush();

    const char cmd[] = "factory_reset";
    fb_provisioning__sf_cmd_rx_test((const uint8_t *)cmd, (uint32_t)sizeof(cmd));

    ASSERT(g_mock_factory_calls == 1, "ops->factory_reset() invoked exactly once");

    char id[EMBEDIQ_NVM_VAL_SIZE] = {0};
    (void)embediq_cfg_get_str("mqtt.client_id", id, sizeof(id), "");
    ASSERT(id[0] == '\0', "mqtt.client_id cleared by factory reset");

    char st[EMBEDIQ_NVM_VAL_SIZE] = {0};
    (void)embediq_cfg_get_str("prov.state", st, sizeof(st), "");
    ASSERT(st[0] == '\0', "prov.state cleared by factory reset");
}

/* ---------------------------------------------------------------------------
 * TEST 6 — factory_reset via sf_cmd_rx publishes MSG_PROV_FACTORY_RESET
 * ------------------------------------------------------------------------- */
static void test_provisioning_factory_reset_publishes_msg(void)
{
    setup();

    const char cmd[] = "factory_reset";
    fb_provisioning__sf_cmd_rx_test((const uint8_t *)cmd, (uint32_t)sizeof(cmd));

    int idx = find_pub(MSG_PROV_FACTORY_RESET);
    ASSERT(idx >= 0, "MSG_PROV_FACTORY_RESET published");
    ASSERT(fb_provisioning__test_pub_status(idx) == 0u,
           "MSG_PROV_FACTORY_RESET reason == 0 (command)");
}

/* ---------------------------------------------------------------------------
 * TEST 7 — full provisioning advances prov.state to CUSTOMER_PROVISIONED
 * ------------------------------------------------------------------------- */
static void test_provisioning_state_advances_to_customer_provisioned(void)
{
    setup();
    /* prov.state absent → UNMANUFACTURED. identity OK + fleet OK. */
    g_mock_identity_rc = EMBEDIQ_OK;
    g_mock_fleet_rc    = EMBEDIQ_OK;

    fb_provisioning__run_startup_test();

    char st[EMBEDIQ_NVM_VAL_SIZE] = {0};
    (void)embediq_cfg_get_str("prov.state", st, sizeof(st), "");
    ASSERT(strcmp(st, "2") == 0,
           "prov.state advanced to CUSTOMER_PROVISIONED (2)");

    char id[EMBEDIQ_NVM_VAL_SIZE] = {0};
    (void)embediq_cfg_get_str("mqtt.client_id", id, sizeof(id), "");
    ASSERT(id[0] != '\0', "mqtt.client_id set during provisioning");
}

/* ---------------------------------------------------------------------------
 * Entry point
 * ------------------------------------------------------------------------- */

int main(void)
{
    test_provisioning_identity_ok_writes_client_id();
    test_provisioning_identity_fail_publishes_status_nonzero();
    test_provisioning_fleet_ok_publishes_cfg_reload();
    test_provisioning_fleet_missing_no_cfg_reload();
    test_provisioning_factory_reset_clears_nvm_keys();
    test_provisioning_factory_reset_publishes_msg();
    test_provisioning_state_advances_to_customer_provisioned();

    printf("\n");
    if (g_tests_failed == 0) {
        printf("All %d tests passed. (0 failed)\n", g_tests_run);
    } else {
        printf("%d of %d tests FAILED.\n", g_tests_failed, g_tests_run);
    }
    return (g_tests_failed > 0) ? 1 : 0;
}
