/*
 * hal/posix/ops/hal_tls_posix.h — POSIX TLS ops table accessor
 *
 * Returns the POSIX/OpenSSL TLS ops table stub. Use this in tests and in
 * platform boot code that registers the TLS ops table via
 * embediq_platform_lib_declare(). Full OpenSSL implementation is Phase 2.
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef HAL_TLS_POSIX_H
#define HAL_TLS_POSIX_H

#include "ops/embediq_tls.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Return the POSIX TLS ops table.
 *
 * All function pointers are non-NULL stubs that return EMBEDIQ_ERR.
 * Full OpenSSL implementation replaces this in Phase 2.
 * Register via embediq_platform_lib_declare() at platform boot.
 */
const embediq_tls_ops_t *hal_tls_posix_ops(void);

#ifdef __cplusplus
}
#endif

#endif /* HAL_TLS_POSIX_H */
