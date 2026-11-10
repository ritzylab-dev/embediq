# tools/ci — CI checks

Scripts run by `.github/workflows/ci.yml`. Each can be run locally from the
repository root.

<!--
@author  Ritesh Anand
@company embediq.com | ritzylab.com
SPDX-License-Identifier: Apache-2.0
-->

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
