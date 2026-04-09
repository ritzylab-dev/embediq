#define _POSIX_C_SOURCE 200809L

/*
 * hal/posix/hal_flash_posix.c — POSIX file-backed flash simulation
 *
 * Implements the hal_flash.h contract using a flat binary file on the host
 * filesystem.  Storage path is controlled by the EMBEDIQ_NVM_PATH env var;
 * if unset, defaults to $HOME/.embediq/nvm_store.bin.
 *
 * Writes are atomic: data is written to <path>.tmp first, then rename(2)
 * moves it over the original.
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
#include <stdlib.h>     /* getenv */
#include <sys/stat.h>   /* mkdir  */
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

/**
 * Resolve paths from EMBEDIQ_NVM_PATH env var (or default).
 * Re-reads the env var every call so tests can change it via setenv().
 */
static void resolve_paths(void)
{
    const char *env = getenv("EMBEDIQ_NVM_PATH");
    if (env && env[0] != '\0') {
        snprintf(s_path,     PATH_BUF, "%s", env);
        snprintf(s_tmp_path, PATH_BUF, "%s.tmp", env);
    } else {
        const char *home = getenv("HOME");
        if (!home) home = "/tmp";

        int home_max = (int)(PATH_BUF - sizeof("/.embediq/nvm_store.bin.tmp"));
        char dir[PATH_BUF];
        snprintf(dir,          PATH_BUF, "%.*s/.embediq", home_max, home);
        mkdir(dir, 0755);

        int dir_max = (int)(PATH_BUF - sizeof("/nvm_store.bin.tmp"));
        snprintf(s_path,     PATH_BUF, "%.*s/nvm_store.bin",     dir_max, dir);
        snprintf(s_tmp_path, PATH_BUF, "%.*s/nvm_store.bin.tmp", dir_max, dir);
    }
}

/* ---------------------------------------------------------------------------
 * File helpers
 * ------------------------------------------------------------------------- */

/** Read the entire backing file into buf (up to buf_size bytes).
 *  Returns the file size, or 0 if the file doesn't exist. */
static size_t read_file(void *buf, size_t buf_size)
{
    FILE *f = fopen(s_path, "rb");
    if (!f) return 0u;
    size_t n = fread(buf, 1u, buf_size, f);
    fclose(f);
    return n;
}

/** Atomically write buf[0..len) to the backing file (tmp + rename). */
static int write_file(const void *buf, size_t len)
{
    ensure_parent_dir(s_tmp_path);
    FILE *f = fopen(s_tmp_path, "wb");
    if (!f) return HAL_ERR_IO;
    size_t written = fwrite(buf, 1u, len, f);
    fclose(f);
    if (written != len) return HAL_ERR_IO;
    if (rename(s_tmp_path, s_path) != 0) return HAL_ERR_IO;
    return HAL_OK;
}

/* ---------------------------------------------------------------------------
 * HAL flash contract implementation
 * ------------------------------------------------------------------------- */

int hal_flash_read(uint32_t addr, void *buf, size_t len)
{
    if (!buf) return HAL_ERR_INVALID;
    resolve_paths();

    /* Start with zeroed buffer (simulates erased flash). */
    memset(buf, 0, len);

    /* Read current file contents. */
    uint8_t file_buf[HAL_FLASH_POSIX_MAX_SIZE];   /* 64 KiB — generous for NVM store */
    size_t file_len = read_file(file_buf, sizeof(file_buf));

    if (file_len > (size_t)addr) {
        size_t avail = file_len - (size_t)addr;
        size_t copy  = (avail < len) ? avail : len;
        memcpy(buf, file_buf + addr, copy);
    }

    return HAL_OK;
}

int hal_flash_write(uint32_t addr, const void *buf, size_t len)
{
    if (!buf) return HAL_ERR_INVALID;
    resolve_paths();

    /* Read current file (may be empty). */
    uint8_t file_buf[HAL_FLASH_POSIX_MAX_SIZE];
    memset(file_buf, 0, sizeof(file_buf));
    size_t file_len = read_file(file_buf, sizeof(file_buf));

    /* Compute required file size after this write. */
    size_t needed = (size_t)addr + len;
    if (needed > sizeof(file_buf)) return HAL_ERR_INVALID;
    if (needed > file_len) file_len = needed;

    /* Overlay the write region. */
    memcpy(file_buf + addr, buf, len);

    return write_file(file_buf, file_len);
}

int hal_flash_erase(uint32_t addr, size_t len)
{
    resolve_paths();

    /* Read current file. */
    uint8_t file_buf[HAL_FLASH_POSIX_MAX_SIZE];
    memset(file_buf, 0, sizeof(file_buf));
    size_t file_len = read_file(file_buf, sizeof(file_buf));

    /* Fill the erased region with 0xFF (real flash erase state). */
    size_t needed = (size_t)addr + len;
    if (needed > sizeof(file_buf)) return HAL_ERR_INVALID;
    if (needed > file_len) file_len = needed;

    memset(file_buf + addr, 0xFF, len);

    return write_file(file_buf, file_len);
}

uint32_t hal_flash_page_size(void)
{
    return HAL_FLASH_PAGE_SIZE;
}
