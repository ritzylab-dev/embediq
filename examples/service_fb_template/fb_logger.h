/*
 * fb_logger.h — Service FB: log collector template
 *
 * Subscribes to MSG_LOG_ENTRY and writes each entry to the registered
 * backend via the embediq_logger_ops_t ops table. This is a minimal
 * Service FB template — copy it, rename, and swap the ops table for
 * your real backend (file, UART, network, syslog, cloud, etc.).
 *
 * Layer: Layer 2 Service FB (platform-agnostic). Accesses all hardware
 * only via the ops table — never includes HAL headers directly.
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef FB_LOGGER_H
#define FB_LOGGER_H

#include "embediq_fb.h"
#include "fb_logger_ops.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Register fb_logger with the framework.
 *
 * `ops` must outlive the FB (point at static storage). The caller remains
 * owner of the ops table memory. fb_logger does not copy it.
 *
 * Returns the FB handle, or NULL on error.
 * Call before embediq_engine_boot().
 */
EmbedIQ_FB_Handle_t fb_logger_register(const embediq_logger_ops_t *ops);

#ifdef __cplusplus
}
#endif

#endif /* FB_LOGGER_H */
