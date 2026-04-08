/*
 * test_embediq_crc.c — Unit tests for the CRC-32b utility
 *
 * Verifies embediq_crc against the ISO-HDLC standard test vector
 * ("123456789" → 0xCBF43926) and checks incremental accumulation
 * produces the same result as the single-call form.
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

/* ISO-HDLC standard check value: CRC-32 of "123456789" */
#define CRC32_STDVEC_INPUT    "123456789"
#define CRC32_STDVEC_EXPECTED 0xCBF43926u

static int s_run    = 0;
static int s_passed = 0;

#define TEST(name, expr)                          \
    do {                                          \
        s_run++;                                  \
        if (expr) { s_passed++; printf("PASS  %s\n", name); } \
        else              { printf("FAIL  %s\n", name); }      \
    } while (0)

int main(void)
{
    /* Standard ISO-HDLC check vector */
    {
        const char *in = CRC32_STDVEC_INPUT;
        TEST("iso_hdlc_check_vector",
             embediq_crc32(in, strlen(in)) == CRC32_STDVEC_EXPECTED);
    }

    /* Single-call equals incremental accumulation */
    {
        const char *msg = "EmbedIQ library";
        uint32_t single = embediq_crc32(msg, strlen(msg));
        uint32_t crc    = embediq_crc32_init();
        crc = embediq_crc32_update(crc, msg,     8u);
        crc = embediq_crc32_update(crc, msg + 8, strlen(msg) - 8u);
        crc = embediq_crc32_final(crc);
        TEST("incremental_matches_single", single == crc);
    }

    /* Empty input returns the post-XOR of an untouched init (0x00000000) */
    {
        TEST("empty_input", embediq_crc32(NULL, 0u) == 0x00000000u);
    }

    /* Two calls on the same data return the same result */
    {
        const char *msg = "repeat";
        TEST("idempotent",
             embediq_crc32(msg, strlen(msg)) == embediq_crc32(msg, strlen(msg)));
    }

    /* ops table: init() must not crash when obs_emit is NULL (pre-declare state) */
    {
        extern embediq_lib_ops_t embediq_crc_ops;
        embediq_err_t r = embediq_crc_ops.init();
        TEST("ops_init_null_emit_safe", r == EMBEDIQ_OK);
    }

    printf("\n%d/%d passed\n", s_passed, s_run);
    return (s_passed == s_run) ? 0 : 1;
}
