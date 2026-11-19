/*
 * tests/compat/wdg_signature_conflict.c — negative control: a definition
 * whose signature disagrees with the public watchdog header
 *
 * Includes embediq_wdg.h, then defines embediq_wdg_register with the shape it
 * had before the header and the implementation agreed: no return value. The
 * symbol name is the same, so a linker or a name-only symbol check accepts
 * it; only a translation unit that sees the canonical declaration rejects it.
 * check_wdg_signature_conflict.sh requires the compiler to refuse this file
 * with "conflicting types". It is never built into any target.
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "embediq_wdg.h"

void embediq_wdg_register(void *fb, uint32_t timeout_ms) { (void)fb; (void)timeout_ms; }
