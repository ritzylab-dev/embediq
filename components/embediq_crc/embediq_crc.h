/*
 * embediq_crc.h — CRC utility (CRC-8, CRC-16/CCITT-FALSE, CRC-32/ISO-HDLC)
 *
 * Software CRC implementations for the three variants most common in embedded
 * firmware. All three provide a single-call form and an init/update/final
 * accumulator for incremental computation over fragmented buffers.
 *
 * No global state. No dynamic allocation. No framework dependencies.
 * Safe to call from any context including ISR. (Component rule C1, C2)
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

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * CRC-8  (poly 0x07, init 0x00, no input/output reflection)
 * Standard check value: CRC-8("123456789") = 0xF4
 * ========================================================================= */

/** Return initial CRC-8 accumulator value. */
uint8_t embediq_crc8_init(void);

/**
 * Feed bytes into CRC-8 accumulator.
 * @param crc   Value from embediq_crc8_init() or previous update().
 * @param data  Input buffer. Ignored if len is 0.
 * @param len   Byte count.
 * @return      Updated accumulator.
 */
uint8_t embediq_crc8_update(uint8_t crc, const void *data, size_t len);

/** Finalise CRC-8. Returns the digest. (CRC-8 needs no XOR — same value.) */
uint8_t embediq_crc8_final(uint8_t crc);

/** Single-call CRC-8. Equivalent to init → update → final. */
uint8_t embediq_crc8(const void *data, size_t len);


/* =========================================================================
 * CRC-16/CCITT-FALSE  (poly 0x1021, init 0xFFFF, no reflection)
 * Standard check value: CRC-16("123456789") = 0x29B1
 * ========================================================================= */

/** Return initial CRC-16 accumulator value (0xFFFF). */
uint16_t embediq_crc16_init(void);

/**
 * Feed bytes into CRC-16 accumulator.
 * @param crc   Value from embediq_crc16_init() or previous update().
 * @param data  Input buffer. Ignored if len is 0.
 * @param len   Byte count.
 * @return      Updated accumulator.
 */
uint16_t embediq_crc16_update(uint16_t crc, const void *data, size_t len);

/** Finalise CRC-16. Returns the digest. (No output XOR for CCITT-FALSE.) */
uint16_t embediq_crc16_final(uint16_t crc);

/** Single-call CRC-16/CCITT-FALSE. */
uint16_t embediq_crc16(const void *data, size_t len);


/* =========================================================================
 * CRC-32/ISO-HDLC  (poly 0xEDB88320 reflected, init 0xFFFFFFFF, XOR-out)
 * Standard check value: CRC-32("123456789") = 0xCBF43926
 * ========================================================================= */

/** Return initial CRC-32 accumulator value (0xFFFFFFFF). */
uint32_t embediq_crc32_init(void);

/**
 * Feed bytes into CRC-32 accumulator.
 * @param crc   Value from embediq_crc32_init() or previous update().
 * @param data  Input buffer. Ignored if len is 0.
 * @param len   Byte count.
 * @return      Updated accumulator.
 */
uint32_t embediq_crc32_update(uint32_t crc, const void *data, size_t len);

/** Finalise CRC-32. XOR-inverts the accumulator and returns the digest. */
uint32_t embediq_crc32_final(uint32_t crc);

/** Single-call CRC-32/ISO-HDLC. */
uint32_t embediq_crc32(const void *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* EMBEDIQ_CRC_H */
