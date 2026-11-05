#define _POSIX_C_SOURCE 200809L

/*
 * tests/unit/test_fb_logger_template.c — the Service FB template reads its payload
 *
 * Drives the template's fb_logger through the real engine: fb_engine__reset()
 * -> fb_logger_register() -> embediq_engine_boot() -> fb_engine__deliver_msg().
 * A sub-function receives the message's payload bytes, so every delivery
 * passes a full-size payload buffer (EMBEDIQ_MSG_MAX_PAYLOAD bytes), as the
 * template's MSG_LOG_ENTRY layout requires. A stub backend records each
 * write: the bytes, the length, and whether the line is NUL-terminated.
 *
 * Cases:
 *   1. NULL payload                 -> no write
 *   2. all-zero payload             -> one write, "" and len 0
 *   3. "abc", NUL, then other bytes -> "abc", len 3, terminated
 *   4. EMBEDIQ_MSG_MAX_PAYLOAD non-NUL bytes -> those bytes, full length, terminated
 *
 * Run:      ./build/tests/unit/test_fb_logger_template
 * Expected: "All N tests passed. (0 failed)"
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "fb_logger.h"
#include "embediq_fb.h"
#include "embediq_msg.h"
#include "embediq_config.h"

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* ---------------------------------------------------------------------------
 * Package-internal APIs (host test builds only)
 * ------------------------------------------------------------------------- */

extern void fb_engine__reset(void);
extern void fb_engine__deliver_msg(EmbedIQ_FB_Handle_t handle,
                                   uint16_t msg_id, const void *msg);

/* The template's placeholder MSG_LOG_ENTRY ID (fb_logger.c). */
#define TEST_MSG_LOG_ENTRY  0x1600u

/* ---------------------------------------------------------------------------
 * Minimal test harness
 * ------------------------------------------------------------------------- */

static int g_tests_run    = 0;
static int g_tests_failed = 0;

#define ASSERT(cond, msg)  do {                                                 \
    g_tests_run++;                                                              \
    if (!(cond)) {                                                              \
        fprintf(stderr, "FAIL  %-44s  %s:%d  %s\n",                             \
                __func__, __FILE__, __LINE__, (msg));                           \
        g_tests_failed++;                                                       \
    } else {                                                                    \
        printf("PASS  %s  %s\n", __func__, (msg));                              \
    }                                                                           \
} while (0)

/* ---------------------------------------------------------------------------
 * Stub backend — records the last write
 * ------------------------------------------------------------------------- */

static int      g_writes = 0;
static uint16_t g_len    = 0u;
static bool     g_terminated = false;
static char     g_bytes[EMBEDIQ_MSG_MAX_PAYLOAD + 1u];

static void stub_write(const char *line, uint16_t len)
{
    g_writes++;
    g_len = len;
    if (len <= EMBEDIQ_MSG_MAX_PAYLOAD) {
        memcpy(g_bytes, line, len);
        g_terminated = (line[len] == '\0');
    } else {
        g_terminated = false;
    }
}

static const embediq_logger_ops_t k_stub_ops = {
    .version = EMBEDIQ_LOGGER_OPS_VERSION,
    .open    = NULL,
    .write   = stub_write,
    .flush   = NULL,
    .close   = NULL,
};

static void stub_clear(void)
{
    g_writes     = 0;
    g_len        = 0u;
    g_terminated = false;
    memset(g_bytes, 0, sizeof(g_bytes));
}

/* A full-size payload buffer. The trailing bytes keep a handler that reads
 * past its payload inside this object, so a wrong length fails an assertion
 * instead of reading some other variable. */
typedef struct {
    uint8_t payload[EMBEDIQ_MSG_MAX_PAYLOAD];
    uint8_t beyond[sizeof(EmbedIQ_Msg_t)];
} payload_buf_t;

static EmbedIQ_FB_Handle_t g_logger = NULL;

static void deliver(const payload_buf_t *buf)
{
    stub_clear();
    fb_engine__deliver_msg(g_logger, TEST_MSG_LOG_ENTRY,
                           (buf != NULL) ? buf->payload : NULL);
}

/* ---------------------------------------------------------------------------
 * Tests
 * ------------------------------------------------------------------------- */

static void test_null_payload_writes_nothing(void)
{
    deliver(NULL);
    ASSERT(g_writes == 0, "a NULL payload is not written");
}

static void test_empty_payload_writes_empty_line(void)
{
    payload_buf_t buf;
    memset(&buf, 0, sizeof(buf));
    deliver(&buf);
    ASSERT(g_writes == 1, "one write");
    ASSERT(g_len == 0u, "len 0");
    ASSERT(g_terminated && g_bytes[0] == '\0', "the line is \"\"");
}

static void test_text_stops_at_first_nul(void)
{
    payload_buf_t buf;
    memset(&buf, 'Z', sizeof(buf));
    memcpy(buf.payload, "abc", 4u);   /* "abc" and its NUL, then 'Z' bytes */
    deliver(&buf);
    ASSERT(g_writes == 1, "one write");
    ASSERT(g_len == 3u, "len 3");
    ASSERT(memcmp(g_bytes, "abc", 3u) == 0, "bytes are \"abc\"");
    ASSERT(g_terminated, "the line is NUL-terminated");
}

static void test_full_payload_is_written_whole(void)
{
    payload_buf_t buf;
    memset(&buf, 0, sizeof(buf));
    for (uint32_t i = 0u; i < EMBEDIQ_MSG_MAX_PAYLOAD; i++) {
        buf.payload[i] = (uint8_t)('A' + (i % 26u));
    }
    deliver(&buf);
    ASSERT(g_writes == 1, "one write");
    ASSERT(g_len == EMBEDIQ_MSG_MAX_PAYLOAD, "len is the full payload size");
    ASSERT(memcmp(g_bytes, buf.payload, EMBEDIQ_MSG_MAX_PAYLOAD) == 0,
           "bytes are the whole payload");
    ASSERT(g_terminated, "the line is NUL-terminated");
}

/* ---------------------------------------------------------------------------
 * main
 * ------------------------------------------------------------------------- */

int main(void)
{
    fb_engine__reset();
    g_logger = fb_logger_register(&k_stub_ops);
    if (g_logger == NULL || embediq_engine_boot() != 0) {
        fprintf(stderr, "FAIL  setup: fb_logger did not register and boot\n");
        return 1;
    }

    test_null_payload_writes_nothing();
    test_empty_payload_writes_empty_line();
    test_text_stops_at_first_nul();
    test_full_payload_is_written_whole();

    printf("\n");
    if (g_tests_failed == 0) {
        printf("All %d tests passed. (0 failed)\n", g_tests_run);
    } else {
        printf("%d of %d tests FAILED.\n", g_tests_failed, g_tests_run);
    }
    return (g_tests_failed > 0) ? 1 : 0;
}
