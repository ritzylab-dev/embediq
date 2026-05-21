/*
 * fbs/drivers/fb_nvm.c — NVM Driver FB (portable)
 *
 * Provides a simple key-value persistent store using the hal_flash.h contract
 * for raw byte storage.  The entire g_nvm[] array is the persistence unit —
 * binary layout, no JSON encoding.
 *
 * In-memory cache: EMBEDIQ_NVM_MAX_KEYS (64) entries, each holding:
 *   - key:       char[EMBEDIQ_NVM_KEY_SIZE] (max 63 usable chars + NUL)
 *   - val:       uint8_t[EMBEDIQ_NVM_VAL_SIZE] (up to 64 bytes)
 *   - val_len:   uint16_t
 *   - schema_id: uint16_t
 *
 * Persistence (Item 4 PR-D blob format):
 *   On init: hal_flash_read(0, &s_persist_buf, sizeof(s_persist_buf))
 *     Verifies EIQ\0 magic and CRC-32/ISO-HDLC. Falls back to empty store
 *     on any validation failure (power-loss safe).
 *   On any write/delete: hal_flash_write(0, &s_persist_buf, sizeof(s_persist_buf))
 *     Header (16 bytes) + CRC-verified payload (8576 bytes) = 8592 bytes total.
 *     Single-call write — HAL atomicity contract (PR-A) guarantees all-or-nothing.
 *   The HAL owns where it's stored.  The FB owns the blob format and schema.
 *
 * Boot phase: EMBEDIQ_BOOT_PHASE_INFRASTRUCTURE (2)
 *
 * Public API (matches embediq_nvm.h contract exactly):
 *   fb_nvm_register()              — register FB
 *   embediq_nvm_set(key, val, len) — store value
 *   embediq_nvm_get(key, val, len) — lookup value
 *   embediq_nvm_delete(key)        — remove key
 *   embediq_nvm_flush()            — persist all pending writes
 *   embediq_nvm_reset()            — wipe all entries
 *
 * Host/test-only API (EMBEDIQ_PLATFORM_HOST):
 *   nvm__init_state()   — clear cache + create mutex
 *   nvm__load_state()   — trigger nvm_load() (simulates power-on reload)
 *
 * Zero POSIX headers.  R-02: no malloc in this file.
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "embediq_platform_msgs.h"
#include "embediq_fb.h"
#include "embediq_nvm.h"
#include "embediq_osal.h"
#include "embediq_config.h"
#include "embediq_obs.h"
#include "hal/hal_flash.h"

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "embediq_cfg_validate.h"  /* cfg_schema_entry_t, cfg_mut_t, cfg_schema_lookup() */
#include "embediq_crc.h"           /* embediq_crc32() — CRC-32/ISO-HDLC */

/* ---------------------------------------------------------------------------
 * Sizing constants (all derived from embediq_config.h — I-08)
 * ------------------------------------------------------------------------- */

#define NVM_KEY_SIZE      EMBEDIQ_NVM_KEY_SIZE          /* from embediq_config.h */
#define NVM_VAL_SIZE      EMBEDIQ_NVM_VAL_SIZE          /* from embediq_config.h */
#define NVM_MAX_KEYS      EMBEDIQ_NVM_MAX_KEYS          /* from embediq_config.h */

/* ---------------------------------------------------------------------------
 * Blob format constants — Item 4 PR-D (power-loss atomicity contract)
 * ------------------------------------------------------------------------- */

/** 4-byte magic at the start of every persisted blob. */
static const uint8_t k_nvm_blob_magic[4] = {'E', 'I', 'Q', '\0'};

/** Blob format version — increment major on breaking layout change. */
#define NVM_BLOB_VER_MAJOR  1u
#define NVM_BLOB_VER_MINOR  0u

/**
 * Blob header — 16 bytes, written before the g_nvm[] payload on every persist.
 *
 * Layout (little-endian, no struct padding on all supported targets):
 *   magic[4]        — 'E','I','Q','\0'
 *   version_major   — NVM_BLOB_VER_MAJOR
 *   version_minor   — NVM_BLOB_VER_MINOR
 *   entry_count     — NVM_MAX_KEYS (uint16_t)
 *   crc32           — CRC-32/ISO-HDLC over the g_nvm[] payload (uint32_t)
 *   reserved[4]     — zero-filled, reserved for future use
 *
 * Total: 4+1+1+2+4+4 = 16 bytes.
 */
typedef struct {
    uint8_t  magic[4];
    uint8_t  version_major;
    uint8_t  version_minor;
    uint16_t entry_count;
    uint32_t crc32;
    uint8_t  reserved[4];
} nvm_blob_header_t;

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

/**
 * Composite persist buffer — header + payload in one allocation.
 * Static (R-02: no malloc). Sized at compile time.
 * Used by both nvm_persist() (write) and nvm_load() (read) to ensure
 * a single hal_flash_write() call — the HAL's atomicity guarantee covers
 * the whole blob as one unit.
 */
static struct {
    nvm_blob_header_t hdr;
    nvm_entry_t       entries[NVM_MAX_KEYS];
} s_persist_buf;

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
 * Persistence: binary layout via HAL flash
 * ------------------------------------------------------------------------- */

static void nvm_persist(void)
{
    /* Build header. */
    s_persist_buf.hdr.magic[0]       = k_nvm_blob_magic[0];
    s_persist_buf.hdr.magic[1]       = k_nvm_blob_magic[1];
    s_persist_buf.hdr.magic[2]       = k_nvm_blob_magic[2];
    s_persist_buf.hdr.magic[3]       = k_nvm_blob_magic[3];
    s_persist_buf.hdr.version_major  = NVM_BLOB_VER_MAJOR;
    s_persist_buf.hdr.version_minor  = NVM_BLOB_VER_MINOR;
    s_persist_buf.hdr.entry_count    = (uint16_t)NVM_MAX_KEYS;
    s_persist_buf.hdr.reserved[0]    = 0u;
    s_persist_buf.hdr.reserved[1]    = 0u;
    s_persist_buf.hdr.reserved[2]    = 0u;
    s_persist_buf.hdr.reserved[3]    = 0u;

    /* Copy live cache into payload and compute CRC over payload only. */
    memcpy(s_persist_buf.entries, g_nvm, sizeof(g_nvm));
    s_persist_buf.hdr.crc32 = embediq_crc32(s_persist_buf.entries,
                                              sizeof(s_persist_buf.entries));

    /* Single atomic write — HAL atomicity contract (PR-A: hal_flash.h) ensures
     * either the full blob lands or nothing changes. */
    hal_flash_write(0u, &s_persist_buf, sizeof(s_persist_buf));
}

static void nvm_load(void)
{
    /* Read the full blob (header + payload) in a single call. */
    int rc = hal_flash_read(0u, &s_persist_buf, sizeof(s_persist_buf));
    if (rc != HAL_OK) {
        /* Flash unreadable — start with defaults (empty store). */
        memset(g_nvm, 0, sizeof(g_nvm));
        return;
    }

    /* Verify magic — catches erased flash, pre-PR-D blobs, and corrupt files. */
    if (memcmp(s_persist_buf.hdr.magic, k_nvm_blob_magic,
               sizeof(k_nvm_blob_magic)) != 0) {
        EMBEDIQ_OBS_EMIT_FAULT(EMBEDIQ_OBS_EVT_CFG_BLOB_INVALID,
                               0u, 0u, 0u, 0u);   /* state 0 = magic_fail */
        memset(g_nvm, 0, sizeof(g_nvm));
        return;
    }

    /* Verify CRC-32/ISO-HDLC over the payload. */
    uint32_t computed = embediq_crc32(s_persist_buf.entries,
                                       sizeof(s_persist_buf.entries));
    if (computed != s_persist_buf.hdr.crc32) {
        EMBEDIQ_OBS_EMIT_FAULT(EMBEDIQ_OBS_EVT_CFG_BLOB_INVALID,
                               0u, 0u, 1u, 0u);   /* state 1 = crc_fail */
        memset(g_nvm, 0, sizeof(g_nvm));
        return;
    }

    /* Blob valid — copy payload to live cache. */
    memcpy(g_nvm, s_persist_buf.entries, sizeof(g_nvm));
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
 * Public NVM API — signatures must match embediq_nvm.h exactly
 * ------------------------------------------------------------------------- */

embediq_err_t embediq_nvm_set(const char *key, const void *val, uint32_t len)
{
    if (!key || !val)                return EMBEDIQ_ERR;
    if (len > NVM_VAL_SIZE)          return EMBEDIQ_ERR;
    if (strlen(key) >= NVM_KEY_SIZE) return EMBEDIQ_ERR;
    if (!g_mutex)                    return EMBEDIQ_ERR;

    if (embediq_osal_mutex_lock(g_mutex, UINT32_MAX) != EMBEDIQ_OK) return EMBEDIQ_ERR;

    /* Mutability guard — factory-class keys are immutable at runtime.
     * IEC 62443 commissioning: fleet push cannot overwrite factory provisioning.
     * Silently reject — emit Observatory event for audit trail. */
    {
        const cfg_schema_entry_t *schema = cfg_schema_lookup(key);
        if (schema != NULL && schema->mutability == CFG_MUT_FACTORY) {
            uint16_t key_crc = (uint16_t)(embediq_crc32(key, strlen(key)) & 0xFFFFu);
            EMBEDIQ_OBS_EMIT_FAULT(EMBEDIQ_OBS_EVT_CFG_MUTABILITY_REJECT,
                                   0u, 0u,
                                   (uint8_t)CFG_MUT_FACTORY,
                                   key_crc);
            embediq_osal_mutex_unlock(g_mutex);
            return EMBEDIQ_ERR;
        }
    }

    uint8_t slot = find_slot(key);
    if (slot == NVM_MAX_KEYS) {
        slot = find_free_slot();
        if (slot == NVM_MAX_KEYS) {
            embediq_osal_mutex_unlock(g_mutex);
            return EMBEDIQ_ERR;   /* store full */
        }
        strncpy(g_nvm[slot].key, key, NVM_KEY_SIZE - 1u);
        g_nvm[slot].key[NVM_KEY_SIZE - 1u] = '\0';
        g_nvm[slot].active = true;
    }

    memcpy(g_nvm[slot].val, val, (size_t)len);
    g_nvm[slot].val_len   = (uint16_t)len;
    g_nvm[slot].schema_id = 0u;

    nvm_persist();

    embediq_osal_mutex_unlock(g_mutex);
    return EMBEDIQ_OK;
}

embediq_err_t embediq_nvm_get(const char *key, void *val, uint32_t *len)
{
    if (!key || !val || !len)  return EMBEDIQ_ERR;
    if (!g_mutex)              return EMBEDIQ_ERR;

    if (embediq_osal_mutex_lock(g_mutex, UINT32_MAX) != EMBEDIQ_OK) return EMBEDIQ_ERR;

    uint8_t slot = find_slot(key);
    if (slot == NVM_MAX_KEYS) {
        embediq_osal_mutex_unlock(g_mutex);
        return EMBEDIQ_ERR;   /* key not found */
    }
    if (g_nvm[slot].val_len > *len) {
        embediq_osal_mutex_unlock(g_mutex);
        return EMBEDIQ_ERR;   /* buffer too small */
    }

    memcpy(val, g_nvm[slot].val, g_nvm[slot].val_len);
    *len = (uint32_t)g_nvm[slot].val_len;

    embediq_osal_mutex_unlock(g_mutex);
    return EMBEDIQ_OK;
}

embediq_err_t embediq_nvm_delete(const char *key)
{
    if (!key)     return EMBEDIQ_ERR;
    if (!g_mutex) return EMBEDIQ_ERR;

    if (embediq_osal_mutex_lock(g_mutex, UINT32_MAX) != EMBEDIQ_OK) return EMBEDIQ_ERR;

    uint8_t slot = find_slot(key);
    if (slot == NVM_MAX_KEYS) {
        embediq_osal_mutex_unlock(g_mutex);
        return EMBEDIQ_OK;   /* idempotent per contract */
    }

    memset(&g_nvm[slot], 0, sizeof(nvm_entry_t));
    nvm_persist();

    embediq_osal_mutex_unlock(g_mutex);
    return EMBEDIQ_OK;
}

embediq_err_t embediq_nvm_flush(void)
{
    if (!g_mutex) return EMBEDIQ_ERR;

    if (embediq_osal_mutex_lock(g_mutex, UINT32_MAX) != EMBEDIQ_OK) return EMBEDIQ_ERR;
    nvm_persist();
    embediq_osal_mutex_unlock(g_mutex);
    return EMBEDIQ_OK;
}

embediq_err_t embediq_nvm_reset(void)
{
    if (!g_mutex) return EMBEDIQ_ERR;

    if (embediq_osal_mutex_lock(g_mutex, UINT32_MAX) != EMBEDIQ_OK) return EMBEDIQ_ERR;
    memset(g_nvm, 0, sizeof(g_nvm));
    nvm_persist();
    embediq_osal_mutex_unlock(g_mutex);
    return EMBEDIQ_OK;
}

/* ---------------------------------------------------------------------------
 * Test-only API — EMBEDIQ_PLATFORM_HOST only
 * ------------------------------------------------------------------------- */

#ifdef EMBEDIQ_PLATFORM_HOST

/**
 * Reset all NVM state: clear in-memory cache, create mutex.
 * Path management is now the HAL's concern (via EMBEDIQ_NVM_PATH env var).
 * Does NOT call fb_engine_boot or register an FB.
 */
void nvm__init_state(void)
{
    memset(g_nvm, 0, sizeof(g_nvm));
    g_nvm_fb = NULL;

    if (!g_mutex) {
        g_mutex = embediq_osal_mutex_create();
    }
}

/**
 * Reload NVM state from flash (test-only: simulates power-on reload).
 * Paired with nvm__init_state() — call after init to test blob loading.
 */
void nvm__load_state(void)
{
    nvm_load();
}

#endif /* EMBEDIQ_PLATFORM_HOST */
