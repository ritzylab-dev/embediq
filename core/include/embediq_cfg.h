/*
 * embediq_cfg.h — Typed configuration helper (application-facing)
 *
 * A thin typed wrapper over the embediq_nvm.h key-value store.
 * Provides get-with-default and set for float, int32, uint32, bool,
 * and NUL-terminated strings. Not a Function Block — no dispatch
 * thread, no messages, no subscriptions.
 *
 * Call site pattern (application FB init):
 *
 *   float kp = embediq_cfg_get_float("motor.pid_kp", 1.0f);
 *   uint32_t port = embediq_cfg_get_u32("mqtt.port", 1883u);
 *
 * Call site pattern (handler persisting a new value):
 *
 *   (void)embediq_cfg_set_float("motor.pid_kp", new_kp);
 *
 * Key convention: "namespace.key_name" — for example "motor.pid_kp",
 * "mqtt.host", "logger.level". Keep keys under EMBEDIQ_NVM_KEY_MAX
 * bytes (including NUL). Over-long keys are rejected by fb_nvm.
 *
 * Storage is owned by fb_nvm (the Layer 2 Driver FB). embediq_cfg has
 * no global mutable state of its own — the getter / setter calls pass
 * straight through to embediq_nvm_get / _set / _delete.
 *
 * Durability: fb_nvm persists at its own cadence. Callers that must
 * guarantee a value is flushed to storage before continuing should call
 * embediq_nvm_flush() explicitly (for example, before MSG_SYS_OTA_READY).
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef EMBEDIQ_CFG_H
#define EMBEDIQ_CFG_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
 * Typed getters — return stored value, or default_val if the key is absent,
 * the stored length doesn't match the expected width, or NVM returned an
 * error. Never block. Never allocate.
 * ------------------------------------------------------------------------- */

float    embediq_cfg_get_float(const char *key, float    default_val);
int32_t  embediq_cfg_get_i32  (const char *key, int32_t  default_val);
uint32_t embediq_cfg_get_u32  (const char *key, uint32_t default_val);
bool     embediq_cfg_get_bool (const char *key, bool     default_val);

/**
 * Fetch a NUL-terminated string into `buf` of capacity `buf_len`.
 * If the key is absent, the default string (if non-NULL) is copied in
 * (also truncated to buf_len-1). In both cases the output is always
 * NUL-terminated when buf_len > 0.
 *
 * @return number of bytes written before the NUL terminator.
 */
size_t   embediq_cfg_get_str  (const char *key, char *buf,
                                size_t buf_len, const char *default_str);

/* ---------------------------------------------------------------------------
 * Typed setters — persist the value under `key`. Return true on success,
 * false on NVM error, over-long key, or store full.
 * ------------------------------------------------------------------------- */

bool     embediq_cfg_set_float(const char *key, float    value);
bool     embediq_cfg_set_i32  (const char *key, int32_t  value);
bool     embediq_cfg_set_u32  (const char *key, uint32_t value);
bool     embediq_cfg_set_bool (const char *key, bool     value);
bool     embediq_cfg_set_str  (const char *key, const char *value);

/**
 * Remove the entry for the given key. Returns true on success or if the
 * key was already absent (fb_nvm makes delete idempotent).
 */
bool     embediq_cfg_delete   (const char *key);

#ifdef __cplusplus
}
#endif

#endif /* EMBEDIQ_CFG_H */
