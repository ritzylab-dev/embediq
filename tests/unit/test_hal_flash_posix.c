#define _POSIX_C_SOURCE 200809L

/*
 * tests/unit/test_hal_flash_posix.c — POSIX flash HAL read and write semantics
 *
 * Exercises hal_flash_posix.c with real filesystem failures, no injection
 * hook: a directory as the backing path (fopen succeeds, fread fails), a
 * store made unreadable by permissions, a store larger than the
 * implementation holds, and a directory the temp file cannot be created in.
 * Also pins the success semantics of hal_flash.h: an absent store reads as
 * zeros, an erased region reads 0xFF, bytes beyond a short store read zero.
 *
 * Every test works in its own directory /tmp/embediq_hal_flash_<pid>/, made
 * in setup() and removed in teardown(), so concurrent test binaries never
 * share a file.
 *
 * Run:      ./build/tests/unit/test_hal_flash_posix
 * Expected: "All N tests passed. (0 failed)"
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "embediq_obs.h"
#include "hal_defs.h"
#include "hal_flash.h"

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>     /* setenv */
#include <string.h>
#include <unistd.h>     /* geteuid, getpid, unlink, rmdir */
#include <sys/stat.h>   /* chmod, mkdir, stat */

/* Observatory host-only ring access (core/src/observatory/obs.c). */
extern void     obs__reset(void);
extern bool     obs__ring_read(uint32_t idx, EmbedIQ_Event_t *out);

/* The largest store hal_flash_posix.c holds (its HAL_FLASH_POSIX_MAX_SIZE). */
#define TEST_FLASH_MAX_SIZE  (64u * 1024u)

/* Buffer and store sizes used by the tests. */
#define TEST_READ_LEN    64u   /* one read of an absent or unreadable store */
#define TEST_STORE_LEN   32u   /* a seeded store                            */
#define TEST_REGION_LEN  16u   /* the erase and short-read stores           */
#define TEST_SHORT_LEN    8u   /* a store shorter than the read             */

/* ---------------------------------------------------------------------------
 * Minimal test harness
 * ------------------------------------------------------------------------- */

static int g_tests_run    = 0;
static int g_tests_failed = 0;

#define ASSERT(cond, msg)  do {                                                 \
    g_tests_run++;                                                              \
    if (!(cond)) {                                                              \
        fprintf(stderr, "FAIL  %-48s  %s:%d  %s\n",                             \
                __func__, __FILE__, __LINE__, (msg));                           \
        g_tests_failed++;                                                       \
    } else {                                                                    \
        printf("PASS  %s  %s\n", __func__, (msg));                              \
    }                                                                           \
} while (0)

/* ---------------------------------------------------------------------------
 * Per-test directory and paths
 * ------------------------------------------------------------------------- */

#define TEST_PATH_MAX  256u

static char g_dir[TEST_PATH_MAX];        /* /tmp/embediq_hal_flash_<pid>      */
static char g_store[TEST_PATH_MAX];      /* <dir>/store.bin                   */
static char g_store_tmp[TEST_PATH_MAX];  /* <dir>/store.bin.tmp               */
static char g_dirstore[TEST_PATH_MAX];   /* <dir>/dirstore (a directory)      */
static char g_dirstore_tmp[TEST_PATH_MAX];

static void setup(void)
{
    snprintf(g_dir,          sizeof(g_dir),          "/tmp/embediq_hal_flash_%d", (int)getpid());
    snprintf(g_store,        sizeof(g_store),        "%s/store.bin", g_dir);
    snprintf(g_store_tmp,    sizeof(g_store_tmp),    "%s/store.bin.tmp", g_dir);
    snprintf(g_dirstore,     sizeof(g_dirstore),     "%s/dirstore", g_dir);
    snprintf(g_dirstore_tmp, sizeof(g_dirstore_tmp), "%s/dirstore.tmp", g_dir);
    (void)mkdir(g_dir, 0755);
    setenv("EMBEDIQ_NVM_PATH", g_store, 1);
    obs__reset();
}

static void teardown(void)
{
    (void)chmod(g_dir, 0755);
    (void)chmod(g_store, 0644);
    (void)unlink(g_store);
    (void)unlink(g_store_tmp);
    (void)rmdir(g_dirstore);
    (void)unlink(g_dirstore_tmp);
    (void)rmdir(g_dir);
}

static bool write_raw(const char *path, const uint8_t *data, size_t len)
{
    FILE *f = fopen(path, "wb");
    if (f == NULL) return false;
    bool ok = fwrite(data, 1u, len, f) == len;
    return (fclose(f) == 0) && ok;
}

static long file_size(const char *path)
{
    struct stat st;
    return (stat(path, &st) == 0) ? (long)st.st_size : -1L;
}

static bool exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

static uint32_t flash_fault_count(void)
{
    uint32_t n = 0u;
    EmbedIQ_Event_t e;
    for (uint32_t i = 0u; obs__ring_read(i, &e); i++) {
        if (e.event_type == EMBEDIQ_OBS_EVT_HAL_FAULT &&
            e.source_fb_id == (uint8_t)EMBEDIQ_HAL_SRC_FLASH) {
            n++;
        }
    }
    return n;
}

static void fill_pattern(uint8_t *buf, size_t len)
{
    for (size_t i = 0u; i < len; i++) buf[i] = (uint8_t)(0x40u + i);
}

/* ---------------------------------------------------------------------------
 * Tests
 * ------------------------------------------------------------------------- */

static void test_absent_store_reads_empty_ok(void)
{
    setup();
    uint8_t buf[TEST_READ_LEN];
    memset(buf, 0xAA, sizeof(buf));
    int rc = hal_flash_read(0u, buf, sizeof(buf));
    bool zeros = true;
    for (size_t i = 0u; i < sizeof(buf); i++) if (buf[i] != 0u) zeros = false;
    ASSERT(rc == HAL_OK, "an absent store reads HAL_OK");
    ASSERT(zeros, "an absent store reads as zeros");
    ASSERT(flash_fault_count() == 0u, "no fault observed");
    teardown();
}

static void test_unreadable_store_read_returns_error(void)
{
    setup();
    (void)mkdir(g_dirstore, 0755);
    setenv("EMBEDIQ_NVM_PATH", g_dirstore, 1);
    uint8_t buf[TEST_READ_LEN];
    int rc = hal_flash_read(0u, buf, sizeof(buf));
    uint32_t faults = flash_fault_count();
    printf("      read(directory store) rc=%d faults=%u\n", rc, (unsigned)faults);
    ASSERT(rc == HAL_ERR_IO, "a store that cannot be read returns HAL_ERR_IO");
    ASSERT(faults == 1u, "exactly one HAL fault observed");
    teardown();
}

static void test_unreadable_store_write_does_not_replace_it(void)
{
    if (geteuid() == 0) {
        printf("SKIP  %s  (running as root; permissions are moot)\n", __func__);
        return;
    }
    setup();
    uint8_t orig[TEST_STORE_LEN];
    fill_pattern(orig, sizeof(orig));
    bool seeded = hal_flash_write(0u, orig, sizeof(orig)) == HAL_OK;

    obs__reset();
    (void)chmod(g_store, 0000);
    const uint8_t data[4] = { 1u, 2u, 3u, 4u };
    int rc_write = hal_flash_write(0u, data, sizeof(data));
    int rc_erase = hal_flash_erase(0u, 4u);
    (void)chmod(g_store, 0644);
    uint32_t faults = flash_fault_count();

    uint8_t back[TEST_STORE_LEN];
    memset(back, 0, sizeof(back));
    int rc_read = hal_flash_read(0u, back, sizeof(back));
    long size   = file_size(g_store);
    printf("      write=%d erase=%d faults=%u | size after=%ld\n",
           rc_write, rc_erase, (unsigned)faults, size);

    ASSERT(seeded, "the store was seeded");
    ASSERT(rc_write == HAL_ERR_IO, "write to an unreadable store returns HAL_ERR_IO");
    ASSERT(rc_erase == HAL_ERR_IO, "erase of an unreadable store returns HAL_ERR_IO");
    ASSERT(faults == 2u, "one HAL fault observed per failed call");
    ASSERT(rc_read == HAL_OK && size == (long)TEST_STORE_LEN && memcmp(back, orig, sizeof(orig)) == 0,
           "the original store is intact");
    ASSERT(!exists(g_store_tmp), "no temp file is left behind");
    teardown();
}

static void test_oversized_store_is_refused_not_truncated(void)
{
    setup();
    static uint8_t big[TEST_FLASH_MAX_SIZE + 16u];
    fill_pattern(big, sizeof(big));
    bool seeded = write_raw(g_store, big, sizeof(big));

    uint8_t buf[TEST_READ_LEN];
    int rc_read  = hal_flash_read(0u, buf, sizeof(buf));
    const uint8_t data[4] = { 1u, 2u, 3u, 4u };
    int rc_write = hal_flash_write(0u, data, sizeof(data));
    long size    = file_size(g_store);
    printf("      read=%d write=%d size=%ld (was %ld)\n",
           rc_read, rc_write, size, (long)sizeof(big));

    ASSERT(seeded, "an oversized store was created");
    ASSERT(rc_read == HAL_ERR_INVALID, "read of an oversized store returns HAL_ERR_INVALID");
    ASSERT(rc_write == HAL_ERR_INVALID, "write to an oversized store returns HAL_ERR_INVALID");
    ASSERT(size == (long)sizeof(big), "the store is not rewritten at the buffer size");
    teardown();
}

static void test_write_failure_leaves_store_and_no_tmp(void)
{
    if (geteuid() == 0) {
        printf("SKIP  %s  (running as root; permissions are moot)\n", __func__);
        return;
    }
    setup();
    uint8_t orig[TEST_STORE_LEN];
    fill_pattern(orig, sizeof(orig));
    bool seeded = hal_flash_write(0u, orig, sizeof(orig)) == HAL_OK;

    (void)chmod(g_dir, 0500);   /* the temp file cannot be created */
    const uint8_t data[4] = { 1u, 2u, 3u, 4u };
    int rc_write = hal_flash_write(0u, data, sizeof(data));
    (void)chmod(g_dir, 0755);

    uint8_t back[TEST_STORE_LEN];
    memset(back, 0, sizeof(back));
    int rc_read = hal_flash_read(0u, back, sizeof(back));

    ASSERT(seeded, "the store was seeded");
    ASSERT(rc_write == HAL_ERR_IO, "a write whose temp file cannot be created returns HAL_ERR_IO");
    ASSERT(rc_read == HAL_OK && file_size(g_store) == (long)TEST_STORE_LEN &&
           memcmp(back, orig, sizeof(orig)) == 0, "the store is intact");
    ASSERT(!exists(g_store_tmp), "no temp file is left behind");
    teardown();
}

static void test_erased_region_reads_erased_value(void)
{
    setup();
    uint8_t orig[TEST_REGION_LEN];
    fill_pattern(orig, sizeof(orig));
    int rc_w = hal_flash_write(0u, orig, sizeof(orig));
    int rc_e = hal_flash_erase(4u, 4u);
    uint8_t back[TEST_REGION_LEN];
    int rc_r = hal_flash_read(0u, back, sizeof(back));

    bool ok = (rc_w == HAL_OK) && (rc_e == HAL_OK) && (rc_r == HAL_OK);
    for (size_t i = 0u; i < sizeof(back); i++) {
        uint8_t want = (i >= 4u && i < 8u) ? 0xFFu : orig[i];
        if (back[i] != want) ok = false;
    }
    ASSERT(ok, "bytes 4-7 read 0xFF after erase; the rest are intact");
    teardown();
}

static void test_short_store_reads_zero_beyond_length(void)
{
    setup();
    uint8_t orig[TEST_SHORT_LEN];
    fill_pattern(orig, sizeof(orig));
    bool seeded = write_raw(g_store, orig, sizeof(orig));
    uint8_t back[TEST_REGION_LEN];
    memset(back, 0xAA, sizeof(back));
    int rc = hal_flash_read(0u, back, sizeof(back));

    bool ok = seeded && (rc == HAL_OK) && memcmp(back, orig, sizeof(orig)) == 0;
    for (size_t i = sizeof(orig); i < sizeof(back); i++) if (back[i] != 0u) ok = false;
    ASSERT(ok, "the stored bytes, then zeros, HAL_OK");
    teardown();
}

/* ---------------------------------------------------------------------------
 * main
 * ------------------------------------------------------------------------- */

int main(void)
{
    embediq_obs_set_transport(EMBEDIQ_OBS_TRANSPORT_TCP);   /* ring only, no output */

    test_absent_store_reads_empty_ok();
    test_unreadable_store_read_returns_error();
    test_unreadable_store_write_does_not_replace_it();
    test_oversized_store_is_refused_not_truncated();
    test_write_failure_leaves_store_and_no_tmp();
    test_erased_region_reads_erased_value();
    test_short_store_reads_zero_beyond_length();

    embediq_obs_set_transport(EMBEDIQ_OBS_TRANSPORT_NULL);

    printf("\n");
    if (g_tests_failed == 0) {
        printf("All %d tests passed. (0 failed)\n", g_tests_run);
    } else {
        printf("%d of %d tests FAILED.\n", g_tests_failed, g_tests_run);
    }
    return (g_tests_failed > 0) ? 1 : 0;
}
