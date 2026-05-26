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
 * Public NVM API (matches embediq_nvm.h contract)
 * ------------------------------------------------------------------------- */

#include "embediq_nvm.h"

/* ---------------------------------------------------------------------------
 * Package-internal NVM test API
 * ------------------------------------------------------------------------- */

extern void        nvm__init_state(void);
extern void        nvm__load_state(void);   /* triggers nvm_load() — test-only */

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
    embediq_err_t rc = embediq_nvm_set("firmware_ver", expected, 3u);
    ASSERT(rc == EMBEDIQ_OK, "set succeeds");

    uint8_t  buf[EMBEDIQ_MSG_MAX_PAYLOAD];
    uint32_t len = sizeof(buf);
    rc = embediq_nvm_get("firmware_ver", buf, &len);

    ASSERT(rc == EMBEDIQ_OK && len == 3u &&
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
    embediq_nvm_set("atomic_key", val, 2u);

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
    embediq_nvm_set("to_delete", val, 1u);

    embediq_err_t rc_del = embediq_nvm_delete("to_delete");
    ASSERT(rc_del == EMBEDIQ_OK, "delete returns OK for existing key");

    uint8_t  buf[EMBEDIQ_MSG_MAX_PAYLOAD];
    uint32_t len = sizeof(buf);
    embediq_err_t rc_get = embediq_nvm_get("to_delete", buf, &len);
    ASSERT(rc_get == EMBEDIQ_ERR,
           "get returns ERR for a deleted key");
}

/* test_nvm_reset_defaults_clears_all
 * After reset_defaults, all previously stored keys must be inaccessible.
 */
static void test_nvm_reset_clears_all(void)
{
    setup();

    const uint8_t v1[] = {0xAA};
    const uint8_t v2[] = {0xBB};
    embediq_nvm_set("k1", v1, 1u);
    embediq_nvm_set("k2", v2, 1u);

    embediq_nvm_reset();

    uint8_t  buf[EMBEDIQ_MSG_MAX_PAYLOAD];
    uint32_t len = sizeof(buf);
    embediq_err_t rc1 = embediq_nvm_get("k1", buf, &len);
    len = sizeof(buf);
    embediq_err_t rc2 = embediq_nvm_get("k2", buf, &len);

    ASSERT(rc1 == EMBEDIQ_ERR && rc2 == EMBEDIQ_ERR,
           "reset wipes all keys from the store");
}

/* test_nvm_flush_succeeds
 * Flush must return EMBEDIQ_OK after a set.
 */
static void test_nvm_flush_succeeds(void)
{
    setup();

    const uint8_t val[] = {0xFF};
    embediq_nvm_set("flush_test", val, 1u);

    embediq_err_t rc = embediq_nvm_flush();
    ASSERT(rc == EMBEDIQ_OK, "flush returns OK after a set");
}

/* test_nvm_get_missing_key_returns_error
 * Getting a key that was never stored must return EMBEDIQ_ERR.
 */
static void test_nvm_get_missing_key_returns_error(void)
{
    setup();

    uint8_t  buf[EMBEDIQ_MSG_MAX_PAYLOAD];
    uint32_t len = sizeof(buf);
    embediq_err_t rc = embediq_nvm_get("does_not_exist", buf, &len);

    ASSERT(rc == EMBEDIQ_ERR,
           "get returns ERR for a key that has never been stored");
}

/* ---------------------------------------------------------------------------
 * HAL flash tests
 * ------------------------------------------------------------------------- */

/* Test buffer sizes — derived from named config constants (I-08). */
#define TEST_HAL_WRITE_BYTES  EMBEDIQ_MAX_SUBFNS_PER_FB   /* 16 */
#define TEST_HAL_ERASE_BYTES  EMBEDIQ_MAX_BOOT_DEPS       /*  8 */

/* test_hal_flash_write_read_roundtrip
 * Write TEST_HAL_WRITE_BYTES bytes at addr 0, read them back, verify match.
 */
static void test_hal_flash_write_read_roundtrip(void)
{
    hal_setup();

    const uint8_t data[TEST_HAL_WRITE_BYTES] = {
        0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02, 0x03, 0x04,
        0x10, 0x20, 0x30, 0x40, 0xAA, 0xBB, 0xCC, 0xDD
    };
    int rc_w = hal_flash_write(0u, data, sizeof(data));
    ASSERT(rc_w == HAL_OK, "hal_flash_write returns HAL_OK");

    uint8_t buf[TEST_HAL_WRITE_BYTES];
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

    uint8_t buf[TEST_HAL_WRITE_BYTES];
    memset(buf, 0xAA, sizeof(buf));   /* fill with non-zero sentinel */
    int rc = hal_flash_read(0u, buf, sizeof(buf));

    /* File doesn't exist — HAL should return OK with zeroed buffer. */
    uint8_t zeros[TEST_HAL_WRITE_BYTES];
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

    const uint8_t data[TEST_HAL_ERASE_BYTES] = {
        0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88
    };
    hal_flash_write(0u, data, sizeof(data));

    int rc_e = hal_flash_erase(0u, sizeof(data));
    ASSERT(rc_e == HAL_OK, "hal_flash_erase returns HAL_OK");

    uint8_t buf[TEST_HAL_ERASE_BYTES];
    hal_flash_read(0u, buf, sizeof(buf));

    uint8_t ff[TEST_HAL_ERASE_BYTES];
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

/* -------------------------------------------------------------------------
 * Tests added by Item 4 PR-D — blob header, CRC validation, mutability
 * ----------------------------------------------------------------------- */

/* test_blob_file_starts_with_magic
 * After a persist, the backing file must begin with the 4-byte EIQ\0 magic.
 *
 * TDD: FAILS before implementation (file starts with raw g_nvm key bytes).
 *      PASSES after implementation (nvm_persist writes the blob header).
 */
static void test_blob_file_starts_with_magic(void)
{
    setup();

    const uint8_t val[] = {0x11};
    embediq_nvm_set("blob_hdr_check", val, 1u);

    FILE *f = fopen(TEST_NVM_PATH, "rb");
    ASSERT(f != NULL, "backing file exists for magic-header check");
    if (!f) return;
    uint8_t hdr[4] = {0};
    size_t  n = fread(hdr, 1u, sizeof(hdr), f);
    fclose(f);

    ASSERT(n == 4u
           && hdr[0] == 'E'
           && hdr[1] == 'I'
           && hdr[2] == 'Q'
           && hdr[3] == '\0',
           "blob file starts with EIQ\\0 magic — nvm_persist writes header");
}

/* test_blob_file_size_after_persist
 * After a persist, the backing file must be exactly 8592 bytes:
 *   16-byte nvm_blob_header_t + NVM_MAX_KEYS * sizeof(nvm_entry_t)
 *   = 16 + 64 * 134 = 16 + 8576 = 8592.
 *
 * TDD: FAILS before implementation (file is 8576 bytes, no header).
 *      PASSES after implementation.
 */
static void test_blob_file_size_after_persist(void)
{
    setup();

    const uint8_t val[] = {0x22};
    embediq_nvm_set("size_check", val, 1u);

    FILE *f = fopen(TEST_NVM_PATH, "rb");
    ASSERT(f != NULL, "backing file exists for size check");
    if (!f) return;
    fseek(f, 0L, SEEK_END);
    long sz = ftell(f);
    fclose(f);

    /* 16-byte header + 64 * 134-byte entries */
    ASSERT(sz == 8592L,
           "blob file is exactly 8592 bytes (16-byte header + 8576-byte payload)");
}

/* test_factory_key_set_rejected
 * embediq_nvm_set() on a factory-class key must return EMBEDIQ_ERR.
 * "device_id" is declared factory in config/config.iq.
 *
 * TDD: FAILS before implementation (no mutability check — set succeeds).
 *      PASSES after implementation.
 */
static void test_factory_key_set_rejected(void)
{
    setup();

    const uint8_t val[] = {0xAA, 0xBB, 0xCC};
    embediq_err_t rc = embediq_nvm_set("device_id", val, 3u);
    ASSERT(rc == EMBEDIQ_ERR,
           "embediq_nvm_set() on a factory-class key returns EMBEDIQ_ERR");
}

/* test_blob_crc_corrupt_causes_fallback
 * Corrupting a payload byte in the backing file must cause nvm_load() to
 * detect a CRC mismatch and fall back to an empty store.
 *
 * Corruption offset = 80:
 *   Pre-impl  file layout: raw g_nvm from offset 0.  Byte 80 =
 *     entry-0 val[16] (val_len=1 so unused — get still succeeds → FAIL).
 *   Post-impl file layout: 16-byte header + g_nvm from offset 16.
 *     Byte 80 = entry-0 val[0] (stored value) — CRC computed over this
 *     byte, so changing it invalidates the CRC → fallback → PASS.
 *
 * TDD: FAILS before implementation (key still found, corrupt byte unused).
 *      PASSES after implementation (CRC mismatch → fallback → key absent).
 */
static void test_blob_crc_corrupt_causes_fallback(void)
{
    setup();

    const uint8_t val[] = {0x42};   /* must NOT be 0xFF to ensure corruption changes the byte */
    embediq_nvm_set("crc_test", val, 1u);

    /* Corrupt one byte of the backing file at offset 80. */
    FILE *f = fopen(TEST_NVM_PATH, "r+b");
    ASSERT(f != NULL, "backing file exists for CRC corruption test");
    if (!f) return;
    fseek(f, 80L, SEEK_SET);
    uint8_t corrupt = 0xFFu;
    fwrite(&corrupt, 1u, 1u, f);
    fclose(f);

    /* Simulate restart — clear in-memory cache and reload from flash. */
    nvm__init_state();
    nvm__load_state();

    uint8_t  buf[EMBEDIQ_MSG_MAX_PAYLOAD];
    uint32_t len = sizeof(buf);
    embediq_err_t rc = embediq_nvm_get("crc_test", buf, &len);
    ASSERT(rc == EMBEDIQ_ERR,
           "CRC-corrupted blob causes fallback — key absent after reload");
}

/* test_blob_persist_and_reload_roundtrip
 * A value stored before a simulated restart must survive the reload cycle
 * (blob format is valid — magic + CRC both pass).
 *
 * Regression test: PASSES before and after implementation.
 * Included to ensure round-trip correctness is preserved after PR-D changes.
 */
static void test_blob_persist_and_reload_roundtrip(void)
{
    setup();

    const uint8_t val[] = {0xCA, 0xFE, 0xBA, 0xBE};
    embediq_nvm_set("reload_test", val, 4u);

    /* Simulate restart — clear in-memory cache and reload from flash. */
    nvm__init_state();
    nvm__load_state();

    uint8_t  buf[EMBEDIQ_MSG_MAX_PAYLOAD];
    uint32_t len = sizeof(buf);
    embediq_err_t rc = embediq_nvm_get("reload_test", buf, &len);

    ASSERT(rc == EMBEDIQ_OK && len == 4u && memcmp(buf, val, 4u) == 0,
           "value survives persist + reload cycle (blob round-trip correct)");
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
    test_nvm_reset_clears_all();
    test_nvm_flush_succeeds();
    test_nvm_get_missing_key_returns_error();

    /* HAL flash tests */
    test_hal_flash_write_read_roundtrip();
    test_hal_flash_read_uninitialised_returns_ok();
    test_hal_flash_erase_zeros_region();
    test_hal_flash_page_size_nonzero();

    /* PR-D: blob header, CRC, mutability tests */
    test_blob_file_starts_with_magic();
    test_blob_file_size_after_persist();
    test_factory_key_set_rejected();
    test_blob_crc_corrupt_causes_fallback();
    test_blob_persist_and_reload_roundtrip();

    printf("\n");
    if (g_tests_failed == 0) {
        printf("All %d tests passed. (0 failed)\n", g_tests_run);
    } else {
        printf("%d of %d tests FAILED.\n", g_tests_failed, g_tests_run);
    }
    return (g_tests_failed > 0) ? 1 : 0;
}
