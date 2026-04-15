/*
 * fb_logger_ops.h — Service FB ops table contract (template)
 *
 * Ops table that fb_logger uses to write log lines to a backend.
 * The Service FB (fb_logger) is platform-agnostic: it knows nothing about
 * files, UARTs, or network sockets. The ops table is its only interface
 * to the outside world. Platforms register a concrete ops table at boot.
 *
 * Ops table ABI rules (R-lib-4, D-LIB-5):
 *   - uint32_t version MUST be the first field (offset 0).
 *   - EMBEDIQ_LOGGER_OPS_VERSION begins at 1u and increments on any
 *     breaking change.
 *   - Callers must check `ops->version >= EMBEDIQ_LOGGER_OPS_VERSION`
 *     before invoking any function pointer.
 *   - Unused function pointers may be NULL; callers must NULL-check.
 *
 * Layer: this header is consumable from Layer 2 Service FBs and from
 * platform code that registers a concrete ops table. It has zero HAL
 * and zero OSAL dependencies (ARCHITECTURE.md §Layer 2 — Service FB).
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef FB_LOGGER_OPS_H
#define FB_LOGGER_OPS_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Current ABI version of the logger ops table. */
#define EMBEDIQ_LOGGER_OPS_VERSION  1u

/**
 * Logger backend ops table.
 *
 * Fields:
 *   version — set to EMBEDIQ_LOGGER_OPS_VERSION at ops table declaration.
 *   open    — open the output (path / topic / socket). May be NULL.
 *             `dest` is backend-specific: a filesystem path, an MQTT topic,
 *             a UART device name, etc. Returns 0 on success, non-zero error.
 *   write   — write one log line. `line` is NUL-terminated; `len` excludes NUL.
 *             Must not block indefinitely. Must not allocate.
 *   flush   — push any buffered output to the OS. May be NULL.
 *   close   — release resources. May be NULL.
 */
typedef struct {
    uint32_t  version;
    int     (*open) (const char *dest);
    void    (*write)(const char *line, uint16_t len);
    void    (*flush)(void);
    void    (*close)(void);
} embediq_logger_ops_t;

#ifdef __cplusplus
}
#endif

#endif /* FB_LOGGER_OPS_H */
