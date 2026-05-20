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

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Mutability classes — must match config.iq grammar */
typedef enum {
    CFG_MUT_FACTORY  = 0,
    CFG_MUT_FLEET    = 1,
    CFG_MUT_USER     = 2,
    CFG_MUT_FIRMWARE = 3,
} cfg_mut_t;

/* Value types — must match config.iq grammar */
typedef enum {
    CFG_TYPE_U32  = 0,
    CFG_TYPE_I32  = 1,
    CFG_TYPE_F32  = 2,
    CFG_TYPE_BOOL = 3,
    CFG_TYPE_STR  = 4,
} cfg_type_t;

typedef struct {
    const char *key;         /* NUL-terminated key name — dot notation */
    cfg_type_t  type;
    cfg_mut_t   mutability;
    bool        sensitive;   /* true = exclude value from all log/obs output */
    union {
        struct { uint32_t dflt; uint32_t min; uint32_t max; } u32;
        struct { int32_t  dflt; int32_t  min; int32_t  max; } i32;
        struct { float    dflt; float    min; float    max; } f32;
        struct { bool     dflt;                             } b;
        struct { const char *dflt; uint8_t max_len;         } str;
    } val;
} cfg_schema_entry_t;


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
