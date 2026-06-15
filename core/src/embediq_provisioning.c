/*
 * core/src/embediq_provisioning.c — Provisioning ops table registration store
 *
 * Implements embediq_provisioning_ops_register() + embediq_provisioning_ops_get()
 * (declared in core/include/ops/embediq_provisioning.h).
 *
 * Same pattern as core/src/embediq_mqtt.c: a single static pointer to the
 * platform ops table, set once at boot by hal_provisioning_<target>_declare().
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ops/embediq_provisioning.h"

static const embediq_provisioning_ops_t *s_ops = NULL;

void embediq_provisioning_ops_register(const embediq_provisioning_ops_t *ops)
{
    if (ops && ops->version >= EMBEDIQ_PROV_OPS_VERSION) {
        s_ops = ops;
    }
}

const embediq_provisioning_ops_t *embediq_provisioning_ops_get(void)
{
    return s_ops;
}
