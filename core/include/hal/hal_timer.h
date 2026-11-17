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
#include "hal_defs.h"

typedef void (*hal_timer_isr_t)(void *ctx);

/*
 * Lifecycle. One timer; at most one callback context is live at a time.
 * init, start, stop and deinit are called by the timer's owner thread, one
 * at a time — never concurrently. The callback may call hal_timer_stop();
 * it never calls init, start or deinit. Where the implementation runs the
 * callback on a thread of its own (POSIX), start and deinit may wait for
 * that thread to finish its current period and callback.
 * Calling context: the timer's owner thread (one caller at a time).
 */

/**
 * hal_timer_init  — configure a periodic hardware timer.
 * @period_us  desired period in microseconds; must be non-zero
 * @cb         callback invoked each period (ISR or deferred context); non-NULL
 * @ctx        opaque pointer forwarded to cb
 * @return     HAL_OK; HAL_ERR_INVALID for a NULL cb or a zero period;
 *             HAL_ERR_BUSY while a callback context from an earlier start is
 *             still held (call hal_timer_deinit() first). A refused call
 *             changes nothing.
 */
int hal_timer_init(uint32_t period_us, hal_timer_isr_t cb, void *ctx);

/**
 * hal_timer_start — enable the timer after init.
 * Already running: HAL_OK, nothing changes. After a stop, the previous
 * callback context is retired before the timer is enabled again, so the
 * callback never runs in two contexts at once.
 * @return HAL_OK; HAL_ERR_INVALID before init; HAL_ERR_IO if the previous
 *         context cannot be retired or a new one cannot be started (the
 *         timer is then stopped).
 */
int hal_timer_start(void);

/**
 * hal_timer_stop  — pause the timer; configuration preserved for restart.
 * A callback already in progress may finish; no new one starts.
 * @return HAL_OK, or HAL_ERR_INVALID before init.
 */
int hal_timer_stop(void);

/**
 * hal_timer_deinit — stop the timer, retire its callback context, then
 * release the configuration. A no-op before init and after a previous
 * deinit. A subsequent hal_timer_init() starts afresh.
 */
void hal_timer_deinit(void);
