#!/usr/bin/env bash
#
# tools/ci/test_check_core_headers.sh — self-test for check_core_headers.sh
#
# Runs the core-header check five times against a scratch copy of
# core/include and generated/ and asserts each verdict:
#
#   1  unmodified copy                          → exit 0, 136 of 136, RESULT: PASS
#   2  <stdint.h> removed from embediq_msg.h    → exit 1, 128 of 136, failing
#                                                 headers exactly embediq_msg.h
#                                                 and embediq_test.h
#   3  <pthread.h> added to embediq_time.h      → exit 1, compiles 136 of 136,
#                                                 system-include rule names
#                                                 exactly embediq_time.h
#   4  --root at an empty directory             → exit 2, no headers found
#   5  CC=/nonexistent/cc                       → exit 2, compiler not found
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

if [ "$failures" -ne 0 ]; then
    echo "SELF-TEST: FAIL ($failures of 5 cases failed)"
    exit 1
fi
echo "SELF-TEST: PASS (5 cases)"
