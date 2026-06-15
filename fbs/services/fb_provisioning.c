/*
 * fbs/services/fb_provisioning.c — Device Identity Lifecycle Gatekeeper
 *
 * Platform-independent Service FB (Layer 2). On boot it validates the device
 * identity through the embediq_provisioning_ops_t table (the POSIX HAL reads
 * and verifies the cert with mbedTLS), derives mqtt.client_id from the cert
 * CN, writes it to NVM, and publishes MSG_CFG_RELOAD so fb_cloud_mqtt connects.
 * It manages the four-state provisioning FSM (prov.state in NVM) and handles
 * the "factory_reset" cloud command.
 *
 * LAYER RULE (boundary_checker.py): this file includes NO mbedTLS headers and
 * NO POSIX filesystem headers. All cert/filesystem work lives in the HAL
 * implementation behind the ops table. Coordination with fb_cloud_mqtt is via
 * MSG_CFG_RELOAD only — fb_cloud_mqtt is not modified and not a depends_on.
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "fb_provisioning.h"
#include "ops/embediq_provisioning.h"
#include "embediq_cfg.h"
#include "embediq_nvm.h"
#include "embediq_fb.h"
#include "embediq_subfn.h"
#include "embediq_bus.h"
#include "embediq_msg.h"
#include "embediq_platform_msgs.h"
#include "embediq_obs.h"
#include "provisioning_msg_catalog.h"

#include <string.h>
#include <stdint.h>

/* FB-level Observatory fault source. fb_cloud_mqtt uses 0u for its service-layer
 * EMBEDIQ_OBS_EMIT_FAULT calls (PR #166); the state field distinguishes paths. */
#define SRC_FB_PROV  0u

/* Provisioning lifecycle states (stored as a one-char string in prov.state). */
#define PROV_STATE_UNMANUFACTURED        0u
#define PROV_STATE_MANUFACTURED          1u
#define PROV_STATE_CUSTOMER_PROVISIONED  2u
#define PROV_STATE_OPERATIONAL           3u

static EmbedIQ_FB_Handle_t s_fb = NULL;  /* EmbedIQ_FB_Handle_t = void* */

/* ---------------------------------------------------------------------------
 * Host-only test capture (engine-free unit testing). Records published
 * messages and report_fault() calls so the unit test can assert on them
 * without booting the engine or the bus.
 * ------------------------------------------------------------------------- */
#ifdef EMBEDIQ_PLATFORM_HOST
#define PROV_TEST_MAX_PUB  16
static uint16_t s_test_pub_id[PROV_TEST_MAX_PUB];
static uint8_t  s_test_pub_status[PROV_TEST_MAX_PUB];
static int      s_test_pub_count   = 0;
static int      s_test_fault_count = 0;

static void prov_test_record(uint16_t msg_id, uint8_t first_byte)
{
    if (s_test_pub_count < PROV_TEST_MAX_PUB) {
        s_test_pub_id[s_test_pub_count]     = msg_id;
        s_test_pub_status[s_test_pub_count] = first_byte;
        s_test_pub_count++;
    }
}
#endif /* EMBEDIQ_PLATFORM_HOST */

/* ---------------------------------------------------------------------------
 * Internal helpers
 * ------------------------------------------------------------------------- */

/* Emit an always-on Observatory fault for a service-layer failure path. */
static void prov_obs_fault(uint8_t state)
{
    EMBEDIQ_OBS_EMIT_FAULT(EMBEDIQ_OBS_EVT_FAULT, SRC_FB_PROV, 0u, state, 0u);
}

/* Publish a bus message. In host unit tests (s_fb == NULL) this records into
 * the test capture buffer only; in a live system it publishes on the bus. */
static void prov_publish(uint16_t msg_id, const void *payload, uint16_t len)
{
    EmbedIQ_Msg_t msg;
    (void)memset(&msg, 0, sizeof(msg));
    msg.msg_id = msg_id;
    if (payload != NULL && len > 0u) {
        (void)memcpy(msg.payload, payload, len);
        msg.payload_len = len;
    }

#ifdef EMBEDIQ_PLATFORM_HOST
    prov_test_record(msg_id,
                     (payload != NULL && len > 0u)
                         ? ((const uint8_t *)payload)[0] : 0u);
#endif

    if (s_fb != NULL) {
        embediq_publish(s_fb, &msg);
    }
}

/* Report an FB fault. In host unit tests (s_fb == NULL) records the call only. */
static void prov_report_fault(uint32_t reason)
{
#ifdef EMBEDIQ_PLATFORM_HOST
    s_test_fault_count++;
#endif
    if (s_fb != NULL) {
        embediq_fb_report_fault(s_fb, reason);
    }
}

/* Read prov.state from NVM. Absent or malformed → UNMANUFACTURED (0). */
static uint8_t prov_read_state(void)
{
    char st[8];
    st[0] = '\0';
    (void)embediq_cfg_get_str("prov.state", st, sizeof(st), "");
    if (st[0] >= '0' && st[0] <= '3') {
        return (uint8_t)(st[0] - '0');
    }
    return PROV_STATE_UNMANUFACTURED;
}

/* ---------------------------------------------------------------------------
 * Startup sequence (Topic 1 decision). Runs in init_fn; also driven directly
 * by the host test accessor. Every error early-return emits an OBS fault first
 * (R-sub-18). Every ops/cfg/nvm return value is captured (R-sub-17).
 * ------------------------------------------------------------------------- */

static void prov_run_startup(void)
{
    /* STEP 1: current lifecycle state. */
    uint8_t state = prov_read_state();

    const embediq_provisioning_ops_t *ops = embediq_provisioning_ops_get();

    /* STEP 2: identity check. ops absent or failure → fault path 0. */
    embediq_provisioning_ctx_t ctx;
    (void)memset(&ctx, 0, sizeof(ctx));

    embediq_err_t err = (ops != NULL) ? ops->identity_check(&ctx) : EMBEDIQ_ERR;
    if (err != EMBEDIQ_OK) {
        MSG_PROV_IDENTITY_READY_Payload_t fail = { .status = 1u };  /* 1 = no/invalid cert */
        prov_obs_fault(0u);
        prov_publish(MSG_PROV_IDENTITY_READY, &fail, (uint16_t)sizeof(fail));
        prov_report_fault(1u);
        return;
    }

    /* STEP 3: write mqtt.client_id from the cert CN if not already set.
     * mqtt.client_id is a CFG_MUT_FACTORY key — embediq_cfg_set_str() would be
     * rejected by the NVM mutability guard. embediq_nvm_provision() is the
     * authorized write-once path: it sets the key only while absent/empty and
     * is idempotent once provisioned (IEC 62443 commissioning immutability). */
    char existing_id[EMBEDIQ_NVM_VAL_SIZE];
    existing_id[0] = '\0';
    (void)embediq_cfg_get_str("mqtt.client_id", existing_id, sizeof(existing_id), "");
    if (strcmp(existing_id, ctx.device_id) != 0) {
        if (embediq_nvm_provision("mqtt.client_id", ctx.device_id) != EMBEDIQ_OK) {
            prov_obs_fault(1u);
            prov_report_fault(2u);
            return;
        }
        if (embediq_nvm_flush() != EMBEDIQ_OK) {
            prov_obs_fault(2u);
            prov_report_fault(3u);
            return;
        }
    }

    /* STEP 3b: advance to MANUFACTURED if not already there. */
    if (state < PROV_STATE_MANUFACTURED) {
        if (!embediq_cfg_set_str("prov.state", "1")) {
            prov_obs_fault(3u);
            prov_report_fault(4u);
            return;
        }
        if (embediq_nvm_flush() != EMBEDIQ_OK) {
            prov_obs_fault(4u);
            prov_report_fault(5u);
            return;
        }
        state = PROV_STATE_MANUFACTURED;
    }

    /* STEP 4: identity is valid — publish readiness (status 0). */
    MSG_PROV_IDENTITY_READY_Payload_t ready = { .status = 0u };
    prov_publish(MSG_PROV_IDENTITY_READY, &ready, (uint16_t)sizeof(ready));

    /* STEP 5: fleet config check. Empty mqtt.host is a normal "awaiting
     * operator config" state — stay at MANUFACTURED, no fault. */
    err = ops->fleet_config_check(&ctx);
    if (err != EMBEDIQ_OK) {
        return;  /* intentional non-fault early return */
    }

    /* STEP 5b: advance to CUSTOMER_PROVISIONED. State-write failures here are
     * non-fatal — we still publish MSG_CFG_RELOAD so a connect is attempted. */
    if (!embediq_cfg_set_str("prov.state", "2")) {
        prov_obs_fault(5u);
    } else if (embediq_nvm_flush() != EMBEDIQ_OK) {
        prov_obs_fault(6u);
    }

    /* STEP 6: signal fleet config received and trigger fb_cloud_mqtt connect. */
    MSG_PROV_CONFIG_RECEIVED_Payload_t cfg = { ._reserved = 0u };
    prov_publish(MSG_PROV_CONFIG_RECEIVED, &cfg, (uint16_t)sizeof(cfg));
    prov_publish(MSG_CFG_RELOAD, NULL, 0u);
}

/* ---------------------------------------------------------------------------
 * MSG_MQTT_CONNECTED → advance to OPERATIONAL (advisory state).
 * ------------------------------------------------------------------------- */

static void prov_handle_connected(void)
{
    uint8_t state = prov_read_state();
    if (state < PROV_STATE_OPERATIONAL) {
        if (!embediq_cfg_set_str("prov.state", "3")) {
            prov_obs_fault(7u);  /* advisory: do not report_fault */
            return;
        }
        if (embediq_nvm_flush() != EMBEDIQ_OK) {
            prov_obs_fault(8u);  /* advisory */
        }
    }
}

/* ---------------------------------------------------------------------------
 * MSG_MQTT_CMD_RX → handle the "factory_reset" command. payload64 points at a
 * region of at least EMBEDIQ_MSG_MAX_PAYLOAD bytes (the dispatcher passes the
 * message payload buffer).
 * ------------------------------------------------------------------------- */

static void prov_handle_cmd(const uint8_t *payload64)
{
    char cmd[EMBEDIQ_MSG_MAX_PAYLOAD + 1];
    (void)memcpy(cmd, payload64, EMBEDIQ_MSG_MAX_PAYLOAD);
    cmd[EMBEDIQ_MSG_MAX_PAYLOAD] = '\0';

    if (strcmp(cmd, "factory_reset") != 0) {
        return;  /* intentional non-fault early return: not our command */
    }

    const embediq_provisioning_ops_t *ops = embediq_provisioning_ops_get();
    if (ops == NULL) {
        prov_obs_fault(9u);
        return;
    }

    embediq_provisioning_ctx_t ctx;
    (void)memset(&ctx, 0, sizeof(ctx));
    embediq_err_t err = ops->factory_reset(&ctx);

    MSG_PROV_FACTORY_RESET_Payload_t pay = { .reason = 0u };  /* 0 = command */
    prov_publish(MSG_PROV_FACTORY_RESET, &pay, (uint16_t)sizeof(pay));

    if (err != EMBEDIQ_OK) {
        prov_obs_fault(10u);
    }
}

/* ---------------------------------------------------------------------------
 * Sub-function handlers (dispatcher passes payload bytes as `msg`).
 * ------------------------------------------------------------------------- */

static void sf_connected(EmbedIQ_FB_Handle_t fb, const void *msg,
                         void *fb_data, void *subfn_data)
{
    (void)fb; (void)msg; (void)fb_data; (void)subfn_data;
    prov_handle_connected();
}

static void sf_cmd_rx(EmbedIQ_FB_Handle_t fb, const void *msg,
                      void *fb_data, void *subfn_data)
{
    (void)fb; (void)fb_data; (void)subfn_data;
    if (msg == NULL) {
        return;  /* intentional non-fault early return: defensive guard */
    }
    prov_handle_cmd((const uint8_t *)msg);
}

/* ---------------------------------------------------------------------------
 * FB init — register sub-functions, then run the startup sequence.
 * ------------------------------------------------------------------------- */

static void prov_init(EmbedIQ_FB_Handle_t fb, void *fb_data)
{
    (void)fb_data;
    s_fb = fb;

    static const uint16_t k_connected_subs[] = { MSG_MQTT_CONNECTED };
    static const uint16_t k_cmd_subs[]       = { MSG_MQTT_CMD_RX };

    static EmbedIQ_SubFn_Config_t k_connected_cfg = {
        .name               = "on_mqtt_connected",
        .init_order         = 1u,
        .run_fn             = sf_connected,
        .subscriptions      = k_connected_subs,
        .subscription_count = 1u,
    };
    static EmbedIQ_SubFn_Config_t k_cmd_cfg = {
        .name               = "on_cmd_rx",
        .init_order         = 2u,
        .run_fn             = sf_cmd_rx,
        .subscriptions      = k_cmd_subs,
        .subscription_count = 1u,
    };

    (void)embediq_subfn_register(fb, &k_connected_cfg);
    (void)embediq_subfn_register(fb, &k_cmd_cfg);

    prov_run_startup();
}

/* ---------------------------------------------------------------------------
 * Public registration
 * ------------------------------------------------------------------------- */

EMBEDIQ_SUBS(g_prov_subs, MSG_MQTT_CONNECTED, MSG_MQTT_CMD_RX);

void fb_provisioning_register(void)
{
    static const EmbedIQ_FB_Config_t k_cfg = {
        .name               = "fb_provisioning",
        .boot_phase         = EMBEDIQ_BOOT_PHASE_INFRASTRUCTURE,
        .init_fn            = prov_init,
        .subscriptions      = g_prov_subs,
        .subscription_count = 2u,
    };
    (void)embediq_fb_register(&k_cfg);
}

/* ---------------------------------------------------------------------------
 * Test-only accessors (host/test builds only — never linked into production).
 * Drive the startup sequence and sub-functions without the engine/bus, and
 * expose the captured publish / fault effects.
 * ------------------------------------------------------------------------- */

#ifdef EMBEDIQ_PLATFORM_HOST

void fb_provisioning__test_reset(void)
{
    s_test_pub_count   = 0;
    s_test_fault_count = 0;
    s_fb               = NULL;  /* engine-free: publishes are captured, not bused */
}

void fb_provisioning__run_startup_test(void)
{
    prov_run_startup();
}

void fb_provisioning__sf_connected_test(void)
{
    prov_handle_connected();
}

void fb_provisioning__sf_cmd_rx_test(const uint8_t *payload, uint32_t len)
{
    uint8_t buf[EMBEDIQ_MSG_MAX_PAYLOAD];
    (void)memset(buf, 0, sizeof(buf));
    uint32_t n = (len < sizeof(buf)) ? len : (uint32_t)sizeof(buf);
    if (payload != NULL && n > 0u) {
        (void)memcpy(buf, payload, n);
    }
    prov_handle_cmd(buf);
}

int fb_provisioning__test_pub_count(void)
{
    return s_test_pub_count;
}

uint16_t fb_provisioning__test_pub_id(int idx)
{
    return (idx >= 0 && idx < s_test_pub_count) ? s_test_pub_id[idx] : 0u;
}

uint8_t fb_provisioning__test_pub_status(int idx)
{
    return (idx >= 0 && idx < s_test_pub_count) ? s_test_pub_status[idx] : 0u;
}

int fb_provisioning__test_fault_count(void)
{
    return s_test_fault_count;
}

#endif /* EMBEDIQ_PLATFORM_HOST */
