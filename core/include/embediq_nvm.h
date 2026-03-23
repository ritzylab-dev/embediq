/*
 * embediq_nvm.h — Non-volatile memory key-value store contract
 *
 * Abstract interface for persisting key-value pairs across power cycles.
 * The POSIX implementation stores data as JSON under ~/.embediq/.
 * The ESP32 implementation uses NVS (Non-Volatile Storage).
 *
 * All operations are synchronous from the caller's perspective.
 * fb_nvm owns internal write buffering and flush scheduling.
 *
 * Constraints (v1):
 *   - Keys are NUL-terminated strings up to EMBEDIQ_NVM_KEY_MAX bytes.
 *   - Values are raw byte arrays up to EMBEDIQ_NVM_VAL_MAX bytes.
 *   - No transactions. embediq_nvm_flush() is the durability boundary.
 *   - embediq_nvm_reset() erases ALL keys — use with care.
 *
 * I-01: Compiles standalone with zero OSAL or BSP dependencies.
 * R-03: C11. Fixed-width types from <stdint.h> only.
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef EMBEDIQ_NVM_H
#define EMBEDIQ_NVM_H

#include "embediq_osal.h"  /* embediq_err_t */
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
 * Sizing limits
 * ------------------------------------------------------------------------- */

/** Maximum key length including the NUL terminator (bytes). */
#define EMBEDIQ_NVM_KEY_MAX  32u

/** Maximum value payload size (bytes). */
#define EMBEDIQ_NVM_VAL_MAX  256u

/* ---------------------------------------------------------------------------
 * Key-value operations
 * ------------------------------------------------------------------------- */

/**
 * Store a value under the given key.
 * Overwrites any existing value for the same key.
 *
 * @param key   NUL-terminated key string; length < EMBEDIQ_NVM_KEY_MAX.
 * @param val   Pointer to value bytes.
 * @param len   Number of bytes in val; must be <= EMBEDIQ_NVM_VAL_MAX.
 * @return EMBEDIQ_OK on success, EMBEDIQ_ERR on invalid args or store error.
 */
embediq_err_t embediq_nvm_set(const char *key, const void *val, uint32_t len);

/**
 * Retrieve the value stored under the given key.
 *
 * @param key   NUL-terminated key string.
 * @param val   Output buffer of at least *len bytes.
 * @param len   In: capacity of val buffer. Out: actual bytes written.
 * @return EMBEDIQ_OK on success, EMBEDIQ_ERR if key not found or buffer too small.
 */
embediq_err_t embediq_nvm_get(const char *key, void *val, uint32_t *len);

/**
 * Remove the entry for the given key.
 * If the key does not exist, returns EMBEDIQ_OK (idempotent).
 */
embediq_err_t embediq_nvm_delete(const char *key);

/**
 * Flush all pending writes to persistent storage.
 * FBs that own NVM state must call this before publishing MSG_SYS_OTA_READY
 * (see docs/architecture/lifecycle.md — OTA shutdown contract).
 *
 * @return EMBEDIQ_OK if all data is safely persisted, EMBEDIQ_ERR on I/O error.
 */
embediq_err_t embediq_nvm_flush(void);

/**
 * Erase all stored keys. Irreversible.
 * Typically called during factory reset only.
 *
 * @return EMBEDIQ_OK on success, EMBEDIQ_ERR on store error.
 */
embediq_err_t embediq_nvm_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* EMBEDIQ_NVM_H */
