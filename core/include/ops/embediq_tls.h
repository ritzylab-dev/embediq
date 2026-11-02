/*
 * ops/embediq_tls.h — TLS transport operations table contract (v2)
 *
 * Defines embediq_tls_ops_t: the ops table contract for TLS transport
 * implementations. Platform HAL implementations provide a concrete ops
 * table per connection slot via hal_tls_<target>_ops_slot(idx).
 * FBs never call TLS directly — the MQTT and OTA platform implementations
 * call TLS internally. Transport must not depend on the message bus.
 *
 * Version history:
 *   v1 — initial stub contract (LIB-5)
 *   v2 — configure() added; callback changed to void *user_ctx;
 *         embediq_tls_err_t added; embediq_bus.h dependency removed.
 *   v2 (additive) — EMBEDIQ_TLS_ERR_BUSY (-12): returned by every op on a slot while a
 *         connect_async() is in flight. No field change; EMBEDIQ_TLS_OPS_VERSION stays 2.
 *   v2 (additive) — connect_async() on a connected slot also returns
 *         EMBEDIQ_TLS_ERR_BUSY. No field change; EMBEDIQ_TLS_OPS_VERSION stays 2.
 *
 * Context safety: INSTANCE-SAFE — each ops table slot represents one
 * independent TLS connection context. Concurrent use of distinct slots
 * is safe. Concurrent use of the same slot requires external serialisation.
 * While a connect_async() on a slot is in flight, every other op on that
 * slot — configure, a second connect_async, disconnect, send, recv —
 * returns EMBEDIQ_TLS_ERR_BUSY without blocking. The slot accepts ops again
 * once the connect has completed (no later than on_complete being invoked).
 * The completion callback and user context belong to the attempt that
 * started them; a later connect_async() on the same slot cannot redirect or
 * drop an earlier completion. A slot that is connected or connecting refuses
 * connect_async() with EMBEDIQ_TLS_ERR_BUSY: disconnect first.
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

#ifdef __cplusplus
extern "C" {
#endif

/** ABI version for embediq_tls_ops_t. Increment on any breaking field change. */
#define EMBEDIQ_TLS_OPS_VERSION  2u

/* ---------------------------------------------------------------------------
 * TLS-specific error codes
 *
 * Separate from embediq_err_t to keep TLS errors local to the TLS contract.
 * Callers must not mix embediq_tls_err_t with embediq_err_t.
 * Use direct comparison against EMBEDIQ_TLS_OK — do not use EMBEDIQ_SUCCEEDED().
 * ------------------------------------------------------------------------- */

typedef int32_t embediq_tls_err_t;

#define EMBEDIQ_TLS_OK                    ((embediq_tls_err_t)  0)
#define EMBEDIQ_TLS_ERR_NOT_CONFIGURED    ((embediq_tls_err_t) -1)  /**< connect_async called before configure() */
#define EMBEDIQ_TLS_ERR_DNS               ((embediq_tls_err_t) -2)  /**< hostname resolution failed */
#define EMBEDIQ_TLS_ERR_TCP               ((embediq_tls_err_t) -3)  /**< TCP connect failed or refused */
#define EMBEDIQ_TLS_ERR_TIMEOUT           ((embediq_tls_err_t) -4)  /**< handshake timed out */
#define EMBEDIQ_TLS_ERR_HANDSHAKE         ((embediq_tls_err_t) -5)  /**< TLS handshake failed (generic) */
#define EMBEDIQ_TLS_ERR_CERT_VERIFY       ((embediq_tls_err_t) -6)  /**< server cert failed verification */
#define EMBEDIQ_TLS_ERR_CERT_EXPIRED      ((embediq_tls_err_t) -7)  /**< our client cert has expired */
#define EMBEDIQ_TLS_ERR_CERT_REJECTED     ((embediq_tls_err_t) -8)  /**< server rejected our client cert */
#define EMBEDIQ_TLS_ERR_NO_RESOURCES      ((embediq_tls_err_t) -9)  /**< slot index out of range */
#define EMBEDIQ_TLS_ERR_SEND              ((embediq_tls_err_t)-10)  /**< send() underlying write failed */
#define EMBEDIQ_TLS_ERR_RECV              ((embediq_tls_err_t)-11)  /**< recv() underlying read failed */
#define EMBEDIQ_TLS_ERR_BUSY              ((embediq_tls_err_t)-12)  /**< connect_async() in flight; or connect_async() on a connected slot */

/* ---------------------------------------------------------------------------
 * Async connect callback
 *
 * Invoked by the TLS implementation when connect_async() completes.
 *
 * result:   EMBEDIQ_TLS_OK on successful handshake, specific error otherwise.
 * user_ctx: the pointer passed unchanged from connect_async() — caller decides
 *           what it points to (OSAL semaphore, bus token wrapper, etc.).
 *
 * Threading: the callback may be invoked from any thread (worker thread,
 * platform thread, etc.). The callback MUST NOT assume it is called from
 * the same thread that called connect_async(). This rule applies to ALL
 * platform implementations including future RTOS ports.
 *
 * The callback must return quickly. It must not block.
 * ------------------------------------------------------------------------- */
typedef void (*embediq_tls_connect_cb_t)(embediq_tls_err_t  result,
                                          void              *user_ctx);

/* ---------------------------------------------------------------------------
 * TLS transport operations table
 *
 * All function pointers must be non-NULL when the ops table is used.
 * Version check: verify ops->version == EMBEDIQ_TLS_OPS_VERSION before use.
 * A version mismatch means the implementation was built against a different
 * contract revision — treat as a build error.
 * ------------------------------------------------------------------------- */
typedef struct {
    /**
     * MUST BE FIRST — ABI versioning.
     * Set to EMBEDIQ_TLS_OPS_VERSION at ops table declaration.
     */
    uint32_t version;

    /**
     * Load TLS credentials into this connection slot.
     *
     * Must be called before the first connect_async(). Calling connect_async()
     * without a prior configure() returns EMBEDIQ_TLS_ERR_NOT_CONFIGURED.
     *
     * May be called again while connected — the new credentials are stored
     * and applied on the NEXT connect_async() after disconnect(). The current
     * session is unaffected. This enables certificate rotation without
     * connection downtime.
     *
     * ca_cert_pem:     NUL-terminated PEM string of the CA certificate used to
     *                  verify the server. Must not be NULL.
     * client_cert_pem: NUL-terminated PEM string of the client certificate.
     *                  Pass NULL for server-only TLS (no mutual authentication).
     * client_key_pem:  NUL-terminated PEM string of the client private key.
     *                  Pass NULL if client_cert_pem is NULL.
     *
     * Callers pass PEM content (not file paths). The caller reads the file or
     * flash cert store and passes the string content. This contract is identical
     * on POSIX (file read) and RTOS (flash cert store read).
     *
     * Returns EMBEDIQ_TLS_OK on success.
     */
    embediq_tls_err_t (*configure)(const char *ca_cert_pem,
                                    const char *client_cert_pem,
                                    const char *client_key_pem);

    /**
     * Initiate a TLS connection asynchronously.
     *
     * Performs DNS resolution, TCP connect, and TLS handshake without
     * blocking the calling thread. Delivers result via on_complete callback.
     *
     * configure() must have been called before this function. Returns
     * EMBEDIQ_TLS_ERR_NOT_CONFIGURED immediately if not configured.
     *
     * host:        NUL-terminated hostname or IP address.
     * port:        TCP port number.
     * sni:         NUL-terminated Server Name Indication string (may equal host).
     * on_complete: callback invoked on success or failure. See embediq_tls_connect_cb_t.
     * user_ctx:    passed unchanged to on_complete. May be NULL.
     *
     * Returns EMBEDIQ_TLS_OK if the operation was successfully initiated.
     * Returns EMBEDIQ_TLS_ERR_BUSY if the slot is connected or connecting
     * (disconnect first).
     * on_complete is NOT called on error return — the caller must check the
     * return value and handle it directly.
     */
    embediq_tls_err_t (*connect_async)(const char              *host,
                                        uint16_t                 port,
                                        const char              *sni,
                                        embediq_tls_connect_cb_t on_complete,
                                        void                    *user_ctx);

    /**
     * Close the TLS session and release the underlying TCP connection.
     * Sends TLS close_notify before closing. Safe to call if not connected.
     */
    embediq_tls_err_t (*disconnect)(void);

    /**
     * Send bytes over the established TLS session.
     *
     * data: pointer to bytes to send.
     * len:  number of bytes (max 65535).
     * Returns EMBEDIQ_TLS_OK on success.
     * Returns EMBEDIQ_TLS_ERR_SEND if not connected or if the send fails.
     * Must return within 50 ms.
     */
    embediq_tls_err_t (*send)(const uint8_t *data, uint16_t len);

    /**
     * Receive bytes from the established TLS session.
     *
     * buf:        receive buffer.
     * len:        buffer capacity in bytes.
     * timeout_ms: maximum milliseconds to wait for data.
     * Returns number of bytes received (> 0) on success.
     * Returns 0 on timeout.
     * Returns EMBEDIQ_TLS_ERR_RECV (negative) on error or disconnection.
     */
    int32_t (*recv)(uint8_t *buf, uint16_t len, uint32_t timeout_ms);

} embediq_tls_ops_t;

#ifdef __cplusplus
}
#endif

#endif /* EMBEDIQ_OPS_TLS_H */
