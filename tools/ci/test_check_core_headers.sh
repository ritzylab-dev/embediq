#!/usr/bin/env bash
#
# tools/ci/test_check_core_headers.sh — self-test for check_core_headers.sh
#
# Runs the core-header check eleven times against a scratch copy of
# core/include and generated/ and asserts each verdict:
#
#   1  unmodified copy                          → exit 0, 136 of 136, quote
#                                                 includes PASS, RESULT: PASS
#   2  <stdint.h> removed from embediq_msg.h    → exit 1, 128 of 136, failing
#                                                 headers exactly embediq_msg.h
#                                                 and embediq_test.h
#   3  <pthread.h> added to embediq_time.h      → exit 1, compiles 136 of 136,
#                                                 system-include rule names
#                                                 exactly embediq_time.h
#   4  --root at an empty directory             → exit 2, no headers found
#   5  CC=/nonexistent/cc                       → exit 2, compiler not found
#   6  "pthread.h" appended to embediq_time.h   → exit 1, compiles 136 of 136,
#                                                 quote-include rule names
#                                                 exactly embediq_time.h
#   7  a function with an unused variable       → exit 1, 132 of 136, failing
#      appended to embediq_time.h                 header exactly embediq_time.h
#                                                 (warnings are errors)
#   8  find (then sort) prints a partial list   → exit 2, header discovery
#      and exits 42                               failed, no compile, no PASS
#   9  sed -n exits 42                          → exit 2, include scan failed
#  10  embediq_time.h unreadable                → exit 2, include scan failed
#                                                 naming it (SKIP as root)
#  11  a header with only a comment added       → exit 0, 140 of 140, three
#                                                 PASS lines (an empty scan is
#                                                 a valid result)
#
# A command shim is an executable in its own scratch directory, put first on
# PATH for one checker run only; the real find and sed are located first.
#
# Usage:   tools/ci/test_check_core_headers.sh   (from anywhere)
# Exit:    0 when every case gives its expected verdict, 1 otherwise,
#          2 when the scratch copy cannot be made.
#
# @author  Ritesh Anand
# @company embediq.com | ritzylab.com
#
# SPDX-License-Identifier: Apache-2.0

set -uo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
CHECK="$ROOT/tools/ci/check_core_headers.sh"
if [ ! -f "$CHECK" ]; then
    echo "FAIL  check_core_headers.sh not found"
    exit 1
fi

REAL_SED="$(command -v sed)" || { echo "SELF-TEST: ERROR (sed not found)"; exit 2; }

SCRATCH="$(mktemp -d "${TMPDIR:-/tmp}/embediq-core-headers.XXXXXX")"
trap 'rm -rf "$SCRATCH"' EXIT
COPY="$SCRATCH/copy"
mkdir -p "$COPY/core" || { echo "SELF-TEST: ERROR (cannot copy the headers)"; exit 2; }
cp -R "$ROOT/core/include" "$COPY/core/include" || { echo "SELF-TEST: ERROR (cannot copy the headers)"; exit 2; }
cp -R "$ROOT/generated" "$COPY/generated" || { echo "SELF-TEST: ERROR (cannot copy the headers)"; exit 2; }
mkdir -p "$SCRATCH/empty"

failures=0
LOG="$SCRATCH/log"

result() {   # $1 = case name, $2 = 1 if every expectation held
    if [ "$2" -eq 1 ]; then
        echo "PASS  $1"
    else
        echo "FAIL  $1"
        sed 's/^/        /' "$LOG"
        failures=$((failures + 1))
    fi
}

# Header file names of the listed failures of one rule ("compile" or "include").
named() { grep -E "^  $1  " "$LOG" | awk '{print $2}' | xargs -n1 basename | sort -u | tr '\n' ' '; }

restore() { cp "$ROOT/core/include/$1" "$COPY/core/include/$1"; }

# --- case 1: unmodified copy.
bash "$CHECK" --root "$COPY" > "$LOG" 2>&1; rc=$?
ok=1
[ "$rc" -eq 0 ] || ok=0
grep -qF "PASS  (compile) 136 of 136 standalone compiles" "$LOG" || ok=0
grep -qF "PASS  (quote includes)" "$LOG" || ok=0
grep -qx "RESULT: PASS" "$LOG" || ok=0
result "1 unmodified copy" "$ok"

# --- case 2: <stdint.h> removed from embediq_msg.h.
grep -v '^#include <stdint.h>' "$ROOT/core/include/embediq_msg.h" > "$COPY/core/include/embediq_msg.h"
bash "$CHECK" --root "$COPY" > "$LOG" 2>&1; rc=$?
ok=1
[ "$rc" -eq 1 ] || ok=0
grep -qF "FAIL  (compile) 128 of 136" "$LOG" || ok=0
[ "$(named compile)" = "embediq_msg.h embediq_test.h " ] || ok=0
result "2 stdint.h removed from embediq_msg.h" "$ok"
restore embediq_msg.h

# --- case 3: <pthread.h> added to embediq_time.h.
{ echo '#include <pthread.h>'; cat "$ROOT/core/include/embediq_time.h"; } > "$COPY/core/include/embediq_time.h"
bash "$CHECK" --root "$COPY" > "$LOG" 2>&1; rc=$?
ok=1
[ "$rc" -eq 1 ] || ok=0
grep -qF "136 of 136 standalone compiles" "$LOG" || ok=0
grep -qF "FAIL  (system includes)" "$LOG" || ok=0
[ "$(named include)" = "embediq_time.h " ] || ok=0
grep -E "^  include  " "$LOG" | grep -qF "pthread.h" || ok=0
result "3 pthread.h added to embediq_time.h" "$ok"
restore embediq_time.h

# --- case 4: no headers under --root.
bash "$CHECK" --root "$SCRATCH/empty" > "$LOG" 2>&1; rc=$?
ok=1
[ "$rc" -eq 2 ] || ok=0
grep -qx "RESULT: ERROR (no headers found)" "$LOG" || ok=0
result "4 empty root" "$ok"

# --- case 5: compiler missing.
CC=/nonexistent/cc bash "$CHECK" --root "$COPY" > "$LOG" 2>&1; rc=$?
ok=1
[ "$rc" -eq 2 ] || ok=0
grep -qx "RESULT: ERROR (compiler not found: /nonexistent/cc)" "$LOG" || ok=0
result "5 compiler not found" "$ok"

# shim NAME BODY — an executable NAME in its own scratch directory; prints the directory.
shim() {
    local dir="$SCRATCH/shim-$1"
    mkdir -p "$dir"
    printf '#!/bin/sh\n%s\n' "$2" > "$dir/$1"
    chmod +x "$dir/$1"
    echo "$dir"
}

# --- case 6: a quote include of a system header appended to embediq_time.h.
{ cat "$ROOT/core/include/embediq_time.h"; echo '#include "pthread.h"'; } > "$COPY/core/include/embediq_time.h"
bash "$CHECK" --root "$COPY" > "$LOG" 2>&1; rc=$?
ok=1
[ "$rc" -eq 1 ] || ok=0
grep -qF "PASS  (compile) 136 of 136 standalone compiles" "$LOG" || ok=0
grep -qF "PASS  (system includes)" "$LOG" || ok=0
grep -qF "FAIL  (quote includes) 1 quote include(s)" "$LOG" || ok=0
[ "$(named include)" = "embediq_time.h " ] || ok=0
grep -E "^  include  " "$LOG" | grep -qF '"pthread.h"' || ok=0
result "6 \"pthread.h\" appended to embediq_time.h" "$ok"
restore embediq_time.h

# --- case 7: a warning in embediq_time.h fails the compile.
{ cat "$ROOT/core/include/embediq_time.h"
  echo 'static inline int standalone_probe(int a) { int unused; return a; }'; } > "$COPY/core/include/embediq_time.h"
bash "$CHECK" --root "$COPY" > "$LOG" 2>&1; rc=$?
ok=1
[ "$rc" -eq 1 ] || ok=0
grep -qF "FAIL  (compile) 132 of 136" "$LOG" || ok=0
[ "$(named compile)" = "embediq_time.h " ] || ok=0
result "7 unused variable in embediq_time.h" "$ok"
restore embediq_time.h

# --- case 8: header discovery prints a partial list and fails.
ok=1
for dir in "$(shim find 'echo ./embediq_time.h; echo "find: failing" >&2; exit 42')" \
           "$(shim sort 'cat > /dev/null; exit 42')"; do
    PATH="$dir:$PATH" bash "$CHECK" --root "$COPY" > "$LOG" 2>&1; rc=$?
    [ "$rc" -eq 2 ] || ok=0
    grep -qx "RESULT: ERROR (header discovery failed: exit 42)" "$LOG" || ok=0
    grep -q "RESULT: PASS" "$LOG" && ok=0
    grep -q "standalone compiles" "$LOG" && ok=0
done
result "8 find or sort fails during header discovery" "$ok"

# --- case 9: an include scan fails.
dir="$(shim sed "if [ \"\$1\" = \"-n\" ]; then exit 42; fi; exec \"$REAL_SED\" \"\$@\"")"
PATH="$dir:$PATH" bash "$CHECK" --root "$COPY" > "$LOG" 2>&1; rc=$?
ok=1
[ "$rc" -eq 2 ] || ok=0
grep -qF "RESULT: ERROR (include scan failed:" "$LOG" || ok=0
grep -qF "exit 42)" "$LOG" || ok=0
grep -q "RESULT: PASS" "$LOG" && ok=0
result "9 sed fails during an include scan" "$ok"

# --- case 10: a header that cannot be read.
if [ "$(id -u)" -eq 0 ]; then
    echo "SKIP  10 unreadable embediq_time.h  (running as root)"
else
    chmod 000 "$COPY/core/include/embediq_time.h"
    bash "$CHECK" --root "$COPY" > "$LOG" 2>&1; rc=$?
    chmod 644 "$COPY/core/include/embediq_time.h"
    ok=1
    [ "$rc" -eq 2 ] || ok=0
    grep -qF "RESULT: ERROR (include scan failed: embediq_time.h:" "$LOG" || ok=0
    grep -q "RESULT: PASS" "$LOG" && ok=0
    result "10 unreadable embediq_time.h" "$ok"
    restore embediq_time.h
fi

# --- case 11: a header with no includes — an empty scan is a valid result.
echo '/* no includes */' > "$COPY/core/include/embediq_probe_empty.h"
bash "$CHECK" --root "$COPY" > "$LOG" 2>&1; rc=$?
ok=1
[ "$rc" -eq 0 ] || ok=0
grep -qF "PASS  (compile) 140 of 140 standalone compiles" "$LOG" || ok=0
grep -qF "PASS  (system includes)" "$LOG" || ok=0
grep -qF "PASS  (quote includes)" "$LOG" || ok=0
grep -qx "RESULT: PASS" "$LOG" || ok=0
result "11 header with no includes" "$ok"
rm -f "$COPY/core/include/embediq_probe_empty.h"

if [ "$failures" -ne 0 ]; then
    echo "SELF-TEST: FAIL ($failures of 11 cases failed)"
    exit 1
fi
echo "SELF-TEST: PASS (11 cases)"
