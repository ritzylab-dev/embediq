/*
 * tests/unit/test_embediq_cfg.c — Unit tests for embediq_cfg
 *
 * Verifies the typed configuration helper: default-on-absent, typed
 * round-trip for float/i32/u32/bool/str, delete, and boundary-length
 * key handling. Each test calls nvm__init_state() first to start from
 * a clean in-memory cache.
 *
 * Run:      ./build/tests/unit/test_embediq_cfg
 * Expected: "All N tests passed. (0 failed)"
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "embediq_cfg.h"
#include "embediq_nvm.h"   /* for EMBEDIQ_NVM_KEY_MAX */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <math.h>

/* Package-internal NVM test helper (EMBEDIQ_PLATFORM_HOST only). */
extern void nvm__init_state(void);

/* ---------------------------------------------------------------------------
 * Minimal test harness
 * ------------------------------------------------------------------------- */

static int g_tests_run    = 0;
static int g_tests_failed = 0;

#define ASSERT(cond, msg)  do {                                                 \
    g_tests_run++;                                                              \
    if (!(cond)) {                                                              \
        fprintf(stderr, "FAIL  %-56s  %s:%d  %s\n",                             \
                __func__, __FILE__, __LINE__, (msg));                           \
        g_tests_failed++;                                                       \
    } else {                                                                    \
        printf("PASS  %s  %s\n", __func__, (msg));                              \
    }                                                                           \
} while (0)

#define APPROX_EQ(a, b, eps)  (fabsf((a) - (b)) <= (eps))

/* ---------------------------------------------------------------------------
 * TEST 1 — get_float returns default when key absent
 * ------------------------------------------------------------------------- */
static void test_absent_key_returns_default(void)
{
    nvm__init_state();
    float v = embediq_cfg_get_float("test.absent_key", 42.0f);
    ASSERT(APPROX_EQ(v, 42.0f, 0.0001f),
           "absent key returns the supplied default");
}

/* ---------------------------------------------------------------------------
 * TEST 2 — set_float + get_float round-trip
 * ------------------------------------------------------------------------- */
static void test_float_round_trip(void)
{
    nvm__init_state();
    ASSERT(embediq_cfg_set_float("test.kp", 1.5f) == true,
           "set_float returns true on success");
    float v = embediq_cfg_get_float("test.kp", 0.0f);
    ASSERT(APPROX_EQ(v, 1.5f, 0.0001f),
           "get_float returns the stored value, not the default");
}

/* ---------------------------------------------------------------------------
 * TEST 3 — get_i32 / set_i32 round-trip (signed negative value)
 * ------------------------------------------------------------------------- */
static void test_i32_round_trip(void)
{
    nvm__init_state();
    ASSERT(embediq_cfg_set_i32("test.rpm", -1000) == true,
           "set_i32 returns true on success");
    int32_t v = embediq_cfg_get_i32("test.rpm", 0);
    ASSERT(v == -1000, "get_i32 round-trips a negative value");
}

/* ---------------------------------------------------------------------------
 * TEST 4 — get_str with truncation into a short buffer
 * ------------------------------------------------------------------------- */
static void test_str_truncation(void)
{
    nvm__init_state();
    ASSERT(embediq_cfg_set_str("test.host", "localhost") == true,
           "set_str returns true on success");

    char buf[6];
    (void)embediq_cfg_get_str("test.host", buf, sizeof(buf), "");
    /* Expected: first 5 chars of "localhost" + NUL. */
    ASSERT(strcmp(buf, "local") == 0,
           "get_str truncates to buf_len-1 chars and NUL-terminates");
    ASSERT(buf[5] == '\0', "terminator present at buf[buf_len-1]");
}

/* ---------------------------------------------------------------------------
 * TEST 5 — get_bool round-trip both directions
 * ------------------------------------------------------------------------- */
static void test_bool_round_trip(void)
{
    nvm__init_state();

    ASSERT(embediq_cfg_set_bool("test.enabled", true) == true,
           "set_bool(true) returns true");
    ASSERT(embediq_cfg_get_bool("test.enabled", false) == true,
           "get_bool returns stored true");

    ASSERT(embediq_cfg_set_bool("test.enabled", false) == true,
           "set_bool(false) returns true");
    ASSERT(embediq_cfg_get_bool("test.enabled", true) == false,
           "get_bool returns stored false (default ignored)");
}

/* ---------------------------------------------------------------------------
 * TEST 6 — delete removes key; next get returns default
 * ------------------------------------------------------------------------- */
static void test_delete_returns_default(void)
{
    nvm__init_state();

    ASSERT(embediq_cfg_set_u32("test.port", 1883u) == true,
           "set_u32 succeeds");
    ASSERT(embediq_cfg_get_u32("test.port", 0u) == 1883u,
           "get_u32 returns stored value before delete");

    ASSERT(embediq_cfg_delete("test.port") == true,
           "delete returns true");

    ASSERT(embediq_cfg_get_u32("test.port", 9999u) == 9999u,
           "post-delete get returns default");
}

/* ---------------------------------------------------------------------------
 * TEST 7 — key at NVM_KEY_MAX boundary does not crash
 * ------------------------------------------------------------------------- */
static void test_boundary_key_length(void)
{
    nvm__init_state();

    /* Build a key of exactly EMBEDIQ_NVM_KEY_MAX-1 printable chars + NUL. */
    char k[EMBEDIQ_NVM_KEY_MAX];
    for (uint32_t i = 0u; i < (uint32_t)sizeof(k) - 1u; i++) {
        k[i] = 'a';
    }
    k[sizeof(k) - 1u] = '\0';

    bool set_ok = embediq_cfg_set_i32(k, 7);
    int32_t got = embediq_cfg_get_i32(k, 0);

    /* The spec allows either success or rejection, as long as there is
     * no crash. If set succeeded, the value round-trips; if not, the
     * default must come back. */
    if (set_ok) {
        ASSERT(got == 7,
               "boundary key set ok → get returns stored value");
    } else {
        ASSERT(got == 0,
               "boundary key set rejected → get returns default");
    }
    ASSERT(true, "boundary-length key did not crash");
}

/* ---------------------------------------------------------------------------
 * Entry point
 * ------------------------------------------------------------------------- */

int main(void)
{
    test_absent_key_returns_default();
    test_float_round_trip();
    test_i32_round_trip();
    test_str_truncation();
    test_bool_round_trip();
    test_delete_returns_default();
    test_boundary_key_length();

    printf("\n");
    if (g_tests_failed == 0) {
        printf("All %d tests passed. (0 failed)\n", g_tests_run);
    } else {
        printf("%d of %d tests FAILED.\n", g_tests_failed, g_tests_run);
    }
    return (g_tests_failed > 0) ? 1 : 0;
}
