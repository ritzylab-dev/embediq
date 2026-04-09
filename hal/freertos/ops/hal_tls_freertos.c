/*
 * hal/freertos/ops/hal_tls_freertos.c — FreeRTOS TLS ops table stub (mbedTLS)
 *
 * Implements embediq_tls_ops_t for FreeRTOS/ESP32 using mbedTLS.
 * This is a stub — all operations return EMBEDIQ_ERR.
 * Full mbedTLS implementation is Phase 2 (after FreeRTOS OSAL is operational).
 *
 * Depends on: third_party/mbedtls/ (source committed in Phase 2).
 *
 * mbedTLS include paths (add when implementing):
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

/* ---------------------------------------------------------------------------
 * Stub implementations — replace with mbedTLS calls in Phase 2.
 * ------------------------------------------------------------------------- */

static embediq_err_t tls_connect_async(const char              *host,
                                        uint16_t                 port,
                                        const char              *sni,
                                        embediq_tls_connect_cb_t on_complete,
                                        embediq_bus_token_t     *token)
{
    (void)host; (void)port; (void)sni; (void)on_complete; (void)token;
    return EMBEDIQ_ERR;
}

static embediq_err_t tls_disconnect(void)
{
    return EMBEDIQ_ERR;
}

static embediq_err_t tls_send(const uint8_t *data, uint16_t len)
{
    (void)data; (void)len;
    return EMBEDIQ_ERR;
}

static int32_t tls_recv(uint8_t *buf, uint16_t len, uint32_t timeout_ms)
{
    (void)buf; (void)len; (void)timeout_ms;
    return (int32_t)EMBEDIQ_ERR;
}

static const embediq_tls_ops_t g_tls_freertos_ops = {
    .version       = EMBEDIQ_TLS_OPS_VERSION,
    .connect_async = tls_connect_async,
    .disconnect    = tls_disconnect,
    .send          = tls_send,
    .recv          = tls_recv,
};

const embediq_tls_ops_t *hal_tls_freertos_ops(void)
{
    return &g_tls_freertos_ops;
}
