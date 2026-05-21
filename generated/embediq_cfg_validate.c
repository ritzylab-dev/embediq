/*
 * embediq_cfg_validate.c — Auto-generated configuration validation table
 *
 * Generated from config/config.iq. Do not edit manually.
 * To regenerate: python3 tools/config_iq/generate.py config/config.iq --out generated/
 *
 * Used by: fb_nvm.c blob loader (Item 4 PR-D) and embediq_nvs_gen.py (Item 4 PR-C).
 * At boot: unknown keys are skipped. Mutability is enforced against this table.
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "embediq_cfg_validate.h"
#include <string.h>   /* strcmp — used by cfg_schema_lookup() */


static const cfg_schema_entry_t k_cfg_schema[] = {
    /* key                   type           mutability       sens  val */
    { "mqtt.host",           CFG_TYPE_STR,  CFG_MUT_FLEET,   false, { .str = { .dflt = "", .max_len = 63 } } },
    { "mqtt.port",           CFG_TYPE_U32,  CFG_MUT_FLEET,   false, { .u32 = { .dflt = 1883, .min = 1, .max = 65535 } } },
    { "report_interval_ms",  CFG_TYPE_U32,  CFG_MUT_FLEET,   false, { .u32 = { .dflt = 30000, .min = 100, .max = 86400000 } } },
    { "device_id",           CFG_TYPE_STR,  CFG_MUT_FACTORY, false, { .str = { .dflt = "", .max_len = 63 } } },
    { "log_level",           CFG_TYPE_U32,  CFG_MUT_FLEET,   false, { .u32 = { .dflt = 1, .min = 0, .max = 3 } } },
    { "wifi.ssid",           CFG_TYPE_STR,  CFG_MUT_USER,    false, { .str = { .dflt = "", .max_len = 63 } } },
    { "wifi.psk",            CFG_TYPE_STR,  CFG_MUT_USER,    true,  { .str = { .dflt = "", .max_len = 63 } } },
    { "boot_count",          CFG_TYPE_U32,  CFG_MUT_FIRMWARE,false, { .u32 = { .dflt = 0, .min = 0, .max = UINT32_MAX } } },
};

#define EMBEDIQ_CFG_SCHEMA_COUNT \
    ((uint16_t)(sizeof(k_cfg_schema) / sizeof(k_cfg_schema[0])))

const cfg_schema_entry_t *cfg_schema_lookup(const char *key)
{
    uint16_t i;
    for (i = 0u; i < EMBEDIQ_CFG_SCHEMA_COUNT; i++) {
        if (strcmp(key, k_cfg_schema[i].key) == 0) {
            return &k_cfg_schema[i];
        }
    }
    return NULL;
}
