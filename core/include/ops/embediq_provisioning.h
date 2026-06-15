/*
 * ops/embediq_provisioning.h — Device Identity Provisioning Ops Table
 *
 * Layer-2 Service FBs call these ops. Platform implementations (HAL layer)
 * provide them. POSIX implementation: hal/posix/ops/hal_provisioning_posix.c.
 *
 * Options A/B/C: only the identity_check() implementation changes per option.
 * fb_provisioning.c is invariant across options — the ops table is the only
 * extension point. See ARCHITECTURE.md: fb_provisioning section.
 *
 * Same ops-table pattern as embediq_mqtt.h / embediq_tls.h. This header is
 * platform-neutral: no mbedTLS, no filesystem, no platform headers.
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef EMBEDIQ_OPS_PROVISIONING_H
#define EMBEDIQ_OPS_PROVISIONING_H

#include <stdint.h>
#include "embediq_osal.h"   /* embediq_err_t */
#include "embediq_config.h" /* EMBEDIQ_NVM_VAL_SIZE */

#ifdef __cplusplus
extern "C" {
#endif

/** ABI version for embediq_provisioning_ops_t. Increment on a breaking change. */
#define EMBEDIQ_PROV_OPS_VERSION  1u

/**
 * Context filled by identity_check() on success.
 *   device_id      — NUL-terminated CN from the device certificate.
 *                    Max EMBEDIQ_NVM_VAL_SIZE-1 usable chars.
 *   identity_valid — 1 if identity_check() succeeded, 0 otherwise.
 */
typedef struct {
    char    device_id[EMBEDIQ_NVM_VAL_SIZE];
    uint8_t identity_valid;
} embediq_provisioning_ctx_t;

/**
 * Provisioning ops table.
 * version MUST be EMBEDIQ_PROV_OPS_VERSION — checked at registration.
 */
typedef struct {
    uint32_t version;

    /**
     * Validate device identity from the platform cert store.
     * On EMBEDIQ_OK: ctx->device_id holds the cert CN (NUL-terminated),
     *                ctx->identity_valid = 1.
     * On EMBEDIQ_ERR: ctx->identity_valid = 0. Reason is platform-logged.
     */
    embediq_err_t (*identity_check)(embediq_provisioning_ctx_t *ctx);

    /**
     * Check fleet configuration is present.
     * Returns EMBEDIQ_OK if mqtt.host is non-empty in embediq_cfg,
     * EMBEDIQ_ERR if mqtt.host is absent or empty.
     */
    embediq_err_t (*fleet_config_check)(embediq_provisioning_ctx_t *ctx);

    /**
     * Factory reset: clear provisioning NVM keys, call embediq_nvm_flush().
     * Keys cleared: mqtt.client_id, prov.state, mqtt.host, mqtt.port,
     *               mqtt.username, mqtt.password.
     * The cert store (EMBEDIQ_CERT_DIR) is NEVER touched.
     * Returns EMBEDIQ_OK on success, EMBEDIQ_ERR on NVM error.
     */
    embediq_err_t (*factory_reset)(embediq_provisioning_ctx_t *ctx);
} embediq_provisioning_ops_t;

/** Get the registered provisioning ops table. Returns NULL if not registered. */
const embediq_provisioning_ops_t *embediq_provisioning_ops_get(void);

/** Register a provisioning ops table. Called from hal_provisioning_posix_declare(). */
void embediq_provisioning_ops_register(const embediq_provisioning_ops_t *ops);

#ifdef __cplusplus
}
#endif

#endif /* EMBEDIQ_OPS_PROVISIONING_H */
