/*
 * embediq_ext_fb.c — External FB C API implementation — static pool + transport dispatch
 *
 * Implements the five-function External FB API using a static pool
 * (EMBEDIQ_BRIDGE_MAX_EXT_FBS slots, R-02 no-malloc compliant). Transport
 * dispatch is compile-time: EMBEDIQ_BRIDGE_TRANSPORT_SOCKET for Linux,
 * EMBEDIQ_BRIDGE_TRANSPORT_QUEUE for RTOS and in-process unit tests.
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "embediq_ext_fb.h"
#include "embediq_bus.h"
#include "embediq_osal.h"
#include "embediq_msg.h"
#include "embediq_config.h"

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#if defined(EMBEDIQ_BRIDGE_TRANSPORT_SOCKET) && defined(EMBEDIQ_PLATFORM_HOST)
#  include <errno.h>
#  include <stdlib.h>     /* getenv */
#  include <sys/socket.h>
#  include <sys/types.h>
#  include <sys/un.h>
#  include <unistd.h>
#endif

/* Package-internal hooks provided by fbs/bridge/fb_bridge.c. Declared here
 * (rather than in a public header) because they are not part of the public
 * API surface — only this file and fb_bridge.c need them. */
extern void fb_bridge__publish_extfb_connected(uint8_t virtual_ep_id,
                                                uint8_t transport,
                                                const char *name);
extern void fb_bridge__publish_extfb_disconnected(uint8_t virtual_ep_id,
                                                   uint8_t reason);

/* ---------------------------------------------------------------------------
 * Slot type — private to this TU (opaque outside)
 * ------------------------------------------------------------------------- */

struct embediq_ext_fb_s {
    uint8_t  virtual_endpoint_id;   /* 0x40 + slot_index */
    bool     in_use;
    char     name[EMBEDIQ_BRIDGE_NAME_LEN];
    uint16_t subscriptions[EMBEDIQ_BRIDGE_MAX_SUB_PER_EXT_FB];
    uint8_t  subscription_count;
#if defined(EMBEDIQ_BRIDGE_TRANSPORT_QUEUE)
    EmbedIQ_Queue_t *inbound_q;     /* msgs from bus to External FB */
    EmbedIQ_Queue_t *outbound_q;    /* msgs from External FB to bus */
#elif defined(EMBEDIQ_BRIDGE_TRANSPORT_SOCKET)
    int fd;                         /* connected socket fd, -1 = disconnected */
#endif
};

/* ---------------------------------------------------------------------------
 * Static pool — R-02: no malloc, sized at compile time
 * ------------------------------------------------------------------------- */

static embediq_ext_fb_t s_pool[EMBEDIQ_BRIDGE_MAX_EXT_FBS];
static bool             s_pool_initialised = false;

static void pool_init_once(void)
{
    if (s_pool_initialised) return;
    memset(s_pool, 0, sizeof(s_pool));
#if defined(EMBEDIQ_BRIDGE_TRANSPORT_SOCKET)
    for (uint8_t i = 0u; i < EMBEDIQ_BRIDGE_MAX_EXT_FBS; i++) {
        s_pool[i].fd = -1;
    }
#endif
    s_pool_initialised = true;
}

/* ---------------------------------------------------------------------------
 * embediq_ext_fb_init
 * ------------------------------------------------------------------------- */

embediq_ext_fb_t *embediq_ext_fb_init(const char *name)
{
    pool_init_once();

    /* Scan for a free slot. */
    uint8_t slot_idx = (uint8_t)EMBEDIQ_BRIDGE_MAX_EXT_FBS;
    for (uint8_t i = 0u; i < EMBEDIQ_BRIDGE_MAX_EXT_FBS; i++) {
        if (!s_pool[i].in_use) {
            slot_idx = i;
            break;
        }
    }
    if (slot_idx >= EMBEDIQ_BRIDGE_MAX_EXT_FBS) {
        return NULL;
    }

    embediq_ext_fb_t *slot = &s_pool[slot_idx];
    slot->in_use              = true;
    slot->virtual_endpoint_id = (uint8_t)(0x40u + slot_idx);
    slot->subscription_count  = 0u;
    if (name != NULL) {
        (void)strncpy(slot->name, name, sizeof(slot->name) - 1u);
        slot->name[sizeof(slot->name) - 1u] = '\0';
    } else {
        slot->name[0] = '\0';
    }

#if defined(EMBEDIQ_BRIDGE_TRANSPORT_QUEUE)
    slot->inbound_q  = embediq_osal_queue_create(
                            (uint16_t)EMBEDIQ_BRIDGE_QUEUE_DEPTH,
                            (uint16_t)sizeof(EmbedIQ_Msg_t));
    slot->outbound_q = embediq_osal_queue_create(
                            (uint16_t)EMBEDIQ_BRIDGE_QUEUE_DEPTH,
                            (uint16_t)sizeof(EmbedIQ_Msg_t));
    if (slot->inbound_q == NULL || slot->outbound_q == NULL) {
        if (slot->inbound_q  != NULL) embediq_osal_queue_destroy(slot->inbound_q);
        if (slot->outbound_q != NULL) embediq_osal_queue_destroy(slot->outbound_q);
        memset(slot, 0, sizeof(*slot));
        return NULL;
    }
    fb_bridge__publish_extfb_connected(slot->virtual_endpoint_id,
                                        1u /* queue transport */,
                                        slot->name);
#elif defined(EMBEDIQ_BRIDGE_TRANSPORT_SOCKET)
    slot->fd = -1;
#  if defined(EMBEDIQ_PLATFORM_HOST)
    const char *sock_path = getenv("EMBEDIQ_BRIDGE_SOCK");
    if (sock_path == NULL || sock_path[0] == '\0') {
        sock_path = "/tmp/embediq_bridge.sock";
    }
    int fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
    if (fd < 0) {
        memset(slot, 0, sizeof(*slot));
        slot->fd = -1;
        return NULL;
    }
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    (void)strncpy(addr.sun_path, sock_path, sizeof(addr.sun_path) - 1u);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(fd);
        memset(slot, 0, sizeof(*slot));
        slot->fd = -1;
        return NULL;
    }
    slot->fd = fd;
    /* Send IDENTIFY (TLV: 0x0004) with the External FB name. */
    {
        uint16_t name_len = (uint16_t)strnlen(slot->name, sizeof(slot->name));
        uint8_t  hdr[4];
        uint16_t ft = 0x0004u;
        memcpy(hdr,     &ft,        sizeof(ft));
        memcpy(hdr + 2, &name_len,  sizeof(name_len));
        (void)send(fd, hdr, sizeof(hdr), 0);
        if (name_len > 0u) (void)send(fd, slot->name, name_len, 0);
    }
    /* Read ENDPOINT_ASSIGN response and overwrite our virtual id.
     * Frame is exactly 5 bytes: 2-byte frame_type + 2-byte payload_len + 1-byte ep_id. */
    {
        uint8_t buf[5];
        ssize_t n = recv(fd, buf, sizeof(buf), 0);
        if (n >= (ssize_t)sizeof(buf)) {
            uint16_t ft;
            memcpy(&ft, buf, sizeof(ft));
            if (ft == 0x0005u) {
                slot->virtual_endpoint_id = buf[4];
            }
        }
    }
#  endif /* EMBEDIQ_PLATFORM_HOST */
    fb_bridge__publish_extfb_connected(slot->virtual_endpoint_id,
                                        0u /* socket transport */,
                                        slot->name);
#endif

    return slot;
}

/* ---------------------------------------------------------------------------
 * embediq_ext_fb_subscribe
 * ------------------------------------------------------------------------- */

embediq_err_t embediq_ext_fb_subscribe(embediq_ext_fb_t *h,
                                        const uint16_t *ids, uint8_t n)
{
    if (h == NULL || ids == NULL) {
        return EMBEDIQ_ERR_INVALID;
    }
    for (uint8_t i = 0u; i < n; i++) {
        if (h->subscription_count >= (uint8_t)EMBEDIQ_BRIDGE_MAX_SUB_PER_EXT_FB) {
            break;
        }
        h->subscriptions[h->subscription_count++] = ids[i];
    }

#if defined(EMBEDIQ_BRIDGE_TRANSPORT_SOCKET) && defined(EMBEDIQ_PLATFORM_HOST)
    /* Forward subscriptions to the bridge over the wire so fb_bridge can
     * deliver matching messages back through this slot's fd. */
    if (h->fd >= 0 && n > 0u) {
        uint8_t  hdr[4];
        uint16_t ft  = 0x0002u;      /* BRIDGE_FRAME_SUBSCRIBE */
        uint16_t len = (uint16_t)(n * sizeof(uint16_t));
        memcpy(hdr,     &ft,  sizeof(ft));
        memcpy(hdr + 2, &len, sizeof(len));
        (void)send(h->fd, hdr, sizeof(hdr), 0);
        (void)send(h->fd, ids, len, 0);
    }
#endif

    return EMBEDIQ_OK;
}

/* ---------------------------------------------------------------------------
 * embediq_ext_fb_publish
 * ------------------------------------------------------------------------- */

embediq_err_t embediq_ext_fb_publish(embediq_ext_fb_t *h,
                                      const EmbedIQ_Msg_t *msg)
{
    if (h == NULL || msg == NULL) {
        return EMBEDIQ_ERR_INVALID;
    }

#if defined(EMBEDIQ_BRIDGE_TRANSPORT_QUEUE)
    /* Stamp + route directly through the bus. The bus copies-by-value so the
     * non-const cast is safe (the parameter is const at the API boundary but
     * the routing path doesn't retain the pointer). */
    EmbedIQ_Msg_t local = *msg;
    return (embediq_err_t)embediq_bus_bridge_route(h->virtual_endpoint_id,
                                                    &local);
#elif defined(EMBEDIQ_BRIDGE_TRANSPORT_SOCKET) && defined(EMBEDIQ_PLATFORM_HOST)
    if (h->fd < 0) return EMBEDIQ_ERR;
    uint8_t  hdr[4];
    uint16_t ft  = 0x0001u;                /* BRIDGE_FRAME_MSG */
    uint16_t len = (uint16_t)sizeof(*msg);
    memcpy(hdr,     &ft,  sizeof(ft));
    memcpy(hdr + 2, &len, sizeof(len));
    if (send(h->fd, hdr, sizeof(hdr), 0) != (ssize_t)sizeof(hdr)) {
        return EMBEDIQ_ERR;
    }
    if (send(h->fd, msg, sizeof(*msg), 0) != (ssize_t)sizeof(*msg)) {
        return EMBEDIQ_ERR;
    }
    return EMBEDIQ_OK;
#else
    (void)h; (void)msg;
    return EMBEDIQ_ERR;
#endif
}

/* ---------------------------------------------------------------------------
 * embediq_ext_fb_recv
 * ------------------------------------------------------------------------- */

embediq_err_t embediq_ext_fb_recv(embediq_ext_fb_t *h,
                                   EmbedIQ_Msg_t *out, uint32_t timeout_ms)
{
    if (h == NULL || out == NULL) {
        return EMBEDIQ_ERR_INVALID;
    }

#if defined(EMBEDIQ_BRIDGE_TRANSPORT_QUEUE)
    return embediq_osal_queue_recv(h->inbound_q, out, timeout_ms);
#elif defined(EMBEDIQ_BRIDGE_TRANSPORT_SOCKET) && defined(EMBEDIQ_PLATFORM_HOST)
    if (h->fd < 0) return EMBEDIQ_ERR;

    struct timeval tv;
    tv.tv_sec  = (time_t)(timeout_ms / 1000u);
    tv.tv_usec = (suseconds_t)((timeout_ms % 1000u) * 1000u);
    (void)setsockopt(h->fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    uint8_t hdr[4];
    ssize_t n = recv(h->fd, hdr, sizeof(hdr), 0);
    if (n <= 0) {
        return (errno == EAGAIN || errno == EWOULDBLOCK)
                   ? EMBEDIQ_ERR_TIMEOUT
                   : EMBEDIQ_ERR;
    }
    uint16_t ft, len;
    memcpy(&ft,  hdr,     sizeof(ft));
    memcpy(&len, hdr + 2, sizeof(len));
    if (ft != 0x0001u || len != sizeof(*out)) {
        /* Drain non-MSG frames silently (e.g. HEALTH_REQ). */
        uint8_t scratch[sizeof(EmbedIQ_Msg_t)];
        if (len > 0u && len <= sizeof(scratch)) {
            (void)recv(h->fd, scratch, len, 0);
        }
        return EMBEDIQ_ERR_TIMEOUT;
    }
    if (recv(h->fd, out, sizeof(*out), 0) != (ssize_t)sizeof(*out)) {
        return EMBEDIQ_ERR;
    }
    return EMBEDIQ_OK;
#else
    (void)h; (void)out; (void)timeout_ms;
    return EMBEDIQ_ERR;
#endif
}

/* ---------------------------------------------------------------------------
 * embediq_ext_fb_deinit
 * ------------------------------------------------------------------------- */

void embediq_ext_fb_deinit(embediq_ext_fb_t *h)
{
    if (h == NULL || !h->in_use) {
        return;
    }

    fb_bridge__publish_extfb_disconnected(h->virtual_endpoint_id,
                                           0u /* clean disconnect */);

#if defined(EMBEDIQ_BRIDGE_TRANSPORT_QUEUE)
    if (h->inbound_q  != NULL) embediq_osal_queue_destroy(h->inbound_q);
    if (h->outbound_q != NULL) embediq_osal_queue_destroy(h->outbound_q);
#elif defined(EMBEDIQ_BRIDGE_TRANSPORT_SOCKET) && defined(EMBEDIQ_PLATFORM_HOST)
    if (h->fd >= 0) {
        close(h->fd);
        h->fd = -1;
    }
#endif

    memset(h, 0, sizeof(*h));
#if defined(EMBEDIQ_BRIDGE_TRANSPORT_SOCKET)
    h->fd = -1;
#endif
}
