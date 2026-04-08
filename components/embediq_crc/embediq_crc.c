/*
 * embediq_crc.c — CRC-32 utility implementation (ISO-HDLC / CRC-32b)
 *
 * Table-driven CRC-32 built with the EMBEDIQ_CRC32_POLY reflected polynomial.
 * Table is computed once on first use (no static initialiser — portable to
 * all C11 targets including MCU with no BSS zeroing guarantee at table scope).
 * No dynamic allocation. No RTOS dependency. ISR-safe (read-only after init).
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "embediq_crc.h"

/* ---------------------------------------------------------------------------
 * Lookup table — built once on first call to embediq_crc32_init()
 * ------------------------------------------------------------------------- */

/** CRC-32 lookup table entries — one per byte value (0x00–0xFF). */
#define EMBEDIQ_CRC32_TABLE_SIZE  256u

static uint32_t s_table[EMBEDIQ_CRC32_TABLE_SIZE];
static int      s_ready = 0;

static void build_table(void)
{
    uint32_t i;
    for (i = 0u; i < EMBEDIQ_CRC32_TABLE_SIZE; i++) {
        uint32_t c = i;
        uint32_t j;
        for (j = 0u; j < 8u; j++) {
            c = (c & 1u) ? (EMBEDIQ_CRC32_POLY ^ (c >> 1u)) : (c >> 1u);
        }
        s_table[i] = c;
    }
    s_ready = 1;
}

/* ---------------------------------------------------------------------------
 * Accumulator API
 * ------------------------------------------------------------------------- */

uint32_t embediq_crc32_init(void)
{
    if (!s_ready) {
        build_table();
    }
    return 0xFFFFFFFFu;
}

uint32_t embediq_crc32_update(uint32_t crc, const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    size_t i;

    if (!s_ready) {
        build_table();
    }
    for (i = 0u; i < len; i++) {
        crc = (crc >> 8u) ^ s_table[(crc ^ p[i]) & 0xFFu];
    }
    return crc;
}

uint32_t embediq_crc32_final(uint32_t crc)
{
    return crc ^ 0xFFFFFFFFu;
}

uint32_t embediq_crc32(const void *data, size_t len)
{
    if (data == NULL || len == 0u) {
        return embediq_crc32_final(embediq_crc32_init());
    }
    return embediq_crc32_final(embediq_crc32_update(embediq_crc32_init(), data, len));
}

/* ---------------------------------------------------------------------------
 * Ops table — framework integration (R-lib-4)
 * ------------------------------------------------------------------------- */

static embediq_err_t crc_init(void)
{
    /* embediq_crc acquires no persistent resources.
     * Emit LIB_INIT as a lifecycle probe-point (R-lib-1).
     * Call through ops table obs_emit, not embediq_obs_emit() directly (R-lib-2).
     * Guard NULL: obs_emit is NULL until platform_lib_declare() assigns it. */
    if (embediq_crc_ops.obs_emit != NULL) {
        embediq_crc_ops.obs_emit(
            0x40u,                      /* EMBEDIQ_OBS_EVT_LIB_INIT */
            embediq_crc_ops.source_id,  /* assigned by platform_lib_declare() */
            0u, 0u, 0u);
    }
    return EMBEDIQ_OK;
}

embediq_lib_ops_t embediq_crc_ops = {
    .name      = "embediq_crc",
    .init      = crc_init,
    .deinit    = NULL,    /* no resources to release */
    .obs_emit  = NULL,    /* assigned by platform_lib_declare() at startup */
    .source_id = 0u       /* assigned by platform_lib_declare() at startup */
};
