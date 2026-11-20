/*
 * tests/compat/wdg_public_client.c — a watchdog client built only from the
 * public header
 *
 * Includes embediq_wdg.h and embediq_fb.h and declares none of the watchdog
 * functions itself: every call below is type-checked against the canonical
 * declarations and resolved by the linker against the production driver
 * library. It boots an engine with fb_watchdog and one application FB, then
 * checks the documented results:
 *   register OK · check-in OK · unregister OK · second unregister ERR ·
 *   check-in after unregister ERR
 *
 * It is also the positive control of check_wdg_signature_conflict.sh: it must
 * compile cleanly with -fsyntax-only where the conflicting definition fails.
 *
 * Run:      ./build/tests/compat/wdg_public_client
 * Expected: "All 5 checks passed. (0 failed)"
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "embediq_fb.h"
#include "embediq_wdg.h"

#include <stdio.h>
#include <stdint.h>

/* The watchdog FB's own registration function (fbs/drivers/fb_watchdog.c). */
extern EmbedIQ_FB_Handle_t fb_watchdog_register(void);

/* A timeout no test run approaches: the client never misses. */
#define CLIENT_TIMEOUT_MS  60000u

static int g_checks = 0;
static int g_failed = 0;

static void check(int ok, const char *what)
{
    g_checks++;
    if (ok) {
        printf("PASS  %s\n", what);
    } else {
        fprintf(stderr, "FAIL  %s\n", what);
        g_failed++;
    }
}

int main(void)
{
    static const EmbedIQ_FB_Config_t k_client = {
        .name       = "wdg_client",
        .boot_phase = EMBEDIQ_BOOT_PHASE_APPLICATION,
    };
    (void)fb_watchdog_register();
    EmbedIQ_FB_Handle_t fb = embediq_fb_register(&k_client);
    if (fb == NULL || embediq_engine_boot() != 0) {
        fprintf(stderr, "FAIL  setup: the engine did not boot\n");
        return 1;
    }

    check(embediq_wdg_register(fb, CLIENT_TIMEOUT_MS) == EMBEDIQ_OK, "register returns EMBEDIQ_OK");
    check(embediq_wdg_checkin(fb) == EMBEDIQ_OK, "check-in returns EMBEDIQ_OK");
    check(embediq_wdg_unregister(fb) == EMBEDIQ_OK, "unregister returns EMBEDIQ_OK");
    check(embediq_wdg_unregister(fb) == EMBEDIQ_ERR, "a second unregister returns EMBEDIQ_ERR");
    check(embediq_wdg_checkin(fb) == EMBEDIQ_ERR, "check-in after unregister returns EMBEDIQ_ERR");

    printf("\n");
    if (g_failed == 0) {
        printf("All %d checks passed. (0 failed)\n", g_checks);
    } else {
        printf("%d of %d checks FAILED.\n", g_failed, g_checks);
    }
    return (g_failed > 0) ? 1 : 0;
}
