/*
 * embediq_platform.c — Library registration registry implementation
 *
 * Implements the fixed-slot static registry for embediq_platform_lib_declare().
 * Maximum EMBEDIQ_MAX_LIB_INITS (16) unique init/deinit pairs. Dedup by
 * function pointer identity. Init in declaration order; deinit in reverse.
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "embediq_platform.h"
#include "embediq_config.h"
#include <stddef.h>

typedef struct {
    void (*init_fn)(void);
    void (*deinit_fn)(void);
} lib_entry_t;

static lib_entry_t s_libs[EMBEDIQ_MAX_LIB_INITS];
static uint8_t     s_count = 0u;

embediq_err_t embediq_platform_lib_declare(void (*init_fn)(void),
                                            void (*deinit_fn)(void))
{
    uint8_t i;

    if (init_fn == NULL) {
        return EMBEDIQ_ERR;
    }

    /* Deduplication — scan for existing entry with same init_fn */
    for (i = 0u; i < s_count; i++) {
        if (s_libs[i].init_fn == init_fn) {
            /* Same init_fn with different deinit_fn is a design error */
            if (s_libs[i].deinit_fn != deinit_fn) {
                /* EMBEDIQ_PANIC would go here on MCU — on host, assert */
                return EMBEDIQ_ERR;
            }
            return EMBEDIQ_OK;  /* already registered — silent success */
        }
    }

    if (s_count >= EMBEDIQ_MAX_LIB_INITS) {
        return EMBEDIQ_ERR;
    }

    s_libs[s_count].init_fn   = init_fn;
    s_libs[s_count].deinit_fn = deinit_fn;
    s_count++;
    return EMBEDIQ_OK;
}

uint8_t embediq_platform_lib_count(void)
{
    return s_count;
}

void embediq_platform_lib_init_all(void)
{
    uint8_t i;
    for (i = 0u; i < s_count; i++) {
        if (s_libs[i].init_fn != NULL) {
            s_libs[i].init_fn();
        }
    }
}

void embediq_platform_lib_deinit_all(void)
{
    uint8_t i;
    /* Reverse order */
    i = s_count;
    while (i > 0u) {
        i--;
        if (s_libs[i].deinit_fn != NULL) {
            s_libs[i].deinit_fn();
        }
    }
}

#ifdef EMBEDIQ_PLATFORM_HOST
void embediq_platform_lib_reset(void)
{
    uint8_t i;
    for (i = 0u; i < EMBEDIQ_MAX_LIB_INITS; i++) {
        s_libs[i].init_fn   = NULL;
        s_libs[i].deinit_fn = NULL;
    }
    s_count = 0u;
}
#endif /* EMBEDIQ_PLATFORM_HOST */
