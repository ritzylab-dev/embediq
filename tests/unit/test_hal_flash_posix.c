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
 * Also: ranges are checked without wrapping (an interval near SIZE_MAX is
 * refused and touches nothing), store paths that do not fit are refused and
 * create nothing (explicit EMBEDIQ_NVM_PATH and the HOME-derived default),
 * and a directory-sync failure after the rename is reported with the new
 * store already published.
 *
 * Every test works in its own directory /tmp/embediq_hal_flash_<pid>/, made
 * in setup() and removed with everything in it in teardown(), so concurrent
 * test binaries never share a file. HOME and EMBEDIQ_NVM_PATH are restored
 * in teardown().
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
#include <dirent.h>     /* opendir — directory listing and removal */
#include <limits.h>     /* PATH_MAX — the HAL's path buffer size */

/* Observatory host-only ring access (core/src/observatory/obs.c). */
extern void     obs__reset(void);
extern bool     obs__ring_read(uint32_t idx, EmbedIQ_Event_t *out);

/* Host-only: the next n directory syncs after a rename fail. */
extern void     hal_flash__fail_next_dir_fsync(uint32_t n);

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

/* "/tmp/embediq_hal_flash_" is 23 chars; a pid printed as int is at most 10
 * digits plus an optional sign (11); plus the NUL. */
#define TEST_DIR_MAX   (23u + 11u + 1u)
/* g_dir plus the longest fixed suffix: sizeof("/dirstore.tmp") is 13 chars +
 * NUL = 14, which also covers "/store.bin.tmp" (14). */
#define TEST_PATH_MAX  (TEST_DIR_MAX + sizeof("/dirstore.tmp"))
/* The HAL's path buffer (PATH_BUF in hal_flash_posix.c). */
#define TEST_HAL_PATH_BUF  ((size_t)PATH_MAX)

static char g_dir[TEST_DIR_MAX];          /* /tmp/embediq_hal_flash_<pid>      */
static char g_store[TEST_PATH_MAX];       /* <dir>/store.bin                   */
static char g_store_tmp[TEST_PATH_MAX];   /* <dir>/store.bin.tmp               */
static char g_dirstore[TEST_PATH_MAX];    /* <dir>/dirstore (a directory)      */
static char g_dirstore_tmp[TEST_PATH_MAX];

/* HOME and EMBEDIQ_NVM_PATH as the process started, restored in teardown. */
static char g_saved_home[TEST_HAL_PATH_BUF];
static bool g_had_home     = false;
static char g_saved_nvm[TEST_HAL_PATH_BUF];
static bool g_had_nvm      = false;

static bool fits(int rc, size_t cap)
{
    return rc >= 0 && (size_t)rc < cap;
}

/* g_dir followed by suffix; the precision bounds g_dir for the compiler. */
static bool dir_path(char *out, size_t cap, const char *suffix)
{
    return fits(snprintf(out, cap, "%.*s%s", (int)(TEST_DIR_MAX - 1u), g_dir, suffix), cap);
}

static void setup(void)
{
    bool ok = fits(snprintf(g_dir, sizeof(g_dir), "/tmp/embediq_hal_flash_%d", (int)getpid()),
                   sizeof(g_dir))
           && dir_path(g_store,        sizeof(g_store),        "/store.bin")
           && dir_path(g_store_tmp,    sizeof(g_store_tmp),    "/store.bin.tmp")
           && dir_path(g_dirstore,     sizeof(g_dirstore),     "/dirstore")
           && dir_path(g_dirstore_tmp, sizeof(g_dirstore_tmp), "/dirstore.tmp");
    if (!ok) {   /* cannot happen with the sizes above; the check states the invariant */
        printf("FAIL  setup: path too long\n");
        exit(1);
    }
    (void)mkdir(g_dir, 0755);
    setenv("EMBEDIQ_NVM_PATH", g_store, 1);
    obs__reset();
}

/* a + "/" + b into out (cap bytes); false if it does not fit. */
static bool join(char *out, size_t cap, const char *a, const char *b)
{
    size_t la = strlen(a), lb = strlen(b);
    if (la + 1u + lb >= cap) return false;
    memcpy(out, a, la);
    out[la] = '/';
    memcpy(out + la + 1u, b, lb + 1u);
    return true;
}

/* Remove path and, if it is a directory, everything below it. */
static void rm_tree(const char *path)
{
    (void)chmod(path, 0755);
    DIR *d = opendir(path);
    if (d == NULL) {
        (void)unlink(path);
        return;
    }
    struct dirent *e;
    char child[TEST_HAL_PATH_BUF];
    while ((e = readdir(d)) != NULL) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
        if (join(child, sizeof(child), path, e->d_name)) rm_tree(child);
    }
    (void)closedir(d);
    (void)rmdir(path);
}

/* Entries in a directory, excluding "." and ".."; -1 if it cannot be opened. */
static int entry_count(const char *path)
{
    DIR *d = opendir(path);
    if (d == NULL) return -1;
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (strcmp(e->d_name, ".") != 0 && strcmp(e->d_name, "..") != 0) n++;
    }
    (void)closedir(d);
    return n;
}

static void teardown(void)
{
    (void)chmod(g_store, 0644);
    rm_tree(g_dir);
    if (g_had_home) setenv("HOME", g_saved_home, 1); else unsetenv("HOME");
    if (g_had_nvm)  setenv("EMBEDIQ_NVM_PATH", g_saved_nvm, 1); else unsetenv("EMBEDIQ_NVM_PATH");
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

    obs__reset();
    (void)chmod(g_dir, 0500);   /* the temp file cannot be created */
    const uint8_t data[4] = { 1u, 2u, 3u, 4u };
    int rc_write = hal_flash_write(0u, data, sizeof(data));
    (void)chmod(g_dir, 0755);
    uint32_t faults = flash_fault_count();

    uint8_t back[TEST_STORE_LEN];
    memset(back, 0, sizeof(back));
    int rc_read = hal_flash_read(0u, back, sizeof(back));

    ASSERT(seeded, "the store was seeded");
    ASSERT(rc_write == HAL_ERR_IO, "a write whose temp file cannot be created returns HAL_ERR_IO");
    ASSERT(faults == 1u, "exactly one HAL fault observed");
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

/* The whole backing file equals data[0..len). */
static bool store_equals(const uint8_t *data, size_t len)
{
    static uint8_t got[TEST_FLASH_MAX_SIZE + 1u];
    FILE *f = fopen(g_store, "rb");
    if (f == NULL) return false;
    size_t n = fread(got, 1u, sizeof(got), f);
    (void)fclose(f);
    return n == len && memcmp(got, data, len) == 0;
}

static void test_directory_sync_failure_reports_after_publication(void)
{
    setup();
    uint8_t orig[TEST_STORE_LEN];
    fill_pattern(orig, sizeof(orig));
    bool seeded = hal_flash_write(0u, orig, sizeof(orig)) == HAL_OK;

    uint8_t next[TEST_STORE_LEN];
    memcpy(next, orig, sizeof(next));
    const uint8_t data[4] = { 0xA1u, 0xA2u, 0xA3u, 0xA4u };
    memcpy(next, data, sizeof(data));

    obs__reset();
    hal_flash__fail_next_dir_fsync(1u);
    int rc = hal_flash_write(0u, data, sizeof(data));
    hal_flash__fail_next_dir_fsync(0u);
    uint32_t faults = flash_fault_count();
    printf("      write with failing directory sync rc=%d faults=%u\n", rc, (unsigned)faults);

    ASSERT(seeded, "the store was seeded");
    ASSERT(rc == HAL_ERR_IO, "a directory-sync failure after the rename returns HAL_ERR_IO");
    ASSERT(faults == 1u, "exactly one HAL fault observed");
    ASSERT(store_equals(next, sizeof(next)), "the new contents are published (no rollback)");
    ASSERT(!exists(g_store_tmp), "no temp file is left behind");
    ASSERT(hal_flash_write(0u, data, sizeof(data)) == HAL_OK, "the next write succeeds");
    teardown();
}

static void test_env_path_whose_temp_suffix_does_not_fit_is_refused(void)
{
    setup();
    /* g_dir + "/" + 'e'... to TEST_HAL_PATH_BUF - 2 chars: the path fits with
     * its NUL, "<path>.tmp" does not. */
    static char path[TEST_HAL_PATH_BUF];
    size_t len = strlen(g_dir);
    memcpy(path, g_dir, len);
    path[len++] = '/';
    while (len < TEST_HAL_PATH_BUF - 2u) path[len++] = 'e';
    path[len] = '\0';
    setenv("EMBEDIQ_NVM_PATH", path, 1);

    int before = entry_count(g_dir);
    const uint8_t data[4] = { 1u, 2u, 3u, 4u };
    int rc = hal_flash_write(0u, data, sizeof(data));
    uint32_t faults = flash_fault_count();
    int after = entry_count(g_dir);
    printf("      path_len=%zu write=%d faults=%u entries %d -> %d\n",
           strlen(path), rc, (unsigned)faults, before, after);

    ASSERT(rc == HAL_ERR_INVALID, "a store path whose temp path does not fit is refused");
    ASSERT(faults == 1u, "exactly one HAL fault observed");
    ASSERT(after == before, "nothing is created");
    teardown();
}

/* Make every directory of path (absolute; components separated by '/'). */
static bool make_dirs(const char *path)
{
    static char partial[TEST_HAL_PATH_BUF];
    size_t len = strlen(path);
    if (len >= sizeof(partial)) return false;
    for (size_t i = 1u; i <= len; i++) {
        if (path[i] == '/' || path[i] == '\0') {
            memcpy(partial, path, i);
            partial[i] = '\0';
            (void)mkdir(partial, 0755);
        }
    }
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static void test_default_path_that_does_not_fit_is_refused(void)
{
    setup();
    /* HOME = <dir>/longhome/hhh.../hhh... of TEST_HAL_PATH_BUF - 20 chars, made
     * of components short enough for any filesystem and a long last one. */
    static char home[TEST_HAL_PATH_BUF];
    static char parent[TEST_HAL_PATH_BUF];
    const size_t target = TEST_HAL_PATH_BUF - 20u;
    const size_t comp_max = 200u, last_min = 40u;
    bool built = join(home, sizeof(home), g_dir, "longhome");
    size_t len = strlen(home);
    size_t parent_len = len;
    while (built && len < target) {
        size_t room = target - len - 1u;          /* chars after the next '/' */
        size_t comp = room;
        if (room > comp_max) comp = (room - comp_max < last_min) ? room / 2u : comp_max;
        parent_len = len;
        home[len++] = '/';
        memset(home + len, 'h', comp);
        len += comp;
        home[len] = '\0';
    }
    memcpy(parent, home, parent_len);
    parent[parent_len] = '\0';
    built = built && len == target && make_dirs(home);

    setenv("HOME", home, 1);
    unsetenv("EMBEDIQ_NVM_PATH");
    obs__reset();
    const uint8_t data[4] = { 1u, 2u, 3u, 4u };
    int rc = hal_flash_write(0u, data, sizeof(data));
    uint32_t faults = flash_fault_count();
    int in_parent = entry_count(parent);
    int in_home   = entry_count(home);
    printf("      home_len=%zu write=%d faults=%u entries: parent=%d home=%d\n",
           strlen(home), rc, (unsigned)faults, in_parent, in_home);

    ASSERT(built, "a HOME of the path buffer size less 20 was created");
    ASSERT(rc == HAL_ERR_INVALID, "a default store path that does not fit is refused");
    ASSERT(faults == 1u, "exactly one HAL fault observed");
    ASSERT(in_parent == 1 && in_home == 0,
           "nothing is created under HOME or under a shortened prefix of it");

    /* A HOME that fits: the store is at exactly <HOME>/.embediq/nvm_store.bin. */
    static char short_home[TEST_HAL_PATH_BUF];
    static char want[TEST_HAL_PATH_BUF];
    bool ok_paths = join(short_home, sizeof(short_home), g_dir, "home")
                 && join(want, sizeof(want), short_home, ".embediq/nvm_store.bin");
    (void)mkdir(short_home, 0755);
    setenv("HOME", short_home, 1);
    int rc_fit = hal_flash_write(0u, data, sizeof(data));
    ASSERT(ok_paths && rc_fit == HAL_OK && exists(want),
           "a HOME that fits stores at <HOME>/.embediq/nvm_store.bin");
    teardown();
}

static void test_ranges_are_checked_without_wrapping(void)
{
    setup();
    const size_t max = TEST_FLASH_MAX_SIZE;
    uint8_t seed[TEST_STORE_LEN];
    fill_pattern(seed, sizeof(seed));
    bool seeded = write_raw(g_store, seed, sizeof(seed));
    uint8_t buf[TEST_SHORT_LEN];
    const uint8_t data[4] = { 0xB1u, 0xB2u, 0xB3u, 0xB4u };

    /* Wrapping intervals: refused, one observation each, store untouched. */
    obs__reset();
    int w_wrap = hal_flash_write(1u, data, SIZE_MAX);
    int e_wrap = hal_flash_erase(1u, SIZE_MAX);
    int r_wrap = hal_flash_read(1u, buf, SIZE_MAX);
    uint32_t wrap_faults = flash_fault_count();
    bool wrap_untouched = store_equals(seed, sizeof(seed));
    printf("      (1, SIZE_MAX): write=%d erase=%d read=%d faults=%u untouched=%d\n",
           w_wrap, e_wrap, r_wrap, (unsigned)wrap_faults, (int)wrap_untouched);
    ASSERT(w_wrap == HAL_ERR_INVALID && e_wrap == HAL_ERR_INVALID && r_wrap == HAL_ERR_INVALID,
           "(1, SIZE_MAX) is HAL_ERR_INVALID for write, erase and read");
    ASSERT(wrap_faults == 3u, "one HAL fault observed per refused call");
    ASSERT(wrap_untouched, "the store is byte-identical");

    /* The last byte, and an interval ending exactly at capacity. */
    uint8_t one = 0xA5u, back_one = 0u;
    int w_last = hal_flash_write((uint32_t)(max - 1u), &one, 1u);
    int r_last = hal_flash_read((uint32_t)(max - 1u), &back_one, 1u);
    ASSERT(w_last == HAL_OK && r_last == HAL_OK && back_one == one
           && file_size(g_store) == (long)max, "(MAX-1, 1) writes and reads the last byte");
    uint8_t back4[4];
    int w_end = hal_flash_write((uint32_t)(max - 4u), data, sizeof(data));
    int r_end = hal_flash_read((uint32_t)(max - 4u), back4, sizeof(back4));
    ASSERT(w_end == HAL_OK && r_end == HAL_OK && memcmp(back4, data, sizeof(data)) == 0,
           "(MAX-4, 4) ends exactly at capacity");

    /* One byte past capacity: refused, store untouched. */
    static uint8_t snap[TEST_FLASH_MAX_SIZE];
    FILE *f = fopen(g_store, "rb");
    size_t snap_len = (f != NULL) ? fread(snap, 1u, sizeof(snap), f) : 0u;
    if (f != NULL) (void)fclose(f);
    obs__reset();
    int w_over = hal_flash_write((uint32_t)(max - 3u), data, sizeof(data));
    int e_over = hal_flash_erase((uint32_t)(max - 3u), sizeof(data));
    int r_over = hal_flash_read((uint32_t)(max - 3u), back4, sizeof(back4));
    ASSERT(w_over == HAL_ERR_INVALID && e_over == HAL_ERR_INVALID && r_over == HAL_ERR_INVALID,
           "(MAX-3, 4) is HAL_ERR_INVALID");
    ASSERT(flash_fault_count() == 3u && store_equals(snap, snap_len),
           "one HAL fault per call; the store is untouched");

    /* Zero length at capacity: a no-op, the store is not rewritten. */
    struct stat st_before, st_after;
    bool have_before = stat(g_store, &st_before) == 0;
    obs__reset();
    int w_zero = hal_flash_write((uint32_t)max, data, 0u);
    int e_zero = hal_flash_erase((uint32_t)max, 0u);
    int r_zero = hal_flash_read((uint32_t)max, buf, 0u);
    bool same_file = have_before && stat(g_store, &st_after) == 0
                  && st_after.st_ino == st_before.st_ino;
    ASSERT(w_zero == HAL_OK && e_zero == HAL_OK && r_zero == HAL_OK,
           "(MAX, 0) is HAL_OK");
    ASSERT(same_file && flash_fault_count() == 0u, "(MAX, 0) does not rewrite the store");

    /* Zero length beyond capacity: refused. */
    obs__reset();
    int w_beyond = hal_flash_write((uint32_t)(max + 1u), data, 0u);
    int e_beyond = hal_flash_erase((uint32_t)(max + 1u), 0u);
    int r_beyond = hal_flash_read((uint32_t)(max + 1u), buf, 0u);
    ASSERT(w_beyond == HAL_ERR_INVALID && e_beyond == HAL_ERR_INVALID
           && r_beyond == HAL_ERR_INVALID && flash_fault_count() == 3u,
           "(MAX+1, 0) is HAL_ERR_INVALID, one fault each");

    ASSERT(seeded, "the store was seeded");
    teardown();
}

/* ---------------------------------------------------------------------------
 * main
 * ------------------------------------------------------------------------- */

int main(void)
{
    const char *home = getenv("HOME");
    const char *nvm  = getenv("EMBEDIQ_NVM_PATH");
    g_had_home = home != NULL && strlen(home) < sizeof(g_saved_home);
    g_had_nvm  = nvm  != NULL && strlen(nvm)  < sizeof(g_saved_nvm);
    if (g_had_home) memcpy(g_saved_home, home, strlen(home) + 1u);
    if (g_had_nvm)  memcpy(g_saved_nvm,  nvm,  strlen(nvm)  + 1u);

    embediq_obs_set_transport(EMBEDIQ_OBS_TRANSPORT_TCP);   /* ring only, no output */

    test_absent_store_reads_empty_ok();
    test_unreadable_store_read_returns_error();
    test_unreadable_store_write_does_not_replace_it();
    test_oversized_store_is_refused_not_truncated();
    test_write_failure_leaves_store_and_no_tmp();
    test_erased_region_reads_erased_value();
    test_short_store_reads_zero_beyond_length();
    test_directory_sync_failure_reports_after_publication();
    test_env_path_whose_temp_suffix_does_not_fit_is_refused();
    test_default_path_that_does_not_fit_is_refused();
    test_ranges_are_checked_without_wrapping();   /* last: on a wrapping HAL it crashes */

    embediq_obs_set_transport(EMBEDIQ_OBS_TRANSPORT_NULL);

    printf("\n");
    if (g_tests_failed == 0) {
        printf("All %d tests passed. (0 failed)\n", g_tests_run);
    } else {
        printf("%d of %d tests FAILED.\n", g_tests_failed, g_tests_run);
    }
    return (g_tests_failed > 0) ? 1 : 0;
}
