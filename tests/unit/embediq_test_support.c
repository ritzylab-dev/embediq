/*
 * embediq_test_support.c — Scenario runner implementation
 *
 * Implements embediq_test_run_scenario() declared in embediq_test.h.
 * Compiled as a static library (embediq_test_support) and linked by test
 * binaries that need deterministic message sequence replay.
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "embediq_test.h"
#include <stddef.h>

#ifdef EMBEDIQ_PLATFORM_HOST

void embediq_test_run_scenario(const embediq_test_scenario_step_t *steps,
                               size_t count)
{
    for (size_t i = 0; i < count; i++) {
        EmbedIQ_Msg_t m = steps[i].msg;   /* copy — bus takes by value */
        bus_inject(&m);
    }
}

#endif /* EMBEDIQ_PLATFORM_HOST */
