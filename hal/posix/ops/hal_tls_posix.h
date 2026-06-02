/*
 * hal/posix/ops/hal_tls_posix.h — POSIX TLS ops table accessor (contract v2)
 *
 * Returns the POSIX/mbedTLS TLS ops table for a specific connection slot.
 * Use hal_tls_posix_ops_slot(idx) to get a slot-specific ops table.
 * Use hal_tls_posix_ops() as a convenience alias for slot 0.
 *
 * Full mbedTLS implementation replaces the stubs in Item 5.5 PR-C.
 * Register the returned ops table via embediq_platform_lib_declare() at boot.
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef HAL_TLS_POSIX_H
#define HAL_TLS_POSIX_H

#include <stdint.h>
#include "ops/embediq_tls.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Return the POSIX TLS ops table for connection slot idx.
 *
 * Returns NULL if idx >= EMBEDIQ_TLS_MAX_CONNECTIONS.
 * Caller must check for NULL and return EMBEDIQ_TLS_ERR_NO_RESOURCES.
 *
 * All function pointers in the returned table are non-NULL stubs that
 * return EMBEDIQ_TLS_ERR_NOT_CONFIGURED until PR-C implements them.
 */
const embediq_tls_ops_t *hal_tls_posix_ops_slot(uint8_t idx);

/**
 * Convenience alias for hal_tls_posix_ops_slot(0).
 * Use when only one TLS connection is needed.
 */
const embediq_tls_ops_t *hal_tls_posix_ops(void);

#ifdef __cplusplus
}
#endif

#endif /* HAL_TLS_POSIX_H */
