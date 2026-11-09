#!/usr/bin/env bash
#
# tools/ci/check_release_symbols.sh — no test hooks in the host release build
#
# Builds the host release profile (-DEMBEDIQ_PLATFORM=host
# -DCMAKE_BUILD_TYPE=Release: EMBEDIQ_TEST_HOOKS off, so no test executable
# is built) and inspects every framework static library:
#
#   (a) no symbol listed in tools/ci/test_hook_inventory.txt is defined;
#   (b) no defined global symbol contains "__" unless it is in the inventory
#       or in the production allowlist below;
#   (c) the core libraries reference no malloc/calloc/realloc/free (R-02).
#       The OSAL POSIX library is the documented allocation site and is not
#       part of (c).
#
# Hooks whose names have no "__" (bus_inject, embediq_platform_lib_reset)
# are caught by (a) only, so they must be in the inventory.
#
# Usage:   tools/ci/check_release_symbols.sh            (from the repo root)
# Env:     CMAKE (default: cmake), BUILD_DIR (default: build-release)
# Exit:    0 when every check passes, 1 on any failure.
#
# @author  Ritesh Anand
# @company embediq.com | ritzylab.com
#
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
CMAKE="${CMAKE:-cmake}"
BUILD_DIR="${BUILD_DIR:-$ROOT/build-release}"
INVENTORY="$ROOT/tools/ci/test_hook_inventory.txt"

# Pinned artefact set: the framework static libraries of the release build.
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

# Core libraries checked for allocation references (c).
CORE_LIBS=(
    core/src/libembediq_fb_engine.a
    core/src/libembediq_cfg.a
)

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

# Mach-O prefixes C symbols with '_'; ELF does not.
strip_prefix() {
    if [ "$(uname -s)" = "Darwin" ]; then sed 's/^_//'; else cat; fi
}

defined_globals() {   # $1 = archive → one defined global symbol per line
    nm -g --defined-only "$1" | awk 'NF >= 3 { print $NF }' | strip_prefix | sort -u
}

undefined_refs() {    # $1 = archive → one undefined symbol per line
    nm -u "$1" | awk 'NF >= 1 && $NF !~ /:$/ { print $NF }' | strip_prefix | sort -u
}

echo "== Configure and build the host release profile into $BUILD_DIR"
"$CMAKE" -S "$ROOT" -B "$BUILD_DIR" -DEMBEDIQ_PLATFORM=host -DCMAKE_BUILD_TYPE=Release
"$CMAKE" --build "$BUILD_DIR" -j "$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

grep -vE '^[[:space:]]*(#|$)' "$INVENTORY" | sort -u > "$WORK/inventory"
printf '%s\n' "${ALLOWLIST[@]}" | sort -u > "$WORK/allowlist"

echo "== Artefacts"
: > "$WORK/all"
for lib in "${LIBS[@]}"; do
    path="$BUILD_DIR/$lib"
    if [ ! -f "$path" ]; then
        echo "FAIL  missing artefact: $lib"
        exit 1
    fi
    defined_globals "$path" > "$WORK/syms"
    echo "  $lib  ($(wc -l < "$WORK/syms" | tr -d ' ') defined globals)"
    sed "s|\$| $lib|" "$WORK/syms" >> "$WORK/all"
done

status=0

# (a) inventoried hooks present
awk 'NR == FNR { inv[$1] = 1; next } ($1 in inv)' "$WORK/inventory" "$WORK/all" \
    | sort > "$WORK/hooks_found"
n_inv=$(wc -l < "$WORK/inventory" | tr -d ' ')
n_found=$(cut -d' ' -f1 "$WORK/hooks_found" | sort -u | wc -l | tr -d ' ')
if [ "$n_found" -gt 0 ]; then
    echo "FAIL  (a) $n_found of $n_inv inventoried test hooks present in release libraries:"
    sed 's/^/        /' "$WORK/hooks_found"
    status=1
else
    echo "PASS  (a) 0 of $n_inv inventoried test hooks present"
fi

# (b) unlisted double-underscore globals
awk 'NR == FNR { known[$1] = 1; next } ($1 ~ /__/) && !($1 in known)' \
    <(cat "$WORK/inventory" "$WORK/allowlist") "$WORK/all" | sort > "$WORK/unlisted"
n_unlisted=$(wc -l < "$WORK/unlisted" | tr -d ' ')
n_allowed=$(awk 'NR == FNR { ok[$1] = 1; next } ($1 in ok)' "$WORK/allowlist" "$WORK/all" | wc -l | tr -d ' ')
if [ "$n_unlisted" -gt 0 ]; then
    echo "FAIL  (b) $n_unlisted defined globals contain \"__\" and are in neither the inventory nor the allowlist:"
    sed 's/^/        /' "$WORK/unlisted"
    status=1
else
    echo "PASS  (b) no unlisted \"__\" globals ($n_allowed allowlisted production definitions)"
fi

# (c) allocation references in core libraries
: > "$WORK/alloc"
for lib in "${CORE_LIBS[@]}"; do
    undefined_refs "$BUILD_DIR/$lib" | grep -xE 'malloc|calloc|realloc|free' \
        | sed "s|\$| $lib|" >> "$WORK/alloc" || true
done
n_alloc=$(wc -l < "$WORK/alloc" | tr -d ' ')
if [ "$n_alloc" -gt 0 ]; then
    echo "FAIL  (c) core libraries reference allocation functions:"
    sed 's/^/        /' "$WORK/alloc"
    status=1
else
    echo "PASS  (c) no malloc/calloc/realloc/free referenced by ${#CORE_LIBS[@]} core libraries"
fi

if [ "$status" -ne 0 ]; then
    echo "RESULT: FAIL"
else
    echo "RESULT: PASS (${#LIBS[@]} artefacts)"
fi
exit "$status"
