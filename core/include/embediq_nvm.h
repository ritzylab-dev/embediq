/*
 * embediq_nvm.h — Non-volatile memory key-value store contract
 *
 * Abstract interface for persisting key-value pairs across power cycles.
 * The POSIX implementation stores data as a binary blob via hal_flash_posix.c (hal/posix/).
 * The ESP32 implementation uses NVS (Non-Volatile Storage).
 *
 * All operations are synchronous from the caller's perspective.
 * Every mutation persists synchronously and returns whether the change
 * reached flash; embediq_nvm_flush() rewrites the current table (use it
 * after a failed mutation to retry).
 *
 * Concurrency — every function is callable from any thread. fb_nvm uses two
 * locks with one fixed order (persist lock, then table lock):
 *   - The table lock guards the key table. It is held only for the table
 *     access itself and, during a persist, for one memcpy of the table into
 *     the persist snapshot — microseconds, never across flash I/O or an
 *     Observatory emit.
 *   - The persist lock serialises flash writes. Writers take it after
 *     releasing the table lock; a writer whose change is already on flash
 *     (a later writer persisted a newer table) skips its write, so an older
 *     table never overwrites a newer one.
 * embediq_nvm_get() takes only the table lock: it never waits for a persist
 * in flight. Writes return EMBEDIQ_OK once their change (or a newer table)
 * is on flash, EMBEDIQ_ERR if the flash write failed, and may wait for
 * another writer's persist.
 * Loading: fb_nvm's nvm_load() runs only at boot, before any other caller
 * exists (and from the host test hook with no concurrent callers). It
 * replaces the table with the flash contents; a change made concurrently by
 * another thread would be discarded and its persist skipped. Reloading at
 * runtime is not supported.
 *
 * Constraints (v1):
 *   - Keys are NUL-terminated strings up to EMBEDIQ_NVM_KEY_MAX bytes.
 *   - Values are raw byte arrays up to EMBEDIQ_NVM_VAL_MAX bytes.
 *   - No transactions.
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

/** Maximum key length including the NUL terminator (bytes).
 *  Matches EMBEDIQ_NVM_KEY_SIZE in embediq_config.h — fb_nvm.c uses config.h as authority. */
#define EMBEDIQ_NVM_KEY_MAX  64u

/** Maximum value payload size (bytes).
 *  Matches EMBEDIQ_NVM_VAL_SIZE in embediq_config.h — fb_nvm.c uses config.h as authority.
 *  String values may use up to 63 bytes usable (64 minus NUL terminator). */
#define EMBEDIQ_NVM_VAL_MAX  64u

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
 * Returns EMBEDIQ_ERR if the flash write failed; the in-memory table keeps
 * the change and the next successful persist (any later write, or flush)
 * carries it.
 *
 * Calling context: any thread. May wait for a persist in flight.
 */
embediq_err_t embediq_nvm_set(const char *key, const void *val, uint32_t len);

/**
 * Retrieve the value stored under the given key.
 *
 * @param key   NUL-terminated key string.
 * @param val   Output buffer of at least *len bytes.
 * @param len   In: capacity of val buffer. Out: actual bytes written.
 * @return EMBEDIQ_OK on success, EMBEDIQ_ERR if key not found or buffer too small.
 *
 * Calling context: any thread. Never waits for flash I/O or a persist in
 * flight; may wait for the microsecond-scale table lock.
 */
embediq_err_t embediq_nvm_get(const char *key, void *val, uint32_t *len);

/**
 * Remove the entry for the given key.
 * Idempotent: deleting an absent key returns EMBEDIQ_OK only once the table
 * without that key is on flash, retrying a persist that previously failed.
 * Returns EMBEDIQ_ERR if the flash write failed; the in-memory table keeps
 * the change and the next successful persist (any later write, or flush)
 * carries it.
 *
 * Calling context: any thread. May wait for a persist in flight.
 */
embediq_err_t embediq_nvm_delete(const char *key);

/**
 * embediq_nvm_provision — write-once provisioning write.
 * Semantics: if key is absent or empty, write value and return EMBEDIQ_OK.
 *            Idempotent: a second call with the key already provisioned does
 *            not change the value; it returns EMBEDIQ_OK only once that value
 *            is on flash, retrying a persist that previously failed.
 * Returns EMBEDIQ_ERR if the flash write failed; the in-memory table keeps
 * the change and the next successful persist (any later write, or flush)
 * carries it.
 * This is the ONLY authorized write path for CFG_MUT_FACTORY keys at runtime.
 * IEC 62443 commissioning immutability: operator cannot overwrite after provisioning.
 * fb_provisioning calls this at UNMANUFACTURED -> MANUFACTURED transition only.
 *
 * Calling context: any thread. May wait for a persist in flight.
 */
embediq_err_t embediq_nvm_provision(const char *key, const char *value);

/**
 * Flush all pending writes to persistent storage.
 * FBs that own NVM state must call this before publishing MSG_SYS_OTA_READY
 * (see docs/architecture/lifecycle.md — OTA shutdown contract).
 *
 * @return EMBEDIQ_OK if all data is safely persisted, EMBEDIQ_ERR on I/O error.
 * Returns EMBEDIQ_ERR if the flash write failed; the in-memory table keeps
 * the change and the next successful persist (any later write, or flush)
 * carries it.
 *
 * Calling context: any thread. Always writes the current table; may wait for
 * a persist in flight.
 */
embediq_err_t embediq_nvm_flush(void);

/**
 * Erase all stored keys. Irreversible.
 * Typically called during factory reset only.
 *
 * @return EMBEDIQ_OK on success, EMBEDIQ_ERR on store error.
 * Returns EMBEDIQ_ERR if the flash write failed; the in-memory table keeps
 * the change and the next successful persist (any later write, or flush)
 * carries it.
 *
 * Calling context: any thread. May wait for a persist in flight.
 */
embediq_err_t embediq_nvm_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* EMBEDIQ_NVM_H */
