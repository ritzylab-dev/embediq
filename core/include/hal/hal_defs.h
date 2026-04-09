/*
 * core/include/hal/hal_defs.h — Shared HAL return codes and common definitions
 *
 * All HAL contract headers include this file for return codes.
 * No peripheral-specific content belongs here — only codes shared
 * across every HAL contract.
 *
 * I-01: Compiles standalone with zero OSAL or BSP dependencies.
 * R-08: No platform headers. Only <stdint.h>.
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef HAL_DEFS_H
#define HAL_DEFS_H

#include <stdint.h>
#include "embediq_obs.h"  /* XOBS-2: HAL observation obligation macro */

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
 * Common HAL return codes — used by every hal_*.h contract
 * ------------------------------------------------------------------------- */

#define HAL_OK            0
#define HAL_ERR_INVALID  (-1)
#define HAL_ERR_BUSY     (-2)
#define HAL_ERR_TIMEOUT  (-3)
#define HAL_ERR_IO       (-4)

/* ---------------------------------------------------------------------------
 * HAL error code range assertion
 *
 * Observability encoding uses (uint8_t)(-(err)) to pack HAL error codes into
 * a single byte. Values outside [-1, -127] alias silently — the assert
 * catches any future constant added outside the safe range before it ships.
 * ------------------------------------------------------------------------- */
_Static_assert(HAL_ERR_INVALID >= -127, "HAL_ERR_INVALID out of obs encoding range");
_Static_assert(HAL_ERR_BUSY    >= -127, "HAL_ERR_BUSY out of obs encoding range");
_Static_assert(HAL_ERR_TIMEOUT >= -127, "HAL_ERR_TIMEOUT out of obs encoding range");
_Static_assert(HAL_ERR_IO      >= -127, "HAL_ERR_IO out of obs encoding range");

/* ---------------------------------------------------------------------------
 * HAL observation obligation macro — XOBS-2 active
 *
 * Every HAL function that returns a non-HAL_OK status MUST call this macro
 * before the error return. Enforced by tools/ci/check_hal_obs.py --strict
 * on every PR.
 *
 * Uses embediq_obs_emit_from_isr() because HAL functions may be called from
 * ISR context on MCU targets. Fire-and-forget — no overflow handling in HAL.
 *
 * Encoding (EMBEDIQ_OBS_EVT_HAL_FAULT = 0x67, FAULT band — always emitted):
 *   source_fb_id:  EMBEDIQ_HAL_SRC_* (FLASH/TIMER/WDG/OBS_STREAM/...)
 *   target_fb_id:  0u (N/A — HAL has no FB target)
 *   state_or_flag: (uint8_t)(-(err_code)): 1=INVALID 2=BUSY 3=TIMEOUT 4=IO
 *   msg_id:        0u (no message context)
 *
 * See: PM/CROSS_LAYER_OBSERVABILITY_PLAN.md — XOBS-2
 * ------------------------------------------------------------------------- */
#define EMBEDIQ_HAL_OBS_EMIT_ERROR(src_id, err_code)               \
    embediq_obs_emit_from_isr(EMBEDIQ_OBS_EVT_HAL_FAULT,           \
                              (uint8_t)(src_id),                    \
                              0u,                                   \
                              (uint8_t)(-(int)(err_code)),          \
                              0u)

#ifdef __cplusplus
}
#endif

#endif /* HAL_DEFS_H */
