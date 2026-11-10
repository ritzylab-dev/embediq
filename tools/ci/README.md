# tools/ci — CI checks

Scripts run by `.github/workflows/ci.yml`. Each can be run locally from the
repository root.

<!--
@author  Ritesh Anand
@company embediq.com | ritzylab.com
SPDX-License-Identifier: Apache-2.0
-->

## Checks

`static_checks.sh` runs the nine blocking checks and the six advisory checks
below, with their self-tests, in one list; the CI `build` job and
`scripts/check.sh` both run it. A blocking check that fails fails the run; an
advisory check is reported only. `check_release_symbols.sh` runs in the CI job
`release-symbols` and `tsan_summarise.py` in the CI job `tsan`; both jobs are
blocking.

| Check | Enforces | Reference | Class | Self-test |
| ----- | -------- | --------- | ----- | --------- |
| `tools/validator.py` | no hardcoded sizing numbers where an `embediq_config.h` constant is required | I-08 | blocking | — |
| `check_lib_obs.py --strict` | every library with a resource acquisition path emits the library init Observatory event | R-lib-1 | blocking | — |
| `check_osal_obs.py --strict` | OSAL failure paths emit Observatory events; `embediq_err_t` is never negated | R-osal-01 | blocking | — |
| `check_hal_obs.py --strict` | every HAL function returning an error emits a HAL Observatory error first | XOBS-2 | blocking | — |
| `check_fb_calls.py` | no forbidden function calls or headers in Functional Block and core code | R-sub-03, R-sub-03a, R-stdio, R-sub-34 | blocking | `test_check_fb_calls.py` |
| `tools/boundary_checker.py` | no source includes a header from a layer it may not access | ARCHITECTURE.md "Layer Boundary Rules" | blocking | `tools/test_boundary_checker.py` |
| `licence_check.py --strict` | every first-party `.c`/`.h` carries `SPDX-License-Identifier: Apache-2.0` | R-04 | blocking | `test_licence_check.py` |
| `check_message_ids.py` | no message ID or message name is defined more than once | CODING_RULES.md, message-ID rules | blocking | `test_check_message_ids.py` |
| `check_core_headers.sh` | every core header compiles standalone and includes only C freestanding headers | R-08, I-01 | blocking | `test_check_core_headers.sh` |
| `component_globals_check.py` | components do not store OSAL handles at file scope | R-lib-4 | advisory | — |
| `component_state_check.py` | components do not call OSAL APIs | R-lib-4 | advisory | — |
| `vendoring_check.py` | every `third_party/` subdirectory has a complete `VENDORING.md` | R-lib-3 | advisory | — |
| `vendoring_date_check.py` | third-party anomaly assessments are reviewed annually | R-lib-3 | advisory | — |
| `ops_version_check.py` | every ops table header defines its ABI version and leads with a `uint32_t version` field | R-lib-4 | advisory | — |
| `binary_blob_check.py` | no compiled objects in source directories | CODING_RULES.md | advisory | — |
| `check_release_symbols.sh` | no test hooks in the host release build | R-07, I-05 | blocking (job `release-symbols`) | `test_check_release_symbols.sh` |
| `tsan_summarise.py` | no ThreadSanitizer finding, runtime abort or sanitizer-handled crash in the test run | ARCHITECTURE.md "Threads in EmbedIQ" | blocking (job `tsan`) | `test_tsan_summarise.py` |

The `tsan` job also builds `tsan_canary.c`, a deliberately racy program, and
requires the summary to reject it before the test run — proof on every run
that a data race reaches the summary and fails it.

## Release symbol check

`check_release_symbols.sh` builds the host release profile
(`-DEMBEDIQ_PLATFORM=host -DCMAKE_BUILD_TYPE=Release`, so `EMBEDIQ_TEST_HOOKS` is
off and no test executable is built) into `build-release/` and inspects the
defined global symbols of every framework static library in a pinned list. It
fails when (a) any symbol listed in `test_hook_inventory.txt` is defined, (b) a
defined global contains `__` and is in neither the inventory nor the script's
production allowlist, or (c) a core library references
`malloc`/`calloc`/`realloc`/`free` (the OSAL POSIX library, the documented
allocation site, is excluded from (c)). The CI job `release-symbols` runs it on
every push and pull request. Set `CMAKE` or `BUILD_DIR` to override the cmake
binary or the build directory.

`test_hook_inventory.txt` is the explicit inventory of test hooks: every
function with external linkage defined inside an `EMBEDIQ_TEST_HOOKS` block in
`core/`, `osal/`, `hal/`, `fbs/` and `examples/`, one per line, sorted. A hook
added to the tree is added to the inventory in the same change. Hooks whose
names contain no `__` (for example `bus_inject`) are caught only through the
inventory.
