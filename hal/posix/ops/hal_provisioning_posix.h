/*
 * hal/posix/ops/hal_provisioning_posix.h — POSIX provisioning HAL declaration
 *
 * Call hal_provisioning_posix_declare() from main / platform init before
 * fb_provisioning_register().
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef HAL_PROVISIONING_POSIX_H
#define HAL_PROVISIONING_POSIX_H
#ifdef __cplusplus
extern "C" {
#endif

/**
 * Register the POSIX provisioning ops table (Option A: pre-provisioned cert).
 * Reads the device cert from EMBEDIQ_CERT_DIR and validates it with mbedTLS.
 * Must be called before fb_provisioning_register().
 */
void hal_provisioning_posix_declare(void);

#ifdef __cplusplus
}
#endif
#endif /* HAL_PROVISIONING_POSIX_H */
