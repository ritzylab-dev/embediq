/*
 * fb_provisioning.h — Device Identity Lifecycle Gatekeeper
 *
 * Validates the device cert (via the provisioning ops table), derives and
 * writes mqtt.client_id from the cert CN, and manages the provisioning FSM
 * (UNMANUFACTURED → MANUFACTURED → CUSTOMER_PROVISIONED → OPERATIONAL).
 * boot_phase = EMBEDIQ_BOOT_PHASE_INFRASTRUCTURE.
 *
 * See ARCHITECTURE.md: fb_provisioning — Device Identity Lifecycle Gatekeeper.
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef FB_PROVISIONING_H
#define FB_PROVISIONING_H

#ifdef __cplusplus
extern "C" {
#endif

/** Register the fb_provisioning Service FB with the framework. */
void fb_provisioning_register(void);

#ifdef __cplusplus
}
#endif

#endif /* FB_PROVISIONING_H */
