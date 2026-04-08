/*
 * embediq_crc.h — CRC-32 utility interface (ISO-HDLC / CRC-32b)
 *
 * Software CRC-32 using the standard ISO-HDLC polynomial (0xEDB88320,
 * reflected). Provides both a single-call form and an init/update/final
 * accumulator for incremental computation. Integrates with the EmbedIQ
 * framework via embediq_lib_ops_t.
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef EMBEDIQ_CRC_H
#define EMBEDIQ_CRC_H

#include <stddef.h>
#include <stdint.h>
#include "embediq_lib.h"   /* embediq_lib_ops_t */

#ifdef __cplusplus
extern "C" {
#endif

/* ISO-HDLC reflected polynomial. Named constant — no bare literal anywhere. */
#define EMBEDIQ_CRC32_POLY  0xEDB88320u

/* ---------------------------------------------------------------------------
 * Accumulator API (incremental computation)
 * ------------------------------------------------------------------------- */

/** Return initial CRC-32 seed. Call before the first embediq_crc32_update(). */
uint32_t embediq_crc32_init(void);

/**
 * Feed bytes into the CRC accumulator.
 * @param crc   Value from embediq_crc32_init() or a previous update().
 * @param data  Input buffer. Ignored (crc returned unchanged) if len is 0.
 * @param len   Byte count.
 * @return      Updated accumulator value.
 */
uint32_t embediq_crc32_update(uint32_t crc, const void *data, size_t len);

/**
 * Finalise and return the CRC-32 digest.
 * @param crc   Accumulator from the last embediq_crc32_update() call.
 * @return      Final CRC-32 (XOR-inverted, ready to transmit or store).
 */
uint32_t embediq_crc32_final(uint32_t crc);

/* ---------------------------------------------------------------------------
 * Single-call API
 * ------------------------------------------------------------------------- */

/**
 * Compute CRC-32 over a contiguous buffer.
 * Equivalent to init → update → final.
 * Returns embediq_crc32_final(embediq_crc32_init()) == 0x00000000u for empty input.
 */
uint32_t embediq_crc32(const void *data, size_t len);

/* ---------------------------------------------------------------------------
 * Framework integration (R-lib-4)
 * ------------------------------------------------------------------------- */

/**
 * embediq_crc_ops — ops table for platform_lib_declare().
 *
 * Usage at system startup:
 *   platform_lib_declare(&embediq_crc_ops);
 *   embediq_crc_ops.init();
 *
 * embediq_crc acquires no persistent resources. init() emits
 * EMBEDIQ_OBS_EVT_LIB_INIT as a probe-point only (R-lib-1).
 */
extern embediq_lib_ops_t embediq_crc_ops;

#ifdef __cplusplus
}
#endif

#endif /* EMBEDIQ_CRC_H */
