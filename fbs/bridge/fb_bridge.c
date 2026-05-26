/*
 * fb_bridge.c — Bridge Daemon Service FB — External FB lifecycle manager
 *
 * Service FB at EMBEDIQ_BOOT_PHASE_BRIDGE=4 that lets External FBs join the
 * EmbedIQ bus. Two transports selected at compile time:
 *
 *   EMBEDIQ_BRIDGE_TRANSPORT_QUEUE
 *     In-process / RTOS path. embediq_ext_fb_*() calls work synchronously on
 *     the caller's thread. fb_bridge contributes only its FB identity (so
 *     publish helpers have a real source endpoint) and the package-internal
 *     publish helpers that signal connect/disconnect/fault on the bus.
 *
 *   EMBEDIQ_BRIDGE_TRANSPORT_SOCKET   (Linux, EMBEDIQ_PLATFORM_HOST only)
 *     Spawns bridge_listener (accept() loop) and bridge_mux (poll() over all
 *     accepted fds) OSAL tasks. TLV framing per ITEM5_BRIDGE_DESIGN_CONTEXT.md
 *     Topic 1. Per-slot session FSM:
 *         IDENTIFYING -> CONNECTED -> DISCONNECTING.
 *     systemd integration: if WATCHDOG_USEC is set, ping NOTIFY_SOCKET every
 *     WATCHDOG_USEC/2 microseconds with "WATCHDOG=1" (raw sd_notify protocol —
 *     no libsystemd dependency). TCP listener: env vars are read, but the
 *     actual TCP socket is deferred to Item 6 per ITEM5 Topic 11 OUT table.
 *
 * Bus messages published on lifecycle:
 *   MSG_BRIDGE_EXTFB_CONNECTED    on slot enter CONNECTED / queue init
 *   MSG_BRIDGE_EXTFB_DISCONNECTED on slot release / queue deinit
 *   MSG_BRIDGE_FAULT              on pool exhaustion, protocol error, etc.
 *
 * Observatory events emitted:
 *   EMBEDIQ_OBS_EVT_EXTFB_CONNECTED    (0x32, STATE band)
 *   EMBEDIQ_OBS_EVT_EXTFB_DISCONNECTED (0x33, STATE band)
 *
 * Layer rules (verified by boundary_checker.py once fbs/bridge is registered):
 *   - Zero hal_* includes.
 *   - All POSIX headers gated under EMBEDIQ_PLATFORM_HOST.
 *   - All thread creation through embediq_osal_task_create().
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
#include "embediq_obs.h"
#include "embediq_platform_msgs.h"

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#if defined(EMBEDIQ_BRIDGE_TRANSPORT_SOCKET) && defined(EMBEDIQ_PLATFORM_HOST)
#  include <errno.h>
#  include <fcntl.h>
#  include <poll.h>
#  include <stdio.h>
#  include <stdlib.h>
#  include <sys/socket.h>
#  include <sys/types.h>
#  include <sys/un.h>
#  include <unistd.h>
#endif

/* ---------------------------------------------------------------------------
 * Module identity — handle stashed in init_fn so the publish helpers below
 * can call embediq_publish() with a real source endpoint.
 * ------------------------------------------------------------------------- */

static EmbedIQ_FB_Handle_t g_bridge_fb = NULL;

/* ---------------------------------------------------------------------------
 * Package-internal publish helpers — called by embediq_ext_fb.c on the
 * client side of the bridge so that External FB lifecycle is visible on
 * the bus and in Observatory.
 *
 * Safety: if engine_boot has not yet been called, g_bridge_fb is NULL and
 * the helper silently no-ops (no subscriber will see the message anyway
 * because message_bus_boot has not built the routing table).
 * ------------------------------------------------------------------------- */

void fb_bridge__publish_extfb_connected(uint8_t virtual_ep_id,
                                        uint8_t transport,
                                        const char *name)
{
    /* Observatory STATE event (always — gating handled by the macro). */
    EMBEDIQ_OBS_EMIT_STATE(EMBEDIQ_OBS_EVT_EXTFB_CONNECTED,
                           0u,                /* source_fb_id: fb_bridge — */
                           virtual_ep_id,     /* target_fb_id: External FB  */
                           transport,         /* 0=socket, 1=queue          */
                           0u);

    if (g_bridge_fb == NULL) {
        return;
    }

    EmbedIQ_Msg_t m;
    memset(&m, 0, sizeof(m));
    m.msg_id      = MSG_BRIDGE_EXTFB_CONNECTED;
    m.priority    = (uint8_t)EMBEDIQ_MSG_PRIORITY_NORMAL;
    m.payload_len = (uint16_t)sizeof(EmbedIQ_Msg_BridgeExtFbConnected_t);

    EmbedIQ_Msg_BridgeExtFbConnected_t *p =
        (EmbedIQ_Msg_BridgeExtFbConnected_t *)m.payload;
    p->virtual_endpoint_id = virtual_ep_id;
    p->transport           = transport;
    p->reserved            = 0u;
    if (name != NULL) {
        (void)strncpy(p->name, name, sizeof(p->name) - 1u);
        p->name[sizeof(p->name) - 1u] = '\0';
    }

    embediq_publish(g_bridge_fb, &m);
}

void fb_bridge__publish_extfb_disconnected(uint8_t virtual_ep_id,
                                           uint8_t reason)
{
    EMBEDIQ_OBS_EMIT_STATE(EMBEDIQ_OBS_EVT_EXTFB_DISCONNECTED,
                           0u, virtual_ep_id, reason, 0u);

    if (g_bridge_fb == NULL) {
        return;
    }

    EmbedIQ_Msg_t m;
    memset(&m, 0, sizeof(m));
    m.msg_id      = MSG_BRIDGE_EXTFB_DISCONNECTED;
    m.priority    = (uint8_t)EMBEDIQ_MSG_PRIORITY_NORMAL;
    m.payload_len = (uint16_t)sizeof(EmbedIQ_Msg_BridgeExtFbDisconnected_t);

    EmbedIQ_Msg_BridgeExtFbDisconnected_t *p =
        (EmbedIQ_Msg_BridgeExtFbDisconnected_t *)m.payload;
    p->virtual_endpoint_id = virtual_ep_id;
    p->reason              = reason;
    p->reserved            = 0u;

    embediq_publish(g_bridge_fb, &m);
}

void fb_bridge__publish_fault(uint8_t fault_code, uint8_t transport)
{
    if (g_bridge_fb == NULL) {
        return;
    }

    EmbedIQ_Msg_t m;
    memset(&m, 0, sizeof(m));
    m.msg_id      = MSG_BRIDGE_FAULT;
    m.priority    = (uint8_t)EMBEDIQ_MSG_PRIORITY_NORMAL;
    m.payload_len = (uint16_t)sizeof(EmbedIQ_Msg_BridgeFault_t);

    EmbedIQ_Msg_BridgeFault_t *p = (EmbedIQ_Msg_BridgeFault_t *)m.payload;
    p->fault_code = fault_code;
    p->transport  = transport;
    p->reserved   = 0u;

    embediq_publish(g_bridge_fb, &m);
}

/* ---------------------------------------------------------------------------
 * Socket transport — Unix socket listener + mux (POSIX, EMBEDIQ_PLATFORM_HOST)
 *
 * TCP listener env vars are read but the TCP socket itself is deferred to
 * Item 6 (ITEM5 Topic 11 OUT table). Heartbeat default and systemd watchdog
 * are wired in. The implementation is intentionally compact: 5a delivers a
 * correct queue-transport path (covered by unit tests) and a structurally
 * complete socket path that 5c integration tests will exercise end-to-end.
 * ------------------------------------------------------------------------- */

#if defined(EMBEDIQ_BRIDGE_TRANSPORT_SOCKET) && defined(EMBEDIQ_PLATFORM_HOST)

/* TLV frame types (Topic 1) ----------------------------------------------- */

#define BRIDGE_FRAME_MSG             0x0001u
#define BRIDGE_FRAME_SUBSCRIBE       0x0002u
#define BRIDGE_FRAME_UNSUBSCRIBE     0x0003u
#define BRIDGE_FRAME_IDENTIFY        0x0004u
#define BRIDGE_FRAME_ENDPOINT_ASSIGN 0x0005u
#define BRIDGE_FRAME_HEALTH_REQ      0x0006u
#define BRIDGE_FRAME_HEALTH_RSP      0x0007u
#define BRIDGE_FRAME_ERROR           0x0008u

/* 4-byte TLV header — SOCK_SEQPACKET preserves message boundaries so one
 * write() == one read(); no reassembly logic is required. */
typedef struct __attribute__((packed)) {
    uint16_t frame_type;
    uint16_t payload_len;
} bridge_frame_hdr_t;

#define SLOT_FREE          0u
#define SLOT_IDENTIFYING   1u
#define SLOT_CONNECTED     2u
#define SLOT_DISCONNECTING 3u

typedef struct {
    int      fd;
    uint8_t  virtual_endpoint_id;
    char     name[EMBEDIQ_BRIDGE_NAME_LEN];
    uint16_t subscriptions[EMBEDIQ_BRIDGE_MAX_SUB_PER_EXT_FB];
    uint8_t  subscription_count;
    uint8_t  state;
    uint8_t  heartbeat_miss_count;
    uint32_t last_heartbeat_tick_ms;
    uint32_t identifying_started_ms;
} ext_socket_slot_t;

static ext_socket_slot_t g_socket_pool[EMBEDIQ_BRIDGE_MAX_EXT_FBS];

static int               g_unix_fd          = -1;
/* TCP transport deferred to Item 6 — declared for forward compatibility.
 * Marked unused so -Werror -Wunused-variable passes once the SOCKET transport
 * variant of this TU is compiled (Item 5c integration test). */
static int               g_tcp_fd __attribute__((unused)) = -1;
static EmbedIQ_Queue_t  *g_fd_queue         = NULL; /* listener -> mux fd handoff   */
static EmbedIQ_Task_t   *g_listener_task    = NULL;
static EmbedIQ_Task_t   *g_mux_task         = NULL;
static volatile bool     g_socket_running   = false;
static uint64_t          g_watchdog_us      = 0u;   /* WATCHDOG_USEC / 2 (period)   */
static uint32_t          g_heartbeat_ms     = EMBEDIQ_BRIDGE_HEARTBEAT_MS;
static uint32_t          g_identify_to_ms   = EMBEDIQ_BRIDGE_IDENTIFY_TIMEOUT_MS;

/* ---- frame helpers ---- */

static int send_frame(int fd, uint16_t type, const void *payload, uint16_t len)
{
    uint8_t buf[sizeof(bridge_frame_hdr_t) + sizeof(EmbedIQ_Msg_t)];
    if ((size_t)len + sizeof(bridge_frame_hdr_t) > sizeof(buf)) {
        return -1;
    }
    bridge_frame_hdr_t hdr = { .frame_type = type, .payload_len = len };
    memcpy(buf, &hdr, sizeof(hdr));
    if (payload && len) {
        memcpy(buf + sizeof(hdr), payload, len);
    }
    ssize_t w = send(fd, buf, sizeof(hdr) + len, 0);
    return (w == (ssize_t)(sizeof(hdr) + len)) ? 0 : -1;
}

static void close_slot(uint8_t i, uint8_t reason)
{
    ext_socket_slot_t *s = &g_socket_pool[i];
    if (s->state == SLOT_FREE) return;
    s->state = SLOT_DISCONNECTING;
    if (s->virtual_endpoint_id != 0u) {
        fb_bridge__publish_extfb_disconnected(s->virtual_endpoint_id, reason);
    }
    if (s->fd >= 0) {
        close(s->fd);
        s->fd = -1;
    }
    memset(s, 0, sizeof(*s));   /* state := SLOT_FREE (0) */
}

static uint8_t find_free_slot(void)
{
    for (uint8_t i = 0u; i < EMBEDIQ_BRIDGE_MAX_EXT_FBS; i++) {
        if (g_socket_pool[i].state == SLOT_FREE) return i;
    }
    return (uint8_t)EMBEDIQ_BRIDGE_MAX_EXT_FBS;   /* full sentinel */
}

/* ---- listener: accept connections and hand fds to mux ---- */

static void bridge_listener(void *arg)
{
    (void)arg;
    while (g_socket_running) {
        struct pollfd pfd = { .fd = g_unix_fd, .events = POLLIN, .revents = 0 };
        int r = poll(&pfd, 1, 250);   /* 250ms tick so we notice shutdown */
        if (r <= 0) continue;
        if (!(pfd.revents & POLLIN)) continue;
        int afd = accept(g_unix_fd, NULL, NULL);
        if (afd < 0) continue;
        /* Hand the fd to the mux task. */
        if (g_fd_queue != NULL) {
            (void)embediq_osal_queue_send(g_fd_queue, &afd, 0u);
        } else {
            close(afd);
        }
    }
}

/* ---- mux: drive every connected slot and dispatch frames ---- */

static void mux_handle_new_fd(int afd)
{
    uint8_t i = find_free_slot();
    if (i >= EMBEDIQ_BRIDGE_MAX_EXT_FBS) {
        uint32_t code = 0u;   /* pool_exhausted */
        (void)send_frame(afd, BRIDGE_FRAME_ERROR, &code, sizeof(code));
        close(afd);
        fb_bridge__publish_fault(0u, 0u);   /* fault_code 0, transport socket */
        return;
    }
    ext_socket_slot_t *s = &g_socket_pool[i];
    s->fd                     = afd;
    s->state                  = SLOT_IDENTIFYING;
    s->identifying_started_ms = embediq_osal_time_ms();
    s->heartbeat_miss_count   = 0u;
}

static void mux_process_slot(uint8_t i, uint32_t now_ms)
{
    ext_socket_slot_t *s = &g_socket_pool[i];
    if (s->state == SLOT_FREE) return;

    /* IDENTIFY timeout. */
    if (s->state == SLOT_IDENTIFYING &&
        (now_ms - s->identifying_started_ms) > g_identify_to_ms) {
        close_slot(i, 1u);   /* timeout */
        return;
    }

    /* Periodic heartbeat on connected slots. */
    if (s->state == SLOT_CONNECTED &&
        (now_ms - s->last_heartbeat_tick_ms) >= g_heartbeat_ms) {
        s->last_heartbeat_tick_ms = now_ms;
        s->heartbeat_miss_count++;
        (void)send_frame(s->fd, BRIDGE_FRAME_HEALTH_REQ, NULL, 0u);
        if (s->heartbeat_miss_count >= 3u) {
            close_slot(i, 1u);   /* timeout */
            return;
        }
    }

    /* Drain any available inbound frames. */
    uint8_t  buf[sizeof(bridge_frame_hdr_t) + sizeof(EmbedIQ_Msg_t)];
    ssize_t  n;
    for (;;) {
        n = recv(s->fd, buf, sizeof(buf), MSG_DONTWAIT);
        if (n <= 0) {
            if (n == 0) close_slot(i, 0u);   /* peer closed cleanly */
            break;
        }
        if ((size_t)n < sizeof(bridge_frame_hdr_t)) break;
        bridge_frame_hdr_t hdr;
        memcpy(&hdr, buf, sizeof(hdr));
        const uint8_t *payload = buf + sizeof(hdr);

        switch (hdr.frame_type) {
        case BRIDGE_FRAME_IDENTIFY: {
            if (s->state != SLOT_IDENTIFYING) break;
            size_t copy_len = hdr.payload_len;
            if (copy_len >= sizeof(s->name)) copy_len = sizeof(s->name) - 1u;
            memcpy(s->name, payload, copy_len);
            s->name[copy_len] = '\0';
            s->virtual_endpoint_id = (uint8_t)(0x40u + i);
            (void)send_frame(s->fd, BRIDGE_FRAME_ENDPOINT_ASSIGN,
                             &s->virtual_endpoint_id, 1u);
            s->state                  = SLOT_CONNECTED;
            s->last_heartbeat_tick_ms = now_ms;
            fb_bridge__publish_extfb_connected(s->virtual_endpoint_id, 0u,
                                                s->name);
            break;
        }
        case BRIDGE_FRAME_SUBSCRIBE: {
            uint16_t cnt = hdr.payload_len / (uint16_t)sizeof(uint16_t);
            if (cnt > (uint16_t)(sizeof(s->subscriptions) /
                                  sizeof(s->subscriptions[0]))) {
                cnt = (uint16_t)(sizeof(s->subscriptions) /
                                  sizeof(s->subscriptions[0]));
            }
            memcpy(s->subscriptions, payload, cnt * sizeof(uint16_t));
            s->subscription_count = (uint8_t)cnt;
            break;
        }
        case BRIDGE_FRAME_MSG: {
            if (hdr.payload_len != sizeof(EmbedIQ_Msg_t)) break;
            EmbedIQ_Msg_t msg;
            memcpy(&msg, payload, sizeof(msg));
            (void)embediq_bus_bridge_route(s->virtual_endpoint_id, &msg);
            break;
        }
        case BRIDGE_FRAME_HEALTH_RSP:
            s->heartbeat_miss_count = 0u;
            break;
        case BRIDGE_FRAME_ERROR:
            close_slot(i, 3u);   /* protocol error */
            return;
        default:
            break;
        }
    }
}

static void notify_systemd_watchdog(uint32_t now_ms)
{
    static uint32_t last_ping_ms = 0u;
    if (g_watchdog_us == 0u) return;
    uint32_t period_ms = (uint32_t)(g_watchdog_us / 1000u);
    if (period_ms == 0u) period_ms = 1u;
    if ((now_ms - last_ping_ms) < period_ms) return;
    last_ping_ms = now_ms;

    const char *notify_sock = getenv("NOTIFY_SOCKET");
    if (notify_sock == NULL || notify_sock[0] == '\0') return;

    int sfd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (sfd < 0) return;
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    /* Linux abstract socket starts with '\0' — preserve the leading zero. */
    if (notify_sock[0] == '@') {
        addr.sun_path[0] = '\0';
        (void)strncpy(addr.sun_path + 1, notify_sock + 1,
                       sizeof(addr.sun_path) - 2u);
    } else {
        (void)strncpy(addr.sun_path, notify_sock, sizeof(addr.sun_path) - 1u);
    }
    const char payload[] = "WATCHDOG=1";
    (void)sendto(sfd, payload, sizeof(payload) - 1u, 0,
                  (struct sockaddr *)&addr, sizeof(addr));
    close(sfd);
}

static void bridge_mux(void *arg)
{
    (void)arg;
    while (g_socket_running) {
        /* Pick up any fds the listener handed us. */
        int afd = -1;
        if (g_fd_queue != NULL &&
            embediq_osal_queue_recv(g_fd_queue, &afd, 0u) == EMBEDIQ_OK) {
            mux_handle_new_fd(afd);
        }

        uint32_t now_ms = embediq_osal_time_ms();
        for (uint8_t i = 0u; i < EMBEDIQ_BRIDGE_MAX_EXT_FBS; i++) {
            mux_process_slot(i, now_ms);
        }
        notify_systemd_watchdog(now_ms);

        /* Sleep briefly so we don't busy-loop. The poll-based read is
         * non-blocking; coarse-grain wake matches the heartbeat cadence. */
        embediq_osal_delay_ms(50u);
    }

    /* Shutdown — close any open slots. */
    for (uint8_t i = 0u; i < EMBEDIQ_BRIDGE_MAX_EXT_FBS; i++) {
        close_slot(i, 0u);
    }
}

static int open_unix_listener(const char *path)
{
    int fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
    if (fd < 0) return -1;
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    (void)strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1u);
    (void)unlink(path);   /* stale socket from prior run is fine to remove */
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(fd);
        return -1;
    }
    if (listen(fd, (int)EMBEDIQ_BRIDGE_MAX_EXT_FBS) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

#endif /* EMBEDIQ_BRIDGE_TRANSPORT_SOCKET && EMBEDIQ_PLATFORM_HOST */

/* ---------------------------------------------------------------------------
 * FB init / exit (called by embediq_engine_boot)
 * ------------------------------------------------------------------------- */

static void bridge_init(EmbedIQ_FB_Handle_t fb, void *fb_data)
{
    (void)fb_data;
    g_bridge_fb = fb;

#if defined(EMBEDIQ_BRIDGE_TRANSPORT_SOCKET) && defined(EMBEDIQ_PLATFORM_HOST)
    memset(g_socket_pool, 0, sizeof(g_socket_pool));

    const char *sock_path = getenv("EMBEDIQ_BRIDGE_SOCK");
    if (sock_path == NULL) sock_path = "/tmp/embediq_bridge.sock";
    if (sock_path[0] != '\0') {
        g_unix_fd = open_unix_listener(sock_path);
    }
    /* TCP listener — deferred per ITEM5 Topic 11 OUT table.
     * Env vars are read for forward-compatibility / documentation only. */
    (void)getenv("EMBEDIQ_BRIDGE_PORT");
    (void)getenv("EMBEDIQ_BRIDGE_HOST");

    const char *hb = getenv("EMBEDIQ_BRIDGE_HEARTBEAT_MS");
    if (hb != NULL && hb[0] != '\0') {
        unsigned long v = strtoul(hb, NULL, 10);
        if (v > 0ul) g_heartbeat_ms = (uint32_t)v;
    }
    const char *wd = getenv("WATCHDOG_USEC");
    if (wd != NULL && wd[0] != '\0') {
        g_watchdog_us = strtoull(wd, NULL, 10) / 2u;
    }

    if (g_unix_fd >= 0) {
        g_fd_queue = embediq_osal_queue_create(EMBEDIQ_BRIDGE_MAX_EXT_FBS,
                                                (uint16_t)sizeof(int));
        g_socket_running = true;
        g_listener_task  = embediq_osal_task_create("fb_bridge_listen",
                                                     bridge_listener, NULL,
                                                     2u, 4096u);
        g_mux_task       = embediq_osal_task_create("fb_bridge_mux",
                                                     bridge_mux, NULL,
                                                     2u, 8192u);
    }
#endif
}

static void bridge_exit(EmbedIQ_FB_Handle_t fb, void *fb_data)
{
    (void)fb; (void)fb_data;
#if defined(EMBEDIQ_BRIDGE_TRANSPORT_SOCKET) && defined(EMBEDIQ_PLATFORM_HOST)
    g_socket_running = false;
    if (g_unix_fd >= 0) {
        close(g_unix_fd);
        g_unix_fd = -1;
    }
    if (g_listener_task != NULL) {
        embediq_osal_task_join(g_listener_task);
        g_listener_task = NULL;
    }
    if (g_mux_task != NULL) {
        embediq_osal_task_join(g_mux_task);
        g_mux_task = NULL;
    }
    if (g_fd_queue != NULL) {
        embediq_osal_queue_destroy(g_fd_queue);
        g_fd_queue = NULL;
    }
#endif
    g_bridge_fb = NULL;
}

/* ---------------------------------------------------------------------------
 * Public: fb_bridge_register()
 * Call before embediq_engine_boot().
 * ------------------------------------------------------------------------- */

EMBEDIQ_PUBS(g_bridge_pubs,
    MSG_BRIDGE_EXTFB_CONNECTED,
    MSG_BRIDGE_EXTFB_DISCONNECTED,
    MSG_BRIDGE_FAULT);

EmbedIQ_FB_Handle_t fb_bridge_register(void)
{
    static const EmbedIQ_FB_Config_t k_cfg = {
        .name              = "fb_bridge",
        .boot_phase        = EMBEDIQ_BOOT_PHASE_BRIDGE,
        .init_fn           = bridge_init,
        .exit_fn           = bridge_exit,
        .publications      = g_bridge_pubs,
        .publication_count = 3u,
    };
    return embediq_fb_register(&k_cfg);
}
