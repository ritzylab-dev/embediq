/*
 * hal/posix/hal_wdg_posix.c — POSIX simulation of the HAL watchdog contract
 *
 * On POSIX there is no real hardware watchdog.  This simulation tracks
 * initialised/kicked state so the Driver FB's health-token logic can call
 * kick() correctly.  No background thread — the Driver FB owns the
 * monitoring loop.
 *
 * Only includes: hal_wdg.h (core/include/hal) and C stdlib headers.
 * Zero embediq_*.h includes — HAL has no knowledge of the framework.
 * R-02: no malloc in this file.
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "hal_wdg.h"

#include <stdint.h>

/* ---------------------------------------------------------------------------
 * Module state
 * ------------------------------------------------------------------------- */

static int      s_inited     = 0;
static uint32_t s_timeout_ms = 0u;

/* ---------------------------------------------------------------------------
 * HAL watchdog contract implementation
 * ------------------------------------------------------------------------- */

int hal_wdg_init(uint32_t timeout_ms)
{
    s_timeout_ms = timeout_ms;
    s_inited     = 1;
    return HAL_OK;
}

int hal_wdg_kick(void)
{
    if (!s_inited) {
        return HAL_ERR_INVALID;
    }
    /* POSIX sim: nothing to do — just acknowledge the kick. */
    return HAL_OK;
}

void hal_wdg_deinit(void)
{
    s_inited     = 0;
    s_timeout_ms = 0u;
}
