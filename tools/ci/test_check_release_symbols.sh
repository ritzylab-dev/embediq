#!/usr/bin/env bash
#
# tools/ci/test_check_release_symbols.sh — self-test for check_release_symbols.sh
#
# Runs the release symbol check seven times against a scratch copy of the
# working tree and asserts each verdict: six cases the check must reject and
# one it must accept.
#
#   S1   a test-only Observatory counter (g_total) left unguarded
#        → FAIL (f) names g_total
#   S2a  sensor__last_temp guarded by the host macro instead of test hooks
#        → FAIL (a) names it in embediq_thermostat
#   S2b  ctrl__reading_count likewise → FAIL (a) names it in embediq_thermostat
#   S3a  nm fails for undefined-symbol inspection (-u)  → RESULT: ERROR
#   S3b  nm fails for defined-symbol inspection         → RESULT: ERROR
#   S4   obs__reset removed from the hook inventory
#        → FAIL (e) names obs__reset
#   S5   unmodified tree → RESULT: PASS, exit 0, (c) passes on an empty
#        allocation list
#
# Every case must exit non-zero except S5. The scratch copy (tracked and
# untracked, non-ignored files; no build directories) lives under $TMPDIR with
# its build directories inside it, and is removed on exit. Cases share those
# build directories: each mutation is undone before the next case, so a case
# rebuilds only the files it touched.
#
# Usage:   tools/ci/test_check_release_symbols.sh   (from the repo root)
# Env:     CMAKE (default: cmake)
# Exit:    0 when every case gives its expected verdict, 1 otherwise.
#
# @author  Ritesh Anand
# @company embediq.com | ritzylab.com
#
# SPDX-License-Identifier: Apache-2.0

set -uo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
export CMAKE="${CMAKE:-cmake}"
REAL_NM="$(command -v nm)" || { echo "nm not found"; exit 1; }

SCRATCH="$(mktemp -d "${TMPDIR:-/tmp}/embediq-release-check.XXXXXX")"
trap 'rm -rf "$SCRATCH"' EXIT
TREE="$SCRATCH/tree"
mkdir -p "$TREE" "$SCRATCH/pristine" "$SCRATCH/logs"

echo "== Copy the working tree into $TREE"
(cd "$ROOT" && git ls-files -co --exclude-standard | grep -vE '^build[^/]*/' > "$SCRATCH/files")
(cd "$ROOT" && tar -cf - -T "$SCRATCH/files") | (cd "$TREE" && tar -xf -)

export BUILD_DIR="$TREE/build-release"
export DEBUG_BUILD_DIR="$TREE/build-debug-check"

# PATH shims: nm failing for one kind of inspection, the real nm otherwise.
make_shim() {   # $1 = shim dir, $2 = argument that makes nm fail
    mkdir -p "$1"
    cat > "$1/nm" <<EOF
#!/usr/bin/env bash
for a in "\$@"; do
    if [ "\$a" = "$2" ]; then echo "nm shim: failing on $2" >&2; exit 42; fi
done
exec "$REAL_NM" "\$@"
EOF
    chmod +x "$1/nm"
}
make_shim "$SCRATCH/shim-undefined" -u
make_shim "$SCRATCH/shim-defined" --defined-only

save()    { mkdir -p "$SCRATCH/pristine/$(dirname "$1")"; cp -p "$TREE/$1" "$SCRATCH/pristine/$1"; }
restore() { cp -p "$SCRATCH/pristine/$1" "$TREE/$1"; touch "$TREE/$1"; }

failures=0

# run_case NAME EXPECT_EXIT(nonzero|zero) PATH_PREFIX LINE... — runs the check
# in the scratch tree; every LINE (fixed string) must appear in its output.
run_case() {
    local name="$1" expect="$2" prefix="$3"; shift 3
    local log="$SCRATCH/logs/$name.log" rc
    if [ -n "$prefix" ]; then
        PATH="$prefix:$PATH" bash "$TREE/tools/ci/check_release_symbols.sh" > "$log" 2>&1
    else
        bash "$TREE/tools/ci/check_release_symbols.sh" > "$log" 2>&1
    fi
    rc=$?
    local ok=1 why=""
    if [ "$expect" = zero ] && [ "$rc" -ne 0 ]; then ok=0; why="exit $rc, expected 0"; fi
    if [ "$expect" = nonzero ] && [ "$rc" -eq 0 ]; then ok=0; why="exit 0 — the check wrongly passes"; fi
    for line in "$@"; do
        if ! grep -qF -- "$line" "$log"; then
            ok=0; why="${why:+$why; }missing: $line"
        fi
    done
    if [ "$ok" -eq 1 ]; then
        echo "PASS  $name (exit $rc)"
    else
        echo "FAIL  $name ($why)"
        echo "      check summary:"
        grep -E '^(PASS|FAIL|RESULT)' "$log" | sed 's/^/        /'
        failures=$((failures + 1))
    fi
}

# --- S1: g_total and its increments outside any EMBEDIQ_TEST_HOOKS block.
OBS=core/src/observatory/obs.c
save "$OBS"
perl -0pi -e 's/#ifdef EMBEDIQ_TEST_HOOKS\n(static uint32_t g_total\b[^\n]*\n)#endif[^\n]*\n/$1/g;
              s/#ifdef EMBEDIQ_TEST_HOOKS\n(\s*g_total\+\+;\n)#endif[^\n]*\n/$1/g' "$TREE/$OBS"
n_unguarded=$(grep -cE '^static uint32_t g_total\b|^\s*g_total\+\+;' "$TREE/$OBS")
echo "== S1: $n_unguarded g_total sites unguarded (definition + 3 increments expected)"
run_case S1 nonzero "" "FAIL  (f)" "g_total"
restore "$OBS"

# --- S2a / S2b: an example hook guarded by the host macro.
for c in "S2a examples/thermostat/fb_temp_sensor.c sensor__last_temp" \
         "S2b examples/thermostat/fb_temp_controller.c ctrl__reading_count"; do
    set -- $c
    save "$2"
    sed -i.bak 's/^#ifdef EMBEDIQ_TEST_HOOKS/#ifdef EMBEDIQ_PLATFORM_HOST/' "$TREE/$2" && rm -f "$TREE/$2.bak"
    run_case "$1" nonzero "" "FAIL  (a)" "$3 examples/thermostat/embediq_thermostat"
    restore "$2"
done

# --- S3a / S3b: symbol inspection fails.
run_case S3a nonzero "$SCRATCH/shim-undefined" "RESULT: ERROR (tool failure)"
run_case S3b nonzero "$SCRATCH/shim-defined"   "RESULT: ERROR (tool failure)"

# --- S4: a hook missing from the inventory.
INV=tools/ci/test_hook_inventory.txt
save "$INV"
grep -vx 'obs__reset' "$SCRATCH/pristine/$INV" > "$TREE/$INV"
run_case S4 nonzero "" "FAIL  (e)" "obs__reset"
restore "$INV"

# --- S5: the unmodified tree passes.
run_case S5 zero "" "PASS  (c) no malloc/calloc/realloc/free" "RESULT: PASS"

if [ "$failures" -ne 0 ]; then
    echo "SELF-TEST: FAIL ($failures of 7 cases failed)"
    exit 1
fi
echo "SELF-TEST: PASS (7 cases)"
