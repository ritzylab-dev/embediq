#define _POSIX_C_SOURCE 200809L

/*
 * hal/posix/ops/hal_provisioning_posix.c — POSIX provisioning ops (Option A)
 *
 * Pre-provisioned cert model: the OEM signing service installs a per-device
 * cert+key into EMBEDIQ_CERT_DIR at the factory. identity_check() reads
 * EMBEDIQ_CERT_DIR/device.crt, validates it with mbedTLS 3.6.1, checks expiry,
 * and extracts the CN into ctx->device_id.
 *
 * mbedTLS and filesystem I/O are permitted in this HAL layer ONLY. The Service
 * FB (fbs/services/fb_provisioning.c) calls these ops via function pointers and
 * never includes mbedTLS or <stdio.h> (boundary_checker.py enforced).
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ops/embediq_provisioning.h"
#include "hal_provisioning_posix.h"
#include "embediq_config.h"
#include "embediq_cfg.h"
#include "embediq_nvm.h"
#include "embediq_obs.h"
#include "hal/hal_defs.h"

#include <mbedtls/x509_crt.h>
#include <mbedtls/oid.h>

#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#ifndef EMBEDIQ_HAL_SRC_PROV
#  define EMBEDIQ_HAL_SRC_PROV  0xDAu  /* provisioning HAL source ID (0xD9=MQTT, 0xD8=TLS) */
#endif

/* PEM read buffer — static (R-02: no malloc). One cert read at a time. */
static char s_cert_pem[EMBEDIQ_PROV_CERT_BUF_SIZE];

/* ---------------------------------------------------------------------------
 * identity_check — read + validate device cert, extract CN
 * ------------------------------------------------------------------------- */

static embediq_err_t posix_identity_check(embediq_provisioning_ctx_t *ctx)
{
    if (ctx == NULL) {
        EMBEDIQ_HAL_OBS_EMIT_ERROR(EMBEDIQ_HAL_SRC_PROV, HAL_ERR_INVALID);
        return EMBEDIQ_ERR;
    }
    ctx->identity_valid = 0u;
    ctx->device_id[0]   = '\0';

    FILE *f = fopen(EMBEDIQ_CERT_DIR "/device.crt", "rb");
    if (f == NULL) {
        EMBEDIQ_HAL_OBS_EMIT_ERROR(EMBEDIQ_HAL_SRC_PROV, HAL_ERR_IO);
        return EMBEDIQ_ERR;
    }
    size_t n = fread(s_cert_pem, 1u, sizeof(s_cert_pem) - 1u, f);
    (void)fclose(f);
    if (n == 0u) {
        EMBEDIQ_HAL_OBS_EMIT_ERROR(EMBEDIQ_HAL_SRC_PROV, HAL_ERR_IO);
        return EMBEDIQ_ERR;
    }
    s_cert_pem[n] = '\0';

    mbedtls_x509_crt crt;
    mbedtls_x509_crt_init(&crt);

    /* +1 to include the NUL terminator — required by the mbedTLS PEM parser. */
    int ret = mbedtls_x509_crt_parse(&crt, (const unsigned char *)s_cert_pem,
                                     strlen(s_cert_pem) + 1u);
    if (ret != 0) {
        mbedtls_x509_crt_free(&crt);
        EMBEDIQ_HAL_OBS_EMIT_ERROR(EMBEDIQ_HAL_SRC_PROV, HAL_ERR_INVALID);
        return EMBEDIQ_ERR;
    }

    if (mbedtls_x509_time_is_past(&crt.valid_to)) {
        mbedtls_x509_crt_free(&crt);
        EMBEDIQ_HAL_OBS_EMIT_ERROR(EMBEDIQ_HAL_SRC_PROV, HAL_ERR_INVALID);
        return EMBEDIQ_ERR;
    }

    /* Extract the Common Name (OID id-at-commonName = 2.5.4.3). Compare the
     * raw OID bytes — MBEDTLS_OID_CMP is not available in this mbedTLS build. */
    static const char k_oid_cn[] = MBEDTLS_OID_AT_CN;
    const size_t      oid_cn_len = sizeof(k_oid_cn) - 1u;
    bool found = false;

    for (const mbedtls_x509_name *node = &crt.subject;
         node != NULL; node = node->next) {
        if (node->oid.len == oid_cn_len &&
            memcmp(node->oid.p, k_oid_cn, oid_cn_len) == 0) {
            size_t cn_len = (node->val.len < (sizeof(ctx->device_id) - 1u))
                            ? node->val.len
                            : (sizeof(ctx->device_id) - 1u);
            memcpy(ctx->device_id, node->val.p, cn_len);
            ctx->device_id[cn_len] = '\0';
            found = true;
            break;
        }
    }

    mbedtls_x509_crt_free(&crt);

    if (!found) {
        EMBEDIQ_HAL_OBS_EMIT_ERROR(EMBEDIQ_HAL_SRC_PROV, HAL_ERR_INVALID);
        return EMBEDIQ_ERR;
    }

    ctx->identity_valid = 1u;
    return EMBEDIQ_OK;
}

/* ---------------------------------------------------------------------------
 * fleet_config_check — mqtt.host present in embediq_cfg?
 * ------------------------------------------------------------------------- */

static embediq_err_t posix_fleet_config_check(embediq_provisioning_ctx_t *ctx)
{
    (void)ctx;

    char host[EMBEDIQ_NVM_VAL_SIZE];
    host[0] = '\0';
    (void)embediq_cfg_get_str("mqtt.host", host, sizeof(host), "");

    /* Empty mqtt.host is a normal "awaiting operator config" state, not a
     * hardware fault — no OBS fault emitted here. */
    if (host[0] == '\0') {
        return EMBEDIQ_ERR;
    }
    return EMBEDIQ_OK;
}

/* ---------------------------------------------------------------------------
 * factory_reset — clear provisioning NVM keys; cert store is never touched
 * ------------------------------------------------------------------------- */

static embediq_err_t posix_factory_reset(embediq_provisioning_ctx_t *ctx)
{
    (void)ctx;

    static const char *const k_keys[] = {
        "mqtt.client_id", "prov.state", "mqtt.host",
        "mqtt.port",      "mqtt.username", "mqtt.password"
    };

    for (size_t i = 0u; i < (sizeof(k_keys) / sizeof(k_keys[0])); i++) {
        /* embediq_nvm_delete() is idempotent — returns EMBEDIQ_OK for an
         * absent key, so any non-OK return is a genuine store error. */
        if (embediq_nvm_delete(k_keys[i]) != EMBEDIQ_OK) {
            EMBEDIQ_HAL_OBS_EMIT_ERROR(EMBEDIQ_HAL_SRC_PROV, HAL_ERR_IO);
            return EMBEDIQ_ERR;
        }
    }

    if (embediq_nvm_flush() != EMBEDIQ_OK) {
        EMBEDIQ_HAL_OBS_EMIT_ERROR(EMBEDIQ_HAL_SRC_PROV, HAL_ERR_IO);
        return EMBEDIQ_ERR;
    }

    return EMBEDIQ_OK;
}

/* ---------------------------------------------------------------------------
 * Registration
 * ------------------------------------------------------------------------- */

static const embediq_provisioning_ops_t s_posix_ops = {
    .version            = EMBEDIQ_PROV_OPS_VERSION,
    .identity_check     = posix_identity_check,
    .fleet_config_check = posix_fleet_config_check,
    .factory_reset      = posix_factory_reset,
};

void hal_provisioning_posix_declare(void)
{
    embediq_provisioning_ops_register(&s_posix_ops);
}
