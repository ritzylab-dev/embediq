#define _POSIX_C_SOURCE 200809L

/*
 * core/src/fsm/fsm_engine.c — Table-driven FSM processor
 *
 * Implements the contract declared in core/include/embediq_sm.h.
 *
 * ALGORITHM:
 *   1. Receive msg + msg_id + current FSM state (sm->current_state).
 *   2. Scan table rows where row.current_state == sm->current_state
 *      AND row.event_msg_id == msg_id.
 *   3. Evaluate row.guard_fn(msg, fb_data).
 *      If guard_fn is NULL: always matches (unconditional transition).
 *   4. First row where guard passes:
 *        a. Execute row.action_fn(msg, fb_data) if non-NULL.
 *        b. Commit sm->current_state = row.next_state.
 *        c. Emit EMBEDIQ_OBS_EVT_FSM_TRANS Observatory event.
 *        d. Return — only the first matching row fires.
 *   5. If NO row matches: stay in current state; emit no Observatory event.
 *   6. EMBEDIQ_SM_TABLE_END sentinel (current_state == 0xFF) terminates scan.
 *
 * GUARDS ARE PURE:
 *   Guard functions must have NO side effects.  They must not modify any
 *   shared state, emit events, or perform I/O.  The engine may call a guard
 *   exactly once per row visited; results must be repeatable and
 *   deterministic.  An impure guard is an architectural defect.
 *
 * R-sub-ORDER (CODING_RULES.md R-15):
 *   When multiple sub-functions each own an FSM and all subscribe to the
 *   same message, embediq_sm_process() is called once per sub-function in
 *   init_order ascending.  Ordering is enforced by the sub-fn dispatcher;
 *   this function is stateless with respect to ordering between FSMs.
 *
 * R-02: no malloc/free in this file.
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "embediq_sm.h"
#include "embediq_obs.h"

/* ---------------------------------------------------------------------------
 * Public: embediq_sm_process()
 * ------------------------------------------------------------------------- */

void embediq_sm_process(EmbedIQ_SM_t *sm, const void *msg, uint16_t msg_id,
                        void *fb_data)
{
    if (!sm || !sm->table) return;

    const EmbedIQ_SM_Row_t *row = sm->table;

    /* Scan until sentinel (current_state == 0xFF terminates the table). */
    while (row->current_state != 0xFFu) {

        if (row->current_state == sm->current_state &&
            row->event_msg_id  == msg_id) {

            /*
             * Guard evaluation — guards MUST be pure (no side effects).
             * NULL guard is treated as always-true: unconditional transition.
             */
            bool guard_passes = (row->guard_fn == NULL) ||
                                  row->guard_fn(msg, fb_data);

            if (guard_passes) {
                uint8_t old_state = sm->current_state;

                /* Execute action if provided; NULL action is a valid no-op. */
                if (row->action_fn) {
                    row->action_fn(msg, fb_data);
                }

                /* Commit the state transition. */
                sm->current_state = row->next_state;

                /*
                 * Emit Observatory event for every transition taken.
                 * source encodes old_state, target encodes new_state —
                 * the FSM processor has no FB endpoint ID of its own.
                 */
                embediq_obs_emit(EMBEDIQ_OBS_EVT_FSM_TRANS,
                                 old_state,
                                 row->next_state,
                                 row->next_state,
                                 msg_id);

                return;   /* first matching row only — stop scanning */
            }
        }

        row++;
    }
    /* No matching row — stay in current state, emit no Observatory event. */
}
