#define _POSIX_C_SOURCE 200809L

/*
 * tests/integration/test_thermostat_capture.c — .iqtrace capture integration test
 *
 * Boots the thermostat FBs, captures a short session to a .iqtrace file,
 * then validates the file: non-empty, IQTR magic, CLI decode exit 0.
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "embediq_fb.h"
#include "embediq_obs.h"
#include "embediq_osal.h"

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

/* ---------------------------------------------------------------------------
 * Package-internal test APIs
 * ------------------------------------------------------------------------- */

extern void     fb_engine__reset(void);
extern void     obs__set_level(uint8_t level);

/* POSIX stream ops registration */
extern void     embediq_obs_stream_posix_register(void);

/* Platform FB APIs */
extern void *fb_timer_register(void);
extern void *fb_nvm_register(void);
extern void *fb_watchdog_register(void);
extern void  fb_timer__stop_and_wait(void);

/* Application FB APIs */
extern void *fb_temp_sensor_register(void);
extern void *fb_temp_controller_register(void);

/* ---------------------------------------------------------------------------
 * Constants
 * ------------------------------------------------------------------------- */

#define CAPTURE_PATH   "/tmp/test_capture_output.iqtrace"
#define TEST_NVM_PATH  "/tmp/test_capture_nvm.bin"

/* ---------------------------------------------------------------------------
 * Shared handles
 * ------------------------------------------------------------------------- */

static void *g_h_timer  = NULL;
static void *g_h_nvm    = NULL;
static void *g_h_wdg    = NULL;
static void *g_h_sensor = NULL;
static void *g_h_ctrl   = NULL;

/* ---------------------------------------------------------------------------
 * main
 * ------------------------------------------------------------------------- */

int main(void)
{
    int failed = 0;

    /* ---- Setup ---- */
    embediq_obs_stream_posix_register();
    fb_engine__reset();
    obs__set_level(2u);
    setenv("EMBEDIQ_NVM_PATH", TEST_NVM_PATH, 1);

    g_h_timer  = fb_timer_register();
    g_h_nvm    = fb_nvm_register();
    g_h_wdg    = fb_watchdog_register();
    g_h_sensor = fb_temp_sensor_register();
    g_h_ctrl   = fb_temp_controller_register();

    int ret = embediq_engine_boot();
    if (ret != 0) {
        fprintf(stderr, "FAIL: embediq_engine_boot() returned %d\n", ret);
        return 1;
    }

    /* ---- Begin capture ---- */
    EmbedIQ_Obs_Session_t session = {
        .device_id         = 0xCAFEBABEu,
        .fw_version        = EMBEDIQ_OBS_FW_VERSION(1, 0, 0),
        .timestamp_base_us = 0ULL,
        .session_id        = 1u,
        .platform_id       = EMBEDIQ_OBS_PLATFORM_POSIX,
        .trace_level       = 2u,
        ._pad              = {0, 0},
        .build_id          = "test_cap0",
    };
    embediq_obs_session_begin(&session);

    ret = embediq_obs_capture_begin(CAPTURE_PATH);
    if (ret != 0) {
        fprintf(stderr, "FAIL: embediq_obs_capture_begin() returned %d\n", ret);
        return 1;
    }

    /* ---- Emit events directly so they land in the capture file ---- */
    embediq_obs_emit(EMBEDIQ_OBS_EVT_LIFECYCLE, 0u, 0xFFu, 2u, 0u);
    embediq_obs_emit(EMBEDIQ_OBS_EVT_MSG_TX, 0u, 1u, 0u, 0x0420u);
    embediq_obs_emit(EMBEDIQ_OBS_EVT_FSM_TRANS, 0u, 1u, 1u, 0x0420u);
    embediq_obs_emit(EMBEDIQ_OBS_EVT_FAULT, 0u, 0xFFu, 3u, 0u);

    /* ---- End capture ---- */
    embediq_obs_capture_end();
    fb_timer__stop_and_wait();

    /* ---- Validate the .iqtrace file ---- */

    /* Check 1: file exists and is non-empty */
    FILE *f = fopen(CAPTURE_PATH, "rb");
    if (!f) {
        fprintf(stderr, "FAIL: cannot open %s\n", CAPTURE_PATH);
        return 1;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size <= 0) {
        fprintf(stderr, "FAIL: file is empty (size=%ld)\n", size);
        fclose(f);
        return 1;
    }
    printf("  .iqtrace file size: %ld bytes\n", size);

    /* Check 2: first 4 bytes are IQTR magic */
    uint8_t magic[4] = {0};
    if (fread(magic, 1, 4, f) != 4) {
        fprintf(stderr, "FAIL: cannot read 4 bytes from file\n");
        fclose(f);
        return 1;
    }
    fclose(f);

    if (magic[0] != 0x49u || magic[1] != 0x51u ||
        magic[2] != 0x54u || magic[3] != 0x52u) {
        fprintf(stderr, "FAIL: bad magic: 0x%02X 0x%02X 0x%02X 0x%02X "
                "(expected 0x49 0x51 0x54 0x52)\n",
                magic[0], magic[1], magic[2], magic[3]);
        failed = 1;
    }

    /* Check 3: CLI decode exits 0 */
    const char *cmd =
        "python3 " EMBEDIQ_SOURCE_DIR "/tools/embediq_obs/embediq_obs.py obs decode "
        CAPTURE_PATH " > /dev/null 2>&1";
    int cli_ret = system(cmd);
    if (cli_ret != 0) {
        fprintf(stderr, "FAIL: CLI decode exited %d\n", cli_ret);
        failed = 1;
    }

    if (failed == 0) {
        printf("PASS: .iqtrace file created, magic valid, CLI decode exit 0\n");
    }
    return failed;
}
