#define _POSIX_C_SOURCE 200809L

/*
 * tests/unit/test_fb_nvm.c — Unit tests for fb_nvm
 *
 * Tests the host file-backed NVM: set/get round-trip, atomic write pattern,
 * delete, reset_defaults, schema version persistence, and missing-key error.
 *
 * All tests redirect the NVM file to /tmp/test_nvm_store.json to avoid
 * touching the user's ~/.embediq/ directory.
 *
 * Run:      ./build/tests/unit/test_fb_nvm
 * Expected: "All 9 tests passed. (0 failed)"
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "embediq_fb.h"
#include "embediq_osal.h"
#include "embediq_config.h"

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* ---------------------------------------------------------------------------
 * Public NVM API
 * ------------------------------------------------------------------------- */

extern int  embediq_nvm_get(const char *key, uint8_t *buf, uint16_t buf_len,
                            uint16_t *len_out, uint16_t *schema_out);
extern int  embediq_nvm_set(const char *key, const uint8_t *val, uint16_t len,
                            uint16_t schema_id);
extern int  embediq_nvm_delete(const char *key);
extern void embediq_nvm_reset_defaults(void);
extern int  embediq_nvm_get_schema_version(const char *key, uint16_t *schema_out);

/* ---------------------------------------------------------------------------
 * Package-internal NVM test API
 * ------------------------------------------------------------------------- */

extern void        nvm__set_path(const char *path);
extern void        nvm__init_state(void);
extern const char *nvm__get_path(void);
extern const char *nvm__get_tmp_path(void);

/* ---------------------------------------------------------------------------
 * Minimal test harness
 * ------------------------------------------------------------------------- */

static int g_tests_run    = 0;
static int g_tests_failed = 0;

#define ASSERT(cond, msg)  do {                                                \
    g_tests_run++;                                                             \
    if (!(cond)) {                                                             \
        fprintf(stderr, "FAIL  %-56s  %s:%d\n", __func__, __FILE__, __LINE__);\
        g_tests_failed++;                                                      \
    } else {                                                                   \
        printf("PASS  %s\n", __func__);                                        \
    }                                                                          \
} while (0)

/* ---------------------------------------------------------------------------
 * Test path (in /tmp — never touches ~/.embediq)
 * ------------------------------------------------------------------------- */

#define TEST_NVM_PATH  "/tmp/test_nvm_store.json"

static void setup(void)
{
    nvm__set_path(TEST_NVM_PATH);
    nvm__init_state();
}

/* ---------------------------------------------------------------------------
 * Tests
 * ------------------------------------------------------------------------- */

/* test_nvm_set_and_get_roundtrip
 * A set immediately followed by a get must return the same bytes and schema.
 */
static void test_nvm_set_and_get_roundtrip(void)
{
    setup();

    const uint8_t expected[] = {0xDE, 0xAD, 0xBE};
    int rc = embediq_nvm_set("firmware_ver", expected, 3u, 42u);
    ASSERT(rc == 0, "set succeeds");

    uint8_t  buf[EMBEDIQ_MSG_MAX_PAYLOAD];
    uint16_t len    = 0u;
    uint16_t schema = 0u;
    rc = embediq_nvm_get("firmware_ver", buf, sizeof(buf), &len, &schema);

    ASSERT(rc == 0 && len == 3u && schema == 42u &&
           memcmp(buf, expected, 3u) == 0,
           "get returns the value stored by set");
}

/* test_nvm_set_atomic_write_creates_tmp_then_renames
 * After a successful set: the .json file must exist and no .tmp file
 * must remain (it was atomically renamed over the .json).
 */
static void test_nvm_set_atomic_write_creates_tmp_then_renames(void)
{
    setup();

    const uint8_t val[] = {0x01, 0x02};
    embediq_nvm_set("atomic_key", val, 2u, 1u);

    /* .tmp file must NOT be present (it was renamed). */
    FILE *f_tmp = fopen(nvm__get_tmp_path(), "rb");
    ASSERT(f_tmp == NULL,
           "no leftover .tmp file — atomic rename completed successfully");
    if (f_tmp) fclose(f_tmp);

    /* .json file must exist. */
    FILE *f_json = fopen(nvm__get_path(), "rb");
    ASSERT(f_json != NULL,
           "nvm_store.json exists after atomic write");
    if (f_json) fclose(f_json);
}

/* test_nvm_delete_removes_key
 * A deleted key must no longer be findable with get.
 */
static void test_nvm_delete_removes_key(void)
{
    setup();

    const uint8_t val[] = {0x55};
    embediq_nvm_set("to_delete", val, 1u, 0u);

    int rc_del = embediq_nvm_delete("to_delete");
    ASSERT(rc_del == 0, "delete returns 0 for existing key");

    uint8_t buf[EMBEDIQ_MSG_MAX_PAYLOAD];
    int rc_get = embediq_nvm_get("to_delete", buf, sizeof(buf), NULL, NULL);
    ASSERT(rc_get == -1,
           "get returns -1 for a deleted key");
}

/* test_nvm_reset_defaults_clears_all
 * After reset_defaults, all previously stored keys must be inaccessible.
 */
static void test_nvm_reset_defaults_clears_all(void)
{
    setup();

    const uint8_t v1[] = {0xAA};
    const uint8_t v2[] = {0xBB};
    embediq_nvm_set("k1", v1, 1u, 0u);
    embediq_nvm_set("k2", v2, 1u, 0u);

    embediq_nvm_reset_defaults();

    uint8_t buf[EMBEDIQ_MSG_MAX_PAYLOAD];
    int rc1 = embediq_nvm_get("k1", buf, sizeof(buf), NULL, NULL);
    int rc2 = embediq_nvm_get("k2", buf, sizeof(buf), NULL, NULL);

    ASSERT(rc1 == -1 && rc2 == -1,
           "reset_defaults wipes all keys from the store");
}

/* test_nvm_schema_version_stored_with_key
 * The schema version stored with nvm_set must be recoverable via
 * nvm_get_schema_version without retrieving the full value.
 */
static void test_nvm_schema_version_stored_with_key(void)
{
    setup();

    const uint8_t val[] = {0xFF};
    embediq_nvm_set("schema_test", val, 1u, 99u);

    uint16_t schema = 0u;
    int rc = embediq_nvm_get_schema_version("schema_test", &schema);

    ASSERT(rc == 0 && schema == 99u,
           "schema version retrieved matches the value passed to nvm_set");
}

/* test_nvm_get_missing_key_returns_error
 * Getting a key that was never stored must return -1.
 */
static void test_nvm_get_missing_key_returns_error(void)
{
    setup();

    uint8_t  buf[EMBEDIQ_MSG_MAX_PAYLOAD];
    uint16_t len    = 0u;
    uint16_t schema = 0u;
    int rc = embediq_nvm_get("does_not_exist", buf, sizeof(buf), &len, &schema);

    ASSERT(rc == -1,
           "get returns -1 for a key that has never been stored");
}

/* ---------------------------------------------------------------------------
 * main
 * ------------------------------------------------------------------------- */

int main(void)
{
    test_nvm_set_and_get_roundtrip();
    test_nvm_set_atomic_write_creates_tmp_then_renames();
    test_nvm_delete_removes_key();
    test_nvm_reset_defaults_clears_all();
    test_nvm_schema_version_stored_with_key();
    test_nvm_get_missing_key_returns_error();

    printf("\n");
    if (g_tests_failed == 0) {
        printf("All %d tests passed. (0 failed)\n", g_tests_run);
    } else {
        printf("%d of %d tests FAILED.\n", g_tests_failed, g_tests_run);
    }
    return (g_tests_failed > 0) ? 1 : 0;
}
