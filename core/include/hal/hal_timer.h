/*
 * core/include/hal/hal_timer.h — HAL timer contract
 *
 * Periodic hardware timer.  The callback is invoked from the timer ISR or
 * equivalent platform context; keep it short and signal a semaphore.
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include <stdint.h>

/* Common HAL return codes — defined here, shared across all hal_*.h */
#define HAL_OK            0
#define HAL_ERR_INVALID  (-1)
#define HAL_ERR_BUSY     (-2)
#define HAL_ERR_TIMEOUT  (-3)
#define HAL_ERR_IO       (-4)

typedef void (*hal_timer_isr_t)(void *ctx);

/**
 * hal_timer_init  — configure a periodic hardware timer.
 * @period_us  desired period in microseconds
 * @cb         callback invoked each period (ISR or deferred context)
 * @ctx        opaque pointer forwarded to cb
 * @return     HAL_OK or negative error code
 */
int hal_timer_init(uint32_t period_us, hal_timer_isr_t cb, void *ctx);

/** hal_timer_start — enable the timer after init.  */
int hal_timer_start(void);

/** hal_timer_stop  — pause the timer; state preserved for restart.  */
int hal_timer_stop(void);

/** hal_timer_deinit — release all timer resources.  */
void hal_timer_deinit(void);
