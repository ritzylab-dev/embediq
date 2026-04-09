/*
 * tests/lib/test_lib_tls_stub.c — Compile-and-contract test for TLS ops stubs
 *
 * Verifies that the POSIX TLS ops table stub:
 *   (a) has version == EMBEDIQ_TLS_OPS_VERSION
 *   (b) has all function pointers non-NULL
 *   (c) connect_async stub returns EMBEDIQ_ERR (not implemented)
 *   (d) disconnect, send, recv stubs return EMBEDIQ_ERR
 *
 * This test deliberately links against embediq_hal_posix, which provides
 * hal_tls_posix_ops(). The test FAILS TO LINK before hal_tls_posix.c exists —
 * that is the intentional TDD red phase.
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include "ops/embediq_tls.h"

/* Forward-declare the accessor — avoids including hal/posix/ impl headers
 * in test code, which would violate the layer boundary rule. */
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

static void test_tls_stub_version(void)
{
    const embediq_tls_ops_t *ops = hal_tls_posix_ops();
    ASSERT(ops != NULL, "hal_tls_posix_ops() must not return NULL");
    ASSERT(ops->version == EMBEDIQ_TLS_OPS_VERSION,
           "stub version must equal EMBEDIQ_TLS_OPS_VERSION");
}

static void test_tls_stub_fn_pointers_non_null(void)
{
    const embediq_tls_ops_t *ops = hal_tls_posix_ops();
    ASSERT(ops->connect_async != NULL, "connect_async must be non-NULL");
    ASSERT(ops->disconnect    != NULL, "disconnect must be non-NULL");
    ASSERT(ops->send          != NULL, "send must be non-NULL");
    ASSERT(ops->recv          != NULL, "recv must be non-NULL");
}

static void test_tls_stub_connect_returns_err(void)
{
    const embediq_tls_ops_t *ops = hal_tls_posix_ops();
    embediq_err_t rc = ops->connect_async("example.com", 8883u, "example.com",
                                           NULL, NULL);
    ASSERT(rc != EMBEDIQ_OK, "stub connect_async must not return EMBEDIQ_OK");
}

static void test_tls_stub_disconnect_returns_err(void)
{
    const embediq_tls_ops_t *ops = hal_tls_posix_ops();
    embediq_err_t rc = ops->disconnect();
    ASSERT(rc != EMBEDIQ_OK, "stub disconnect must not return EMBEDIQ_OK");
}

static void test_tls_stub_send_returns_err(void)
{
    const embediq_tls_ops_t *ops = hal_tls_posix_ops();
    uint8_t buf[4] = {0};
    embediq_err_t rc = ops->send(buf, sizeof(buf));
    ASSERT(rc != EMBEDIQ_OK, "stub send must not return EMBEDIQ_OK");
}

static void test_tls_stub_recv_returns_err(void)
{
    const embediq_tls_ops_t *ops = hal_tls_posix_ops();
    uint8_t buf[4] = {0};
    int32_t rc = ops->recv(buf, sizeof(buf), 0u);
    ASSERT(rc < 0, "stub recv must return negative on unimplemented path");
}

int main(void)
{
    test_tls_stub_version();
    test_tls_stub_fn_pointers_non_null();
    test_tls_stub_connect_returns_err();
    test_tls_stub_disconnect_returns_err();
    test_tls_stub_send_returns_err();
    test_tls_stub_recv_returns_err();

    printf("\nAll %d tests passed. (%d failed)\n",
           g_tests_run - g_tests_failed, g_tests_failed);
    return g_tests_failed > 0 ? 1 : 0;
}
