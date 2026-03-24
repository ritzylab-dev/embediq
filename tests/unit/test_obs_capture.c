#define _POSIX_C_SOURCE 200809L

/*
 * tests/unit/test_obs_capture.c — Unit tests for .iqtrace binary capture
 *
 * Verifies the file transport produces conforming .iqtrace files per
 * docs/observability/iqtrace_format.md.
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "embediq_obs.h"
#include "embediq_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* ---------------------------------------------------------------------------
 * Package-internal Observatory API (EMBEDIQ_PLATFORM_HOST)
 * ------------------------------------------------------------------------- */

extern void     obs__reset(void);
extern uint32_t obs__ring_count(void);

/* ---------------------------------------------------------------------------
 * Minimal test harness
 * ------------------------------------------------------------------------- */

static int g_tests_run    = 0;
static int g_tests_failed = 0;

#define ASSERT(cond, msg)  do {                                                \
    g_tests_run++;                                                             \
    if (!(cond)) {                                                             \
        fprintf(stderr, "FAIL  %-56s  %s:%d\n", __func__, __FILE__, __LINE__);\
        g_tests_failed++;                                                      \
    } else {                                                                   \
        printf("PASS  %s\n", __func__);                                        \
    }                                                                          \
} while (0)

/* ---------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */

static const char *TEST_PATH = "/tmp/test_obs_capture.iqtrace";

static int read_u16_le(const char *path, size_t offset, uint16_t *out)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    if (fseek(f, (long)offset, SEEK_SET) != 0) { fclose(f); return -1; }
    uint8_t buf[2];
    if (fread(buf, 1, 2, f) != 2) { fclose(f); return -1; }
    fclose(f);
    *out = (uint16_t)((uint16_t)buf[1] << 8u) | (uint16_t)buf[0];
    return 0;
}

static long file_size(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fclose(f);
    return sz;
}

static int read_bytes(const char *path, size_t offset, uint8_t *buf, size_t n)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    if (fseek(f, (long)offset, SEEK_SET) != 0) { fclose(f); return -1; }
    if (fread(buf, 1, n, f) != n) { fclose(f); return -1; }
    fclose(f);
    return 0;
}

static EmbedIQ_Obs_Session_t make_test_session(void)
{
    EmbedIQ_Obs_Session_t s;
    memset(&s, 0, sizeof(s));
    s.device_id         = 0xDEADBEEFu;
    s.fw_version        = EMBEDIQ_OBS_FW_VERSION(1, 2, 3);
    s.timestamp_base_us = 1234567890000000ULL;
    s.session_id        = 7u;
    s.platform_id       = EMBEDIQ_OBS_PLATFORM_POSIX;
    s.trace_level       = 1u;
    memcpy(s.build_id, "deadbeef", 9);  /* includes NUL */
    return s;
}

/* ---------------------------------------------------------------------------
 * Tests
 * ------------------------------------------------------------------------- */

static void test_capture_file_header(void)
{
    obs__reset();
    setenv("EMBEDIQ_OBS_PATH", TEST_PATH, 1);
    EmbedIQ_Obs_Session_t s = make_test_session();
    embediq_obs_session_begin(&s);
    embediq_obs_capture_begin(NULL);
    embediq_obs_capture_end();

    uint8_t magic[4];
    read_bytes(TEST_PATH, 0, magic, 4);
    ASSERT(magic[0] == 0x49u && magic[1] == 0x51u &&
           magic[2] == 0x54u && magic[3] == 0x52u,
           "file magic must be IQTR");

    uint16_t ver = 0;
    read_u16_le(TEST_PATH, 4, &ver);
    ASSERT(ver == 0x0001u, "file version must be 1 LE");

    uint8_t reserved[2];
    read_bytes(TEST_PATH, 6, reserved, 2);
    ASSERT(reserved[0] == 0x00u && reserved[1] == 0x00u,
           "reserved bytes must be zero");
}

static void test_capture_session_tlv(void)
{
    obs__reset();
    setenv("EMBEDIQ_OBS_PATH", TEST_PATH, 1);
    EmbedIQ_Obs_Session_t s = make_test_session();
    embediq_obs_session_begin(&s);
    embediq_obs_capture_begin(NULL);
    embediq_obs_capture_end();

    uint16_t type = 0, length = 0;
    read_u16_le(TEST_PATH, 8, &type);
    read_u16_le(TEST_PATH, 10, &length);
    ASSERT(type == 0x0001u, "first TLV type must be SESSION (0x0001)");
    ASSERT(length == 40u,   "SESSION TLV length must be 40");

    /* Verify device_id in the TLV payload (offset 12, little-endian) */
    uint8_t did[4];
    read_bytes(TEST_PATH, 12, did, 4);
    uint32_t device_id = (uint32_t)did[0] | ((uint32_t)did[1] << 8u) |
                         ((uint32_t)did[2] << 16u) | ((uint32_t)did[3] << 24u);
    ASSERT(device_id == 0xDEADBEEFu, "SESSION payload device_id must match");
}

static void test_capture_stream_end_after_no_events(void)
{
    obs__reset();
    setenv("EMBEDIQ_OBS_PATH", TEST_PATH, 1);
    EmbedIQ_Obs_Session_t s = make_test_session();
    embediq_obs_session_begin(&s);
    embediq_obs_capture_begin(NULL);
    embediq_obs_capture_end();

    /* 8 header + 44 SESSION TLV + 4 STREAM_END TLV = 56 */
    ASSERT(file_size(TEST_PATH) == 56, "file size must be 56 bytes with no events");

    uint16_t type = 0, length = 0;
    read_u16_le(TEST_PATH, 52, &type);
    read_u16_le(TEST_PATH, 54, &length);
    ASSERT(type == 0x0004u, "last TLV type must be STREAM_END (0x0004)");
    ASSERT(length == 0u,    "STREAM_END length must be 0");
}

static void test_capture_event_tlv(void)
{
    obs__reset();
    setenv("EMBEDIQ_OBS_PATH", TEST_PATH, 1);
    EmbedIQ_Obs_Session_t s = make_test_session();
    embediq_obs_session_begin(&s);
    embediq_obs_capture_begin(NULL);
    embediq_obs_emit(EMBEDIQ_OBS_EVT_LIFECYCLE, 0u, 0xFFu, 2u, 0u);
    embediq_obs_capture_end();

    /* 8 + 44 + 18 EVENT TLV + 4 STREAM_END = 74 */
    ASSERT(file_size(TEST_PATH) == 74, "file size must be 74 bytes with 1 event");

    uint16_t type = 0, length = 0;
    read_u16_le(TEST_PATH, 52, &type);
    read_u16_le(TEST_PATH, 54, &length);
    ASSERT(type == 0x0002u, "EVENT TLV type must be 0x0002");
    ASSERT(length == 14u,   "EVENT TLV length must be 14");

    uint8_t evt_type = 0;
    read_bytes(TEST_PATH, 56, &evt_type, 1);
    ASSERT(evt_type == EMBEDIQ_OBS_EVT_LIFECYCLE,
           "EVENT payload event_type must match emitted type");
}

static void test_capture_requires_session(void)
{
    obs__reset();
    /* No session_begin — capture must fail */
    int ret = embediq_obs_capture_begin(NULL);
    ASSERT(ret == -1, "capture_begin must return -1 without prior session_begin");
}

/* ---------------------------------------------------------------------------
 * main
 * ------------------------------------------------------------------------- */

int main(void)
{
    test_capture_file_header();
    test_capture_session_tlv();
    test_capture_stream_end_after_no_events();
    test_capture_event_tlv();
    test_capture_requires_session();

    printf("\n");
    if (g_tests_failed == 0) {
        printf("All %d tests passed. (0 failed)\n", g_tests_run);
    } else {
        printf("%d of %d tests FAILED.\n", g_tests_failed, g_tests_run);
    }
    return (g_tests_failed > 0) ? 1 : 0;
}
