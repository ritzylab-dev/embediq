#define _POSIX_C_SOURCE 200809L

/*
 * tests/integration/test_bridge_socket.c — Integration test for fb_bridge socket transport (Item 5c)
 *
 * Boots fb_bridge with EMBEDIQ_BRIDGE_TRANSPORT_SOCKET against a test-specific
 * Unix socket path, fork/exec's tests/integration/bridge_client.py (which
 * speaks raw TLV — no SDK import), and confirms two bus messages arrive on
 * the internal harness FB:
 *
 *   1. MSG_BRIDGE_EXTFB_CONNECTED (0x06A4) — emitted by fb_bridge when the
 *      client completes the IDENTIFY/ENDPOINT_ASSIGN handshake.
 *   2. MSG_HARNESS_A (0x0430) — published by the client over BRIDGE_FRAME_MSG,
 *      routed onto the bus by fb_bridge via embediq_bus_bridge_route().
 *
 * Pass criterion: both messages received within timeout, child process exits
 * cleanly with status 0. ctest timeout: 30 seconds.
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "embediq_fb.h"
#include "embediq_bus.h"
#include "embediq_msg.h"
#include "embediq_osal.h"
#include "embediq_config.h"
#include "embediq_platform_msgs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#if defined(EMBEDIQ_PLATFORM_HOST)
#  include <errno.h>
#  include <unistd.h>
#  include <sys/types.h>
#  include <sys/wait.h>
#  include <signal.h>
#endif

/* ---------------------------------------------------------------------------
 * Test-only / package-internal APIs (same as tests/unit/test_fb_bridge.c)
 * ------------------------------------------------------------------------- */

extern void               fb_engine__reset(void);
extern void               message_bus__reset(void);
extern bool               message_bus__test_recv(uint8_t ep_id, uint8_t prio,
                                                  EmbedIQ_Msg_t *out);
extern EmbedIQ_FB_Handle_t fb_bridge_register(void);

/* ---------------------------------------------------------------------------
 * Constants
 * ------------------------------------------------------------------------- */

#define TEST_SOCK_PATH        "/tmp/test_bridge_5c.sock"
#define MSG_HARNESS_A         0x0430u
#define WAIT_PER_MSG_MS       5000u
#define POLL_TICK_MS            50u

/* ---------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */

/** Drain queued messages, looking for `target_msg_id`. Returns true on success. */
static bool wait_for_msg(uint8_t recv_ep, uint16_t target_msg_id,
                          uint32_t timeout_ms, EmbedIQ_Msg_t *out)
{
    uint32_t t0 = embediq_osal_time_ms();
    while ((embediq_osal_time_ms() - t0) < timeout_ms) {
        if (message_bus__test_recv(recv_ep,
                                   (uint8_t)EMBEDIQ_MSG_PRIORITY_NORMAL,
                                   out)) {
            if (out->msg_id == target_msg_id) {
                return true;
            }
            /* Different msg_id — discard and keep polling. */
            continue;
        }
        embediq_osal_delay_ms(POLL_TICK_MS);
    }
    return false;
}

/* ---------------------------------------------------------------------------
 * Main
 * ------------------------------------------------------------------------- */

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

#if !defined(EMBEDIQ_PLATFORM_HOST)
    fprintf(stderr, "[TEST] bridge_socket: EMBEDIQ_PLATFORM_HOST required\n");
    return EXIT_FAILURE;
#else
    /* 1. Tell fb_bridge to bind a test-specific socket path. */
    setenv("EMBEDIQ_BRIDGE_SOCK", TEST_SOCK_PATH, 1);
    (void)unlink(TEST_SOCK_PATH);   /* clean any leftover socket from a prior run */

    /* 2. Register fb_bridge + a harness receiver FB. */
    fb_engine__reset();
    message_bus__reset();

    (void)fb_bridge_register();

    EMBEDIQ_SUBS(harness_subs,
                 MSG_BRIDGE_EXTFB_CONNECTED,
                 MSG_BRIDGE_EXTFB_DISCONNECTED,
                 MSG_BRIDGE_FAULT,
                 MSG_HARNESS_A);

    EmbedIQ_FB_Config_t cfg_recv = {
        .name               = "fb_test_5c_recv",
        .boot_phase         = EMBEDIQ_BOOT_PHASE_APPLICATION,
        .subscriptions      = harness_subs,
        .subscription_count = 4u,
    };
    (void)embediq_fb_register(&cfg_recv);

    /* 3. Boot the engine (fires fb_bridge.init_fn → spawns listener + mux tasks). */
    if (embediq_engine_boot() != 0) {
        fprintf(stderr, "[TEST] embediq_engine_boot() failed\n");
        return EXIT_FAILURE;
    }

    uint8_t recv_ep = embediq_bus_resolve_name("fb_test_5c_recv");
    if (recv_ep == 0xFFu) {
        fprintf(stderr, "[TEST] failed to resolve harness FB endpoint\n");
        return EXIT_FAILURE;
    }

    /* 4. Let bridge_listener / bridge_mux finish startup. */
    embediq_osal_delay_ms(100u);

    /* 5. Build path to bridge_client.py via EMBEDIQ_SOURCE_DIR (set by CMake). */
    char client_path[1024];
    int n = snprintf(client_path, sizeof(client_path),
                     "%s/tests/integration/bridge_client.py",
                     EMBEDIQ_SOURCE_DIR);
    if (n < 0 || (size_t)n >= sizeof(client_path)) {
        fprintf(stderr, "[TEST] client path too long\n");
        return EXIT_FAILURE;
    }

    /* 6. fork() + exec() the raw-TLV Python client. */
    pid_t child = fork();
    if (child < 0) {
        fprintf(stderr, "[TEST] fork() failed: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }
    if (child == 0) {
        /* --- child --- */
        char *args[] = { (char *)"python3", client_path,
                          (char *)TEST_SOCK_PATH, NULL };
        execvp("python3", args);
        /* exec returned — fatal in the child. */
        fprintf(stderr, "[TEST] execvp(python3) failed: %s\n", strerror(errno));
        _exit(127);
    }

    /* --- parent: wait for the two expected bus messages --- */
    EmbedIQ_Msg_t recv;
    bool got_connected = wait_for_msg(recv_ep, MSG_BRIDGE_EXTFB_CONNECTED,
                                       WAIT_PER_MSG_MS, &recv);
    if (!got_connected) {
        fprintf(stderr,
                "[TEST] timed out waiting for MSG_BRIDGE_EXTFB_CONNECTED\n");
        kill(child, SIGTERM);
        (void)waitpid(child, NULL, 0);
        return EXIT_FAILURE;
    }
    printf("[TEST] got MSG_BRIDGE_EXTFB_CONNECTED (virtual_ep=%u)\n",
           recv.source_endpoint_id);

    bool got_harness = wait_for_msg(recv_ep, MSG_HARNESS_A,
                                     WAIT_PER_MSG_MS, &recv);
    if (!got_harness) {
        fprintf(stderr, "[TEST] timed out waiting for MSG_HARNESS_A\n");
        kill(child, SIGTERM);
        (void)waitpid(child, NULL, 0);
        return EXIT_FAILURE;
    }
    printf("[TEST] got MSG_HARNESS_A from virtual_ep=0x%02X\n",
           recv.source_endpoint_id);

    /* 7. Reap child process. */
    int wstatus = 0;
    pid_t reaped = waitpid(child, &wstatus, 0);
    if (reaped != child) {
        fprintf(stderr, "[TEST] waitpid() failed\n");
        return EXIT_FAILURE;
    }
    if (!WIFEXITED(wstatus) || WEXITSTATUS(wstatus) != 0) {
        fprintf(stderr,
                "[TEST] bridge_client.py exited abnormally (status=%d, sig=%d)\n",
                WIFEXITED(wstatus) ? WEXITSTATUS(wstatus) : -1,
                WIFSIGNALED(wstatus) ? WTERMSIG(wstatus) : 0);
        return EXIT_FAILURE;
    }

    printf("[TEST] bridge_socket PASS\n");
    return EXIT_SUCCESS;
#endif /* EMBEDIQ_PLATFORM_HOST */
}
