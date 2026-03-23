#define _POSIX_C_SOURCE 200809L

/*
 * tests/unit/test_fb_nvm.c — Unit tests for fb_nvm + HAL flash
 *
 * Tests the host file-backed NVM: set/get round-trip, atomic write pattern,
 * delete, reset_defaults, schema version persistence, and missing-key error.
 * Also tests the HAL flash contract (hal_flash.h) directly.
 *
 * All tests use EMBEDIQ_NVM_PATH env var to point to /tmp — never touches
 * the user's ~/.embediq/ directory.
 *
 * Run:      ./build/tests/unit/test_fb_nvm
 * Expected: "All 13 tests passed. (0 failed)"
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "embediq_fb.h"
#include "embediq_osal.h"
#include "embediq_config.h"
#include "hal/hal_flash.h"

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>     /* setenv */

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

extern void        nvm__init_state(void);

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

#define TEST_NVM_PATH  "/tmp/test_nvm_store.bin"
#define TEST_HAL_PATH  "/tmp/test_hal_flash.bin"

static void setup(void)
{
    setenv("EMBEDIQ_NVM_PATH", TEST_NVM_PATH, 1);
    nvm__init_state();
}

static void hal_setup(void)
{
    setenv("EMBEDIQ_NVM_PATH", TEST_HAL_PATH, 1);
    remove(TEST_HAL_PATH);
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
 * After a successful set: the backing file must exist and no .tmp file
 * must remain (it was atomically renamed).
 */
static void test_nvm_set_atomic_write_creates_tmp_then_renames(void)
{
    setup();

    const uint8_t val[] = {0x01, 0x02};
    embediq_nvm_set("atomic_key", val, 2u, 1u);

    /* .tmp file must NOT be present (it was renamed). */
    FILE *f_tmp = fopen(TEST_NVM_PATH ".tmp", "rb");
    ASSERT(f_tmp == NULL,
           "no leftover .tmp file — atomic rename completed successfully");
    if (f_tmp) fclose(f_tmp);

    /* backing file must exist. */
    FILE *f_bin = fopen(TEST_NVM_PATH, "rb");
    ASSERT(f_bin != NULL,
           "nvm_store.bin exists after atomic write");
    if (f_bin) fclose(f_bin);
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
 * HAL flash tests
 * ------------------------------------------------------------------------- */

/* test_hal_flash_write_read_roundtrip
 * Write 16 bytes at addr 0, read them back, verify match.
 */
static void test_hal_flash_write_read_roundtrip(void)
{
    hal_setup();

    const uint8_t data[16] = {
        0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02, 0x03, 0x04,
        0x10, 0x20, 0x30, 0x40, 0xAA, 0xBB, 0xCC, 0xDD
    };
    int rc_w = hal_flash_write(0u, data, sizeof(data));
    ASSERT(rc_w == HAL_OK, "hal_flash_write returns HAL_OK");

    uint8_t buf[16];
    memset(buf, 0, sizeof(buf));
    int rc_r = hal_flash_read(0u, buf, sizeof(buf));
    ASSERT(rc_r == HAL_OK && memcmp(buf, data, sizeof(data)) == 0,
           "hal_flash_read returns written data");
}

/* test_hal_flash_read_uninitialised_returns_ok
 * Fresh flash read before any write returns HAL_OK (erased/zero).
 */
static void test_hal_flash_read_uninitialised_returns_ok(void)
{
    hal_setup();

    uint8_t buf[16];
    memset(buf, 0xAA, sizeof(buf));   /* fill with non-zero sentinel */
    int rc = hal_flash_read(0u, buf, sizeof(buf));

    /* File doesn't exist — HAL should return OK with zeroed buffer. */
    uint8_t zeros[16];
    memset(zeros, 0, sizeof(zeros));
    ASSERT(rc == HAL_OK && memcmp(buf, zeros, sizeof(zeros)) == 0,
           "uninitialised read returns HAL_OK with zeroed buffer");
}

/* test_hal_flash_erase_zeros_region
 * Write bytes, erase, read back — erased region must be 0xFF.
 */
static void test_hal_flash_erase_zeros_region(void)
{
    hal_setup();

    const uint8_t data[8] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
    hal_flash_write(0u, data, sizeof(data));

    int rc_e = hal_flash_erase(0u, sizeof(data));
    ASSERT(rc_e == HAL_OK, "hal_flash_erase returns HAL_OK");

    uint8_t buf[8];
    hal_flash_read(0u, buf, sizeof(buf));

    uint8_t ff[8];
    memset(ff, 0xFF, sizeof(ff));
    ASSERT(memcmp(buf, ff, sizeof(ff)) == 0,
           "erased region reads back as 0xFF");
}

/* test_hal_flash_page_size_nonzero
 * hal_flash_page_size() must return a value > 0.
 */
static void test_hal_flash_page_size_nonzero(void)
{
    uint32_t ps = hal_flash_page_size();
    ASSERT(ps > 0u, "hal_flash_page_size returns non-zero");
}

/* ---------------------------------------------------------------------------
 * main
 * ------------------------------------------------------------------------- */

int main(void)
{
    /* NVM Driver FB tests */
    test_nvm_set_and_get_roundtrip();
    test_nvm_set_atomic_write_creates_tmp_then_renames();
    test_nvm_delete_removes_key();
    test_nvm_reset_defaults_clears_all();
    test_nvm_schema_version_stored_with_key();
    test_nvm_get_missing_key_returns_error();

    /* HAL flash tests */
    test_hal_flash_write_read_roundtrip();
    test_hal_flash_read_uninitialised_returns_ok();
    test_hal_flash_erase_zeros_region();
    test_hal_flash_page_size_nonzero();

    printf("\n");
    if (g_tests_failed == 0) {
        printf("All %d tests passed. (0 failed)\n", g_tests_run);
    } else {
        printf("%d of %d tests FAILED.\n", g_tests_failed, g_tests_run);
    }
    return (g_tests_failed > 0) ? 1 : 0;
}
