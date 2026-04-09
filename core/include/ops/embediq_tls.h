/*
 * ops/embediq_tls.h — TLS transport operations table contract
 *
 * Defines embediq_tls_ops_t: the ops table contract for TLS transport
 * implementations. Platform HAL implementations register a concrete
 * ops table via embediq_platform_lib_declare() at boot. FBs that require
 * secure transport call only through this contract — never through mbedTLS
 * or OpenSSL headers directly.
 *
 * Context safety: INSTANCE-SAFE — each ops table instance represents one
 * independent TLS connection context. Concurrent use of distinct instances
 * is safe. Concurrent use of the same instance requires external serialisation.
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef EMBEDIQ_OPS_TLS_H
#define EMBEDIQ_OPS_TLS_H

#include <stdint.h>
#include "embediq_osal.h"   /* embediq_err_t */
#include "embediq_bus.h"    /* embediq_bus_token_t */

#ifdef __cplusplus
extern "C" {
#endif

/** ABI version for embediq_tls_ops_t. Increment on any breaking field change. */
#define EMBEDIQ_TLS_OPS_VERSION  1u

/**
 * Callback type invoked when an async TLS connect operation completes.
 *
 * result: EMBEDIQ_OK on successful handshake, EMBEDIQ_ERR on any failure.
 * token:  opaque — the ONLY permitted operation is passing it to
 *         embediq_bus_post_async(). Valid only for the duration of this call.
 *
 * The callback MUST NOT block, call OSAL primitives, or access FB state.
 */
typedef void (*embediq_tls_connect_cb_t)(embediq_err_t    result,
                                          embediq_bus_token_t *token);

/**
 * TLS transport operations table.
 *
 * All function pointers must be non-NULL when registered, except
 * connect_async which is mandatory (TLS handshake is always long-running).
 *
 * Version check: the engine calls ops->version >= EMBEDIQ_TLS_OPS_VERSION
 * before using any function pointer. A mismatch triggers EMBEDIQ_PANIC
 * on constrained profiles, EMBEDIQ_LOG_WARN on rich profiles. (I-17)
 */
typedef struct {
    /**
     * MUST BE FIRST — ABI versioning (I-17).
     * Set to EMBEDIQ_TLS_OPS_VERSION at ops table declaration.
     */
    uint32_t version;

    /**
     * Initiate a TLS connection asynchronously.
     *
     * Performs DNS resolution, TCP connect, and TLS handshake without
     * blocking the calling thread. Delivers result via on_complete callback.
     *
     * host:        NUL-terminated hostname or IP address.
     * port:        TCP port number.
     * sni:         NUL-terminated Server Name Indication string (may equal host).
     * on_complete: callback invoked on success or failure.
     * token:       passed unchanged to on_complete.
     *
     * Returns EMBEDIQ_OK if the operation was successfully initiated.
     * Returns EMBEDIQ_ERR if the connection attempt could not be started
     * (e.g. already connected, no resources). on_complete is NOT called on
     * this error path — the caller must handle the return value.
     */
    embediq_err_t (*connect_async)(const char              *host,
                                    uint16_t                 port,
                                    const char              *sni,
                                    embediq_tls_connect_cb_t on_complete,
                                    embediq_bus_token_t     *token);

    /**
     * Close the TLS session and release the underlying TCP connection.
     * Sends TLS close_notify before closing. Safe to call if not connected.
     */
    embediq_err_t (*disconnect)(void);

    /**
     * Send bytes over the established TLS session.
     *
     * data: pointer to bytes to send.
     * len:  number of bytes.
     * Returns EMBEDIQ_OK on success. Returns EMBEDIQ_ERR if not connected
     * or if the underlying send fails. Must return within 50 ms.
     */
    embediq_err_t (*send)(const uint8_t *data, uint16_t len);

    /**
     * Receive bytes from the established TLS session.
     *
     * buf:        receive buffer.
     * len:        buffer capacity in bytes.
     * timeout_ms: maximum milliseconds to wait for data.
     * Returns number of bytes received (> 0) on success.
     * Returns 0 on timeout. Returns EMBEDIQ_ERR on error or disconnection.
     */
    int32_t (*recv)(uint8_t *buf, uint16_t len, uint32_t timeout_ms);

} embediq_tls_ops_t;

#ifdef __cplusplus
}
#endif

#endif /* EMBEDIQ_OPS_TLS_H */
