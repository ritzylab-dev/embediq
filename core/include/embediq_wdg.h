/*
 * embediq_wdg.h — Watchdog health-token contract
 *
 * Health-token model: each FB that wants liveness monitoring registers
 * with a timeout, then calls embediq_wdg_checkin() periodically.
 * fb_watchdog checks all registered tokens on a 100 ms interval.
 * If any FB misses its checkin deadline, MSG_WDG_MISSED_CHECKIN is published.
 *
 * Philosophy: the watchdog is an observer, not a terminator. At Phase 1 it
 * reports misses. The system fault policy (reboot vs. quarantine vs. log) is
 * an application decision made in response to MSG_WDG_MISSED_CHECKIN.
 *
 * I-01: Compiles standalone with zero OSAL or BSP dependencies.
 * R-03: C11. Fixed-width types from <stdint.h> only.
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef EMBEDIQ_WDG_H
#define EMBEDIQ_WDG_H

#include "embediq_osal.h"  /* embediq_err_t */
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
 * Types
 * ------------------------------------------------------------------------- */

/**
 * FB identifier for watchdog registration.
 * Maps to the FB's registered endpoint index (0–EMBEDIQ_MAX_ENDPOINTS-1).
 */
typedef uint8_t embediq_fb_id_t;

/* ---------------------------------------------------------------------------
 * Watchdog API
 * ------------------------------------------------------------------------- */

/**
 * Register an FB for liveness monitoring.
 *
 * @param fb_id      The registering FB's endpoint index.
 * @param timeout_ms Maximum allowed interval between checkins (milliseconds).
 *                   Must be > 0. Minimum effective granularity: 100 ms.
 * @return EMBEDIQ_OK on success, EMBEDIQ_ERR if already registered or invalid args.
 */
embediq_err_t embediq_wdg_register(embediq_fb_id_t fb_id, uint32_t timeout_ms);

/**
 * Reset the checkin timer for a registered FB.
 * Call at least once every timeout_ms milliseconds or a missed-checkin event
 * will be published.
 *
 * @param fb_id  The calling FB's endpoint index (must be registered).
 * @return EMBEDIQ_OK on success, EMBEDIQ_ERR if fb_id is not registered.
 */
embediq_err_t embediq_wdg_checkin(embediq_fb_id_t fb_id);

/**
 * Deregister an FB from watchdog monitoring.
 * Safe to call from the FB's exit_fn during clean shutdown.
 * If fb_id is not registered, returns EMBEDIQ_OK (idempotent).
 *
 * @param fb_id  The FB's endpoint index.
 * @return EMBEDIQ_OK on success, EMBEDIQ_ERR on internal error.
 */
embediq_err_t embediq_wdg_deregister(embediq_fb_id_t fb_id);

#ifdef __cplusplus
}
#endif

#endif /* EMBEDIQ_WDG_H */
