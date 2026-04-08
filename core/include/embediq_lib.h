/*
 * embediq_lib.h — Library component integration contract
 *
 * Defines embediq_lib_ops_t: the ops table every EmbedIQ library component
 * must populate to integrate with the framework. Libraries are not FBs —
 * they do not receive messages. The ops table is the only integration point.
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef EMBEDIQ_LIB_H
#define EMBEDIQ_LIB_H

#include <stdint.h>
#include "embediq_osal.h"   /* for embediq_err_t, EMBEDIQ_OK */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Library component ops table.
 *
 * All function pointers are optional — set unused slots to NULL.
 * The framework checks for NULL before calling any slot.
 *
 * Rules:
 *   R-lib-2: Libraries must not call embediq_obs_emit() directly.
 *            Use ops->obs_emit instead — allows host-side testing without live observer.
 *   R-lib-3: source_id is assigned by platform_lib_declare(). Never hard-code it.
 *   R-lib-4: Every registered library must declare one of these structs.
 *            Partial tables are allowed — unused pointers are NULL.
 */
typedef struct {
    /** Short name used in logs and diagnostics, e.g. "embediq_crc". */
    const char *name;

    /**
     * init() — called at system startup.
     * Must emit EMBEDIQ_OBS_EVT_LIB_INIT (0x40) via obs_emit if the library
     * acquires any resource (memory, peripheral, RTOS object). R-lib-1.
     * Returns EMBEDIQ_OK on success, negative on failure.
     */
    embediq_err_t (*init)(void);

    /**
     * deinit() — called at teardown or on init failure recovery.
     * Must emit EMBEDIQ_OBS_EVT_LIB_DEINIT (0x41) via obs_emit. R-lib-1.
     * NULL if no teardown is needed.
     */
    void (*deinit)(void);

    /**
     * obs_emit — assigned by platform_lib_declare(). Do not set manually.
     * Library calls this instead of embediq_obs_emit() directly (R-lib-2).
     * NULL until platform_lib_declare() assigns it — library must guard with NULL check.
     */
    void (*obs_emit)(uint8_t event_type,
                     uint8_t source_fb_id,
                     uint8_t msg_id,
                     uint8_t state_or_flag,
                     uint8_t extra);

    /**
     * source_id — assigned by platform_lib_declare() from the 0xE0–0xEF range.
     * Do not set manually (R-lib-3). Zero until assigned.
     */
    uint8_t source_id;
} embediq_lib_ops_t;

/**
 * platform_lib_declare() — register a library with the EmbedIQ framework.
 *
 * Assigns source_id from EMBEDIQ_LIB_SRC_BASE (0xE0) upward.
 * Sets ops->obs_emit to the framework emit function.
 * Maximum 16 libraries per build — build-time _Static_assert enforced in LIB-4.
 *
 * Returns EMBEDIQ_OK on success.
 * Returns EMBEDIQ_ERR if the 16-slot limit is exceeded (assert fires first).
 */
embediq_err_t platform_lib_declare(embediq_lib_ops_t *ops);

#ifdef __cplusplus
}
#endif

#endif /* EMBEDIQ_LIB_H */
