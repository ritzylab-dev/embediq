#define _POSIX_C_SOURCE 200809L

/*
 * platform/posix/fb_nvm.c — NVM Platform FB (host file-backed implementation)
 *
 * Provides a simple key-value persistent store backed by a JSON file at
 * ~/.embediq/nvm_store.json.  All storage is static (R-02: no malloc).
 *
 * In-memory cache: EMBEDIQ_MAX_ENDPOINTS (64) entries, each holding:
 *   - key:       char[EMBEDIQ_MSG_MAX_PAYLOAD]   (max 63 usable chars + NUL)
 *   - val:       uint8_t[EMBEDIQ_MSG_MAX_PAYLOAD] (up to 64 bytes)
 *   - val_len:   uint16_t
 *   - schema_id: uint16_t
 *
 * Persistence:
 *   Writes are atomic: data is written to nvm_store.json.tmp first, then
 *   rename(2) (POSIX-atomic) moves it over nvm_store.json.  A power-loss
 *   during the rename leaves either the old file or the new file intact.
 *
 * JSON file format (written by this module; self-describing):
 *   {"v":1,"e":[
 *   {"k":"key1","s":1,"l":3,"d":"AABBCC"},
 *   {"k":"key2","s":2,"l":2,"d":"0102"}
 *   ]}
 *
 * Boot phase: EMBEDIQ_BOOT_PHASE_INFRASTRUCTURE (2)
 *
 * Public API:
 *   fb_nvm_register()                           — register FB
 *   embediq_nvm_get(key, buf, buf_len, ...)      — lookup value
 *   embediq_nvm_set(key, val, len, schema_id)    — store value
 *   embediq_nvm_delete(key)                      — remove key
 *   embediq_nvm_reset_defaults()                 — wipe all entries
 *   embediq_nvm_get_schema_version(key, out)     — query schema ID
 *
 * Host/test-only API (EMBEDIQ_PLATFORM_HOST):
 *   nvm__init_state()   — clear cache + delete disk files + create mutex
 *   nvm__set_path(path) — override file path (tests use /tmp)
 *   nvm__get_path()     — return active .json path
 *   nvm__get_tmp_path() — return active .tmp path
 *
 * R-02: no malloc in this file.
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "embediq_platform_msgs.h"
#include "embediq_fb.h"
#include "embediq_osal.h"
#include "embediq_config.h"
#include "embediq_obs.h"

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>     /* getenv */
#include <sys/stat.h>   /* mkdir */

/* ---------------------------------------------------------------------------
 * Sizing constants (all derived from embediq_config.h — I-08)
 * ------------------------------------------------------------------------- */

#define NVM_KEY_SIZE      EMBEDIQ_MSG_MAX_PAYLOAD       /* 64: max key length  */
#define NVM_VAL_SIZE      EMBEDIQ_MSG_MAX_PAYLOAD       /* 64: max value bytes */
#define NVM_MAX_KEYS      EMBEDIQ_MAX_ENDPOINTS         /* 64: max entries     */

/*
 * Path buffer: NVM_KEY_SIZE * 3 = 192 bytes — plenty for
 * /home/<username>/.embediq/nvm_store.json (longest reasonable path ≪ 192).
 */
#define NVM_PATH_BUF      (NVM_KEY_SIZE * 3u)

/*
 * Per-entry JSON string: key (≤63) + 2*val hex (≤128) + field names/brackets.
 * NVM_KEY_SIZE * 4 = 256 chars is a conservative upper bound per entry.
 */
#define NVM_ENTRY_CHARS   (NVM_KEY_SIZE * 4u)

/*
 * File buffer: one entry per slot plus header/footer overhead.
 */
#define NVM_FILE_BUF_SIZE (NVM_MAX_KEYS * NVM_ENTRY_CHARS + NVM_KEY_SIZE)

/* ---------------------------------------------------------------------------
 * Internal types
 * ------------------------------------------------------------------------- */

typedef struct {
    char     key[NVM_KEY_SIZE];
    uint8_t  val[NVM_VAL_SIZE];
    uint16_t val_len;
    uint16_t schema_id;
    bool     active;
} nvm_entry_t;

/* ---------------------------------------------------------------------------
 * Module state (static, R-02)
 * ------------------------------------------------------------------------- */

static nvm_entry_t      g_nvm[NVM_MAX_KEYS];
static EmbedIQ_Mutex_t *g_mutex       = NULL;
static EmbedIQ_FB_Handle_t g_nvm_fb   = NULL;

/* File paths — set during init */
static char g_nvm_path[NVM_PATH_BUF];
static char g_nvm_tmp_path[NVM_PATH_BUF];

/* Shared file I/O buffer — static to avoid large stack allocation. */
static char g_file_buf[NVM_FILE_BUF_SIZE];

/* ---------------------------------------------------------------------------
 * Internal helpers
 * ------------------------------------------------------------------------- */

/** Find a slot by key; returns index or NVM_MAX_KEYS on miss. */
static uint8_t find_slot(const char *key)
{
    for (uint8_t i = 0u; i < NVM_MAX_KEYS; i++) {
        if (g_nvm[i].active && strncmp(g_nvm[i].key, key, NVM_KEY_SIZE) == 0) {
            return i;
        }
    }
    return NVM_MAX_KEYS;   /* sentinel: not found */
}

/** Find a free slot; returns NVM_MAX_KEYS if the store is full. */
static uint8_t find_free_slot(void)
{
    for (uint8_t i = 0u; i < NVM_MAX_KEYS; i++) {
        if (!g_nvm[i].active) return i;
    }
    return NVM_MAX_KEYS;
}

/* ---------------------------------------------------------------------------
 * Persistence: write in-memory cache to disk (atomic via tmp + rename)
 * ------------------------------------------------------------------------- */

static void nvm_persist(void)
{
    int pos = 0;
    int remaining = (int)NVM_FILE_BUF_SIZE;

    pos += snprintf(g_file_buf + pos, (size_t)remaining,
                    "{\"v\":1,\"e\":[\n");
    remaining -= pos;

    bool first = true;
    for (uint8_t i = 0u; i < NVM_MAX_KEYS; i++) {
        if (!g_nvm[i].active) continue;

        /* Hex-encode the value bytes. */
        char hex[NVM_VAL_SIZE * 2u + 1u];
        for (uint16_t j = 0u; j < g_nvm[i].val_len; j++) {
            snprintf(hex + (int)(j * 2u), 3, "%02X",
                     (unsigned)g_nvm[i].val[j]);
        }
        hex[g_nvm[i].val_len * 2u] = '\0';

        int n = snprintf(g_file_buf + pos, (size_t)(NVM_FILE_BUF_SIZE - pos),
                         "%s{\"k\":\"%s\",\"s\":%u,\"l\":%u,\"d\":\"%s\"}",
                         first ? "" : ",\n",
                         g_nvm[i].key,
                         (unsigned)g_nvm[i].schema_id,
                         (unsigned)g_nvm[i].val_len,
                         hex);
        if (n > 0) pos += n;
        first = false;
    }

    int n = snprintf(g_file_buf + pos, (size_t)(NVM_FILE_BUF_SIZE - pos),
                     "\n]}");
    if (n > 0) pos += n;

    /* Atomic write: tmp → rename */
    FILE *f = fopen(g_nvm_tmp_path, "wb");
    if (!f) return;
    fwrite(g_file_buf, 1u, (size_t)pos, f);
    fclose(f);
    rename(g_nvm_tmp_path, g_nvm_path);
}

/* ---------------------------------------------------------------------------
 * Persistence: load from disk into the in-memory cache
 * ------------------------------------------------------------------------- */

static void nvm_load(void)
{
    FILE *f = fopen(g_nvm_path, "rb");
    if (!f) return;   /* file may not exist yet — that is normal */

    size_t n = fread(g_file_buf, 1u, NVM_FILE_BUF_SIZE - 1u, f);
    fclose(f);
    g_file_buf[n] = '\0';

    /*
     * Walk the buffer looking for entry objects.
     * Expected pattern per entry:  {"k":"KEY","s":S,"l":L,"d":"HEX"}
     */
    char *p = g_file_buf;
    while ((p = strstr(p, "\"k\":\"")) != NULL) {
        p += 5;   /* advance past "k":"  */

        /* Extract key — stop at closing '"'. */
        char key[NVM_KEY_SIZE];
        int  ki = 0;
        while (*p && *p != '"' && ki < (int)(NVM_KEY_SIZE) - 1) {
            key[ki++] = *p++;
        }
        key[ki] = '\0';
        if (*p != '"') continue;
        p++;

        /* Extract schema_id. */
        char *s_pos = strstr(p, "\"s\":");
        if (!s_pos) continue;
        uint16_t schema_id = 0u;
        {
            const char *sp = s_pos + 4;
            while (*sp >= '0' && *sp <= '9') {
                schema_id = (uint16_t)(schema_id * 10u + (uint16_t)(*sp - '0'));
                sp++;
            }
        }

        /* Extract value length. */
        char *l_pos = strstr(p, "\"l\":");
        if (!l_pos) continue;
        uint16_t val_len = 0u;
        {
            const char *lp = l_pos + 4;
            while (*lp >= '0' && *lp <= '9') {
                val_len = (uint16_t)(val_len * 10u + (uint16_t)(*lp - '0'));
                lp++;
            }
        }
        if (val_len > NVM_VAL_SIZE) continue;

        /* Extract hex-encoded data. */
        char *d_pos = strstr(p, "\"d\":\"");
        if (!d_pos) continue;
        d_pos += 5;

        uint8_t val[NVM_VAL_SIZE];
        for (uint16_t j = 0u; j < val_len; j++) {
            unsigned byte = 0u;
            const char *hp = d_pos + (int)(j * 2u);
            /* Decode one hex byte manually (no sscanf needed). */
            for (int nibble = 0; nibble < 2; nibble++) {
                char c = hp[nibble];
                byte <<= 4u;
                if (c >= '0' && c <= '9')      byte |= (unsigned)(c - '0');
                else if (c >= 'A' && c <= 'F') byte |= (unsigned)(c - 'A' + 10);
                else if (c >= 'a' && c <= 'f') byte |= (unsigned)(c - 'a' + 10);
            }
            val[j] = (uint8_t)byte;
        }

        /* Store in cache (skip if full or key already present). */
        uint8_t slot = find_slot(key);
        if (slot == NVM_MAX_KEYS) {
            slot = find_free_slot();
            if (slot == NVM_MAX_KEYS) break;   /* store full */
            strncpy(g_nvm[slot].key, key, NVM_KEY_SIZE - 1u);
            g_nvm[slot].key[NVM_KEY_SIZE - 1u] = '\0';
            g_nvm[slot].active = true;
        }
        memcpy(g_nvm[slot].val, val, val_len);
        g_nvm[slot].val_len   = val_len;
        g_nvm[slot].schema_id = schema_id;

        p = d_pos;   /* advance past this entry's data field */
    }
}

/* ---------------------------------------------------------------------------
 * FB init / exit (called by embediq_engine_boot)
 * ------------------------------------------------------------------------- */

static void nvm_init(EmbedIQ_FB_Handle_t fb, void *fb_data)
{
    (void)fb_data;
    g_nvm_fb = fb;

    if (!g_mutex) {
        g_mutex = embediq_osal_mutex_create();
    }

    /* Build the storage directory and file paths. */
    const char *home = getenv("HOME");
    if (!home) home = "/tmp";

    char dir[NVM_PATH_BUF];
    snprintf(dir, NVM_PATH_BUF, "%s/.embediq", home);
    mkdir(dir, 0755);   /* ignore EEXIST */

    snprintf(g_nvm_path,     NVM_PATH_BUF, "%s/nvm_store.json",     dir);
    snprintf(g_nvm_tmp_path, NVM_PATH_BUF, "%s/nvm_store.json.tmp", dir);

    nvm_load();
}

static void nvm_exit(EmbedIQ_FB_Handle_t fb, void *fb_data)
{
    (void)fb; (void)fb_data;
    /* Nothing to tear down — the file is written on every set/delete. */
}

/* ---------------------------------------------------------------------------
 * Public: fb_nvm_register()
 * Call before embediq_engine_boot().
 * ------------------------------------------------------------------------- */

EmbedIQ_FB_Handle_t fb_nvm_register(void)
{
    static const EmbedIQ_FB_Config_t k_cfg = {
        .name       = "fb_nvm",
        .boot_phase = EMBEDIQ_BOOT_PHASE_INFRASTRUCTURE,
        .init_fn    = nvm_init,
        .exit_fn    = nvm_exit,
    };
    return embediq_fb_register(&k_cfg);
}

/* ---------------------------------------------------------------------------
 * Public NVM API
 * ------------------------------------------------------------------------- */

/**
 * Get the value for a key.
 * @return  0 on success; -1 if key not found or buf_len too small.
 */
int embediq_nvm_get(const char *key, uint8_t *buf, uint16_t buf_len,
                    uint16_t *len_out, uint16_t *schema_out)
{
    if (!key || !buf) return -1;
    if (!g_mutex) return -1;

    embediq_osal_mutex_lock(g_mutex, 0u);

    uint8_t slot = find_slot(key);
    if (slot == NVM_MAX_KEYS) {
        embediq_osal_mutex_unlock(g_mutex);
        return -1;   /* key not found */
    }
    if (g_nvm[slot].val_len > buf_len) {
        embediq_osal_mutex_unlock(g_mutex);
        return -1;   /* buffer too small */
    }

    memcpy(buf, g_nvm[slot].val, g_nvm[slot].val_len);
    if (len_out)    *len_out    = g_nvm[slot].val_len;
    if (schema_out) *schema_out = g_nvm[slot].schema_id;

    embediq_osal_mutex_unlock(g_mutex);
    return 0;
}

/**
 * Store a key-value pair and persist to disk.
 * @return  0 on success; -1 on invalid args or if the store is full.
 */
int embediq_nvm_set(const char *key, const uint8_t *val, uint16_t len,
                    uint16_t schema_id)
{
    if (!key || !val) return -1;
    if (len > NVM_VAL_SIZE)            return -1;
    if (strlen(key) >= NVM_KEY_SIZE)   return -1;
    if (!g_mutex) return -1;

    embediq_osal_mutex_lock(g_mutex, 0u);

    uint8_t slot = find_slot(key);
    if (slot == NVM_MAX_KEYS) {
        slot = find_free_slot();
        if (slot == NVM_MAX_KEYS) {
            embediq_osal_mutex_unlock(g_mutex);
            return -1;   /* store full */
        }
        strncpy(g_nvm[slot].key, key, NVM_KEY_SIZE - 1u);
        g_nvm[slot].key[NVM_KEY_SIZE - 1u] = '\0';
        g_nvm[slot].active = true;
    }

    memcpy(g_nvm[slot].val, val, len);
    g_nvm[slot].val_len   = len;
    g_nvm[slot].schema_id = schema_id;

    nvm_persist();

    embediq_osal_mutex_unlock(g_mutex);
    return 0;
}

/**
 * Delete a key.
 * @return  0 on success; -1 if key not found.
 */
int embediq_nvm_delete(const char *key)
{
    if (!key) return -1;
    if (!g_mutex) return -1;

    embediq_osal_mutex_lock(g_mutex, 0u);

    uint8_t slot = find_slot(key);
    if (slot == NVM_MAX_KEYS) {
        embediq_osal_mutex_unlock(g_mutex);
        return -1;
    }

    memset(&g_nvm[slot], 0, sizeof(nvm_entry_t));
    nvm_persist();

    embediq_osal_mutex_unlock(g_mutex);
    return 0;
}

/** Remove all key-value pairs and persist the empty store. */
void embediq_nvm_reset_defaults(void)
{
    if (!g_mutex) return;

    embediq_osal_mutex_lock(g_mutex, 0u);
    memset(g_nvm, 0, sizeof(g_nvm));
    nvm_persist();
    embediq_osal_mutex_unlock(g_mutex);
}

/**
 * Get only the schema version for a key (lighter-weight than nvm_get).
 * @return  0 on success; -1 if key not found.
 */
int embediq_nvm_get_schema_version(const char *key, uint16_t *schema_out)
{
    if (!key || !schema_out) return -1;
    if (!g_mutex) return -1;

    embediq_osal_mutex_lock(g_mutex, 0u);

    uint8_t slot = find_slot(key);
    if (slot == NVM_MAX_KEYS) {
        embediq_osal_mutex_unlock(g_mutex);
        return -1;
    }
    *schema_out = g_nvm[slot].schema_id;

    embediq_osal_mutex_unlock(g_mutex);
    return 0;
}

/* ---------------------------------------------------------------------------
 * Test-only API — EMBEDIQ_PLATFORM_HOST only
 * ------------------------------------------------------------------------- */

#ifdef EMBEDIQ_PLATFORM_HOST

/**
 * Override the NVM file path.
 * Call before nvm__init_state() so that tests use an isolated location
 * (e.g. /tmp/test_nvm.json) instead of ~/.embediq/nvm_store.json.
 */
void nvm__set_path(const char *path)
{
    if (!path) return;
    snprintf(g_nvm_path,     NVM_PATH_BUF, "%s", path);
    snprintf(g_nvm_tmp_path, NVM_PATH_BUF, "%s.tmp", path);
}

/**
 * Reset all NVM state: clear in-memory cache, delete disk files, create mutex.
 * Does NOT call fb_engine_boot or register an FB.
 */
void nvm__init_state(void)
{
    memset(g_nvm, 0, sizeof(g_nvm));
    g_nvm_fb = NULL;

    /* Remove any leftover files at the configured path. */
    if (g_nvm_path[0] != '\0') {
        remove(g_nvm_path);
        remove(g_nvm_tmp_path);
    }

    if (!g_mutex) {
        g_mutex = embediq_osal_mutex_create();
    }
}

const char *nvm__get_path(void)     { return g_nvm_path;     }
const char *nvm__get_tmp_path(void) { return g_nvm_tmp_path; }

#endif /* EMBEDIQ_PLATFORM_HOST */
