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
 * Locking — two locks, one fixed order (persist lock, then table lock):
 *   g_mutex          table lock: g_nvm[] and g_generation. Held only for
 *                    table access and, inside a persist, for the memcpy of the
 *                    table into the snapshot. Never held across flash I/O or
 *                    an Observatory emit, and never held while taking the
 *                    persist lock — so readers never wait for a persist.
 *   g_persist_mutex  persist lock: s_persist_buf, g_persisted_gen and the
 *                    hal_flash_* calls. Taken by writers and by nvm_load().
 *   Every mutation bumps g_generation under the table lock. A writer whose
 *   generation is already on flash (a later writer persisted a newer table)
 *   skips its flash write, so an older table never overwrites a newer one.
 *
 * Durability is a result: every mutation persists synchronously and returns
 * whether its change (or a newer table) reached flash. A failed flash write
 * returns EMBEDIQ_ERR and emits one EMBEDIQ_OBS_EVT_NVM_PERSIST_FAIL; the
 * table keeps the change and the next successful persist (any later write,
 * or flush) carries it.
 *
 * Boot phase: EMBEDIQ_BOOT_PHASE_INFRASTRUCTURE (2)
 *
 * Public API (matches embediq_nvm.h contract exactly):
 *   fb_nvm_register()              — register FB
 *   embediq_nvm_set(key, val, len) — store value
 *   embediq_nvm_get(key, val, len) — lookup value
 *   embediq_nvm_delete(key)        — remove key
 *   embediq_nvm_flush()            — rewrite the current table (retry)
 *   embediq_nvm_reset()            — wipe all entries
 *
 * Host/test-only API (EMBEDIQ_PLATFORM_HOST):
 *   nvm__init_state()   — clear cache + create both locks
 *   nvm__load_state()   — trigger nvm_load() (simulates power-on reload)
 *   nvm__set_persist_delay_ms() — slow every persist (concurrency tests)
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
static EmbedIQ_Mutex_t *g_mutex         = NULL;   /* table lock (see header) */
static EmbedIQ_Mutex_t *g_persist_mutex = NULL;   /* persist lock (see header) */
static EmbedIQ_FB_Handle_t g_nvm_fb     = NULL;

/* Generation of the table: bumped by every mutation. Under g_mutex. */
static uint32_t g_generation    = 0u;
/* Generation of the table last written to flash. Under g_persist_mutex.
 * Invariant: g_persisted_gen <= g_generation (modulo 2^32) — it only ever
 * takes a value g_generation has had. */
static uint32_t g_persisted_gen = 0u;

/**
 * Composite persist buffer — header + payload in one allocation.
 * Static (R-02: no malloc). Sized at compile time. Owned by g_persist_mutex.
 * Used by both nvm_persist() (write) and nvm_load() (read) to ensure
 * a single hal_flash_write() call — the HAL's atomicity guarantee covers
 * the whole blob as one unit.
 */
static struct {
    nvm_blob_header_t hdr;
    nvm_entry_t       entries[NVM_MAX_KEYS];
} s_persist_buf;

#ifdef EMBEDIQ_PLATFORM_HOST
/* Test-only: delay applied to every persist immediately before the flash
 * write, to widen the window in which other callers overlap a persist. */
static uint32_t s_persist_delay_ms = 0u;
#endif

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

/* Load-path outcomes, emitted as EMBEDIQ_OBS_EVT_CFG_BLOB_INVALID state. */
#define NVM_LOAD_OK          0xFFu   /* no event */
#define NVM_LOAD_MAGIC_FAIL  0u
#define NVM_LOAD_CRC_FAIL    1u

/**
 * Write the table to flash unless a table at least as new as `gen` is already
 * there (`force` writes regardless — used by embediq_nvm_flush()).
 * Returns EMBEDIQ_OK when generation `gen` (or newer) is on flash after the
 * call; EMBEDIQ_ERR when the flash write failed (g_persisted_gen unchanged;
 * one NVM_PERSIST_FAIL emitted) or a lock could not be taken (the OSAL
 * observes that; no event here).
 * Calling context: any thread; the caller holds neither lock. Takes the
 * persist lock, then the table lock only for the skip test and the snapshot
 * memcpy.
 */
static embediq_err_t nvm_persist(uint32_t gen, bool force)
{
    if (embediq_osal_mutex_lock(g_persist_mutex, UINT32_MAX) != EMBEDIQ_OK) return EMBEDIQ_ERR;
    if (embediq_osal_mutex_lock(g_mutex, UINT32_MAX) != EMBEDIQ_OK) {
        embediq_osal_mutex_unlock(g_persist_mutex);
        return EMBEDIQ_ERR;
    }

    /* Skip iff flash is at least as new as the caller's change, both
     * distances measured back from the current generation in uint32
     * arithmetic: (cur - persisted) <= (cur - gen). Both are small unless
     * 2^32 mutations happen with no successful persist, so a failing flash
     * (persisted falling ever further behind) never wraps into a false skip,
     * which a signed difference between gen and persisted would after 2^31. */
    uint32_t cur = g_generation;
    if (!force && (uint32_t)(cur - g_persisted_gen) <= (uint32_t)(cur - gen)) {
        embediq_osal_mutex_unlock(g_mutex);
        embediq_osal_mutex_unlock(g_persist_mutex);   /* this change is on flash */
        return EMBEDIQ_OK;
    }

    /* Snapshot the live table — the only work done under the table lock. */
    memcpy(s_persist_buf.entries, g_nvm, sizeof(g_nvm));
    uint32_t snap_gen = cur;
    embediq_osal_mutex_unlock(g_mutex);

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

    /* CRC over payload only. */
    s_persist_buf.hdr.crc32 = embediq_crc32(s_persist_buf.entries,
                                              sizeof(s_persist_buf.entries));

#ifdef EMBEDIQ_PLATFORM_HOST
    if (s_persist_delay_ms > 0u) {
        embediq_osal_delay_ms(s_persist_delay_ms);
    }
#endif

    /* Single atomic write — HAL atomicity contract (PR-A: hal_flash.h) ensures
     * either the full blob lands or nothing changes. On failure the generation
     * stays behind, so the next write retries. */
    int hal_rc = hal_flash_write(0u, &s_persist_buf, sizeof(s_persist_buf));
    if (hal_rc == HAL_OK) {
        g_persisted_gen = snap_gen;
    }

    embediq_osal_mutex_unlock(g_persist_mutex);

    if (hal_rc != HAL_OK) {
        EMBEDIQ_OBS_EMIT_FAULT(EMBEDIQ_OBS_EVT_NVM_PERSIST_FAIL, 0u, 0u,
                               (uint8_t)hal_rc, (uint16_t)(snap_gen & 0xFFFFu));
        return EMBEDIQ_ERR;
    }
    return EMBEDIQ_OK;
}

/**
 * Load the table from flash (boot, or a simulated power-on in tests).
 * nvm_load() runs only at boot, before any other caller exists (and from the
 * host test hook with no concurrent callers). It replaces the table with the
 * flash contents; a change made concurrently by another thread would be
 * discarded and its persist skipped. Reloading at runtime is not supported.
 * Takes the persist lock, then the table lock. Observatory events are emitted
 * after both locks are released.
 */
static void nvm_load(void)
{
    if (!g_mutex || !g_persist_mutex) return;   /* APIs report EMBEDIQ_ERR */
    if (embediq_osal_mutex_lock(g_persist_mutex, UINT32_MAX) != EMBEDIQ_OK) return;

    /* Read the full blob (header + payload) in a single call. */
    uint8_t outcome = NVM_LOAD_OK;
    bool    valid   = hal_flash_read(0u, &s_persist_buf, sizeof(s_persist_buf)) == HAL_OK;

    /* Verify magic — catches erased flash, pre-PR-D blobs, and corrupt files. */
    if (valid && memcmp(s_persist_buf.hdr.magic, k_nvm_blob_magic,
                        sizeof(k_nvm_blob_magic)) != 0) {
        outcome = NVM_LOAD_MAGIC_FAIL;
        valid   = false;
    }

    /* Verify CRC-32/ISO-HDLC over the payload. */
    if (valid && embediq_crc32(s_persist_buf.entries, sizeof(s_persist_buf.entries))
                 != s_persist_buf.hdr.crc32) {
        outcome = NVM_LOAD_CRC_FAIL;
        valid   = false;
    }

    if (embediq_osal_mutex_lock(g_mutex, UINT32_MAX) == EMBEDIQ_OK) {
        if (valid) {
            memcpy(g_nvm, s_persist_buf.entries, sizeof(g_nvm));   /* blob valid */
        } else {
            memset(g_nvm, 0, sizeof(g_nvm));   /* start with defaults (empty store) */
        }
        g_persisted_gen = g_generation;   /* the table now matches what boot saw */
        embediq_osal_mutex_unlock(g_mutex);
    }
    embediq_osal_mutex_unlock(g_persist_mutex);

    if (outcome != NVM_LOAD_OK) {
        EMBEDIQ_OBS_EMIT_FAULT(EMBEDIQ_OBS_EVT_CFG_BLOB_INVALID,
                               0u, 0u, outcome, 0u);   /* 0 = magic_fail, 1 = crc_fail */
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
    if (!g_persist_mutex) {
        g_persist_mutex = embediq_osal_mutex_create();
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
    if (!g_mutex || !g_persist_mutex) return EMBEDIQ_ERR;

    /* Mutability guard — factory-class keys are immutable at runtime.
     * IEC 62443 commissioning: fleet push cannot overwrite factory provisioning.
     * Silently reject — emit Observatory event for audit trail. The schema is
     * static, so the check and the emit need no lock. */
    {
        const cfg_schema_entry_t *schema = cfg_schema_lookup(key);
        if (schema != NULL && schema->mutability == CFG_MUT_FACTORY) {
            uint16_t key_crc = (uint16_t)(embediq_crc32(key, strlen(key)) & 0xFFFFu);
            EMBEDIQ_OBS_EMIT_FAULT(EMBEDIQ_OBS_EVT_CFG_MUTABILITY_REJECT,
                                   0u, 0u,
                                   (uint8_t)CFG_MUT_FACTORY,
                                   key_crc);
            return EMBEDIQ_ERR;
        }
    }

    if (embediq_osal_mutex_lock(g_mutex, UINT32_MAX) != EMBEDIQ_OK) return EMBEDIQ_ERR;

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
    uint32_t gen = ++g_generation;

    embediq_osal_mutex_unlock(g_mutex);
    return nvm_persist(gen, false);
}

/* Write-once provisioning write — the ONLY authorized runtime write path for
 * CFG_MUT_FACTORY keys. Bypasses the mutability guard in embediq_nvm_set() but
 * preserves IEC 62443 commissioning immutability: a key may be set only while
 * absent or empty (commissioning); once provisioned it is idempotent (an
 * operator in the field cannot overwrite it). factory_reset deletes the key,
 * so the next boot re-provisions. See ARCHITECTURE.md fb_provisioning section. */
embediq_err_t embediq_nvm_provision(const char *key, const char *value)
{
    if (!key || !value) {
        EMBEDIQ_OBS_EMIT_FAULT(EMBEDIQ_OBS_EVT_FAULT, 0u, 0u, 0u, 0u);
        return EMBEDIQ_ERR;
    }
    size_t vlen = strlen(value);
    if (strlen(key) >= NVM_KEY_SIZE || vlen > NVM_VAL_SIZE) {
        EMBEDIQ_OBS_EMIT_FAULT(EMBEDIQ_OBS_EVT_FAULT, 0u, 0u, 1u, 0u);
        return EMBEDIQ_ERR;
    }
    if (!g_mutex || !g_persist_mutex) {
        EMBEDIQ_OBS_EMIT_FAULT(EMBEDIQ_OBS_EVT_FAULT, 0u, 0u, 2u, 0u);
        return EMBEDIQ_ERR;
    }
    if (embediq_osal_mutex_lock(g_mutex, UINT32_MAX) != EMBEDIQ_OK) {
        EMBEDIQ_OBS_EMIT_FAULT(EMBEDIQ_OBS_EVT_FAULT, 0u, 0u, 3u, 0u);
        return EMBEDIQ_ERR;
    }

    /* Write-once: a non-empty existing value means already provisioned.
     * Idempotent on the value, not on having tried: keep the commissioned
     * value and return OK only once the table holding it is on flash — a
     * no-op when it is, a real retry when an earlier persist failed. */
    uint8_t slot = find_slot(key);
    if (slot != NVM_MAX_KEYS && g_nvm[slot].val_len > 0u) {
        uint32_t cur = g_generation;
        embediq_osal_mutex_unlock(g_mutex);
        return nvm_persist(cur, false);
    }

    if (slot == NVM_MAX_KEYS) {
        slot = find_free_slot();
        if (slot == NVM_MAX_KEYS) {
            embediq_osal_mutex_unlock(g_mutex);
            EMBEDIQ_OBS_EMIT_FAULT(EMBEDIQ_OBS_EVT_FAULT, 0u, 0u, 4u, 0u);
            return EMBEDIQ_ERR;   /* store full */
        }
        strncpy(g_nvm[slot].key, key, NVM_KEY_SIZE - 1u);
        g_nvm[slot].key[NVM_KEY_SIZE - 1u] = '\0';
        g_nvm[slot].active = true;
    }

    memcpy(g_nvm[slot].val, value, vlen);
    g_nvm[slot].val_len   = (uint16_t)vlen;
    g_nvm[slot].schema_id = 0u;
    uint32_t gen = ++g_generation;

    embediq_osal_mutex_unlock(g_mutex);
    return nvm_persist(gen, false);
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
    if (!key)                         return EMBEDIQ_ERR;
    if (!g_mutex || !g_persist_mutex) return EMBEDIQ_ERR;

    if (embediq_osal_mutex_lock(g_mutex, UINT32_MAX) != EMBEDIQ_OK) return EMBEDIQ_ERR;

    /* Absent key: idempotent on durability, not on having tried. Return OK
     * only once the table without the key is on flash — a no-op when it is,
     * a real retry when an earlier delete's persist failed. */
    uint8_t slot = find_slot(key);
    if (slot == NVM_MAX_KEYS) {
        uint32_t cur = g_generation;
        embediq_osal_mutex_unlock(g_mutex);
        return nvm_persist(cur, false);
    }

    memset(&g_nvm[slot], 0, sizeof(nvm_entry_t));
    uint32_t gen = ++g_generation;

    embediq_osal_mutex_unlock(g_mutex);
    return nvm_persist(gen, false);
}

embediq_err_t embediq_nvm_flush(void)
{
    if (!g_mutex || !g_persist_mutex) return EMBEDIQ_ERR;

    return nvm_persist(0u, true);
}

embediq_err_t embediq_nvm_reset(void)
{
    if (!g_mutex || !g_persist_mutex) return EMBEDIQ_ERR;

    if (embediq_osal_mutex_lock(g_mutex, UINT32_MAX) != EMBEDIQ_OK) return EMBEDIQ_ERR;
    memset(g_nvm, 0, sizeof(g_nvm));
    uint32_t gen = ++g_generation;
    embediq_osal_mutex_unlock(g_mutex);

    return nvm_persist(gen, false);
}

/* ---------------------------------------------------------------------------
 * Test-only API — EMBEDIQ_PLATFORM_HOST only
 * ------------------------------------------------------------------------- */

#ifdef EMBEDIQ_PLATFORM_HOST

/**
 * Reset all NVM state: clear in-memory cache, create both locks.
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
    if (!g_persist_mutex) {
        g_persist_mutex = embediq_osal_mutex_create();
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

/**
 * Delay every subsequent persist by ms milliseconds before the flash write
 * (0 = no delay). Set it before starting concurrent callers.
 */
void nvm__set_persist_delay_ms(uint32_t ms)
{
    s_persist_delay_ms = ms;
}

#endif /* EMBEDIQ_PLATFORM_HOST */
