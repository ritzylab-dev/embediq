/*
 * core/include/hal/hal_wdg.h — HAL watchdog contract
 *
 * Used by fb_watchdog (Driver FB) via the embediq_wdg.h ops table.
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef HAL_WDG_H
#define HAL_WDG_H

#include <stdint.h>
#include "hal_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

int  hal_wdg_init(uint32_t timeout_ms);
/** hal_wdg_kick — reset the watchdog timer.  Must be called before timeout. */
int  hal_wdg_kick(void);
void hal_wdg_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* HAL_WDG_H */
