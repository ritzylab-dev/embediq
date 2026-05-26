<!--
 MISRA_COMPLIANCE.md — EmbedIQ MISRA-C:2012 posture and intentional deviations

 @author  Ritesh Anand
 @company embediq.com | ritzylab.com

 SPDX-License-Identifier: Apache-2.0
-->

# MISRA-C:2012 Compliance

## Overview

EmbedIQ uses **cppcheck** (open-source, GPL-3.0) for MISRA-C:2012 style
analysis on every PR via GitHub Actions CI. **This is NOT a certified MISRA
compliance tool.** For formal safety certification (ISO 26262, IEC 61508), a
certified tool (PC-lint Plus, Polyspace, IAR, LDRA) is required.

cppcheck covers the mechanically-checkable subset of MISRA-C:2012 rules.
Coverage is approximately **130 of 143 rules**.

The scan runs as a **warning-only** step — it never blocks a PR. The purpose
is to surface violations early so they do not accumulate. All intentional
deviations are documented below.

cppcheck is invoked as an external CI subprocess. Its GPL-3.0 licence applies
only to the tool binary on the CI runner; no cppcheck source or binary is
linked into any EmbedIQ build artefact, so the framework's Apache-2.0
licensing (R-04) is unaffected.

---

## Tool

| Field      | Value                                                                  |
| ---------- | ---------------------------------------------------------------------- |
| Tool       | cppcheck                                                               |
| License    | GPL-3.0                                                                |
| Addon      | misra.py (ships with cppcheck)                                         |
| Source     | github.com/danmar/cppcheck                                             |
| Version    | Pinned in CI: see `.github/workflows/ci.yml` install step              |
| Standard   | C11 (`--std=c11`)                                                      |
| Scan dirs  | `core/src/`, `fbs/drivers/`, `fbs/services/`, `hal/posix/`, `osal/posix/` |
| Severity   | Warning-only (exit 0). `--strict` reserved for future gate upgrade.    |

The CI invocation is `python3 tools/ci/check_misra.py`. The script wraps
cppcheck on a per-file basis; it implements no MISRA rule logic itself.

---

## Intentional Deviations

The deviations below are accepted with rationale. cppcheck warnings that match
these categories are noise — they do not represent defects and will not be
fixed.

| Rule                   | Category | Location          | Rationale                                                                                                                                                                                                                                                  |
| ---------------------- | -------- | ----------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| MISRA-C:2012 Rule 21.6 | Required | `hal/posix/`      | POSIX HAL uses `fwrite`/`fread` for Observatory stream output. This is intentional — `hal/posix/` is a POSIX-layer implementation, not production embedded code. The HAL contract boundary ensures this code never runs on bare metal targets.             |
| MISRA-C:2012 Rule 15.5 | Advisory | `core/src/`, `fbs/` | Early-return error-handling pattern is used throughout. Single-exit-point (Rule 15.5) conflicts with readable C error paths. All early returns are from error conditions, never from mid-logic.                                                            |
| MISRA-C:2012 Rule 21.3 | Required | N/A               | Dynamic memory (`malloc`/`free`) is already prohibited by EmbedIQ Rule I-07 and R-02, enforced by `check_fb_calls.py` in strict mode. MISRA-21.3 is redundant — any violation caught by cppcheck here is a double-violation and a CI blocker via the existing check. |
| MISRA-C:2012 Rule 20.9 | Advisory | `core/include/`   | Conditional compilation via `#ifdef EMBEDIQ_TEST_*` is intentional. Test-only symbols are guarded and compile to zero bytes in production (Rule I-05, R-07).                                                                                               |
| MISRA-C:2012 Rule 11.5 | Advisory | `core/src/bus/`   | `void*` cast in message bus for type-erased payload handling. The bus contract guarantees alignment and size via `_Static_assert`.                                                                                                                         |

---

## What Is Not a Deviation

EmbedIQ already enforces several MISRA-aligned properties through its own CI
scripts, independent of cppcheck. These are blocking checks — a violation
fails CI today, not just generates a warning:

- **No dynamic allocation:** `tools/ci/check_fb_calls.py` (strict) — maps to MISRA 21.3.
- **No cross-layer POSIX calls in FBs:** `tools/ci/check_fb_calls.py` — maps to MISRA 21.6 scope (forbidden POSIX threading and timing headers in FB code).
- **Event record size invariant:** `_Static_assert(sizeof(EmbedIQ_Event_t) == 14, ...)` — maps to MISRA 9.1 (deterministic struct layout).
- **SPDX identifier on all files:** `tools/ci/licence_check.py`.
- **Library Observatory obligation:** `tools/ci/check_lib_obs.py` — every library that allocates resources must emit `EMBEDIQ_OBS_EVT_LIB_INIT` / `LIB_DEINIT` (R-lib-1).
- **OSAL/HAL Observatory obligation:** `tools/ci/check_osal_obs.py`, `tools/ci/check_hal_obs.py` — every failure path must emit the relevant XOBS event.

These properties are in scope of MISRA but are enforced by EmbedIQ's own,
purpose-built tooling rather than relying on cppcheck. cppcheck's MISRA addon
is layered on top to catch the wider mechanical subset of the standard.

---

## Upgrade Path

When the project is ready to make MISRA a blocking gate:

1. Run `python3 tools/ci/check_misra.py --strict` locally; fix all violations not in the deviation table.
2. Add new deviations to this document with rationale.
3. Change the CI step in `.github/workflows/ci.yml` to: `python3 tools/ci/check_misra.py --strict`.
4. Update this document: change "Severity: Warning-only" to "Severity: Blocking (exit 1 on violation)".

For formal safety certification (ISO 26262, IEC 61508, DO-178C):

- Replace cppcheck with a certified tool (PC-lint Plus, Polyspace, IAR, LDRA, Coverity).
- Obtain a MISRA-C:2012 Compliance Matrix from the tool vendor.
- Document the certified tool version, configuration file, and ruleset in this document.
- Move the deviation table into the formal compliance matrix format required by the certifier.

---

*EmbedIQ — embediq.com — Apache 2.0*
