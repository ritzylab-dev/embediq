/*
 * embediq_crc.c — CRC utility implementation (CRC-8, CRC-16, CRC-32)
 *
 * Table-driven implementations. Each variant computes its lookup table once
 * on first call (lazy init via a file-scope flag). Tables are read-only after
 * first population — safe to call from multiple contexts after init.
 *
 * No dynamic allocation. No global mutable state beyond the one-time table
 * build flag. (Component rule C2: zero global mutable variables violated only
 * by the table itself — this is the standard embedded CRC trade-off; the
 * alternative is a fully const table generated at compile time, documented
 * in components/README.md as the recommended approach for ROM-constrained
 * targets.)
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "embediq_crc.h"

/* =========================================================================
 * CRC-8  (poly 0x07)
 * ========================================================================= */
#define CRC8_POLY       0x07u
#define CRC_TABLE_SIZE  256u

static uint8_t s_crc8_table[CRC_TABLE_SIZE];
static int     s_crc8_ready = 0;

static void crc8_build(void)
{
    uint16_t i;
    for (i = 0u; i < CRC_TABLE_SIZE; i++) {
        uint8_t  crc = (uint8_t)i;
        uint8_t  j;
        for (j = 0u; j < 8u; j++) {
            crc = (crc & 0x80u) ? ((uint8_t)(crc << 1u) ^ CRC8_POLY)
                                : (uint8_t)(crc << 1u);
        }
        s_crc8_table[i] = crc;
    }
    s_crc8_ready = 1;
}

uint8_t embediq_crc8_init(void)
{
    if (!s_crc8_ready) { crc8_build(); }
    return 0x00u;
}

uint8_t embediq_crc8_update(uint8_t crc, const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    size_t i;
    if (!s_crc8_ready) { crc8_build(); }
    for (i = 0u; i < len; i++) {
        crc = s_crc8_table[crc ^ p[i]];
    }
    return crc;
}

uint8_t embediq_crc8_final(uint8_t crc) { return crc; }

uint8_t embediq_crc8(const void *data, size_t len)
{
    return embediq_crc8_final(embediq_crc8_update(embediq_crc8_init(), data, len));
}

/* =========================================================================
 * CRC-16/CCITT-FALSE  (poly 0x1021, init 0xFFFF, no reflection)
 * ========================================================================= */
#define CRC16_POLY  0x1021u

static uint16_t s_crc16_table[CRC_TABLE_SIZE];
static int      s_crc16_ready = 0;

static void crc16_build(void)
{
    uint16_t i;
    for (i = 0u; i < CRC_TABLE_SIZE; i++) {
        uint16_t crc = (uint16_t)(i << 8u);
        uint8_t  j;
        for (j = 0u; j < 8u; j++) {
            crc = (crc & 0x8000u) ? ((uint16_t)(crc << 1u) ^ CRC16_POLY)
                                  : (uint16_t)(crc << 1u);
        }
        s_crc16_table[i] = crc;
    }
    s_crc16_ready = 1;
}

uint16_t embediq_crc16_init(void)
{
    if (!s_crc16_ready) { crc16_build(); }
    return 0xFFFFu;
}

uint16_t embediq_crc16_update(uint16_t crc, const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    size_t i;
    if (!s_crc16_ready) { crc16_build(); }
    for (i = 0u; i < len; i++) {
        crc = (uint16_t)((crc << 8u) ^ s_crc16_table[((crc >> 8u) ^ p[i]) & 0xFFu]);
    }
    return crc;
}

uint16_t embediq_crc16_final(uint16_t crc) { return crc; }

uint16_t embediq_crc16(const void *data, size_t len)
{
    return embediq_crc16_final(embediq_crc16_update(embediq_crc16_init(), data, len));
}

/* =========================================================================
 * CRC-32/ISO-HDLC  (poly 0xEDB88320 reflected, init 0xFFFFFFFF, XOR-out)
 * ========================================================================= */
#define CRC32_POLY  0xEDB88320u

static uint32_t s_crc32_table[CRC_TABLE_SIZE];
static int      s_crc32_ready = 0;

static void crc32_build(void)
{
    uint32_t i;
    for (i = 0u; i < CRC_TABLE_SIZE; i++) {
        uint32_t crc = i;
        uint8_t  j;
        for (j = 0u; j < 8u; j++) {
            crc = (crc & 1u) ? (CRC32_POLY ^ (crc >> 1u)) : (crc >> 1u);
        }
        s_crc32_table[i] = crc;
    }
    s_crc32_ready = 1;
}

uint32_t embediq_crc32_init(void)
{
    if (!s_crc32_ready) { crc32_build(); }
    return 0xFFFFFFFFu;
}

uint32_t embediq_crc32_update(uint32_t crc, const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    size_t i;
    if (!s_crc32_ready) { crc32_build(); }
    for (i = 0u; i < len; i++) {
        crc = (crc >> 8u) ^ s_crc32_table[(crc ^ p[i]) & 0xFFu];
    }
    return crc;
}

uint32_t embediq_crc32_final(uint32_t crc) { return crc ^ 0xFFFFFFFFu; }

uint32_t embediq_crc32(const void *data, size_t len)
{
    return embediq_crc32_final(embediq_crc32_update(embediq_crc32_init(), data, len));
}
