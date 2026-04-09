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

#ifdef __cplusplus
}
#endif

#endif /* HAL_DEFS_H */
