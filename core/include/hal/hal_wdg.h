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
#pragma once
#include <stdint.h>
#include "hal_timer.h"

int  hal_wdg_init(uint32_t timeout_ms);
/** hal_wdg_kick — reset the watchdog timer.  Must be called before timeout. */
int  hal_wdg_kick(void);
void hal_wdg_deinit(void);
