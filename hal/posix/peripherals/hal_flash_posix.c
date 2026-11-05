#define _POSIX_C_SOURCE 200809L

/*
 * hal/posix/hal_flash_posix.c — POSIX file-backed flash simulation
 *
 * Implements the hal_flash.h contract using a flat binary file on the host
 * filesystem.  Storage path is controlled by the EMBEDIQ_NVM_PATH env var;
 * if unset, defaults to $HOME/.embediq/nvm_store.bin.
 *
 * The store is one flat backing file holding the whole partition: a read
 * copies bytes out of it, and a write or erase reads the whole file, changes
 * the region and writes the whole file back. Writes are atomic: data is
 * written to <path>.tmp, flushed and fsync'd, then rename(2) moves it over
 * the original and the containing directory is fsync'd, so the new entry is
 * durable too. A failure before the rename removes the temp file and leaves
 * the original untouched; a directory-sync failure after the rename returns
 * HAL_ERR_IO with the new contents in place but their durability uncertain.
 * The durability promise assumes the store's directory already exists; the
 * directory is created on first use as a convenience, and the durability of
 * its own entry is not promised. Ranges are checked by subtraction against
 * HAL_FLASH_POSIX_MAX_SIZE before any arithmetic, and a store or temp path
 * that does not fit PATH_BUF is refused, never shortened. An absent backing file (ENOENT) is an empty store and reads as
 * zeros; any other open or read failure is HAL_ERR_IO, and a backing file
 * larger than HAL_FLASH_POSIX_MAX_SIZE is HAL_ERR_INVALID — a write or erase
 * then fails without touching the store. To exercise the failures: point
 * EMBEDIQ_NVM_PATH at a directory for a real read error (fopen succeeds,
 * fread fails), or use the host hooks below (hal_flash__fail_next_read /
 * hal_flash__fail_next_write) for a counted number of failures.
 *
 * Only includes: hal_flash.h (core/include/hal) and C stdlib/POSIX headers.
 * Zero embediq_*.h includes — HAL has no knowledge of the framework.
 * R-02: no malloc in this file.
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "hal_flash.h"

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>      /* ENOENT */
#include <stdlib.h>     /* getenv */
#include <sys/stat.h>   /* mkdir  */
#include <fcntl.h>      /* open — directory sync */
#include <unistd.h>     /* fsync, close — POSIX HAL includes */
#include <limits.h>     /* PATH_MAX */

/* ---------------------------------------------------------------------------
 * Constants
 * ------------------------------------------------------------------------- */

#define HAL_FLASH_PAGE_SIZE   256u
#define PATH_BUF              PATH_MAX

/* POSIX-only: 64 KiB file buffer for simulating flash erase/write operations.
 * Never used on MCU targets — real HAL implementations use direct flash access. */
#define HAL_FLASH_POSIX_MAX_SIZE  (64u * 1024u)

/* ---------------------------------------------------------------------------
 * Path helpers
 * ------------------------------------------------------------------------- */

static char s_path[PATH_BUF];
static char s_tmp_path[PATH_BUF];

/* True when the last resolve_paths() would have truncated a path: a truncated
 * temp path could alias the store path, so every operation is refused. */
static bool s_path_invalid = false;

#ifdef EMBEDIQ_PLATFORM_HOST
/* Test-only: hal_flash_write calls attempted, and how many of the next
 * calls fail with HAL_ERR_IO without touching the file. Written by the test
 * thread between operations; the NVM persist lock serialises the writers. */
static uint32_t s_write_calls  = 0u;
static uint32_t s_fail_writes  = 0u;
static uint32_t s_fail_reads   = 0u;   /* next n hal_flash_read calls fail */
static uint32_t s_fail_dir_syncs = 0u; /* next n directory syncs fail */
#endif

/**
 * Ensure parent directory exists (one level only).
 * Scans backwards for '/' and mkdir everything up to that point.
 */
static void ensure_parent_dir(const char *path)
{
    char dir[PATH_BUF];
    strncpy(dir, path, PATH_BUF - 1u);
    dir[PATH_BUF - 1u] = '\0';

    /* Find last slash. */
    char *slash = strrchr(dir, '/');
    if (!slash || slash == dir) return;
    *slash = '\0';
    mkdir(dir, 0755);   /* ignore EEXIST */
}

/** True when snprintf's result rc means the output was cut short (or failed). */
static bool truncated(int rc)
{
    return rc < 0 || (size_t)rc >= PATH_BUF;
}

/**
 * Resolve paths from EMBEDIQ_NVM_PATH env var (or default).
 * Re-reads the env var every call so tests can change it via setenv().
 * Sets s_path_invalid when either path would not fit in PATH_BUF.
 */
static void resolve_paths(void)
{
    const char *env = getenv("EMBEDIQ_NVM_PATH");
    if (env && env[0] != '\0') {
        int rc_path = snprintf(s_path,     PATH_BUF, "%s", env);
        int rc_tmp  = snprintf(s_tmp_path, PATH_BUF, "%s.tmp", env);
        s_path_invalid = truncated(rc_path) || truncated(rc_tmp);
    } else {
        const char *home = getenv("HOME");
        if (!home) home = "/tmp";

        /* The whole of HOME, never a shortened prefix: a path that does not
         * fit is refused before anything is created. */
        char dir[PATH_BUF];
        int rc_dir  = snprintf(dir,        PATH_BUF, "%s/.embediq", home);
        int rc_path = snprintf(s_path,     PATH_BUF, "%s/.embediq/nvm_store.bin", home);
        int rc_tmp  = snprintf(s_tmp_path, PATH_BUF, "%s/.embediq/nvm_store.bin.tmp", home);
        s_path_invalid = truncated(rc_dir) || truncated(rc_path) || truncated(rc_tmp);
        if (!s_path_invalid) {
            mkdir(dir, 0755);   /* ignore EEXIST */
        }
    }
}

/** True when [addr, addr+len) lies within the store's capacity; checked by
 *  subtraction so no sum can wrap. */
static bool range_ok(uint32_t addr, size_t len)
{
    return (size_t)addr <= HAL_FLASH_POSIX_MAX_SIZE
        && len <= HAL_FLASH_POSIX_MAX_SIZE - (size_t)addr;
}

/* ---------------------------------------------------------------------------
 * File helpers
 * ------------------------------------------------------------------------- */

/**
 * Read the entire backing file into buf (up to buf_size bytes); *out_len is
 * the number of bytes read.
 * HAL_OK: the file was read in full, or does not exist (ENOENT: an empty
 * store, *out_len = 0). HAL_ERR_IO: any other open failure, or a read error.
 * HAL_ERR_INVALID: the file holds more than buf_size bytes, so a rewrite from
 * buf would truncate it. Each failure emits its one HAL observation here.
 */
static int read_file(void *buf, size_t buf_size, size_t *out_len)
{
    *out_len = 0u;
    FILE *f = fopen(s_path, "rb");
    if (!f) {
        if (errno == ENOENT) return HAL_OK;   /* absent store: empty */
        EMBEDIQ_HAL_OBS_EMIT_ERROR(EMBEDIQ_HAL_SRC_FLASH, HAL_ERR_IO);
        return HAL_ERR_IO;
    }
    size_t n  = fread(buf, 1u, buf_size, f);
    int    rc = HAL_OK;
    if (ferror(f)) {
        rc = HAL_ERR_IO;
    } else if (n == buf_size) {
        /* A full buffer: the store is too large if any byte remains. */
        if (fgetc(f) != EOF)  rc = HAL_ERR_INVALID;
        else if (ferror(f))   rc = HAL_ERR_IO;
    }
    (void)fclose(f);   /* read-only stream: nothing buffered to lose */
    if (rc != HAL_OK) {
        EMBEDIQ_HAL_OBS_EMIT_ERROR(EMBEDIQ_HAL_SRC_FLASH, rc);
        return rc;
    }
    *out_len = n;
    return HAL_OK;
}

/**
 * fsync the directory that holds the store, so the entry the rename created
 * reaches the medium. Returns true on success.
 */
static bool sync_store_dir(void)
{
#ifdef EMBEDIQ_PLATFORM_HOST
    if (s_fail_dir_syncs > 0u) {
        s_fail_dir_syncs--;
        return false;
    }
#endif
    char dir[PATH_BUF];
    const char *slash = strrchr(s_path, '/');
    if (slash == NULL) {
        dir[0] = '.'; dir[1] = '\0';
    } else if (slash == s_path) {
        dir[0] = '/'; dir[1] = '\0';
    } else {
        size_t n = (size_t)(slash - s_path);   /* < PATH_BUF: s_path fits */
        memcpy(dir, s_path, n);
        dir[n] = '\0';
    }
    int fd = open(dir, O_RDONLY | O_DIRECTORY);
    if (fd < 0) return false;
    bool ok = fsync(fd) == 0;
    if (close(fd) != 0) ok = false;
    return ok;
}

/**
 * Atomically write buf[0..len) to the backing file: write <path>.tmp, flush,
 * fsync and close it, rename it over the store, then fsync the directory.
 * The rename is the publication point. A failure before it removes the temp
 * file and leaves the store as it was. A directory-sync failure after it
 * returns HAL_ERR_IO with the new store in place but its durability
 * uncertain; no rollback is attempted, since an unproven rollback is worse
 * than an honest error. Each failure emits one HAL observation, at the
 * failing step, and returns HAL_ERR_IO.
 */
static int write_file(const void *buf, size_t len)
{
    ensure_parent_dir(s_tmp_path);
    FILE *f = fopen(s_tmp_path, "wb");
    if (!f) { EMBEDIQ_HAL_OBS_EMIT_ERROR(EMBEDIQ_HAL_SRC_FLASH, HAL_ERR_IO); return HAL_ERR_IO; }

    /* A buffered write reports ENOSPC/EIO at flush or close, not at fwrite. */
    bool ok = fwrite(buf, 1u, len, f) == len;
    ok = ok && (fflush(f) == 0);
    ok = ok && (fsync(fileno(f)) == 0);   /* durable before it replaces the store */
    if (fclose(f) != 0) ok = false;
    if (ok && rename(s_tmp_path, s_path) != 0) ok = false;

    if (!ok) {
        (void)remove(s_tmp_path);   /* never left behind, never renamed in */
        EMBEDIQ_HAL_OBS_EMIT_ERROR(EMBEDIQ_HAL_SRC_FLASH, HAL_ERR_IO);
        return HAL_ERR_IO;
    }

    /* Published. The new store is visible; report, do not undo, a failure
     * to make its directory entry durable. */
    if (!sync_store_dir()) {
        EMBEDIQ_HAL_OBS_EMIT_ERROR(EMBEDIQ_HAL_SRC_FLASH, HAL_ERR_IO);
        return HAL_ERR_IO;
    }
    return HAL_OK;
}

/* ---------------------------------------------------------------------------
 * HAL flash contract implementation
 * ------------------------------------------------------------------------- */

int hal_flash_read(uint32_t addr, void *buf, size_t len)
{
    if (!buf || !range_ok(addr, len)) { EMBEDIQ_HAL_OBS_EMIT_ERROR(EMBEDIQ_HAL_SRC_FLASH, HAL_ERR_INVALID); return HAL_ERR_INVALID; }
    if (len == 0u) return HAL_OK;   /* nothing to read */
    resolve_paths();
#ifdef EMBEDIQ_PLATFORM_HOST
    if (s_fail_reads > 0u) {
        s_fail_reads--;
        EMBEDIQ_HAL_OBS_EMIT_ERROR(EMBEDIQ_HAL_SRC_FLASH, HAL_ERR_IO);
        return HAL_ERR_IO;
    }
#endif
    if (s_path_invalid) { EMBEDIQ_HAL_OBS_EMIT_ERROR(EMBEDIQ_HAL_SRC_FLASH, HAL_ERR_INVALID); return HAL_ERR_INVALID; }

    /* Read current file contents. On failure buf is left unspecified;
     * read_file emitted the one observation for it (hal_defs.h). */
    uint8_t file_buf[HAL_FLASH_POSIX_MAX_SIZE];   /* 64 KiB — generous for NVM store */
    size_t file_len = 0u;
    int rc = read_file(file_buf, sizeof(file_buf), &file_len);
    if (rc != HAL_OK) return rc;

    /* Bytes beyond the stored length read as zeros. */
    memset(buf, 0, len);
    if (file_len > (size_t)addr) {
        size_t avail = file_len - (size_t)addr;
        size_t copy  = (avail < len) ? avail : len;
        memcpy(buf, file_buf + addr, copy);
    }

    return HAL_OK;
}

int hal_flash_write(uint32_t addr, const void *buf, size_t len)
{
    if (!buf || !range_ok(addr, len)) { EMBEDIQ_HAL_OBS_EMIT_ERROR(EMBEDIQ_HAL_SRC_FLASH, HAL_ERR_INVALID); return HAL_ERR_INVALID; }
    if (len == 0u) return HAL_OK;   /* no-op: the store is not rewritten */
#ifdef EMBEDIQ_PLATFORM_HOST
    s_write_calls++;
    if (s_fail_writes > 0u) {
        s_fail_writes--;
        EMBEDIQ_HAL_OBS_EMIT_ERROR(EMBEDIQ_HAL_SRC_FLASH, HAL_ERR_IO);
        return HAL_ERR_IO;
    }
#endif
    resolve_paths();
    if (s_path_invalid) { EMBEDIQ_HAL_OBS_EMIT_ERROR(EMBEDIQ_HAL_SRC_FLASH, HAL_ERR_INVALID); return HAL_ERR_INVALID; }

    /* Read current file (may be absent). A failed or oversized read returns
     * without writing: the store is never rebuilt from what could not be
     * read. read_file emitted the one observation for it (hal_defs.h). */
    uint8_t file_buf[HAL_FLASH_POSIX_MAX_SIZE];
    memset(file_buf, 0, sizeof(file_buf));
    size_t file_len = 0u;
    int rc = read_file(file_buf, sizeof(file_buf), &file_len);
    if (rc != HAL_OK) return rc;

    /* Required file size after this write; range_ok() bounded it. */
    size_t needed = (size_t)addr + len;
    if (needed > file_len) file_len = needed;

    /* Overlay the write region. */
    memcpy(file_buf + addr, buf, len);

    return write_file(file_buf, file_len);
}

int hal_flash_erase(uint32_t addr, size_t len)
{
    if (!range_ok(addr, len)) { EMBEDIQ_HAL_OBS_EMIT_ERROR(EMBEDIQ_HAL_SRC_FLASH, HAL_ERR_INVALID); return HAL_ERR_INVALID; }
    if (len == 0u) return HAL_OK;   /* no-op: the store is not rewritten */
    resolve_paths();
    if (s_path_invalid) { EMBEDIQ_HAL_OBS_EMIT_ERROR(EMBEDIQ_HAL_SRC_FLASH, HAL_ERR_INVALID); return HAL_ERR_INVALID; }

    /* Read current file; on failure return without writing, as in
     * hal_flash_write. read_file emitted the one observation (hal_defs.h). */
    uint8_t file_buf[HAL_FLASH_POSIX_MAX_SIZE];
    memset(file_buf, 0, sizeof(file_buf));
    size_t file_len = 0u;
    int rc = read_file(file_buf, sizeof(file_buf), &file_len);
    if (rc != HAL_OK) return rc;

    /* Fill the erased region with 0xFF (real flash erase state); range_ok()
     * bounded the sum. */
    size_t needed = (size_t)addr + len;
    if (needed > file_len) file_len = needed;

    memset(file_buf + addr, 0xFF, len);

    return write_file(file_buf, file_len);
}

uint32_t hal_flash_page_size(void)
{
    return HAL_FLASH_PAGE_SIZE;
}

#ifdef EMBEDIQ_PLATFORM_HOST
/** The next n hal_flash_write calls fail with HAL_ERR_IO (0 = none; test use only). */
void hal_flash__fail_next_write(uint32_t n)
{
    s_fail_writes = n;
}

/** The next n hal_flash_read calls fail with HAL_ERR_IO (0 = none; test use only). */
void hal_flash__fail_next_read(uint32_t n)
{
    s_fail_reads = n;
}

/** The next n directory syncs after a rename fail (0 = none; test use only):
 *  the write is published and then reports HAL_ERR_IO. */
void hal_flash__fail_next_dir_fsync(uint32_t n)
{
    s_fail_dir_syncs = n;
}

/** hal_flash_write calls attempted since the process started (test use only). */
uint32_t hal_flash__write_calls(void)
{
    return s_write_calls;
}
#endif
