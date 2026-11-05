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
 * Expected: "All 34 tests passed. (0 failed)"
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "embediq_fb.h"
#include "embediq_osal.h"
#include "embediq_config.h"
#include "embediq_obs.h"
#include "hal/hal_flash.h"

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>     /* setenv */
#include <pthread.h>
#include <unistd.h>     /* getpid, rmdir */
#include <sys/stat.h>   /* mkdir */
#include <dirent.h>     /* opendir — test directory removal */

/* ---------------------------------------------------------------------------
 * Public NVM API (matches embediq_nvm.h contract)
 * ------------------------------------------------------------------------- */

#include "embediq_nvm.h"

/* ---------------------------------------------------------------------------
 * Package-internal NVM test API
 * ------------------------------------------------------------------------- */

extern void        nvm__init_state(void);
extern void        nvm__load_state(void);   /* triggers nvm_load() — test-only */
extern void        nvm__set_persist_delay_ms(uint32_t ms);

/* Host-only flash failure injection (hal/posix/peripherals/hal_flash_posix.c). */
extern void        hal_flash__fail_next_write(uint32_t n);
extern void        hal_flash__fail_next_read(uint32_t n);
extern void        hal_flash__fail_next_dir_fsync(uint32_t n);
extern void        hal_flash__fail_next_parent_fsync(uint32_t n);
extern uint32_t    hal_flash__parent_syncs(void);
extern bool        hal_flash__dir_entry_pending(void);
extern void        hal_flash__last_parent_synced(char *out, size_t cap);
extern uint32_t    hal_flash__write_calls(void);

/* Observatory host-only ring access (core/src/observatory/obs.c). */
extern void        obs__reset(void);
extern bool        obs__ring_read(uint32_t idx, EmbedIQ_Event_t *out);

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
 * Concurrency: reads during a persist, concurrent writers
 * ------------------------------------------------------------------------- */

#define PERSIST_DELAY_MS       200u    /* slowed persist: a long write in flight */
#define READER_START_DELAY_MS  10u     /* reader starts once the persist is in flight */
#define READ_LATENCY_MAX_US    20000u  /* a read must not wait for the persist */
#define CONCURRENT_SET_ITERS   100u
#define WRITER_A_BASE          0x10000u
#define WRITER_B_BASE          0x20000u

static void *slow_persist_setter(void *arg)
{
    (void)arg;
    uint32_t v = 7u;
    (void)embediq_nvm_set("persist.slow", &v, sizeof(v));
    return NULL;
}

/* test_nvm_get_does_not_wait_for_persist
 * A read issued while another thread's set is persisting (200 ms) returns at
 * once. embediq_cfg_get_* are single-call wrappers over embediq_nvm_get, so
 * this is the cfg getter's read path.
 */
static void test_nvm_get_does_not_wait_for_persist(void)
{
    setup();
    uint32_t seed = 1u;
    (void)embediq_nvm_set("read.key", &seed, sizeof(seed));

    nvm__set_persist_delay_ms(PERSIST_DELAY_MS);
    pthread_t writer;
    pthread_create(&writer, NULL, slow_persist_setter, NULL);
    embediq_osal_delay_ms(READER_START_DELAY_MS);

    uint8_t  buf[sizeof(uint32_t)];
    uint32_t len = sizeof(buf);
    uint32_t t0  = embediq_osal_time_us();
    embediq_err_t rc = embediq_nvm_get("read.key", buf, &len);
    uint32_t latency_us = embediq_osal_time_us() - t0;

    pthread_join(writer, NULL);
    nvm__set_persist_delay_ms(0u);
    printf("      read latency during a %u ms persist: %u us\n",
           (unsigned)PERSIST_DELAY_MS, (unsigned)latency_us);
    ASSERT(rc == EMBEDIQ_OK && latency_us < READ_LATENCY_MAX_US,
           "a read must not wait for another thread's persist");
}

static void *repeat_setter(void *arg)
{
    uint32_t base = *(const uint32_t *)arg;
    for (uint32_t i = 0u; i < CONCURRENT_SET_ITERS; i++) {
        uint32_t v = base + i;
        (void)embediq_nvm_set("persist.race", &v, sizeof(v));
    }
    return NULL;
}

static bool read_u32(const char *key, uint32_t *out)
{
    uint32_t len = sizeof(*out);
    return embediq_nvm_get(key, out, &len) == EMBEDIQ_OK && len == sizeof(*out);
}

/* test_concurrent_sets_persist_newest
 * Two threads set the same key 100 times each. Flash must hold the newest
 * value — the one the table holds after both finish — both before and after
 * an explicit flush (no older write may land after a newer one).
 */
static void test_concurrent_sets_persist_newest(void)
{
    setup();
    uint32_t base_a = WRITER_A_BASE, base_b = WRITER_B_BASE;
    pthread_t ta, tb;
    pthread_create(&ta, NULL, repeat_setter, &base_a);
    pthread_create(&tb, NULL, repeat_setter, &base_b);
    pthread_join(ta, NULL);
    pthread_join(tb, NULL);

    uint32_t live = 0u, stored = 0u, flushed = 0u;
    bool ok_live = read_u32("persist.race", &live);

    nvm__init_state();                     /* simulate restart without a flush */
    nvm__load_state();
    bool ok_stored = read_u32("persist.race", &stored);

    (void)embediq_nvm_flush();
    nvm__init_state();
    nvm__load_state();
    bool ok_flushed = read_u32("persist.race", &flushed);

    bool newest = live == base_a + CONCURRENT_SET_ITERS - 1u
                  || live == base_b + CONCURRENT_SET_ITERS - 1u;
    printf("      live=0x%x stored=0x%x after_flush=0x%x\n",
           (unsigned)live, (unsigned)stored, (unsigned)flushed);
    ASSERT(ok_live && ok_stored && ok_flushed && newest
           && stored == live && flushed == live,
           "concurrent sets: flash holds the newest value, before and after flush");
}

/* ---------------------------------------------------------------------------
 * Persist failures: returned to the caller, observed, retried until durable
 * ------------------------------------------------------------------------- */

#define PERSIST_FAIL_SETS      70000u   /* well past any 16-bit generation lag */

/* Reload the table from flash, as a power cycle does. */
static void reload_from_flash(void)
{
    nvm__init_state();
    nvm__load_state();
}

static bool get_u32(const char *key, uint32_t *out)
{
    uint32_t len = sizeof(*out);
    return embediq_nvm_get(key, out, &len) == EMBEDIQ_OK && len == sizeof(*out);
}

static uint32_t count_persist_fail_events(void)
{
    uint32_t n = 0u;
    EmbedIQ_Event_t e;
    for (uint32_t i = 0u; obs__ring_read(i, &e); i++) {
        if (e.event_type == EMBEDIQ_OBS_EVT_NVM_PERSIST_FAIL) n++;
    }
    return n;
}

static uint32_t count_events(uint8_t type, uint8_t state)
{
    uint32_t n = 0u;
    EmbedIQ_Event_t e;
    for (uint32_t i = 0u; obs__ring_read(i, &e); i++) {
        if (e.event_type == type && e.state_or_flag == state) n++;
    }
    return n;
}

#define BLOB_STATE_READ_FAIL   3u   /* CFG_BLOB_INVALID state: flash could not be read */

/* test_unreadable_flash_refuses_to_persist_until_flush_reloads
 * k persisted. A boot whose flash read fails: RAM empty, one CFG_BLOB_INVALID
 * (read failure). delete(k) and set(j) → ERR, each with NVM_PERSIST_FAIL state
 * FLASH_UNREADABLE, and nothing is written. flush with the read still failing
 * → ERR. Read recovers: flush → OK and RAM holds k, not j; after a reload k is
 * present and j absent.
 */
static void test_unreadable_flash_refuses_to_persist_until_flush_reloads(void)
{
    setup();
    uint32_t k = 0x4Bu, j = 0x4Au, got = 0u;
    embediq_err_t rc_seed = embediq_nvm_set("persist.kept", &k, sizeof(k));
    obs__reset();
    embediq_obs_set_transport(EMBEDIQ_OBS_TRANSPORT_TCP);   /* ring only, no output */

    nvm__init_state();
    hal_flash__fail_next_read(UINT32_MAX);
    nvm__load_state();
    bool     empty      = !get_u32("persist.kept", &got);
    uint32_t read_fails = count_events(EMBEDIQ_OBS_EVT_CFG_BLOB_INVALID, BLOB_STATE_READ_FAIL);

    uint32_t before = hal_flash__write_calls();
    embediq_err_t rc_del   = embediq_nvm_delete("persist.kept");
    embediq_err_t rc_set   = embediq_nvm_set("persist.unsaved", &j, sizeof(j));
    embediq_err_t rc_flush = embediq_nvm_flush();
    uint32_t writes  = hal_flash__write_calls() - before;
    uint32_t refused = count_events(EMBEDIQ_OBS_EVT_NVM_PERSIST_FAIL,
                                    (uint8_t)EMBEDIQ_NVM_PERSIST_FLASH_UNREADABLE);

    hal_flash__fail_next_read(0u);
    embediq_err_t rc_reload = embediq_nvm_flush();
    bool k_in_ram = get_u32("persist.kept", &got) && got == k;
    bool j_in_ram = get_u32("persist.unsaved", &got);

    reload_from_flash();
    bool k_after = get_u32("persist.kept", &got) && got == k;
    bool j_after = get_u32("persist.unsaved", &got);
    embediq_obs_set_transport(EMBEDIQ_OBS_TRANSPORT_NULL);

    printf("      seed=%d empty=%d read_fail_events=%u delete=%d set=%d flush_unreadable=%d "
           "flash_writes=%u refused_events=%u | flush_recovered=%d k_in_ram=%d j_in_ram=%d "
           "k_after_reload=%d j_after_reload=%d\n",
           (int)rc_seed, (int)empty, (unsigned)read_fails, (int)rc_del, (int)rc_set,
           (int)rc_flush, (unsigned)writes, (unsigned)refused, (int)rc_reload,
           (int)k_in_ram, (int)j_in_ram, (int)k_after, (int)j_after);
    ASSERT(rc_seed == EMBEDIQ_OK && empty && read_fails == 1u
           && rc_del == EMBEDIQ_ERR && rc_set == EMBEDIQ_ERR && rc_flush == EMBEDIQ_ERR
           && writes == 0u && refused == 3u
           && rc_reload == EMBEDIQ_OK && k_in_ram && !j_in_ram && k_after && !j_after,
           "unreadable flash is never overwritten; flush re-reads it and restores the table");
}

/* test_real_read_failure_puts_store_in_unreadable_state
 * The same protection without the injection hook: the backing path is a
 * directory, so the flash read really fails. Load: one CFG_BLOB_INVALID
 * (read failure). set(k) → ERR, one NVM_PERSIST_FAIL state FLASH_UNREADABLE,
 * nothing written. The path is pointed at an absent file: flush → OK with one
 * CFG_BLOB_INVALID state 0 (no blob yet — an empty store); set(k) → OK with
 * one flash write; after a reload k is present.
 */
static void test_real_read_failure_puts_store_in_unreadable_state(void)
{
    setup();
    char dir[96];
    snprintf(dir, sizeof(dir), "/tmp/test_nvm_store_dir_%d", (int)getpid());
    (void)mkdir(dir, 0755);
    (void)remove(TEST_NVM_PATH);
    uint32_t k = 0x5Bu, got = 0u;
    obs__reset();
    embediq_obs_set_transport(EMBEDIQ_OBS_TRANSPORT_TCP);   /* ring only, no output */

    setenv("EMBEDIQ_NVM_PATH", dir, 1);
    nvm__init_state();
    nvm__load_state();
    uint32_t blob_events = 0u;
    for (uint8_t st = 0u; st < 4u; st++) {
        blob_events += count_events(EMBEDIQ_OBS_EVT_CFG_BLOB_INVALID, st);
    }
    uint32_t read_fails = count_events(EMBEDIQ_OBS_EVT_CFG_BLOB_INVALID, BLOB_STATE_READ_FAIL);

    uint32_t before = hal_flash__write_calls();
    embediq_err_t rc_set = embediq_nvm_set("persist.real", &k, sizeof(k));
    uint32_t writes  = hal_flash__write_calls() - before;
    uint32_t refused = count_events(EMBEDIQ_OBS_EVT_NVM_PERSIST_FAIL,
                                    (uint8_t)EMBEDIQ_NVM_PERSIST_FLASH_UNREADABLE);
    uint32_t persist_fails = count_persist_fail_events();

    setenv("EMBEDIQ_NVM_PATH", TEST_NVM_PATH, 1);   /* absent: an empty store */
    embediq_err_t rc_flush = embediq_nvm_flush();
    uint32_t no_blob = count_events(EMBEDIQ_OBS_EVT_CFG_BLOB_INVALID, 0u);

    uint32_t before2 = hal_flash__write_calls();
    embediq_err_t rc_set2 = embediq_nvm_set("persist.real", &k, sizeof(k));
    uint32_t writes2 = hal_flash__write_calls() - before2;

    reload_from_flash();
    bool k_after = get_u32("persist.real", &got) && got == k;
    embediq_obs_set_transport(EMBEDIQ_OBS_TRANSPORT_NULL);
    (void)rmdir(dir);
    char dir_tmp[sizeof(dir) + 4u];
    snprintf(dir_tmp, sizeof(dir_tmp), "%s.tmp", dir);
    (void)remove(dir_tmp);   /* a temp file a failed write may have left */

    printf("      blob_events=%u read_fail_events=%u set=%d flash_writes=%u "
           "refused_events=%u persist_fail_events=%u | flush=%d no_blob_events=%u "
           "set_after=%d writes_after=%u k_after_reload=%d\n",
           (unsigned)blob_events, (unsigned)read_fails, (int)rc_set, (unsigned)writes,
           (unsigned)refused, (unsigned)persist_fails, (int)rc_flush, (unsigned)no_blob,
           (int)rc_set2, (unsigned)writes2, (int)k_after);
    ASSERT(blob_events == 1u && read_fails == 1u
           && rc_set == EMBEDIQ_ERR && writes == 0u && refused == 1u && persist_fails == 1u
           && rc_flush == EMBEDIQ_OK && no_blob == 1u
           && rc_set2 == EMBEDIQ_OK && writes2 == 1u && k_after,
           "a real read failure makes the store unreadable; flush on a readable path recovers");
}

/* test_set_reports_persist_failure
 * A set whose flash write fails returns EMBEDIQ_ERR and emits one
 * NVM_PERSIST_FAIL; the value is readable from RAM. The next set (write
 * succeeds) returns OK, and a reload shows both values.
 */
static void test_set_reports_persist_failure(void)
{
    setup();
    obs__reset();
    embediq_obs_set_transport(EMBEDIQ_OBS_TRANSPORT_TCP);   /* ring only, no output */
    uint32_t a = 0xA1u, b = 0xB2u, got = 0u;

    hal_flash__fail_next_write(1u);
    embediq_err_t rc_fail = embediq_nvm_set("persist.fail_a", &a, sizeof(a));
    uint32_t events      = count_persist_fail_events();
    bool in_ram          = get_u32("persist.fail_a", &got) && got == a;

    embediq_err_t rc_ok  = embediq_nvm_set("persist.fail_b", &b, sizeof(b));
    reload_from_flash();
    uint32_t got_a = 0u, got_b = 0u;
    bool both = get_u32("persist.fail_a", &got_a) && got_a == a
                && get_u32("persist.fail_b", &got_b) && got_b == b;
    embediq_obs_set_transport(EMBEDIQ_OBS_TRANSPORT_NULL);

    printf("      failed_set=%d persist_fail_events=%u in_ram=%d next_set=%d "
           "both_after_reload=%d\n", (int)rc_fail, (unsigned)events, (int)in_ram,
           (int)rc_ok, (int)both);
    ASSERT(rc_fail == EMBEDIQ_ERR && events == 1u && in_ram && rc_ok == EMBEDIQ_OK && both,
           "failed persist returned and observed; the next persist carries the change");
}

/* The value stored on flash for key, read through the HAL, not through NVM:
 * an entry is the key field (EMBEDIQ_NVM_KEY_SIZE bytes, NUL-padded) followed
 * by its value bytes. Returns false if the key is not in the image. */
static bool flash_u32(const char *key, uint32_t *out)
{
    static uint8_t img[EMBEDIQ_NVM_MAX_KEYS * (EMBEDIQ_NVM_KEY_SIZE + EMBEDIQ_NVM_VAL_SIZE) * 2u];
    if (hal_flash_read(0u, img, sizeof(img)) != HAL_OK) return false;
    size_t klen = strlen(key) + 1u;   /* with its NUL */
    for (size_t i = 0u; i + EMBEDIQ_NVM_KEY_SIZE + sizeof(*out) <= sizeof(img); i++) {
        if (memcmp(img + i, key, klen) == 0) {
            memcpy(out, img + i + EMBEDIQ_NVM_KEY_SIZE, sizeof(*out));
            return true;
        }
    }
    return false;
}

/* test_set_after_directory_sync_failure_retries_and_persists
 * A directory-sync failure after the rename: the HAL reports HAL_ERR_IO with
 * the new store already published. set(k=2) → ERR, one NVM_PERSIST_FAIL with
 * the HAL code; RAM and the store both hold 2; one flash write. The next
 * set(j) → OK with one more write; after a reload k == 2 and j == 5. The
 * flush path: a failed directory sync fails the flush; the next flush is OK.
 */
static void test_set_after_directory_sync_failure_retries_and_persists(void)
{
    setup();
    uint32_t k1 = 1u, k2 = 2u, j = 5u, got = 0u, on_flash = 0u;
    embediq_err_t rc_seed = embediq_nvm_set("persist.dirsync_k", &k1, sizeof(k1));
    obs__reset();
    embediq_obs_set_transport(EMBEDIQ_OBS_TRANSPORT_TCP);   /* ring only, no output */

    uint32_t before = hal_flash__write_calls();
    hal_flash__fail_next_dir_fsync(1u);
    embediq_err_t rc_fail = embediq_nvm_set("persist.dirsync_k", &k2, sizeof(k2));
    uint32_t writes   = hal_flash__write_calls() - before;
    uint32_t fails    = count_persist_fail_events();
    uint32_t hal_code = count_events(EMBEDIQ_OBS_EVT_NVM_PERSIST_FAIL, (uint8_t)HAL_ERR_IO);
    bool in_ram       = get_u32("persist.dirsync_k", &got) && got == k2;
    bool published    = flash_u32("persist.dirsync_k", &on_flash) && on_flash == k2;

    uint32_t before2 = hal_flash__write_calls();
    embediq_err_t rc_next = embediq_nvm_set("persist.dirsync_j", &j, sizeof(j));
    uint32_t writes2 = hal_flash__write_calls() - before2;

    reload_from_flash();
    uint32_t got_k = 0u, got_j = 0u;
    bool both = get_u32("persist.dirsync_k", &got_k) && got_k == k2
             && get_u32("persist.dirsync_j", &got_j) && got_j == j;

    hal_flash__fail_next_dir_fsync(1u);
    embediq_err_t rc_flush_fail = embediq_nvm_flush();
    embediq_err_t rc_flush_ok   = embediq_nvm_flush();
    hal_flash__fail_next_dir_fsync(0u);
    embediq_obs_set_transport(EMBEDIQ_OBS_TRANSPORT_NULL);

    printf("      seed=%d failed_set=%d flash_writes=%u persist_fail_events=%u with_hal_code=%u "
           "in_ram=%d published=%d | next_set=%d next_writes=%u both_after_reload=%d | "
           "flush_fail=%d flush_ok=%d\n",
           (int)rc_seed, (int)rc_fail, (unsigned)writes, (unsigned)fails, (unsigned)hal_code,
           (int)in_ram, (int)published, (int)rc_next, (unsigned)writes2, (int)both,
           (int)rc_flush_fail, (int)rc_flush_ok);
    ASSERT(rc_seed == EMBEDIQ_OK && rc_fail == EMBEDIQ_ERR && writes == 1u
           && fails == 1u && hal_code == 1u && in_ram && published
           && rc_next == EMBEDIQ_OK && writes2 == 1u && both
           && rc_flush_fail == EMBEDIQ_ERR && rc_flush_ok == EMBEDIQ_OK,
           "a directory-sync failure fails the persist; the next persist carries the change");
}

/* HOME as the process started; saved and restored by the test that changes it. */
static char g_home_saved[1024];
static bool g_home_was_set = false;

static void home_save(void)
{
    const char *home = getenv("HOME");
    g_home_was_set = home != NULL && strlen(home) < sizeof(g_home_saved);
    if (g_home_was_set) memcpy(g_home_saved, home, strlen(home) + 1u);
}

static void home_restore(void)
{
    if (g_home_was_set) setenv("HOME", g_home_saved, 1); else unsetenv("HOME");
}

/* Remove path and, if it is a directory, everything below it. */
static void rm_tree(const char *path)
{
    DIR *d = opendir(path);
    if (d == NULL) {
        (void)unlink(path);
        return;
    }
    struct dirent *e;
    char child[1024];
    while ((e = readdir(d)) != NULL) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
        size_t lp = strlen(path), ln = strlen(e->d_name);
        if (lp + 1u + ln >= sizeof(child)) continue;
        memcpy(child, path, lp);
        child[lp] = '/';
        memcpy(child + lp + 1u, e->d_name, ln + 1u);
        rm_tree(child);
    }
    (void)closedir(d);
    (void)rmdir(path);
}

/* test_first_configuration_is_durable_only_after_parent_sync
 * Default store path, HOME without .embediq. Loading creates nothing. With
 * the sync of .embediq's parent failing, set(k=1) → ERR with one
 * NVM_PERSIST_FAIL carrying the HAL code and no parent sync. set(j=5) → OK:
 * the retry syncs HOME. After a reload k == 1 and j == 5.
 */
static void test_first_configuration_is_durable_only_after_parent_sync(void)
{
    char home[96], dot[128], last[1024];
    snprintf(home, sizeof(home), "/tmp/test_nvm_first_home_%d", (int)getpid());
    snprintf(dot,  sizeof(dot),  "%s/.embediq", home);
    home_save();
    rm_tree(home);
    (void)mkdir(home, 0755);
    setenv("HOME", home, 1);
    unsetenv("EMBEDIQ_NVM_PATH");
    obs__reset();
    embediq_obs_set_transport(EMBEDIQ_OBS_TRANSPORT_TCP);   /* ring only, no output */

    nvm__init_state();
    nvm__load_state();
    struct stat st;
    bool created_by_load = stat(dot, &st) == 0;

    uint32_t k = 1u, j = 5u, got_k = 0u, got_j = 0u;
    uint32_t syncs0 = hal_flash__parent_syncs();
    hal_flash__fail_next_parent_fsync(1u);
    embediq_err_t rc_k = embediq_nvm_set("persist.first_k", &k, sizeof(k));
    hal_flash__fail_next_parent_fsync(0u);
    uint32_t fails    = count_persist_fail_events();
    uint32_t hal_code = count_events(EMBEDIQ_OBS_EVT_NVM_PERSIST_FAIL, (uint8_t)HAL_ERR_IO);
    uint32_t syncs_k  = hal_flash__parent_syncs() - syncs0;

    embediq_err_t rc_j = embediq_nvm_set("persist.first_j", &j, sizeof(j));
    uint32_t syncs_j = hal_flash__parent_syncs() - syncs0;
    hal_flash__last_parent_synced(last, sizeof(last));
    bool synced_home = strcmp(last, home) == 0;

    reload_from_flash();
    bool both = get_u32("persist.first_k", &got_k) && got_k == k
             && get_u32("persist.first_j", &got_j) && got_j == j;
    embediq_obs_set_transport(EMBEDIQ_OBS_TRANSPORT_NULL);

    home_restore();
    setenv("EMBEDIQ_NVM_PATH", TEST_NVM_PATH, 1);
    rm_tree(home);

    printf("      created_by_load=%d set_k=%d persist_fail_events=%u with_hal_code=%u "
           "parent_syncs=%u | set_j=%d parent_syncs=%u synced_home=%d both_after_reload=%d\n",
           (int)created_by_load, (int)rc_k, (unsigned)fails, (unsigned)hal_code,
           (unsigned)syncs_k, (int)rc_j, (unsigned)syncs_j, (int)synced_home, (int)both);
    ASSERT(!created_by_load && rc_k == EMBEDIQ_ERR && fails == 1u && hal_code == 1u
           && syncs_k == 0u && rc_j == EMBEDIQ_OK && syncs_j == 1u && synced_home && both,
           "the first configuration is acknowledged only after its directory is durable");
}

/* test_provision_retry_reestablishes_durability
 * Seed; flash fails: provision → ERR, flush → ERR. Flash recovers: provision
 * again → OK with exactly one flash write; after a reload the identity is
 * there.
 */
static void test_provision_retry_reestablishes_durability(void)
{
    setup();
    uint32_t seed = 1u;
    (void)embediq_nvm_set("persist.seed", &seed, sizeof(seed));

    hal_flash__fail_next_write(UINT32_MAX);
    embediq_err_t rc_prov  = embediq_nvm_provision("factory.identity", "device1");
    embediq_err_t rc_flush = embediq_nvm_flush();
    hal_flash__fail_next_write(0u);

    uint32_t before = hal_flash__write_calls();
    embediq_err_t rc_retry = embediq_nvm_provision("factory.identity", "device1");
    uint32_t retry_writes = hal_flash__write_calls() - before;

    reload_from_flash();
    char     id[EMBEDIQ_NVM_VAL_MAX];
    uint32_t len = sizeof(id);
    embediq_err_t rc_get = embediq_nvm_get("factory.identity", id, &len);
    bool identity = rc_get == EMBEDIQ_OK && len == 7u && memcmp(id, "device1", 7u) == 0;

    printf("      failed_provision=%d failed_flush=%d retry=%d retry_flash_writes=%u "
           "identity_after_reload=%d\n", (int)rc_prov, (int)rc_flush, (int)rc_retry,
           (unsigned)retry_writes, (int)identity);
    ASSERT(rc_prov == EMBEDIQ_ERR && rc_flush == EMBEDIQ_ERR && rc_retry == EMBEDIQ_OK
           && retry_writes == 1u && identity,
           "provision retried after a failed persist writes once and is durable");
}

/* test_delete_retry_reestablishes_durability
 * k persisted; flash fails: delete k → ERR, and again → ERR. Flash recovers:
 * delete k again → OK with exactly one flash write; after a reload k is
 * absent. Deleting a key that never existed, with the absence already on
 * flash → OK with no flash write.
 */
static void test_delete_retry_reestablishes_durability(void)
{
    setup();
    embediq_err_t rc_seed = embediq_nvm_set("credential", "secret", 6u);

    hal_flash__fail_next_write(UINT32_MAX);
    embediq_err_t rc_del   = embediq_nvm_delete("credential");
    embediq_err_t rc_still = embediq_nvm_delete("credential");
    hal_flash__fail_next_write(0u);

    uint32_t before = hal_flash__write_calls();
    embediq_err_t rc_retry = embediq_nvm_delete("credential");
    uint32_t retry_writes = hal_flash__write_calls() - before;

    before = hal_flash__write_calls();
    embediq_err_t rc_never = embediq_nvm_delete("never.stored");
    uint32_t never_writes = hal_flash__write_calls() - before;

    reload_from_flash();
    char     out[EMBEDIQ_NVM_VAL_MAX];
    uint32_t len = sizeof(out);
    bool absent = embediq_nvm_get("credential", out, &len) != EMBEDIQ_OK;

    printf("      seed=%d failed_delete=%d failed_again=%d retry=%d retry_flash_writes=%u "
           "absent_after_reload=%d | never_stored=%d never_flash_writes=%u\n",
           (int)rc_seed, (int)rc_del, (int)rc_still, (int)rc_retry,
           (unsigned)retry_writes, (int)absent, (int)rc_never, (unsigned)never_writes);
    ASSERT(rc_seed == EMBEDIQ_OK && rc_del == EMBEDIQ_ERR && rc_still == EMBEDIQ_ERR
           && rc_retry == EMBEDIQ_OK && retry_writes == 1u && absent
           && rc_never == EMBEDIQ_OK && never_writes == 0u,
           "delete retried after a failed persist writes once; an absence on flash needs no write");
}

/* test_flush_reports_failure
 * flush returns ERR while the write fails and OK once it succeeds.
 */
static void test_flush_reports_failure(void)
{
    setup();
    hal_flash__fail_next_write(1u);
    embediq_err_t rc_fail = embediq_nvm_flush();
    embediq_err_t rc_ok   = embediq_nvm_flush();
    printf("      failed_flush=%d recovered_flush=%d\n", (int)rc_fail, (int)rc_ok);
    ASSERT(rc_fail == EMBEDIQ_ERR && rc_ok == EMBEDIQ_OK,
           "flush returns the flash write result");
}

/* test_skip_rule_never_skips_an_unpersisted_change
 * (1) 70,000 sets of one key with the flash failing, then one set after it
 *     recovers: OK, exactly one flash write, the last value after a reload.
 * (2) k=1 persisted; k=2 fails; a set of a different key afterwards persists
 *     the whole table: after a reload k == 2.
 */
static void test_skip_rule_never_skips_an_unpersisted_change(void)
{
    setup();
    hal_flash__fail_next_write(UINT32_MAX);
    for (uint32_t i = 0u; i < PERSIST_FAIL_SETS; i++) {
        (void)embediq_nvm_set("persist.lag", &i, sizeof(i));
    }
    hal_flash__fail_next_write(0u);
    uint32_t last = PERSIST_FAIL_SETS;
    uint32_t before = hal_flash__write_calls();
    embediq_err_t rc_last = embediq_nvm_set("persist.lag", &last, sizeof(last));
    uint32_t writes = hal_flash__write_calls() - before;
    reload_from_flash();
    uint32_t got = 0u;
    bool lag_ok = get_u32("persist.lag", &got) && got == last;

    uint32_t k1 = 1u, k2 = 2u, other = 3u;
    embediq_err_t rc_k1 = embediq_nvm_set("persist.k", &k1, sizeof(k1));
    hal_flash__fail_next_write(1u);
    embediq_err_t rc_k2 = embediq_nvm_set("persist.k", &k2, sizeof(k2));
    embediq_err_t rc_other = embediq_nvm_set("persist.other", &other, sizeof(other));
    reload_from_flash();
    uint32_t k = 0u;
    bool carried = get_u32("persist.k", &k) && k == k2;

    printf("      after_%u_failed_sets: set=%d flash_writes=%u reloaded_last=%d | "
           "k1=%d k2=%d other=%d k_after_reload=%u\n", (unsigned)PERSIST_FAIL_SETS,
           (int)rc_last, (unsigned)writes, (int)lag_ok, (int)rc_k1, (int)rc_k2,
           (int)rc_other, (unsigned)k);
    ASSERT(rc_last == EMBEDIQ_OK && writes == 1u && lag_ok && rc_k1 == EMBEDIQ_OK
           && rc_k2 == EMBEDIQ_ERR && rc_other == EMBEDIQ_OK && carried,
           "a change not on flash is never skipped; a later persist carries it");
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

    /* Concurrency */
    test_nvm_get_does_not_wait_for_persist();
    test_concurrent_sets_persist_newest();

    /* Persist failures */
    test_set_reports_persist_failure();
    test_provision_retry_reestablishes_durability();
    test_delete_retry_reestablishes_durability();
    test_unreadable_flash_refuses_to_persist_until_flush_reloads();
    test_real_read_failure_puts_store_in_unreadable_state();
    test_set_after_directory_sync_failure_retries_and_persists();
    test_first_configuration_is_durable_only_after_parent_sync();
    test_flush_reports_failure();
    test_skip_rule_never_skips_an_unpersisted_change();

    printf("\n");
    if (g_tests_failed == 0) {
        printf("All %d tests passed. (0 failed)\n", g_tests_run);
    } else {
        printf("%d of %d tests FAILED.\n", g_tests_failed, g_tests_run);
    }
    return (g_tests_failed > 0) ? 1 : 0;
}
