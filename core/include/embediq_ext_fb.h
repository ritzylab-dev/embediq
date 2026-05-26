/*
 * embediq_ext_fb.h — External FB C API — client side of the bridge protocol
 *
 * Five-function API for any thread to communicate with the EmbedIQ bus as an
 * External FB. Transport-agnostic at the call site: compile with
 * EMBEDIQ_BRIDGE_TRANSPORT_SOCKET (Linux default) or EMBEDIQ_BRIDGE_TRANSPORT_QUEUE
 * (RTOS/in-process). No FB knowledge required from the caller.
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef EMBEDIQ_EXT_FB_H
#define EMBEDIQ_EXT_FB_H

#ifdef __cplusplus
extern "C" {
#endif

#include "embediq_osal.h"   /* embediq_err_t, EMBEDIQ_OK, EMBEDIQ_ERR* */
#include "embediq_msg.h"
#include "embediq_config.h"
#include <stdint.h>

typedef struct embediq_ext_fb_s embediq_ext_fb_t;  /* opaque */

embediq_ext_fb_t *embediq_ext_fb_init(const char *name);
embediq_err_t     embediq_ext_fb_subscribe(embediq_ext_fb_t *h,
                                            const uint16_t *ids, uint8_t n);
embediq_err_t     embediq_ext_fb_publish(embediq_ext_fb_t *h,
                                          const EmbedIQ_Msg_t *msg);
embediq_err_t     embediq_ext_fb_recv(embediq_ext_fb_t *h,
                                       EmbedIQ_Msg_t *out, uint32_t timeout_ms);
void              embediq_ext_fb_deinit(embediq_ext_fb_t *h);

#ifdef __cplusplus
}
#endif

#endif /* EMBEDIQ_EXT_FB_H */
