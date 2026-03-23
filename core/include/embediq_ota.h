/*
 * embediq_ota.h — OTA firmware update contract
 *
 * Defines the platform operations table and FSM states for over-the-air
 * firmware updates. fb_ota (Phase 2, P2-T5) owns the FSM logic and
 * coordinates the lifecycle shutdown protocol documented in
 * docs/architecture/lifecycle.md.
 *
 * Platform integration pattern:
 *   1. Platform boot code calls embediq_ota_register_ops() with a populated
 *      embediq_ota_ops_t table (once, during EMBEDIQ_BOOT_PHASE_PLATFORM).
 *   2. fb_ota drives the update using the ops table — no direct flash calls.
 *   3. After verify+apply, fb_ota publishes MSG_SYS_OTA_REQUEST and coordinates
 *      shutdown via MSG_SYS_OTA_READY collection (500 ms deadline).
 *
 * All ops functions MUST be safe to call from the fb_ota dispatch thread.
 * None of them may block longer than 100 ms (use chunked write for large images).
 *
 * I-01: Compiles standalone with zero OSAL or BSP dependencies.
 * R-03: C11. Fixed-width types from <stdint.h> only.
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef EMBEDIQ_OTA_H
#define EMBEDIQ_OTA_H

#include "embediq_osal.h"  /* embediq_err_t */
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
 * OTA platform operations table
 *
 * Platform provides a concrete implementation of this table.
 * All function pointers must be non-NULL when registered.
 * ------------------------------------------------------------------------- */

typedef struct {
    /**
     * Prepare the inactive partition for writing (erase, bounds check).
     * Called once at the start of each OTA session.
     * @return EMBEDIQ_OK if partition is ready, EMBEDIQ_ERR otherwise.
     */
    embediq_err_t (*begin)(void);

    /**
     * Write one chunk of image data to the inactive partition.
     * May be called many times. Chunks are contiguous; offset is tracked
     * internally by the platform.
     *
     * @param chunk  Pointer to image bytes.
     * @param len    Number of bytes in this chunk.
     * @return EMBEDIQ_OK on success, EMBEDIQ_ERR on flash write error.
     */
    embediq_err_t (*write)(const uint8_t *chunk, uint32_t len);

    /**
     * Verify integrity of the written image (hash, signature, or CRC).
     * Called once after all chunks have been written.
     * @return EMBEDIQ_OK if image is valid, EMBEDIQ_ERR if corrupted or rejected.
     */
    embediq_err_t (*verify)(void);

    /**
     * Mark the inactive partition as the next boot target.
     * Does NOT reboot — the caller (fb_ota) triggers reboot by calling
     * embediq_engine_dispatch_shutdown() after the OTA lifecycle completes.
     * @return EMBEDIQ_OK on success, EMBEDIQ_ERR if the partition cannot be marked.
     */
    embediq_err_t (*apply)(void);

    /**
     * Abort the OTA session and restore the partition to its pre-OTA state.
     * Safe to call at any point after begin().
     * @return EMBEDIQ_OK on success.
     */
    embediq_err_t (*abort)(void);
} embediq_ota_ops_t;

/* ---------------------------------------------------------------------------
 * Registration
 * ------------------------------------------------------------------------- */

/**
 * Register the platform OTA operations table.
 * Call once during EMBEDIQ_BOOT_PHASE_PLATFORM before fb_ota initialises.
 *
 * @param ops  Pointer to a statically allocated ops table. Must remain valid
 *             for the lifetime of the process.
 * @return EMBEDIQ_OK on success, EMBEDIQ_ERR if ops is NULL or any fn pointer is NULL.
 */
embediq_err_t embediq_ota_register_ops(const embediq_ota_ops_t *ops);

/* ---------------------------------------------------------------------------
 * OTA FSM states — observable via Observatory events
 *
 * State transitions are logged by fb_ota as Observatory events so that
 * external tools (Studio, CLI monitor) can display update progress.
 * ------------------------------------------------------------------------- */

typedef enum {
    EMBEDIQ_OTA_STATE_IDLE           = 0,  /**< No OTA in progress.                    */
    EMBEDIQ_OTA_STATE_DOWNLOADING,         /**< Receiving image chunks.                */
    EMBEDIQ_OTA_STATE_VERIFYING,           /**< Running integrity check.               */
    EMBEDIQ_OTA_STATE_READY_TO_APPLY,      /**< Verified; waiting for safe shutdown.   */
    EMBEDIQ_OTA_STATE_APPLYING,            /**< Marking boot partition; near reboot.   */
    EMBEDIQ_OTA_STATE_DONE,                /**< apply() succeeded; reboot imminent.    */
    EMBEDIQ_OTA_STATE_ABORTED              /**< OTA cancelled or failed.               */
} embediq_ota_state_t;

#ifdef __cplusplus
}
#endif

#endif /* EMBEDIQ_OTA_H */
