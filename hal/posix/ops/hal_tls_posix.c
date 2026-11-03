#define _POSIX_C_SOURCE 200809L  /* pthread, select, usleep */

/*
 * hal/posix/ops/hal_tls_posix.c — POSIX TLS ops table (mbedTLS 3.6.1)
 *
 * Real TLS implementation for the POSIX/Linux host platform.
 * Uses mbedTLS 3.6.1 (vendored in third_party/mbedtls/).
 *
 * Static pool: EMBEDIQ_TLS_MAX_CONNECTIONS slots (embediq_config.h).
 * Each slot is one independent TLS connection context.
 * Trampoline pattern: each slot has its own set of static function wrappers
 * because ops table function pointers carry no context/slot parameter.
 *
 * Thread safety: one pthread_mutex_t per slot, only ever unlocked by the
 * thread that locked it. All public functions lock the slot mutex.
 * connect_async() refuses a slot that is connected or connecting; otherwise
 * it sets `connecting` under the mutex, fills the slot's argument entry,
 * releases the mutex and spawns a detached connect thread. While `connecting`
 * is true the connect thread owns the slot's mbedTLS and socket state
 * exclusively, without holding the mutex; every other op checks `connecting`
 * under the mutex and returns EMBEDIQ_TLS_ERR_BUSY. The connect thread copies
 * its argument entry before any blocking work and uses only that copy: the
 * entry may be reused by the next attempt once `connecting` is cleared. It
 * publishes its result and clears `connecting` under the mutex, unlocks, then
 * calls the copy's on_complete with the copy's user_ctx (mutex not held).
 *
 * Phase 3 note: FreeRTOS implementation uses mbedTLS with OSAL primitives
 * instead of pthreads. The ops table contract is identical.
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "hal_tls_posix.h"
#include "embediq_config.h"
#include "hal/hal_defs.h"

#include <pthread.h>
#include <stdatomic.h>
#include <string.h>
#include <sys/select.h>
#include <sys/time.h>
#include <stdbool.h>
#include <stdio.h>

/* mbedTLS headers — permitted for hal/posix/ implementation files */
#include "mbedtls/ssl.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/pk.h"
#include "mbedtls/error.h"

/* Implementation-internal buffer sizes — not product-tunable config knobs */
#define TLS_HOSTNAME_MAX   256u   /* max hostname / SNI string length */
#define TLS_PORT_STR_MAX     8u   /* "65535\0" fits in 6 bytes; 8u with margin */

/* ---------------------------------------------------------------------------
 * Per-slot TLS state
 * Static pool — no malloc (R-02). Size controlled by embediq_config.h.
 * ------------------------------------------------------------------------- */

typedef struct {
    mbedtls_ssl_context      ssl;
    mbedtls_ssl_config       conf;
    mbedtls_x509_crt         ca_cert;
    mbedtls_x509_crt         client_cert;
    mbedtls_pk_context       client_key;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_entropy_context  entropy;
    mbedtls_net_context      net;
    pthread_mutex_t          mutex;
    bool                     configured;
    bool                     connected;
    bool                     has_client_cert;
    bool                     connecting;   /* connect_async in flight; under mutex */
} tls_slot_t;

static tls_slot_t g_slots[EMBEDIQ_TLS_MAX_CONNECTIONS];

/* ---------------------------------------------------------------------------
 * Connect thread argument — one per slot
 * ------------------------------------------------------------------------- */

typedef struct {
    uint8_t                  slot_idx;
    char                     host[TLS_HOSTNAME_MAX];
    uint16_t                 port;
    char                     sni[TLS_HOSTNAME_MAX];
    embediq_tls_connect_cb_t on_complete;
    void                    *user_ctx;
} connect_args_t;

static connect_args_t g_connect_args[EMBEDIQ_TLS_MAX_CONNECTIONS];

#ifdef EMBEDIQ_PLATFORM_HOST
/* Test hook: called by connect_finish after the slot is released and before
 * on_complete, with the attempt's user_ctx. NULL = no hook. */
typedef void (*tls_connect_finish_hook_t)(void *user_ctx);
static _Atomic(tls_connect_finish_hook_t) s_connect_finish_hook = NULL;
/* Sessions whose ssl context and socket are initialised and not yet freed. */
static atomic_int s_live_sessions = 0;
#define LIVE_SESSION_ADD(n) atomic_fetch_add(&s_live_sessions, (n))
#else
#define LIVE_SESSION_ADD(n) ((void)0)
#endif

/* ---------------------------------------------------------------------------
 * Internal helpers
 * ------------------------------------------------------------------------- */

static void slot_free_certs(tls_slot_t *s)
{
    mbedtls_ssl_free(&s->ssl);
    mbedtls_ssl_config_free(&s->conf);
    mbedtls_x509_crt_free(&s->ca_cert);
    mbedtls_x509_crt_free(&s->client_cert);
    mbedtls_pk_free(&s->client_key);
    mbedtls_net_free(&s->net);
}

static embediq_tls_err_t map_mbedtls_handshake_err(tls_slot_t *s, int rc)
{
    if (rc == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED) {
        uint32_t flags = mbedtls_ssl_get_verify_result(&s->ssl);
        if (flags & MBEDTLS_X509_BADCERT_EXPIRED)  { return EMBEDIQ_TLS_ERR_CERT_EXPIRED;  }
        if (flags & MBEDTLS_X509_BADCERT_REVOKED)  { return EMBEDIQ_TLS_ERR_CERT_REJECTED; }
        if (flags & MBEDTLS_X509_BADCERT_NOT_TRUSTED) { return EMBEDIQ_TLS_ERR_CERT_VERIFY; }
        return EMBEDIQ_TLS_ERR_CERT_VERIFY;
    }
    if (rc == MBEDTLS_ERR_SSL_TIMEOUT) { return EMBEDIQ_TLS_ERR_TIMEOUT; }
    return EMBEDIQ_TLS_ERR_HANDSHAKE;
}

/* ---------------------------------------------------------------------------
 * Connect thread — spawned by connect_async()
 * Owns the slot exclusively while s->connecting is true and does its work
 * WITHOUT the slot mutex, from its own copy of the attempt. At the end it
 * takes the mutex, publishes the result and clears `connecting`, unlocks,
 * then calls the attempt's on_complete (mutex not held).
 * ------------------------------------------------------------------------- */

/* attempt is the connect thread's own copy: after the unlock the slot and its
 * argument entry belong to the next attempt, so nothing after it reads them. */
static void connect_finish(tls_slot_t *s, const connect_args_t *attempt,
                           embediq_tls_err_t result)
{
    pthread_mutex_lock(&s->mutex);
    s->connected  = (result == EMBEDIQ_TLS_OK);
    s->connecting = false;
    pthread_mutex_unlock(&s->mutex);
#ifdef EMBEDIQ_PLATFORM_HOST
    tls_connect_finish_hook_t hook = atomic_load(&s_connect_finish_hook);
    if (hook) hook(attempt->user_ctx);
#endif
    attempt->on_complete(result, attempt->user_ctx);
}

static void *connect_thread(void *arg)
{
    /* Copy the attempt first, while `connecting` keeps the entry unchanged. */
    const connect_args_t attempt = *(const connect_args_t *)arg;
    const connect_args_t *a = &attempt;
    tls_slot_t     *s   = &g_slots[a->slot_idx];
    embediq_tls_err_t result;
    char            port_str[TLS_PORT_STR_MAX];
    int             rc;

    snprintf(port_str, sizeof(port_str), "%u", (unsigned)a->port);

    /* TCP connect (includes DNS resolution) */
    rc = mbedtls_net_connect(&s->net, a->host, port_str, MBEDTLS_NET_PROTO_TCP);
    if (rc != 0) {
        EMBEDIQ_HAL_OBS_EMIT_ERROR(EMBEDIQ_HAL_SRC_TLS, HAL_ERR_IO);
        result = (rc == MBEDTLS_ERR_NET_UNKNOWN_HOST) ?
                 EMBEDIQ_TLS_ERR_DNS : EMBEDIQ_TLS_ERR_TCP;
        connect_finish(s, a, result);
        return NULL;
    }

    /* Initialise SSL context for this connection */
    mbedtls_ssl_init(&s->ssl);
    LIVE_SESSION_ADD(1);
    rc = mbedtls_ssl_setup(&s->ssl, &s->conf);
    if (rc != 0) {
        EMBEDIQ_HAL_OBS_EMIT_ERROR(EMBEDIQ_HAL_SRC_TLS, HAL_ERR_IO);
        mbedtls_net_free(&s->net);
        connect_finish(s, a, EMBEDIQ_TLS_ERR_HANDSHAKE);
        return NULL;
    }

    /* SNI — identifies the target server for cert verification */
    rc = mbedtls_ssl_set_hostname(&s->ssl, a->sni);
    if (rc != 0) {
        EMBEDIQ_HAL_OBS_EMIT_ERROR(EMBEDIQ_HAL_SRC_TLS, HAL_ERR_INVALID);
        mbedtls_ssl_free(&s->ssl);
        mbedtls_net_free(&s->net);
        LIVE_SESSION_ADD(-1);
        connect_finish(s, a, EMBEDIQ_TLS_ERR_HANDSHAKE);
        return NULL;
    }

    /* Wire mbedTLS I/O to the BSD socket */
    mbedtls_ssl_set_bio(&s->ssl, &s->net,
                        mbedtls_net_send, mbedtls_net_recv, NULL);

    /* TLS handshake — retry on WANT_READ / WANT_WRITE (non-blocking I/O events) */
    do {
        rc = mbedtls_ssl_handshake(&s->ssl);
    } while (rc == MBEDTLS_ERR_SSL_WANT_READ ||
             rc == MBEDTLS_ERR_SSL_WANT_WRITE);

    if (rc != 0) {
        EMBEDIQ_HAL_OBS_EMIT_ERROR(EMBEDIQ_HAL_SRC_TLS, HAL_ERR_IO);
        result = map_mbedtls_handshake_err(s, rc);
        mbedtls_ssl_free(&s->ssl);
        mbedtls_net_free(&s->net);
        LIVE_SESSION_ADD(-1);
        connect_finish(s, a, result);
        return NULL;
    }

    connect_finish(s, a, EMBEDIQ_TLS_OK);
    return NULL;
}

/* ---------------------------------------------------------------------------
 * Generic configure/connect/disconnect/send/recv — called by slot trampolines
 * ------------------------------------------------------------------------- */

static embediq_tls_err_t slot_configure(uint8_t idx,
                                         const char *ca_cert_pem,
                                         const char *client_cert_pem,
                                         const char *client_key_pem)
{
    tls_slot_t *s = &g_slots[idx];
    int         rc;

    pthread_mutex_lock(&s->mutex);

    if (s->connecting) {
        pthread_mutex_unlock(&s->mutex);
        return EMBEDIQ_TLS_ERR_BUSY;   /* connect thread owns the slot */
    }

    /* Free any previous configuration */
    if (s->connected) LIVE_SESSION_ADD(-1);
    slot_free_certs(s);
    mbedtls_ctr_drbg_free(&s->ctr_drbg);
    mbedtls_entropy_free(&s->entropy);
    s->configured      = false;
    s->connected       = false;
    s->has_client_cert = false;

    /* Re-initialise mbedTLS contexts */
    mbedtls_ssl_config_init(&s->conf);
    mbedtls_x509_crt_init(&s->ca_cert);
    mbedtls_x509_crt_init(&s->client_cert);
    mbedtls_pk_init(&s->client_key);
    mbedtls_ctr_drbg_init(&s->ctr_drbg);
    mbedtls_entropy_init(&s->entropy);
    mbedtls_net_init(&s->net);

    /* Seed DRBG from /dev/urandom (POSIX platform) */
    rc = mbedtls_ctr_drbg_seed(&s->ctr_drbg, mbedtls_entropy_func,
                                 &s->entropy, NULL, 0);
    if (rc != 0) {
        EMBEDIQ_HAL_OBS_EMIT_ERROR(EMBEDIQ_HAL_SRC_TLS, HAL_ERR_IO);
        pthread_mutex_unlock(&s->mutex);
        return EMBEDIQ_TLS_ERR_NOT_CONFIGURED;
    }

    /* TLS 1.2 client defaults */
    rc = mbedtls_ssl_config_defaults(&s->conf,
                                      MBEDTLS_SSL_IS_CLIENT,
                                      MBEDTLS_SSL_TRANSPORT_STREAM,
                                      MBEDTLS_SSL_PRESET_DEFAULT);
    if (rc != 0) {
        EMBEDIQ_HAL_OBS_EMIT_ERROR(EMBEDIQ_HAL_SRC_TLS, HAL_ERR_IO);
        pthread_mutex_unlock(&s->mutex);
        return EMBEDIQ_TLS_ERR_NOT_CONFIGURED;
    }

    /* Always verify server certificate */
    mbedtls_ssl_conf_authmode(&s->conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_rng(&s->conf, mbedtls_ctr_drbg_random, &s->ctr_drbg);

    /* Load CA certificate (mandatory) */
    rc = mbedtls_x509_crt_parse(&s->ca_cert,
                                  (const unsigned char *)ca_cert_pem,
                                  strlen(ca_cert_pem) + 1u);
    if (rc != 0) {
        EMBEDIQ_HAL_OBS_EMIT_ERROR(EMBEDIQ_HAL_SRC_TLS, HAL_ERR_INVALID);
        pthread_mutex_unlock(&s->mutex);
        return EMBEDIQ_TLS_ERR_CERT_VERIFY;
    }
    mbedtls_ssl_conf_ca_chain(&s->conf, &s->ca_cert, NULL);

    /* Load client cert + key if provided (mutual TLS) */
    if (client_cert_pem != NULL && client_key_pem != NULL) {
        rc = mbedtls_x509_crt_parse(&s->client_cert,
                                      (const unsigned char *)client_cert_pem,
                                      strlen(client_cert_pem) + 1u);
        if (rc != 0) {
            EMBEDIQ_HAL_OBS_EMIT_ERROR(EMBEDIQ_HAL_SRC_TLS, HAL_ERR_INVALID);
            pthread_mutex_unlock(&s->mutex);
            return EMBEDIQ_TLS_ERR_CERT_REJECTED;
        }

        rc = mbedtls_pk_parse_key(&s->client_key,
                                    (const unsigned char *)client_key_pem,
                                    strlen(client_key_pem) + 1u,
                                    NULL, 0,
                                    mbedtls_ctr_drbg_random, &s->ctr_drbg);
        if (rc != 0) {
            EMBEDIQ_HAL_OBS_EMIT_ERROR(EMBEDIQ_HAL_SRC_TLS, HAL_ERR_INVALID);
            pthread_mutex_unlock(&s->mutex);
            return EMBEDIQ_TLS_ERR_CERT_REJECTED;
        }

        rc = mbedtls_ssl_conf_own_cert(&s->conf, &s->client_cert, &s->client_key);
        if (rc != 0) {
            EMBEDIQ_HAL_OBS_EMIT_ERROR(EMBEDIQ_HAL_SRC_TLS, HAL_ERR_IO);
            pthread_mutex_unlock(&s->mutex);
            return EMBEDIQ_TLS_ERR_NOT_CONFIGURED;
        }
        s->has_client_cert = true;
    }

    s->configured = true;
    pthread_mutex_unlock(&s->mutex);
    return EMBEDIQ_TLS_OK;
}

static embediq_tls_err_t slot_connect_async(uint8_t                   idx,
                                              const char               *host,
                                              uint16_t                  port,
                                              const char               *sni,
                                              embediq_tls_connect_cb_t  on_complete,
                                              void                     *user_ctx)
{
    tls_slot_t *s = &g_slots[idx];

    pthread_mutex_lock(&s->mutex);

    /* A live session or an attempt in flight owns the socket and the ssl
     * context: disconnect first. */
    if (s->connecting || s->connected) {
        pthread_mutex_unlock(&s->mutex);
        return EMBEDIQ_TLS_ERR_BUSY;
    }
    if (!s->configured) {
        EMBEDIQ_HAL_OBS_EMIT_ERROR(EMBEDIQ_HAL_SRC_TLS, HAL_ERR_INVALID);
        pthread_mutex_unlock(&s->mutex);
        return EMBEDIQ_TLS_ERR_NOT_CONFIGURED;
    }

    /* Take ownership of the slot for the connect thread, then release the
     * mutex: it is only ever unlocked by the thread that locked it. */
    s->connecting = true;
    connect_args_t *a = &g_connect_args[idx];
    a->slot_idx   = idx;
    a->port       = port;
    a->on_complete = on_complete;
    a->user_ctx   = user_ctx;
    strncpy(a->host, host, sizeof(a->host) - 1u);
    a->host[sizeof(a->host) - 1u] = '\0';
    strncpy(a->sni,  sni,  sizeof(a->sni)  - 1u);
    a->sni[sizeof(a->sni)  - 1u] = '\0';

    /* Re-init net context for new connection */
    mbedtls_net_init(&s->net);
    pthread_mutex_unlock(&s->mutex);

    pthread_t tid;
    int rc = pthread_create(&tid, NULL, connect_thread, a);
    if (rc != 0) {
        EMBEDIQ_HAL_OBS_EMIT_ERROR(EMBEDIQ_HAL_SRC_TLS, HAL_ERR_IO);
        pthread_mutex_lock(&s->mutex);
        s->connecting = false;
        pthread_mutex_unlock(&s->mutex);
        return EMBEDIQ_TLS_ERR_NO_RESOURCES;
    }
    pthread_detach(tid);
    return EMBEDIQ_TLS_OK;
}

static embediq_tls_err_t slot_disconnect(uint8_t idx)
{
    tls_slot_t *s = &g_slots[idx];
    pthread_mutex_lock(&s->mutex);

    if (s->connecting) {
        pthread_mutex_unlock(&s->mutex);
        return EMBEDIQ_TLS_ERR_BUSY;
    }
    if (s->connected) {
        mbedtls_ssl_close_notify(&s->ssl);  /* best-effort — ignore error */
        mbedtls_ssl_free(&s->ssl);
        mbedtls_net_free(&s->net);
        s->connected = false;
        LIVE_SESSION_ADD(-1);
    }

    pthread_mutex_unlock(&s->mutex);
    return EMBEDIQ_TLS_OK;
}

static embediq_tls_err_t slot_send(uint8_t idx, const uint8_t *data, uint16_t len)
{
    tls_slot_t *s = &g_slots[idx];
    pthread_mutex_lock(&s->mutex);

    if (s->connecting) {
        pthread_mutex_unlock(&s->mutex);
        return EMBEDIQ_TLS_ERR_BUSY;
    }
    if (!s->connected) {
        EMBEDIQ_HAL_OBS_EMIT_ERROR(EMBEDIQ_HAL_SRC_TLS, HAL_ERR_INVALID);
        pthread_mutex_unlock(&s->mutex);
        return EMBEDIQ_TLS_ERR_SEND;
    }

    size_t sent  = 0u;
    size_t total = (size_t)len;
    int    rc;

    while (sent < total) {
        rc = mbedtls_ssl_write(&s->ssl, data + sent, total - sent);
        if (rc > 0) {
            sent += (size_t)rc;
        } else if (rc != MBEDTLS_ERR_SSL_WANT_WRITE) {
            EMBEDIQ_HAL_OBS_EMIT_ERROR(EMBEDIQ_HAL_SRC_TLS, HAL_ERR_IO);
            pthread_mutex_unlock(&s->mutex);
            return EMBEDIQ_TLS_ERR_SEND;
        }
    }

    pthread_mutex_unlock(&s->mutex);
    return EMBEDIQ_TLS_OK;
}

static int32_t slot_recv(uint8_t idx, uint8_t *buf, uint16_t len, uint32_t timeout_ms)
{
    tls_slot_t *s = &g_slots[idx];
    pthread_mutex_lock(&s->mutex);

    if (s->connecting) {
        pthread_mutex_unlock(&s->mutex);
        return (int32_t)EMBEDIQ_TLS_ERR_BUSY;
    }
    if (!s->connected) {
        EMBEDIQ_HAL_OBS_EMIT_ERROR(EMBEDIQ_HAL_SRC_TLS, HAL_ERR_INVALID);
        pthread_mutex_unlock(&s->mutex);
        return (int32_t)EMBEDIQ_TLS_ERR_RECV;
    }

    /* Wait for data with timeout using select() on the socket fd */
    fd_set         rfds;
    struct timeval tv;
    FD_ZERO(&rfds);
    FD_SET(s->net.fd, &rfds);
    tv.tv_sec  = (long)(timeout_ms / 1000u);
    tv.tv_usec = (long)((timeout_ms % 1000u) * 1000u);

    int ready = select(s->net.fd + 1, &rfds, NULL, NULL, &tv);
    if (ready == 0) {
        pthread_mutex_unlock(&s->mutex);
        return 0;   /* timeout — no data */
    }
    if (ready < 0) {
        EMBEDIQ_HAL_OBS_EMIT_ERROR(EMBEDIQ_HAL_SRC_TLS, HAL_ERR_IO);
        pthread_mutex_unlock(&s->mutex);
        return (int32_t)EMBEDIQ_TLS_ERR_RECV;
    }

    int rc = mbedtls_ssl_read(&s->ssl, buf, (size_t)len);
    if (rc > 0) {
        pthread_mutex_unlock(&s->mutex);
        return (int32_t)rc;
    }
    if (rc == 0 || rc == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
        s->connected = false;
        pthread_mutex_unlock(&s->mutex);
        return (int32_t)EMBEDIQ_TLS_ERR_RECV;
    }
    EMBEDIQ_HAL_OBS_EMIT_ERROR(EMBEDIQ_HAL_SRC_TLS, HAL_ERR_IO);
    pthread_mutex_unlock(&s->mutex);
    return (int32_t)EMBEDIQ_TLS_ERR_RECV;
}

/* ---------------------------------------------------------------------------
 * Slot 0 trampolines
 * Each slot needs its own static wrapper functions because ops table
 * function pointers carry no context parameter.
 * ------------------------------------------------------------------------- */

static embediq_tls_err_t s0_configure(const char *ca, const char *cc, const char *ck)
    { return slot_configure(0u, ca, cc, ck); }
static embediq_tls_err_t s0_connect_async(const char *h, uint16_t p, const char *sni,
    embediq_tls_connect_cb_t cb, void *ctx)
    { return slot_connect_async(0u, h, p, sni, cb, ctx); }
static embediq_tls_err_t s0_disconnect(void)        { return slot_disconnect(0u); }
static embediq_tls_err_t s0_send(const uint8_t *d, uint16_t l) { return slot_send(0u, d, l); }
static int32_t           s0_recv(uint8_t *b, uint16_t l, uint32_t t) { return slot_recv(0u, b, l, t); }

static const embediq_tls_ops_t g_slot0_ops = {
    .version       = EMBEDIQ_TLS_OPS_VERSION,
    .configure     = s0_configure,
    .connect_async = s0_connect_async,
    .disconnect    = s0_disconnect,
    .send          = s0_send,
    .recv          = s0_recv,
};

/* ---------------------------------------------------------------------------
 * Slot 1 trampolines (compiled only if EMBEDIQ_TLS_MAX_CONNECTIONS >= 2)
 * ------------------------------------------------------------------------- */

#if EMBEDIQ_TLS_MAX_CONNECTIONS >= 2

static embediq_tls_err_t s1_configure(const char *ca, const char *cc, const char *ck)
    { return slot_configure(1u, ca, cc, ck); }
static embediq_tls_err_t s1_connect_async(const char *h, uint16_t p, const char *sni,
    embediq_tls_connect_cb_t cb, void *ctx)
    { return slot_connect_async(1u, h, p, sni, cb, ctx); }
static embediq_tls_err_t s1_disconnect(void)        { return slot_disconnect(1u); }
static embediq_tls_err_t s1_send(const uint8_t *d, uint16_t l) { return slot_send(1u, d, l); }
static int32_t           s1_recv(uint8_t *b, uint16_t l, uint32_t t) { return slot_recv(1u, b, l, t); }

static const embediq_tls_ops_t g_slot1_ops = {
    .version       = EMBEDIQ_TLS_OPS_VERSION,
    .configure     = s1_configure,
    .connect_async = s1_connect_async,
    .disconnect    = s1_disconnect,
    .send          = s1_send,
    .recv          = s1_recv,
};

#endif /* EMBEDIQ_TLS_MAX_CONNECTIONS >= 2 */

/* ---------------------------------------------------------------------------
 * Static pool initialisation
 * Called lazily on first hal_tls_posix_ops_slot() access.
 * ------------------------------------------------------------------------- */

static bool g_pool_initialised = false;

static void pool_init(void)
{
    if (g_pool_initialised) { return; }

    for (uint8_t i = 0u; i < (uint8_t)EMBEDIQ_TLS_MAX_CONNECTIONS; i++) {
        pthread_mutex_init(&g_slots[i].mutex, NULL);
        mbedtls_ssl_init(&g_slots[i].ssl);
        mbedtls_ssl_config_init(&g_slots[i].conf);
        mbedtls_x509_crt_init(&g_slots[i].ca_cert);
        mbedtls_x509_crt_init(&g_slots[i].client_cert);
        mbedtls_pk_init(&g_slots[i].client_key);
        mbedtls_ctr_drbg_init(&g_slots[i].ctr_drbg);
        mbedtls_entropy_init(&g_slots[i].entropy);
        mbedtls_net_init(&g_slots[i].net);
        g_slots[i].configured      = false;
        g_slots[i].connected       = false;
        g_slots[i].has_client_cert = false;
        g_slots[i].connecting      = false;
    }
    g_pool_initialised = true;
}

/* ---------------------------------------------------------------------------
 * Public accessors
 * ------------------------------------------------------------------------- */

const embediq_tls_ops_t *hal_tls_posix_ops_slot(uint8_t idx)
{
    pool_init();

    switch (idx) {
        case 0u: return &g_slot0_ops;
#if EMBEDIQ_TLS_MAX_CONNECTIONS >= 2
        case 1u: return &g_slot1_ops;
#endif
        default:
            EMBEDIQ_HAL_OBS_EMIT_ERROR(EMBEDIQ_HAL_SRC_TLS, HAL_ERR_INVALID);
            return NULL;
    }
}

const embediq_tls_ops_t *hal_tls_posix_ops(void)
{
    return hal_tls_posix_ops_slot(0u);
}

#ifdef EMBEDIQ_PLATFORM_HOST
/** Install (fn) or remove (NULL) the hook connect_finish calls between
 *  releasing the slot and invoking on_complete (test use only). */
void hal_tls__set_connect_finish_hook(void (*fn)(void *user_ctx))
{
    atomic_store(&s_connect_finish_hook, fn);
}

/** Number of sessions initialised and not yet released (test use only). */
int hal_tls__live_sessions(void)
{
    return atomic_load(&s_live_sessions);
}
#endif
