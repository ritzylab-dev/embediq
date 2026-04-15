/*
 * fb_logger.c — Service FB: log collector template
 *
 * Subscribes to MSG_LOG_ENTRY and forwards each entry to the backend
 * registered via the embediq_logger_ops_t ops table.
 *
 * Layer rules honoured (ARCHITECTURE.md §Layer 2 — Service FB):
 *   - Zero HAL includes (Service FBs are platform-agnostic).
 *   - Zero OSAL / RTOS / POSIX threading calls (R-sub-03).
 *   - Zero blocking primitives in handlers (R-sub-03a).
 *   - Zero dynamic allocation (I-07, R-02) — static FB state only.
 *   - Payload access via memcpy into a local bounded buffer (I-03 compliant).
 *
 * MSG_LOG_ENTRY ID = 0x1600 is used as a placeholder in this template.
 * In a real project, reserve the message ID in messages_registry.json
 * (community / third-party range 0x1400–0xFFFF) and generate the payload
 * struct from an .iq schema. Hand-written payload structs are forbidden
 * (I-04).
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "fb_logger.h"
#include "embediq_subfn.h"
#include "embediq_bus.h"
#include "embediq_msg.h"

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/* ---------------------------------------------------------------------------
 * Message ID — placeholder for template purposes.
 * Reserve a real ID in messages_registry.json before shipping.
 * ------------------------------------------------------------------------- */
#define MSG_LOG_ENTRY  0x1600u

/* Bounded log-line buffer. Size chosen to fit EmbedIQ_Msg_t.payload. */
#define LOGGER_LINE_MAX  64u

/* ---------------------------------------------------------------------------
 * Module state (static — no dynamic allocation, R-02)
 * ------------------------------------------------------------------------- */

static const embediq_logger_ops_t *g_ops = NULL;

/* ---------------------------------------------------------------------------
 * Sub-function: forward one MSG_LOG_ENTRY to the backend
 * ------------------------------------------------------------------------- */

static void logger_run(EmbedIQ_FB_Handle_t fb, const void *msg,
                       void *fb_data, void *subfn_data)
{
    (void)fb; (void)fb_data; (void)subfn_data;

    if (g_ops == NULL || g_ops->write == NULL || msg == NULL) {
        return;  /* backend not registered — template is inert */
    }

    /* Extract a bounded, NUL-terminated line from the inline payload.
     * EmbedIQ_Msg_t.payload is uint8_t[EMBEDIQ_MSG_MAX_PAYLOAD]; copy into a
     * local buffer, then force termination at the last byte. */
    const EmbedIQ_Msg_t *m = (const EmbedIQ_Msg_t *)msg;
    char line[LOGGER_LINE_MAX];

    uint16_t n = m->payload_len;
    if (n >= sizeof(line)) {
        n = (uint16_t)(sizeof(line) - 1u);
    }
    memcpy(line, m->payload, n);
    line[n] = '\0';

    g_ops->write(line, n);
}

/* ---------------------------------------------------------------------------
 * FB init — open the backend, register the sub-function (R-sub-08)
 * ------------------------------------------------------------------------- */

static void logger_init(EmbedIQ_FB_Handle_t fb, void *fb_data)
{
    (void)fb_data;

    if (g_ops != NULL && g_ops->open != NULL) {
        (void)g_ops->open(NULL);  /* caller may override dest in a real impl */
    }

    static const uint16_t k_subfn_subs[] = { MSG_LOG_ENTRY };
    static EmbedIQ_SubFn_Config_t k_subfn = {
        .name               = "logger_run",
        .init_order         = 1u,
        .init_fn            = NULL,
        .run_fn             = logger_run,
        .exit_fn            = NULL,
        .subscriptions      = k_subfn_subs,
        .subscription_count = 1u,
        .subfn_data         = NULL,
        .fsm                = NULL,
        .osal_signal        = NULL,   /* Application / Service FB: always NULL */
    };
    (void)embediq_subfn_register(fb, &k_subfn);
}

/* ---------------------------------------------------------------------------
 * FB exit — flush and close the backend
 * ------------------------------------------------------------------------- */

static void logger_exit(EmbedIQ_FB_Handle_t fb, void *fb_data)
{
    (void)fb; (void)fb_data;

    if (g_ops == NULL) return;
    if (g_ops->flush != NULL) g_ops->flush();
    if (g_ops->close != NULL) g_ops->close();
}

/* ---------------------------------------------------------------------------
 * Public: fb_logger_register()
 * ------------------------------------------------------------------------- */

EMBEDIQ_SUBS(g_logger_subs, MSG_LOG_ENTRY);

EmbedIQ_FB_Handle_t fb_logger_register(const embediq_logger_ops_t *ops)
{
    if (ops == NULL || ops->version < EMBEDIQ_LOGGER_OPS_VERSION) {
        return NULL;  /* ABI mismatch — reject at registration time */
    }
    g_ops = ops;

    static const EmbedIQ_FB_Config_t k_cfg = {
        .name               = "fb_logger",
        .boot_phase         = EMBEDIQ_BOOT_PHASE_INFRASTRUCTURE,
        .init_fn            = logger_init,
        .exit_fn            = logger_exit,
        .subscriptions      = g_logger_subs,
        .subscription_count = 1u,
    };
    return embediq_fb_register(&k_cfg);
}
