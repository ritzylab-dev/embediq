/*
 * core/src/bus/message_bus_internal.h — checked bus preparation for the engine
 *
 * Production-private interface between the FB engine (registry/fb_engine.c)
 * and the message bus (bus/message_bus.c). Not part of any public contract:
 * only these two files include it.
 *
 * Publishing rule: every publishing entry point reads the bus readiness
 * first; while the bus is not ready it refuses the message and reads no
 * other bus state. The engine prepares the bus last in boot — after its
 * execution locks, dispatch semaphores and notify function are in place —
 * and readiness is the last thing preparation stores, so nothing a publisher
 * can reach is incomplete, and a boot that fails never tears down a ready
 * bus. A thread that exists before boot may publish at any time.
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef EMBEDIQ_MESSAGE_BUS_INTERNAL_H
#define EMBEDIQ_MESSAGE_BUS_INTERNAL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Results of message_bus_prepare(). */
#define MESSAGE_BUS_PREPARED        0    /* this call made the bus ready           */
#define MESSAGE_BUS_ALREADY_READY   1    /* ready before the call; nothing changed */
#define MESSAGE_BUS_PREPARE_FAILED  (-1) /* a queue could not be created           */

/**
 * Create the three priority queues of every registered endpoint and build the
 * routing table, then mark the bus ready. All or nothing: if any queue cannot
 * be created, every queue this call created is destroyed, its pointer
 * cleared, the bus stays not ready, one EMBEDIQ_OBS_EVT_FAULT event with
 * EMBEDIQ_FAULT_BOOT_RESOURCE is emitted (target = the endpoint, msg_id = the
 * queue's resource code, embediq_fb.h), and MESSAGE_BUS_PREPARE_FAILED is
 * returned. A bus that is already ready is left untouched — its queues and
 * token table may be in use by publishers. Readiness is stored last, and
 * once stored it is cleared only by the test reset hook.
 * Calling context: boot thread only, with the registration set fixed.
 */
int message_bus_prepare(void);

#ifdef __cplusplus
}
#endif

#endif /* EMBEDIQ_MESSAGE_BUS_INTERNAL_H */
