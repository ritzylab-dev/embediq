/*
 * embediq_platform.h — Library registration and platform init contract
 *
 * Provides embediq_platform_lib_declare(): the mechanism by which libraries
 * with global resource needs (heap, RTOS objects, peripheral claims) register
 * their init and deinit functions with the EmbedIQ framework. The engine calls
 * each unique init_fn once at boot in declaration order, and each unique
 * deinit_fn once at shutdown in reverse order.
 *
 * This is the only supported mechanism for library global initialisation.
 * Libraries must not initialise global state outside of their registered
 * init_fn. (R-lib-1, R-lib-3)
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef EMBEDIQ_PLATFORM_H
#define EMBEDIQ_PLATFORM_H

#include <stdint.h>
#include "embediq_osal.h"   /* embediq_err_t */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Register a library's global init and deinit functions.
 *
 * Deduplication: if init_fn has already been registered (same function pointer),
 * this call is silently ignored. Dedup is by pointer identity (C11 §6.5.9).
 *
 * Ordering: init functions are called in declaration order.
 *           deinit functions are called in reverse declaration order.
 *
 * deinit_fn may be NULL if the library has no teardown.
 *
 * Panics (EMBEDIQ_PANIC) if the same init_fn is declared with a different
 * deinit_fn on a second call — inconsistent teardown is a design error.
 *
 * Returns EMBEDIQ_OK on success.
 * Returns EMBEDIQ_ERR if EMBEDIQ_MAX_LIB_INITS slots are already full.
 */
embediq_err_t embediq_platform_lib_declare(void (*init_fn)(void),
                                            void (*deinit_fn)(void));

/**
 * Return the count of unique init functions registered so far.
 * Primarily used for testing and diagnostics.
 */
uint8_t embediq_platform_lib_count(void);

/**
 * Call each registered init_fn in declaration order.
 * Called once by the engine at boot — do not call manually.
 */
void embediq_platform_lib_init_all(void);

/**
 * Call each registered deinit_fn in reverse declaration order.
 * Called once by the engine at shutdown — do not call manually.
 */
void embediq_platform_lib_deinit_all(void);

#ifdef EMBEDIQ_PLATFORM_HOST
/**
 * Reset the registry to empty. Test isolation only — host builds only.
 * Never call from production firmware.
 */
void embediq_platform_lib_reset(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* EMBEDIQ_PLATFORM_H */
