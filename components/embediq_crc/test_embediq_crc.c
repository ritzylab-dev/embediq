/*
 * test_embediq_crc.c — Unit tests for embediq_crc (CRC-8, CRC-16, CRC-32)
 *
 * Verifies all three CRC variants against published standard check vectors.
 * CRC-8 (0xF4 for "123456789"), CRC-16/CCITT-FALSE (0x29B1),
 * CRC-32/ISO-HDLC (0xCBF43926). Incremental accumulation verified
 * to match single-call form for each variant.
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "embediq_crc.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Standard check vector — same input used for all three variants */
#define CHECK_INPUT    "123456789"
#define CHECK_LEN      9u

/* Published check values (from https://crccalc.com/) */
#define CRC8_CHECK     0xF4u        /* CRC-8 (poly 0x07, init 0x00) */
#define CRC16_CHECK    0x29B1u      /* CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF) */
#define CRC32_CHECK    0xCBF43926u  /* CRC-32/ISO-HDLC (poly 0xEDB88320 reflected) */

static int s_run = 0, s_pass = 0;

#define TEST(name, expr) \
    do { s_run++; \
         if(expr){s_pass++;printf("PASS  %s\n",name);} \
         else{printf("FAIL  %s (line %d)\n",name,__LINE__);} \
    } while(0)

static void test_crc8(void)
{
    const uint8_t *in = (const uint8_t *)CHECK_INPUT;

    /* Standard check vector */
    TEST("crc8_check_vector", embediq_crc8(in, CHECK_LEN) == CRC8_CHECK);

    /* Incremental == single-call */
    {
        uint8_t a = embediq_crc8(in, CHECK_LEN);
        uint8_t b = embediq_crc8_final(
                        embediq_crc8_update(
                            embediq_crc8_update(embediq_crc8_init(), in, 4u),
                            in + 4u, CHECK_LEN - 4u));
        TEST("crc8_incremental", a == b);
    }

    /* Empty input */
    TEST("crc8_empty", embediq_crc8(NULL, 0u) == embediq_crc8_final(embediq_crc8_init()));
}

static void test_crc16(void)
{
    const uint8_t *in = (const uint8_t *)CHECK_INPUT;

    TEST("crc16_check_vector", embediq_crc16(in, CHECK_LEN) == CRC16_CHECK);

    {
        uint16_t a = embediq_crc16(in, CHECK_LEN);
        uint16_t b = embediq_crc16_final(
                         embediq_crc16_update(
                             embediq_crc16_update(embediq_crc16_init(), in, 4u),
                             in + 4u, CHECK_LEN - 4u));
        TEST("crc16_incremental", a == b);
    }

    TEST("crc16_empty", embediq_crc16(NULL, 0u) == embediq_crc16_final(embediq_crc16_init()));
}

static void test_crc32(void)
{
    const uint8_t *in = (const uint8_t *)CHECK_INPUT;

    TEST("crc32_check_vector", embediq_crc32(in, CHECK_LEN) == CRC32_CHECK);

    {
        uint32_t a = embediq_crc32(in, CHECK_LEN);
        uint32_t b = embediq_crc32_final(
                         embediq_crc32_update(
                             embediq_crc32_update(embediq_crc32_init(), in, 4u),
                             in + 4u, CHECK_LEN - 4u));
        TEST("crc32_incremental", a == b);
    }

    TEST("crc32_empty", embediq_crc32(NULL, 0u) == embediq_crc32_final(embediq_crc32_init()));
}

int main(void)
{
    printf("=== test_embediq_crc ===\n");
    test_crc8();
    test_crc16();
    test_crc32();
    printf("\n%d/%d passed\n", s_pass, s_run);
    return (s_pass == s_run) ? 0 : 1;
}
