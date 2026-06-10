/*
 * embediq_cfg_generated.h — Auto-generated typed configuration accessors
 *
 * Generated from config/config.iq. Do not edit manually.
 * To regenerate: python3 tools/config_iq/generate.py config/config.iq --out generated/
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef EMBEDIQ_CFG_GENERATED_H
#define EMBEDIQ_CFG_GENERATED_H

#include "embediq_cfg.h"
#include "embediq_osal.h"   /* embediq_err_t, EMBEDIQ_OK, EMBEDIQ_ERR */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif


/** mqtt.host — str[63], fleet-pushable, default "" */
static inline embediq_err_t embediq_cfg_get_mqtt_host(char *out, size_t len) {
    if (!out || len == 0u) return EMBEDIQ_ERR;
    embediq_cfg_get_str("mqtt.host", out, len, "");
    return EMBEDIQ_OK;
}
static inline embediq_err_t embediq_cfg_set_mqtt_host(const char *value) {
    return embediq_cfg_set_str("mqtt.host", value) ? EMBEDIQ_OK : EMBEDIQ_ERR;
}

/** mqtt.port — u32, fleet-pushable, range 1..65535, default 1883 */
static inline embediq_err_t embediq_cfg_get_mqtt_port(uint32_t *out) {
    if (!out) return EMBEDIQ_ERR;
    *out = embediq_cfg_get_u32("mqtt.port", 1883u);
    return EMBEDIQ_OK;
}
static inline embediq_err_t embediq_cfg_set_mqtt_port(uint32_t value) {
    return embediq_cfg_set_u32("mqtt.port", value) ? EMBEDIQ_OK : EMBEDIQ_ERR;
}

/** mqtt.client_id — str[63], factory-locked */
static inline embediq_err_t embediq_cfg_get_mqtt_client_id(char *out, size_t len) {
    if (!out || len == 0u) return EMBEDIQ_ERR;
    embediq_cfg_get_str("mqtt.client_id", out, len, "");
    return EMBEDIQ_OK;
}
/* No setter — factory-locked keys are immutable at runtime */

/** mqtt.username — str[63], fleet-pushable, default "" */
static inline embediq_err_t embediq_cfg_get_mqtt_username(char *out, size_t len) {
    if (!out || len == 0u) return EMBEDIQ_ERR;
    embediq_cfg_get_str("mqtt.username", out, len, "");
    return EMBEDIQ_OK;
}
static inline embediq_err_t embediq_cfg_set_mqtt_username(const char *value) {
    return embediq_cfg_set_str("mqtt.username", value) ? EMBEDIQ_OK : EMBEDIQ_ERR;
}

/** mqtt.password — str[63], fleet-pushable, default ""
 *  SENSITIVE: value is never included in Observatory event payloads or logs. */
static inline embediq_err_t embediq_cfg_get_mqtt_password(char *out, size_t len) {
    if (!out || len == 0u) return EMBEDIQ_ERR;
    embediq_cfg_get_str("mqtt.password", out, len, "");
    return EMBEDIQ_OK;
}
static inline embediq_err_t embediq_cfg_set_mqtt_password(const char *value) {
    return embediq_cfg_set_str("mqtt.password", value) ? EMBEDIQ_OK : EMBEDIQ_ERR;
}

/** mqtt.keepalive_sec — u32, fleet-pushable, range 0..65535, default 60 */
static inline embediq_err_t embediq_cfg_get_mqtt_keepalive_sec(uint32_t *out) {
    if (!out) return EMBEDIQ_ERR;
    *out = embediq_cfg_get_u32("mqtt.keepalive_sec", 60u);
    return EMBEDIQ_OK;
}
static inline embediq_err_t embediq_cfg_set_mqtt_keepalive_sec(uint32_t value) {
    return embediq_cfg_set_u32("mqtt.keepalive_sec", value) ? EMBEDIQ_OK : EMBEDIQ_ERR;
}

/** report_interval_ms — u32, fleet-pushable, range 100..86400000, default 30000 */
static inline embediq_err_t embediq_cfg_get_report_interval_ms(uint32_t *out) {
    if (!out) return EMBEDIQ_ERR;
    *out = embediq_cfg_get_u32("report_interval_ms", 30000u);
    return EMBEDIQ_OK;
}
static inline embediq_err_t embediq_cfg_set_report_interval_ms(uint32_t value) {
    return embediq_cfg_set_u32("report_interval_ms", value) ? EMBEDIQ_OK : EMBEDIQ_ERR;
}

/** device_id — str[63], factory-locked */
static inline embediq_err_t embediq_cfg_get_device_id(char *out, size_t len) {
    if (!out || len == 0u) return EMBEDIQ_ERR;
    embediq_cfg_get_str("device_id", out, len, "");
    return EMBEDIQ_OK;
}
/* No setter — factory-locked keys are immutable at runtime */

/** log_level — u32, fleet-pushable, range 0..3, default 1 */
static inline embediq_err_t embediq_cfg_get_log_level(uint32_t *out) {
    if (!out) return EMBEDIQ_ERR;
    *out = embediq_cfg_get_u32("log_level", 1u);
    return EMBEDIQ_OK;
}
static inline embediq_err_t embediq_cfg_set_log_level(uint32_t value) {
    return embediq_cfg_set_u32("log_level", value) ? EMBEDIQ_OK : EMBEDIQ_ERR;
}

/** wifi.ssid — str[63], user-settable */
static inline embediq_err_t embediq_cfg_get_wifi_ssid(char *out, size_t len) {
    if (!out || len == 0u) return EMBEDIQ_ERR;
    embediq_cfg_get_str("wifi.ssid", out, len, "");
    return EMBEDIQ_OK;
}
static inline embediq_err_t embediq_cfg_set_wifi_ssid(const char *value) {
    return embediq_cfg_set_str("wifi.ssid", value) ? EMBEDIQ_OK : EMBEDIQ_ERR;
}

/** wifi.psk — str[63], user-settable
 *  SENSITIVE: value is never included in Observatory event payloads or logs. */
static inline embediq_err_t embediq_cfg_get_wifi_psk(char *out, size_t len) {
    if (!out || len == 0u) return EMBEDIQ_ERR;
    embediq_cfg_get_str("wifi.psk", out, len, "");
    return EMBEDIQ_OK;
}
static inline embediq_err_t embediq_cfg_set_wifi_psk(const char *value) {
    return embediq_cfg_set_str("wifi.psk", value) ? EMBEDIQ_OK : EMBEDIQ_ERR;
}

/** boot_count — u32, firmware-internal */
static inline embediq_err_t embediq_cfg_get_boot_count(uint32_t *out) {
    if (!out) return EMBEDIQ_ERR;
    *out = embediq_cfg_get_u32("boot_count", 0u);
    return EMBEDIQ_OK;
}
/* No setter — firmware-internal keys are written only by firmware internals */

#ifdef __cplusplus
}
#endif

#endif /* EMBEDIQ_CFG_GENERATED_H */
