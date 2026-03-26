/*
 * core/include/embediq_endpoint.h — Endpoint Router contract
 *
 * The Endpoint Router provides name-based dynamic registration and
 * lookup for Function Blocks, including External FBs (Layer 3/Bridge).
 * It is separate from the Message Bus: the Bus routes messages by FB
 * address; the Router resolves names to addresses.
 *
 * Phase 3 (Bridge) will add transport-level lookup; for now only
 * in-process registration is needed.
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef EMBEDIQ_ENDPOINT_H
#define EMBEDIQ_ENDPOINT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum byte length (including NUL) of a registered endpoint name. */
#define EMBEDIQ_EP_NAME_MAX   32

typedef uint16_t embediq_ep_id_t;

/** Sentinel: returned by lookup when a name is not found. */
#define EMBEDIQ_EP_INVALID    ((embediq_ep_id_t)0xFFFFu)

/**
 * embediq_endpoint_register — register this FB's name → id mapping.
 * @name       NUL-terminated name, max EMBEDIQ_EP_NAME_MAX bytes
 * @ep_id_out  receives the assigned id on success
 * @return     0 on success, negative on error (name too long, table full)
 */
int embediq_endpoint_register(const char *name, embediq_ep_id_t *ep_id_out);

/**
 * embediq_endpoint_lookup — look up a name, writing the id to *ep_id_out.
 * @return  0 on success; -1 if not found (*ep_id_out set to EMBEDIQ_EP_INVALID)
 */
int embediq_endpoint_lookup(const char *name, embediq_ep_id_t *ep_id_out);

/**
 * embediq_endpoint_unregister — remove a registration.
 * @return  0 on success, -1 if id was not registered
 */
int embediq_endpoint_unregister(embediq_ep_id_t ep_id);

#ifdef __cplusplus
}
#endif

#endif /* EMBEDIQ_ENDPOINT_H */
