/*
 * core/include/embediq_endpoint.h — Endpoint identifier types
 *
 * Endpoint identifier types and limits. This header declares no functions:
 * endpoints are assigned by the engine at registration.
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * I-01: Compiles standalone with zero OSAL or BSP dependencies.
 * R-03: C11. Fixed-width types from <stdint.h> only.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef EMBEDIQ_ENDPOINT_H
#define EMBEDIQ_ENDPOINT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum byte length (including NUL) of an endpoint name. */
#define EMBEDIQ_EP_NAME_MAX   32

typedef uint16_t embediq_ep_id_t;

/** Sentinel: no endpoint. */
#define EMBEDIQ_EP_INVALID    ((embediq_ep_id_t)0xFFFFu)

#ifdef __cplusplus
}
#endif

#endif /* EMBEDIQ_ENDPOINT_H */
