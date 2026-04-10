/*
 * hal/posix/ops/hal_tls_posix.c — POSIX TLS ops table stub
 *
 * Implements embediq_tls_ops_t for the POSIX/host platform.
 * All operations return EMBEDIQ_ERR — this is a compile-time contract stub.
 * Full OpenSSL implementation (connect_async, disconnect, send, recv) is Phase 2.
 *
 * Context safety: INSTANCE-SAFE — the ops table is a const singleton;
 * no shared mutable state. Concurrent callers see the same return values.
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "hal_tls_posix.h"

/* ---------------------------------------------------------------------------
 * Stub implementations — all return EMBEDIQ_ERR.
 * Phase 2: replace each stub with the OpenSSL implementation.
 * ------------------------------------------------------------------------- */

static embediq_err_t tls_connect_async(const char              *host,
                                        uint16_t                 port,
                                        const char              *sni,
                                        embediq_tls_connect_cb_t on_complete,
                                        embediq_bus_token_t     *token)
{
    (void)host; (void)port; (void)sni; (void)on_complete; (void)token;
    /* Stub: on_complete is NOT called — caller must handle EMBEDIQ_ERR return. */
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

/* ---------------------------------------------------------------------------
 * Ops table singleton
 * version MUST be first field — ABI versioning (D-LIB-5, I-17).
 * All function pointers non-NULL — contract enforcement (ops table rule 3).
 * ------------------------------------------------------------------------- */
static const embediq_tls_ops_t g_tls_posix_ops = {
    .version       = EMBEDIQ_TLS_OPS_VERSION,
    .connect_async = tls_connect_async,
    .disconnect    = tls_disconnect,
    .send          = tls_send,
    .recv          = tls_recv,
};

const embediq_tls_ops_t *hal_tls_posix_ops(void)
{
    return &g_tls_posix_ops;
}
