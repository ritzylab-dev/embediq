/*
 * hal/freertos/ops/hal_tls_freertos.c — FreeRTOS TLS ops table stub (contract v2)
 *
 * Implements embediq_tls_ops_t for FreeRTOS/ESP32 using mbedTLS.
 * All operations return EMBEDIQ_TLS_ERR_NOT_CONFIGURED — this is a stub.
 * Full mbedTLS implementation is Phase 3 (after FreeRTOS OSAL is operational).
 *
 * mbedTLS include paths (add when implementing in Phase 3):
 *   third_party/mbedtls/include/mbedtls/ssl.h
 *   third_party/mbedtls/include/mbedtls/net_sockets.h
 *   third_party/mbedtls/include/mbedtls/entropy.h
 *   third_party/mbedtls/include/mbedtls/ctr_drbg.h
 *
 * Context safety: INSTANCE-SAFE — no shared mutable state in this stub.
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ops/embediq_tls.h"

static embediq_tls_err_t tls_configure(const char *ca, const char *cc, const char *ck)
{
    (void)ca; (void)cc; (void)ck;
    return EMBEDIQ_TLS_ERR_NOT_CONFIGURED;
}

static embediq_tls_err_t tls_connect_async(const char              *host,
                                             uint16_t                 port,
                                             const char              *sni,
                                             embediq_tls_connect_cb_t on_complete,
                                             void                    *user_ctx)
{
    (void)host; (void)port; (void)sni; (void)on_complete; (void)user_ctx;
    return EMBEDIQ_TLS_ERR_NOT_CONFIGURED;
}

static embediq_tls_err_t tls_disconnect(void)
{
    return EMBEDIQ_TLS_ERR_NOT_CONFIGURED;
}

static embediq_tls_err_t tls_send(const uint8_t *data, uint16_t len)
{
    (void)data; (void)len;
    return EMBEDIQ_TLS_ERR_NOT_CONFIGURED;
}

static int32_t tls_recv(uint8_t *buf, uint16_t len, uint32_t timeout_ms)
{
    (void)buf; (void)len; (void)timeout_ms;
    return (int32_t)EMBEDIQ_TLS_ERR_NOT_CONFIGURED;
}

static const embediq_tls_ops_t g_tls_freertos_ops = {
    .version       = EMBEDIQ_TLS_OPS_VERSION,
    .configure     = tls_configure,
    .connect_async = tls_connect_async,
    .disconnect    = tls_disconnect,
    .send          = tls_send,
    .recv          = tls_recv,
};

const embediq_tls_ops_t *hal_tls_freertos_ops(void)
{
    return &g_tls_freertos_ops;
}
