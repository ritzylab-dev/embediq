/*
 * embediq_wdg.h — Watchdog health-token contract
 *
 * Health-token model: each FB that wants liveness monitoring registers
 * with a timeout, then calls embediq_wdg_checkin() periodically.
 * fb_watchdog checks all registered tokens on a 100 ms interval.
 * If any FB misses its checkin deadline, MSG_WDG_MISSED_CHECKIN is published.
 *
 * What the driver does: it reports each missed check-in (an Observatory
 * event, the monitored FB's fault, MSG_WDG_MISSED_CHECKIN) and it feeds the
 * HAL watchdog only after a scan in which every registered FB was healthy.
 * What the application decides: how to react to MSG_WDG_MISSED_CHECKIN. On a
 * target whose hardware watchdog is enabled, feeding that has stopped ends in
 * a hardware reset; the POSIX host build simulates the HAL and performs none.
 *
 * Calls (register, checkin, unregister):
 *   - Calling context: any thread (an FB thread or a platform thread); not
 *     an ISR. Each call may wait for the watchdog's table lock.
 *   - fb is a handle returned by embediq_fb_register(). NULL is rejected; any
 *     other pointer is not validated and must not be passed.
 *   - Every call returns EMBEDIQ_OK or EMBEDIQ_ERR. A call made before the
 *     watchdog service is ready (fb_watchdog initialised, its lock and its
 *     monitor running) returns EMBEDIQ_ERR and changes nothing.
 *   - A rejected call emits one EMBEDIQ_OBS_EVT_FAULT event: source = fb's
 *     endpoint (0xFF for NULL), target = the watchdog FB's endpoint (0xFF
 *     while the service is not ready), state_or_flag = one of the
 *     EMBEDIQ_FAULT_WDG_* codes below, msg_id = 0 or, for
 *     EMBEDIQ_FAULT_WDG_UNAVAILABLE, the EMBEDIQ_WDG_RESOURCE_* that failed.
 *
 * Deadlines:
 *   - timeout_ms is in milliseconds, EMBEDIQ_WDG_TIMEOUT_MS_MIN …
 *     EMBEDIQ_WDG_TIMEOUT_MS_MAX (1 … UINT32_MAX/2).
 *   - An entry expires when more than timeout_ms has elapsed since its last
 *     register or check-in.
 *   - Elapsed time is (uint32_t)(now_ms - last_ms) on the 32-bit millisecond
 *     clock. That subtraction gives the true elapsed time for any gap shorter
 *     than one full wrap of the clock (about 49.7 days); a gap of a full wrap
 *     or more cannot be told from a short one. The supported maximum timeout
 *     is deliberately half that range (EMBEDIQ_WDG_TIMEOUT_MS_MAX) so that
 *     every supported deadline is reached well before the clock can wrap.
 *   - The monitor scans about every 100 ms. That is a polling interval, not
 *     a bound on the reaction time: reporting a miss publishes a HIGH message
 *     and may wait for queue space.
 *
 * Includes embediq_fb.h (EmbedIQ_FB_Handle_t) and embediq_osal.h
 * (embediq_err_t, EMBEDIQ_OK / EMBEDIQ_ERR) — both core contract headers, so
 * this header compiles standalone; no OSAL implementation or BSP header.
 * R-03: C11. Fixed-width types from <stdint.h> only.
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef EMBEDIQ_WDG_H
#define EMBEDIQ_WDG_H

#include "embediq_fb.h"    /* EmbedIQ_FB_Handle_t */
#include "embediq_osal.h"  /* embediq_err_t, EMBEDIQ_OK, EMBEDIQ_ERR */
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
 * Supported timeout range, in milliseconds
 *
 * MIN: a zero timeout would expire on every scan; 1 ms is the smallest
 *      deadline the millisecond clock can express.
 * MAX: UINT32_MAX / 2 = 2,147,483,647 ms (about 24.8 days). This is the
 *      supported policy, chosen so that a deadline always falls well inside
 *      one wrap of the 32-bit millisecond clock; the elapsed-time subtraction
 *      itself stays correct up to a full wrap.
 * ------------------------------------------------------------------------- */

#define EMBEDIQ_WDG_TIMEOUT_MS_MIN  1u
#define EMBEDIQ_WDG_TIMEOUT_MS_MAX  (UINT32_MAX / 2u)

/* ---------------------------------------------------------------------------
 * Watchdog fault codes — state_or_flag of an EMBEDIQ_OBS_EVT_FAULT event
 * emitted by the watchdog service. Distinct from EmbedIQ_FB_State_t (0–5),
 * the engine codes in embediq_fb.h (0x80–0x83) and the timer codes in
 * embediq_timer.h (0x90–0x91); 0x94 leaves 0x92–0x93 free next to the timer.
 * ------------------------------------------------------------------------- */

#define EMBEDIQ_FAULT_WDG_NOT_READY       0x94u  /**< a call arrived before the service was ready */
#define EMBEDIQ_FAULT_WDG_INVALID_ARG     0x95u  /**< NULL handle or timeout outside the supported range */
#define EMBEDIQ_FAULT_WDG_TABLE_FULL      0x96u  /**< no free entry */
#define EMBEDIQ_FAULT_WDG_NOT_REGISTERED  0x97u  /**< check-in or unregister of an FB that is not registered */
#define EMBEDIQ_FAULT_WDG_UNAVAILABLE     0x98u  /**< a required resource could not be prepared or taken; msg_id names it */

/* msg_id of an EMBEDIQ_FAULT_WDG_UNAVAILABLE event: the resource that failed.
 * Numbered from 1 so that msg_id 0 always means "no resource". */
#define EMBEDIQ_WDG_RESOURCE_MUTEX    1u  /**< the table lock could not be created */
#define EMBEDIQ_WDG_RESOURCE_HAL      2u  /**< hal_wdg_init() failed */
#define EMBEDIQ_WDG_RESOURCE_MONITOR  3u  /**< the monitor task could not be created */
#define EMBEDIQ_WDG_RESOURCE_LOCK     4u  /**< the table lock could not be taken */

/* ---------------------------------------------------------------------------
 * Watchdog API
 * ------------------------------------------------------------------------- */

/**
 * Register an FB for liveness monitoring, or update its registration.
 *
 * Calling context: any thread (an FB thread or a platform thread); not an
 * ISR. May wait for the watchdog's table lock.
 *
 * @param fb          Handle returned by embediq_fb_register(); not NULL.
 * @param timeout_ms  Milliseconds, EMBEDIQ_WDG_TIMEOUT_MS_MIN …
 *                    EMBEDIQ_WDG_TIMEOUT_MS_MAX.
 * @return EMBEDIQ_OK for a new entry, and for an FB already registered (its
 *         timeout is replaced and its window restarts). EMBEDIQ_OK means the
 *         entry is being monitored.
 *         EMBEDIQ_ERR — nothing changed, an existing entry keeps its timeout
 *         and deadline — for a NULL handle, a timeout outside the range, a
 *         full table, or a watchdog service that is not ready or cannot take
 *         its lock.
 */
embediq_err_t embediq_wdg_register(EmbedIQ_FB_Handle_t fb, uint32_t timeout_ms);

/**
 * Record a heartbeat: restart the FB's window.
 *
 * Calling context: any thread (an FB thread or a platform thread); not an
 * ISR. May wait for the watchdog's table lock.
 *
 * @param fb  Handle returned by embediq_fb_register(); not NULL.
 * @return EMBEDIQ_OK when the window was restarted. EMBEDIQ_ERR when the FB
 *         is not registered (its heartbeat protects nothing), the handle is
 *         NULL, or the service is not ready or cannot take its lock.
 */
embediq_err_t embediq_wdg_checkin(EmbedIQ_FB_Handle_t fb);

/**
 * Remove an FB from future scans.
 *
 * Unregistering twice is an error, not a no-op. A miss the monitor had
 * already selected when the entry was removed (or re-registered, or checked
 * in) may still be reported afterwards; unregister does not wait for the
 * monitor, so it is safe from the FB's own fault_fn.
 *
 * Calling context: any thread (an FB thread or a platform thread); not an
 * ISR. May wait for the watchdog's table lock.
 *
 * @param fb  Handle returned by embediq_fb_register(); not NULL.
 * @return EMBEDIQ_OK when the entry was removed. EMBEDIQ_ERR when the FB is
 *         not registered, the handle is NULL, or the service is not ready or
 *         cannot take its lock.
 */
embediq_err_t embediq_wdg_unregister(EmbedIQ_FB_Handle_t fb);

#ifdef __cplusplus
}
#endif

#endif /* EMBEDIQ_WDG_H */
