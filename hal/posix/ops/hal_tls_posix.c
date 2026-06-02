/*
 * hal/posix/ops/hal_tls_posix.c — POSIX TLS ops table stubs (contract v2)
 *
 * Implements embediq_tls_ops_t for the POSIX/host platform.
 * All operations return error codes — this is a compile-time contract stub.
 * Full mbedTLS implementation (configure, connect_async, disconnect, send, recv)
 * is Item 5.5 PR-C. mbedTLS source is added in PR-B.
 *
 * Static pool: EMBEDIQ_TLS_MAX_CONNECTIONS slots, each with its own ops table.
 * hal_tls_posix_ops_slot(idx) returns the ops table for slot idx, or NULL
 * if idx >= EMBEDIQ_TLS_MAX_CONNECTIONS.
 *
 * Context safety: INSTANCE-SAFE — each slot is an independent singleton.
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "hal_tls_posix.h"
#include "embediq_config.h"
#include "hal/hal_defs.h"

/* ---------------------------------------------------------------------------
 * Slot 0 stub implementations
 * Replace with real mbedTLS calls in PR-C.
 * Each slot requires its own set of static functions (trampoline pattern —
 * ops table function pointers cannot carry a context/slot index).
 * ------------------------------------------------------------------------- */

static embediq_tls_err_t tls_s0_configure(const char *ca, const char *cc, const char *ck)
{
    (void)ca; (void)cc; (void)ck;
    return EMBEDIQ_TLS_ERR_NOT_CONFIGURED; /* stub: not yet implemented */
}

static embediq_tls_err_t tls_s0_connect_async(const char              *host,
                                                uint16_t                 port,
                                                const char              *sni,
                                                embediq_tls_connect_cb_t on_complete,
                                                void                    *user_ctx)
{
    (void)host; (void)port; (void)sni; (void)on_complete; (void)user_ctx;
    return EMBEDIQ_TLS_ERR_NOT_CONFIGURED;
}

static embediq_tls_err_t tls_s0_disconnect(void) { return EMBEDIQ_TLS_ERR_NOT_CONFIGURED; }

static embediq_tls_err_t tls_s0_send(const uint8_t *data, uint16_t len)
{
    (void)data; (void)len;
    return EMBEDIQ_TLS_ERR_NOT_CONFIGURED;
}

static int32_t tls_s0_recv(uint8_t *buf, uint16_t len, uint32_t timeout_ms)
{
    (void)buf; (void)len; (void)timeout_ms;
    return (int32_t)EMBEDIQ_TLS_ERR_NOT_CONFIGURED;
}

static const embediq_tls_ops_t g_tls_slot0_ops = {
    .version       = EMBEDIQ_TLS_OPS_VERSION,
    .configure     = tls_s0_configure,
    .connect_async = tls_s0_connect_async,
    .disconnect    = tls_s0_disconnect,
    .send          = tls_s0_send,
    .recv          = tls_s0_recv,
};

/* ---------------------------------------------------------------------------
 * Slot 1 stub implementations (only compiled if EMBEDIQ_TLS_MAX_CONNECTIONS >= 2)
 * ------------------------------------------------------------------------- */

#if EMBEDIQ_TLS_MAX_CONNECTIONS >= 2

static embediq_tls_err_t tls_s1_configure(const char *ca, const char *cc, const char *ck)
{
    (void)ca; (void)cc; (void)ck;
    return EMBEDIQ_TLS_ERR_NOT_CONFIGURED;
}

static embediq_tls_err_t tls_s1_connect_async(const char              *host,
                                                uint16_t                 port,
                                                const char              *sni,
                                                embediq_tls_connect_cb_t on_complete,
                                                void                    *user_ctx)
{
    (void)host; (void)port; (void)sni; (void)on_complete; (void)user_ctx;
    return EMBEDIQ_TLS_ERR_NOT_CONFIGURED;
}

static embediq_tls_err_t tls_s1_disconnect(void) { return EMBEDIQ_TLS_ERR_NOT_CONFIGURED; }

static embediq_tls_err_t tls_s1_send(const uint8_t *data, uint16_t len)
{
    (void)data; (void)len;
    return EMBEDIQ_TLS_ERR_NOT_CONFIGURED;
}

static int32_t tls_s1_recv(uint8_t *buf, uint16_t len, uint32_t timeout_ms)
{
    (void)buf; (void)len; (void)timeout_ms;
    return (int32_t)EMBEDIQ_TLS_ERR_NOT_CONFIGURED;
}

static const embediq_tls_ops_t g_tls_slot1_ops = {
    .version       = EMBEDIQ_TLS_OPS_VERSION,
    .configure     = tls_s1_configure,
    .connect_async = tls_s1_connect_async,
    .disconnect    = tls_s1_disconnect,
    .send          = tls_s1_send,
    .recv          = tls_s1_recv,
};

#endif /* EMBEDIQ_TLS_MAX_CONNECTIONS >= 2 */

/* ---------------------------------------------------------------------------
 * Slot accessor
 * Returns NULL for out-of-range indices — caller must handle NULL gracefully.
 * ------------------------------------------------------------------------- */

const embediq_tls_ops_t *hal_tls_posix_ops_slot(uint8_t idx)
{
    switch (idx) {
        case 0:  return &g_tls_slot0_ops;
#if EMBEDIQ_TLS_MAX_CONNECTIONS >= 2
        case 1:  return &g_tls_slot1_ops;
#endif
        default:
            EMBEDIQ_HAL_OBS_EMIT_ERROR(EMBEDIQ_HAL_SRC_TLS, HAL_ERR_INVALID);
            return NULL;
    }
}

/* Convenience alias: slot 0 is the default single-connection slot. */
const embediq_tls_ops_t *hal_tls_posix_ops(void)
{
    return hal_tls_posix_ops_slot(0u);
}
