/*
 * embediq_subfn.h — Sub-function contract
 *
 * Core Layer contract for registering sub-functions within a Functional Block.
 * Declarations only — zero implementation in this file.
 *
 * I-01: Compiles standalone (no OSAL/BSP). Depends only on embediq_fb.h.
 * R-03: C11.
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef EMBEDIQ_SUBFN_H
#define EMBEDIQ_SUBFN_H

#include "embediq_fb.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
 * OSAL signal type — opaque; implementation lives in osal/
 *
 * Pointer-to-incomplete-type: Core headers may declare EmbedIQ_Signal_t*
 * fields but must never dereference or size the struct (I-01, R-sub-10).
 *
 * osal_signal is valid ONLY in Platform FB sub-functions (R-sub-11).
 * Application FB sub-functions must set osal_signal = NULL.
 * ------------------------------------------------------------------------- */

/** Opaque OSAL signal. Platform FBs only — NULL for all application FBs. */
typedef struct EmbedIQ_Signal_s EmbedIQ_Signal_t;

/* ---------------------------------------------------------------------------
 * Sub-function callback types
 * ------------------------------------------------------------------------- */

/** Called once during FB init, in ascending init_order. */
typedef void (*embediq_subfn_init_fn)(EmbedIQ_FB_Handle_t fb, void *fb_data,
                                      void *subfn_data);

/** Called for every message that matches a sub-function subscription. */
typedef void (*embediq_subfn_run_fn)(EmbedIQ_FB_Handle_t fb, const void *msg,
                                     void *fb_data, void *subfn_data);

/** Called during ordered FB shutdown, reverse init_order. */
typedef void (*embediq_subfn_exit_fn)(EmbedIQ_FB_Handle_t fb, void *fb_data,
                                      void *subfn_data);

/* ---------------------------------------------------------------------------
 * Sub-function configuration — passed to embediq_subfn_register()
 *
 * Must be called from within the parent FB's init_fn only (R-sub-08).
 * Sub-function registrations must all complete before init_fn returns.
 * ------------------------------------------------------------------------- */

typedef struct {
    const char             *name;               /**< Sub-function name (diagnostics). */
    uint8_t                 init_order;          /**< Ascending call order for init_fn. */
    embediq_subfn_init_fn   init_fn;             /**< Optional. NULL = no init. */
    embediq_subfn_run_fn    run_fn;              /**< Handler called per matching message. */
    embediq_subfn_exit_fn   exit_fn;             /**< Optional. NULL = no cleanup. */
    const uint16_t         *subscriptions;       /**< MSG_IDs this sub-fn handles. */
    uint8_t                 subscription_count;  /**< Length of subscriptions[]. */
    void                   *subfn_data;          /**< Sub-function-private context. */
    void                   *fsm;                 /**< Optional EmbedIQ_SM_t* for table-driven FSM. */
    EmbedIQ_Signal_t       *osal_signal;         /**< Platform FBs only. NULL for app FBs. */
} EmbedIQ_SubFn_Config_t;

/* ---------------------------------------------------------------------------
 * Public API — implemented in core/src/dispatcher/
 * ------------------------------------------------------------------------- */

/** Register a sub-function with its parent FB. Must be called from init_fn. */
int embediq_subfn_register(EmbedIQ_FB_Handle_t fb,
                            const EmbedIQ_SubFn_Config_t *config);

#ifdef __cplusplus
}
#endif

#endif /* EMBEDIQ_SUBFN_H */
