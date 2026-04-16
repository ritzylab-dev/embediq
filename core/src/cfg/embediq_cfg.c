/*
 * core/src/cfg/embediq_cfg.c — Typed configuration helper
 *
 * Implements the embediq_cfg.h API as thin, typed call-throughs to the
 * embediq_nvm.h contract. Values are stored as their fixed-width byte
 * representation (little- or big-endian — consistent with the host's
 * native layout; fb_nvm treats the bytes opaquely).
 *
 * Lives in core/src/ (not components/) because it depends on the
 * framework's embediq_nvm.h contract. core/src is the one layer that
 * may include core/include/ headers by design (boundary_checker.py).
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "embediq_cfg.h"
#include "embediq_nvm.h"
#include "embediq_osal.h"   /* embediq_err_t, EMBEDIQ_OK */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/* ---------------------------------------------------------------------------
 * Typed getters
 * ------------------------------------------------------------------------- */

float embediq_cfg_get_float(const char *key, float default_val)
{
    if (key == NULL) return default_val;
    uint8_t  buf[sizeof(float)];
    uint32_t len = (uint32_t)sizeof(float);
    if (embediq_nvm_get(key, buf, &len) != EMBEDIQ_OK
        || len != (uint32_t)sizeof(float)) {
        return default_val;
    }
    float v;
    memcpy(&v, buf, sizeof(float));
    return v;
}

int32_t embediq_cfg_get_i32(const char *key, int32_t default_val)
{
    if (key == NULL) return default_val;
    uint8_t  buf[sizeof(int32_t)];
    uint32_t len = (uint32_t)sizeof(int32_t);
    if (embediq_nvm_get(key, buf, &len) != EMBEDIQ_OK
        || len != (uint32_t)sizeof(int32_t)) {
        return default_val;
    }
    int32_t v;
    memcpy(&v, buf, sizeof(int32_t));
    return v;
}

uint32_t embediq_cfg_get_u32(const char *key, uint32_t default_val)
{
    if (key == NULL) return default_val;
    uint8_t  buf[sizeof(uint32_t)];
    uint32_t len = (uint32_t)sizeof(uint32_t);
    if (embediq_nvm_get(key, buf, &len) != EMBEDIQ_OK
        || len != (uint32_t)sizeof(uint32_t)) {
        return default_val;
    }
    uint32_t v;
    memcpy(&v, buf, sizeof(uint32_t));
    return v;
}

bool embediq_cfg_get_bool(const char *key, bool default_val)
{
    if (key == NULL) return default_val;
    uint8_t  raw;
    uint32_t len = 1u;
    if (embediq_nvm_get(key, &raw, &len) != EMBEDIQ_OK || len != 1u) {
        return default_val;
    }
    return (raw != 0u);
}

size_t embediq_cfg_get_str(const char *key, char *buf,
                           size_t buf_len, const char *default_str)
{
    if (buf == NULL || buf_len == 0u) return 0u;

    /* Reserve one byte for the NUL terminator. */
    size_t capacity = buf_len - 1u;

    if (key != NULL) {
        /* fb_nvm returns an error if the requested buffer is smaller than
         * the stored value, so we read into a full-size scratch buffer and
         * truncate here. This preserves the "always returns something and
         * NUL-terminates" contract for short caller buffers. */
        uint8_t  raw[EMBEDIQ_NVM_VAL_MAX];
        uint32_t len = (uint32_t)sizeof(raw);
        if (embediq_nvm_get(key, raw, &len) == EMBEDIQ_OK) {
            size_t n = (size_t)len;
            if (n > capacity) n = capacity;
            memcpy(buf, raw, n);
            buf[n] = '\0';
            return n;
        }
    }

    /* Fall-through: key absent (or NULL) — copy default_str truncated. */
    if (default_str != NULL) {
        size_t n = strlen(default_str);
        if (n > capacity) n = capacity;
        memcpy(buf, default_str, n);
        buf[n] = '\0';
        return n;
    }

    buf[0] = '\0';
    return 0u;
}

/* ---------------------------------------------------------------------------
 * Typed setters
 * ------------------------------------------------------------------------- */

bool embediq_cfg_set_float(const char *key, float value)
{
    if (key == NULL) return false;
    uint8_t buf[sizeof(float)];
    memcpy(buf, &value, sizeof(float));
    return (embediq_nvm_set(key, buf, (uint32_t)sizeof(float)) == EMBEDIQ_OK);
}

bool embediq_cfg_set_i32(const char *key, int32_t value)
{
    if (key == NULL) return false;
    uint8_t buf[sizeof(int32_t)];
    memcpy(buf, &value, sizeof(int32_t));
    return (embediq_nvm_set(key, buf, (uint32_t)sizeof(int32_t)) == EMBEDIQ_OK);
}

bool embediq_cfg_set_u32(const char *key, uint32_t value)
{
    if (key == NULL) return false;
    uint8_t buf[sizeof(uint32_t)];
    memcpy(buf, &value, sizeof(uint32_t));
    return (embediq_nvm_set(key, buf, (uint32_t)sizeof(uint32_t)) == EMBEDIQ_OK);
}

bool embediq_cfg_set_bool(const char *key, bool value)
{
    if (key == NULL) return false;
    uint8_t raw = value ? 1u : 0u;
    return (embediq_nvm_set(key, &raw, 1u) == EMBEDIQ_OK);
}

bool embediq_cfg_set_str(const char *key, const char *value)
{
    if (key == NULL || value == NULL) return false;
    /* Store the string body without the NUL — get_str reads length back
     * from NVM and NUL-terminates in the caller's buffer. */
    uint32_t len = (uint32_t)strlen(value);
    return (embediq_nvm_set(key, (const uint8_t *)value, len) == EMBEDIQ_OK);
}

bool embediq_cfg_delete(const char *key)
{
    if (key == NULL) return false;
    return (embediq_nvm_delete(key) == EMBEDIQ_OK);
}
