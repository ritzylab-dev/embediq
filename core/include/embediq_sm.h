/*
 * embediq_sm.h — Finite State Machine contract
 *
 * Core Layer contract for table-driven FSMs attached to sub-functions.
 * Declarations only — zero implementation in this file.
 *
 * I-01: Compiles standalone (no OSAL/BSP). Depends only on <stdint.h> and
 *       <stdbool.h>.
 * R-03: C11.
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef EMBEDIQ_SM_H
#define EMBEDIQ_SM_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
 * FSM callback types
 * ------------------------------------------------------------------------- */

/**
 * Guard function — evaluated before a transition row is taken.
 * Return true to allow the transition; false to skip and continue scanning.
 * NULL guard is treated as always-true.
 */
typedef bool (*embediq_sm_guard_fn)(const void *msg, void *fb_data);

/**
 * Action function — executed when a transition is taken (after guard passes).
 * Must not call embediq_fb_register() or embediq_subfn_register().
 * NULL action performs no side-effect but the state transition still occurs.
 */
typedef void (*embediq_sm_action_fn)(const void *msg, void *fb_data);

/* ---------------------------------------------------------------------------
 * FSM transition table row
 * ------------------------------------------------------------------------- */

typedef struct {
    uint8_t               current_state; /**< State this row applies to. */
    uint16_t              event_msg_id;  /**< MSG_ID that triggers this row. */
    embediq_sm_guard_fn   guard_fn;      /**< Optional guard. NULL = always true. */
    embediq_sm_action_fn  action_fn;     /**< Optional action. NULL = no side-effect. */
    uint8_t               next_state;    /**< State to transition into on match. */
} EmbedIQ_SM_Row_t;

/**
 * Table sentinel — must be the last row in every EmbedIQ_SM_Row_t[] table.
 * embediq_sm_process() stops scanning on this row.
 */
#define EMBEDIQ_SM_TABLE_END \
    { 0xFF, 0xFFFF, NULL, NULL, 0xFF }

/* ---------------------------------------------------------------------------
 * FSM instance
 * ------------------------------------------------------------------------- */

typedef struct {
    uint8_t                 current_state; /**< Active state index. Mutable. */
    const EmbedIQ_SM_Row_t *table;         /**< Pointer to the transition table. */
    const char             *name;          /**< FSM name for Observatory tracing. */
} EmbedIQ_SM_t;

/* ---------------------------------------------------------------------------
 * Public API — implemented in core/src/fsm/
 * ------------------------------------------------------------------------- */

/**
 * Process one message through the FSM.
 *
 * Scans the table for the first row where current_state matches and
 * event_msg_id matches and guard_fn returns true (or is NULL).
 * Fires action_fn (if non-NULL) and updates sm->current_state.
 * Stops at the EMBEDIQ_SM_TABLE_END sentinel.
 */
void embediq_sm_process(EmbedIQ_SM_t *sm, const void *msg, uint16_t msg_id,
                        void *fb_data);

#ifdef __cplusplus
}
#endif

#endif /* EMBEDIQ_SM_H */
