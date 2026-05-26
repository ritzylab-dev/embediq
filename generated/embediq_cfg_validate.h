/*
 * embediq_cfg_validate.h — Auto-generated configuration schema types and lookup
 *
 * Generated from config/config.iq. Do not edit manually.
 * To regenerate: python3 tools/config_iq/generate.py config/config.iq --out generated/
 *
 * Declares cfg_mut_t, cfg_type_t, cfg_schema_entry_t, and cfg_schema_lookup().
 * Included by: embediq_cfg_validate.c and fbs/drivers/fb_nvm.c (Item 4 PR-D).
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef EMBEDIQ_CFG_VALIDATE_H
#define EMBEDIQ_CFG_VALIDATE_H

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

/**
 * Look up a key in the generated schema table.
 *
 * @param key  NUL-terminated key name (dot notation).
 * @return     Pointer to matching cfg_schema_entry_t, or NULL if key is unknown.
 */
const cfg_schema_entry_t *cfg_schema_lookup(const char *key);

#endif /* EMBEDIQ_CFG_VALIDATE_H */
