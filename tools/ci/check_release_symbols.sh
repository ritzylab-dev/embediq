#!/usr/bin/env bash
#
# tools/ci/check_release_symbols.sh — no test hooks in the host release build
#
# Builds the host release profile (-DEMBEDIQ_PLATFORM=host
# -DCMAKE_BUILD_TYPE=Release: EMBEDIQ_TEST_HOOKS off, so no test executable
# is built) and a Debug profile of the same artefacts (test hooks on), then
# inspects a pinned artefact set: the framework static libraries and the
# example executables.
#
#   (a) no symbol listed in tools/ci/test_hook_inventory.txt is defined in
#       any release artefact;
#   (b) no defined global symbol of a release ARCHIVE contains "__" unless it
#       is in the inventory or in the production allowlist below;
#   (c) the core libraries reference no malloc/calloc/realloc/free (R-02).
#       The OSAL POSIX library is the documented allocation site and is not
#       part of (c);
#   (d) every inventory symbol is defined in the Debug build of the same
#       artefact set, so (a) looks for symbols that exist;
#   (e) every defined global of a Debug ARCHIVE that contains "__" is in the
#       inventory or the allowlist, so the inventory is complete;
#   (f) no static listed in tools/ci/test_static_state_inventory.txt appears
#       in the full symbol table (locals included) of the release core
#       library.
#
# (b) and (e) cover archives only: a linked ELF executable also defines
# C-runtime globals that contain "__" (__TMC_END__, __bss_start,
# __data_start, __dso_handle) and are not ours.
#
# Limits: hooks whose names have no "__" (bus_inject,
# embediq_platform_lib_reset) are covered by (a) and (d) only — an inventory
# line deleted for one of them is not detectable by any symbol rule. (f) is
# evidence per toolchain: a compiler may legitimately drop a static whose
# address never escapes, which would pass (f) vacuously; the self-test
# (tools/ci/test_check_release_symbols.sh) guards against that.
#
# A tool failure (nm, grep, awk, sort, the build) is never read as an empty
# result: the script stops with "RESULT: ERROR (tool failure)" and exit 2.
#
# Usage:   tools/ci/check_release_symbols.sh            (from the repo root)
# Env:     CMAKE (default: cmake), BUILD_DIR (default: build-release),
#          DEBUG_BUILD_DIR (default: build-debug-check)
# Exit:    0 when every check passes, 1 on any FAIL, 2 on a tool failure.
#
# @author  Ritesh Anand
# @company embediq.com | ritzylab.com
#
# SPDX-License-Identifier: Apache-2.0

set -Eeuo pipefail
trap 'echo "RESULT: ERROR (tool failure)"; exit 2' ERR

command -v nm > /dev/null
export LC_ALL=C   # one collation for sort and comm

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
CMAKE="${CMAKE:-cmake}"
BUILD_DIR="${BUILD_DIR:-$ROOT/build-release}"
DEBUG_BUILD_DIR="${DEBUG_BUILD_DIR:-$ROOT/build-debug-check}"
INVENTORY="$ROOT/tools/ci/test_hook_inventory.txt"
STATIC_INVENTORY="$ROOT/tools/ci/test_static_state_inventory.txt"

# Pinned artefact set: the framework static libraries and the example
# executables (two inventoried hooks live in the thermostat example).
# fb_bridge.c and embediq_ext_fb.c are compiled into both bridge archives.
LIBS=(
    core/src/libembediq_fb_engine.a
    core/src/libembediq_cfg.a
    osal/posix/libembediq_osal_posix.a
    hal/posix/libembediq_hal_posix.a
    fbs/drivers/libembediq_driver_fbs.a
    fbs/services/libembediq_service_fbs.a
    fbs/bridge/libembediq_bridge.a
    fbs/bridge/libembediq_bridge_socket.a
)
EXES=(
    examples/thermostat/embediq_thermostat
    examples/gateway/embediq_gateway
    examples/env_monitor/env_monitor
    examples/gpio_blink/embediq_gpio_blink
)
ARTEFACTS=("${LIBS[@]}" "${EXES[@]}")

# CMake targets producing the artefact set (built for the Debug profile).
TARGETS=(
    embediq_fb_engine embediq_cfg embediq_osal_posix embediq_hal_posix
    embediq_driver_fbs embediq_service_fbs embediq_bridge embediq_bridge_socket
    thermostat gateway env_monitor gpio_blink
)

# Core libraries checked for allocation references (c).
CORE_LIBS=(
    core/src/libembediq_fb_engine.a
    core/src/libembediq_cfg.a
)

# Release library whose full symbol table is checked for static state (f).
STATIC_LIB=core/src/libembediq_fb_engine.a

# Production symbols whose names contain "__": defined outside any test-hook
# block and called by production code. fb_bridge__publish_* are called by the
# socket transport; fb_telemetry__agg_* by fb_telemetry's sub-functions (and
# also by its unit test).
ALLOWLIST=(
    fb_bridge__publish_extfb_connected
    fb_bridge__publish_extfb_disconnected
    fb_bridge__publish_fault
    fb_telemetry__agg_counter
    fb_telemetry__agg_gauge
    fb_telemetry__agg_histogram
)

JOBS="$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# Mach-O prefixes C symbols with '_'; ELF does not.
strip_prefix() {
    if [ "$(uname -s)" = "Darwin" ]; then sed 's/^_//'; else cat; fi
}

# Each inspection writes the raw nm output to a file first, so an nm failure
# stops the script instead of reading as an empty symbol list.
defined_globals() {   # $1 = artefact, $2 = output file → one symbol per line
    nm -g --defined-only "$1" > "$WORK/nm.raw"
    awk 'NF >= 3 { print $NF }' "$WORK/nm.raw" | strip_prefix | sort -u > "$2"
}

undefined_refs() {    # $1 = archive, $2 = output file → one symbol per line
    nm -u "$1" > "$WORK/nm.raw"
    awk 'NF >= 1 && $NF !~ /:$/ { print $NF }' "$WORK/nm.raw" | strip_prefix | sort -u > "$2"
}

all_symbols() {       # $1 = archive, $2 = output file → every symbol name, locals included
    nm "$1" > "$WORK/nm.raw"
    awk 'NF >= 2 && $NF !~ /:$/ { print $NF }' "$WORK/nm.raw" | strip_prefix | sort -u > "$2"
}

# grep that treats "no match" (status 1) as an empty result and any other
# failure as a tool failure.
filter() {            # grep arguments...
    grep "$@" || [ $? -eq 1 ]
}

count() { wc -l < "$1" | tr -d ' '; }

# Collect "symbol artefact" lines of defined globals over a build directory.
collect() {           # $1 = build dir, $2 = output file, artefacts...
    local dir="$1" out="$2"; shift 2
    : > "$out"
    for a in "$@"; do
        if [ ! -f "$dir/$a" ]; then
            echo "FAIL  missing artefact: $dir/$a"
            exit 1
        fi
        defined_globals "$dir/$a" "$WORK/syms"
        sed "s|\$| $a|" "$WORK/syms" >> "$out"
    done
}

echo "== Configure and build the host release profile into $BUILD_DIR"
"$CMAKE" -S "$ROOT" -B "$BUILD_DIR" -DEMBEDIQ_PLATFORM=host -DCMAKE_BUILD_TYPE=Release
"$CMAKE" --build "$BUILD_DIR" -j "$JOBS"

echo "== Configure and build the Debug profile of the artefact set into $DEBUG_BUILD_DIR"
"$CMAKE" -S "$ROOT" -B "$DEBUG_BUILD_DIR" -DEMBEDIQ_PLATFORM=host -DCMAKE_BUILD_TYPE=Debug
"$CMAKE" --build "$DEBUG_BUILD_DIR" -j "$JOBS" --target "${TARGETS[@]}"

filter -vE '^[[:space:]]*(#|$)' "$INVENTORY" > "$WORK/inventory.raw"
sort -u "$WORK/inventory.raw" > "$WORK/inventory"
filter -vE '^[[:space:]]*(#|$)' "$STATIC_INVENTORY" > "$WORK/static.raw"
sort -u "$WORK/static.raw" > "$WORK/static"
printf '%s\n' "${ALLOWLIST[@]}" | sort -u > "$WORK/allowlist"
cat "$WORK/inventory" "$WORK/allowlist" > "$WORK/known"

echo "== Release artefacts"
collect "$BUILD_DIR" "$WORK/rel_all" "${ARTEFACTS[@]}"
collect "$BUILD_DIR" "$WORK/rel_libs" "${LIBS[@]}"
for a in "${ARTEFACTS[@]}"; do
    filter -c " $a\$" "$WORK/rel_all" > "$WORK/n"
    echo "  $a  ($(cat "$WORK/n") defined globals)"
done
collect "$DEBUG_BUILD_DIR" "$WORK/dbg_all" "${ARTEFACTS[@]}"
collect "$DEBUG_BUILD_DIR" "$WORK/dbg_libs" "${LIBS[@]}"

status=0
n_inv=$(count "$WORK/inventory")

# (a) inventoried hooks present in release artefacts
awk 'NR == FNR { inv[$1] = 1; next } ($1 in inv)' "$WORK/inventory" "$WORK/rel_all" \
    | sort > "$WORK/hooks_found"
cut -d' ' -f1 "$WORK/hooks_found" | sort -u > "$WORK/hooks_found_names"
n_found=$(count "$WORK/hooks_found_names")
if [ "$n_found" -gt 0 ]; then
    echo "FAIL  (a) $n_found of $n_inv inventoried test hooks present in release artefacts:"
    sed 's/^/        /' "$WORK/hooks_found"
    status=1
else
    echo "PASS  (a) 0 of $n_inv inventoried test hooks present in ${#ARTEFACTS[@]} release artefacts"
fi

# (b) unlisted double-underscore globals in release archives
awk 'NR == FNR { known[$1] = 1; next } ($1 ~ /__/) && !($1 in known)' \
    "$WORK/known" "$WORK/rel_libs" | sort > "$WORK/unlisted"
awk 'NR == FNR { ok[$1] = 1; next } ($1 in ok)' "$WORK/allowlist" "$WORK/rel_libs" > "$WORK/allowed"
n_unlisted=$(count "$WORK/unlisted")
if [ "$n_unlisted" -gt 0 ]; then
    echo "FAIL  (b) $n_unlisted release-archive globals contain \"__\" and are in neither the inventory nor the allowlist:"
    sed 's/^/        /' "$WORK/unlisted"
    status=1
else
    echo "PASS  (b) no unlisted \"__\" globals in ${#LIBS[@]} release archives ($(count "$WORK/allowed") allowlisted production definitions)"
fi

# (c) allocation references in core libraries
: > "$WORK/alloc"
for lib in "${CORE_LIBS[@]}"; do
    undefined_refs "$BUILD_DIR/$lib" "$WORK/undef"
    filter -xE 'malloc|calloc|realloc|free' "$WORK/undef" > "$WORK/hits"
    sed "s|\$| $lib|" "$WORK/hits" >> "$WORK/alloc"
done
n_alloc=$(count "$WORK/alloc")
if [ "$n_alloc" -gt 0 ]; then
    echo "FAIL  (c) core libraries reference allocation functions:"
    sed 's/^/        /' "$WORK/alloc"
    status=1
else
    echo "PASS  (c) no malloc/calloc/realloc/free referenced by ${#CORE_LIBS[@]} core libraries"
fi

# (d) every inventory symbol is defined in the Debug artefact set
cut -d' ' -f1 "$WORK/dbg_all" | sort -u > "$WORK/dbg_names"
comm -23 "$WORK/inventory" "$WORK/dbg_names" > "$WORK/unowned"
n_unowned=$(count "$WORK/unowned")
if [ "$n_unowned" -gt 0 ]; then
    echo "FAIL  (d) inventory names a symbol no inspected artefact defines — the inventory or the artefact set is wrong:"
    sed 's/^/        /' "$WORK/unowned"
    status=1
else
    echo "PASS  (d) all $n_inv inventoried hooks defined in the Debug build of the ${#ARTEFACTS[@]} artefacts"
fi

# (e) inventory completeness: Debug-archive "__" globals are all known
awk 'NR == FNR { known[$1] = 1; next } ($1 ~ /__/) && !($1 in known)' \
    "$WORK/known" "$WORK/dbg_libs" | sort > "$WORK/missing"
n_missing=$(count "$WORK/missing")
if [ "$n_missing" -gt 0 ]; then
    echo "FAIL  (e) $n_missing Debug-build globals contain \"__\" and are in neither the inventory nor the allowlist:"
    sed 's/^/        /' "$WORK/missing"
    status=1
else
    echo "PASS  (e) every \"__\" global of ${#LIBS[@]} Debug archives is inventoried or allowlisted"
fi

# (f) static instrumentation in the release core library (full symbol table)
all_symbols "$BUILD_DIR/$STATIC_LIB" "$WORK/static_syms"
comm -12 "$WORK/static" "$WORK/static_syms" > "$WORK/static_found"
n_static=$(count "$WORK/static")
if [ "$(count "$WORK/static_found")" -gt 0 ]; then
    echo "FAIL  (f) static instrumentation present in release: $(tr '\n' ' ' < "$WORK/static_found")($STATIC_LIB)"
    status=1
else
    echo "PASS  (f) 0 of $n_static inventoried test-only statics in $STATIC_LIB"
fi

if [ "$status" -ne 0 ]; then
    echo "RESULT: FAIL"
else
    echo "RESULT: PASS (${#ARTEFACTS[@]} artefacts)"
fi
exit "$status"
