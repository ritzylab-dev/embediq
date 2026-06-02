/*
 * tests/lib/test_lib_tls_stub.c — Contract and stub tests for TLS ops (v2)
 *
 * Verifies that the POSIX TLS ops table stubs (contract v2):
 *   (a) version == EMBEDIQ_TLS_OPS_VERSION (2)
 *   (b) all function pointers non-NULL
 *   (c) configure() stub returns EMBEDIQ_TLS_ERR_NOT_CONFIGURED
 *   (d) connect_async() stub returns EMBEDIQ_TLS_ERR_NOT_CONFIGURED
 *   (e) disconnect(), send(), recv() stubs return error codes
 *   (f) hal_tls_posix_ops_slot(0) returns non-NULL
 *   (g) hal_tls_posix_ops_slot(EMBEDIQ_TLS_MAX_CONNECTIONS) returns NULL
 *   (h) slot 0 and slot 1 are independent (different pointers)
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdint.h>
#include "ops/embediq_tls.h"
#include "embediq_config.h"

extern const embediq_tls_ops_t *hal_tls_posix_ops_slot(uint8_t idx);
extern const embediq_tls_ops_t *hal_tls_posix_ops(void);

static int g_tests_run    = 0;
static int g_tests_failed = 0;

#define ASSERT(cond, msg)  do {                                               \
    g_tests_run++;                                                            \
    if (!(cond)) {                                                            \
        fprintf(stderr, "FAIL  %-56s  %s:%d  — %s\n",                       \
                __func__, __FILE__, __LINE__, (msg));                         \
        g_tests_failed++;                                                     \
    } else {                                                                  \
        printf("PASS  %s\n", __func__);                                       \
    }                                                                         \
} while (0)

static void test_version(void)
{
    const embediq_tls_ops_t *ops = hal_tls_posix_ops();
    ASSERT(ops != NULL, "hal_tls_posix_ops() must not return NULL");
    ASSERT(ops->version == EMBEDIQ_TLS_OPS_VERSION,
           "version must equal EMBEDIQ_TLS_OPS_VERSION (2)");
}

static void test_fn_pointers_non_null(void)
{
    const embediq_tls_ops_t *ops = hal_tls_posix_ops();
    ASSERT(ops->configure     != NULL, "configure must be non-NULL");
    ASSERT(ops->connect_async != NULL, "connect_async must be non-NULL");
    ASSERT(ops->disconnect    != NULL, "disconnect must be non-NULL");
    ASSERT(ops->send          != NULL, "send must be non-NULL");
    ASSERT(ops->recv          != NULL, "recv must be non-NULL");
}

static void test_configure_stub(void)
{
    const embediq_tls_ops_t *ops = hal_tls_posix_ops();
    embediq_tls_err_t rc = ops->configure("fake-ca", NULL, NULL);
    ASSERT(rc != EMBEDIQ_TLS_OK, "stub configure must not return EMBEDIQ_TLS_OK");
}

static void test_connect_returns_err(void)
{
    const embediq_tls_ops_t *ops = hal_tls_posix_ops();
    embediq_tls_err_t rc = ops->connect_async("example.com", 8883u,
                                               "example.com", NULL, NULL);
    ASSERT(rc != EMBEDIQ_TLS_OK, "stub connect_async must not return EMBEDIQ_TLS_OK");
}

static void test_disconnect_returns_err(void)
{
    const embediq_tls_ops_t *ops = hal_tls_posix_ops();
    embediq_tls_err_t rc = ops->disconnect();
    ASSERT(rc != EMBEDIQ_TLS_OK, "stub disconnect must not return EMBEDIQ_TLS_OK");
}

static void test_send_returns_err(void)
{
    const embediq_tls_ops_t *ops = hal_tls_posix_ops();
    uint8_t buf[4] = {0};
    embediq_tls_err_t rc = ops->send(buf, sizeof(buf));
    ASSERT(rc != EMBEDIQ_TLS_OK, "stub send must not return EMBEDIQ_TLS_OK");
}

static void test_recv_returns_err(void)
{
    const embediq_tls_ops_t *ops = hal_tls_posix_ops();
    uint8_t buf[4] = {0};
    int32_t rc = ops->recv(buf, sizeof(buf), 0u);
    ASSERT(rc < 0, "stub recv must return negative on unimplemented path");
}

static void test_slot0_non_null(void)
{
    const embediq_tls_ops_t *ops = hal_tls_posix_ops_slot(0u);
    ASSERT(ops != NULL, "slot 0 must return non-NULL");
}

static void test_slot_out_of_range_null(void)
{
    const embediq_tls_ops_t *ops = hal_tls_posix_ops_slot(EMBEDIQ_TLS_MAX_CONNECTIONS);
    ASSERT(ops == NULL, "out-of-range slot must return NULL");
}

#if EMBEDIQ_TLS_MAX_CONNECTIONS >= 2
static void test_slot0_slot1_independent(void)
{
    const embediq_tls_ops_t *s0 = hal_tls_posix_ops_slot(0u);
    const embediq_tls_ops_t *s1 = hal_tls_posix_ops_slot(1u);
    ASSERT(s0 != NULL, "slot 0 must be non-NULL");
    ASSERT(s1 != NULL, "slot 1 must be non-NULL");
    ASSERT(s0 != s1,   "slot 0 and slot 1 must be independent ops tables");
}
#endif

int main(void)
{
    test_version();
    test_fn_pointers_non_null();
    test_configure_stub();
    test_connect_returns_err();
    test_disconnect_returns_err();
    test_send_returns_err();
    test_recv_returns_err();
    test_slot0_non_null();
    test_slot_out_of_range_null();
#if EMBEDIQ_TLS_MAX_CONNECTIONS >= 2
    test_slot0_slot1_independent();
#endif

    printf("\n%d/%d tests passed.\n",
           g_tests_run - g_tests_failed, g_tests_run);
    return g_tests_failed > 0 ? 1 : 0;
}
