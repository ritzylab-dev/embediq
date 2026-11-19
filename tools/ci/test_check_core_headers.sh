#!/usr/bin/env bash
#
# tools/ci/test_check_core_headers.sh — self-test for check_core_headers.sh
#
# Runs the core-header check sixteen times against a scratch copy of
# core/include and generated/ and asserts each verdict:
#
#   1  unmodified copy                          → exit 0, 132 of 132, quote
#                                                 includes PASS, RESULT: PASS
#   2  <stdint.h> removed from embediq_msg.h    → exit 1, 124 of 132, failing
#                                                 headers exactly embediq_msg.h
#                                                 and embediq_test.h
#   3  <pthread.h> added to                     → exit 1, compiles 132 of 132,
#      embediq_endpoint.h                         system-include rule names
#                                                 exactly embediq_endpoint.h
#   4  --root at an empty directory             → exit 2, no headers found
#   5  CC=/nonexistent/cc                       → exit 2, compiler not found
#   6  "pthread.h" appended to                  → exit 1, compiles 132 of 132,
#      embediq_endpoint.h                         quote-include rule names
#                                                 exactly embediq_endpoint.h
#   7  a function with an unused variable       → exit 1, 128 of 132, failing
#      appended to embediq_endpoint.h             header exactly
#                                                 embediq_endpoint.h (warnings
#                                                 are errors)
#   8  find (then sort) prints a partial list   → exit 2, header discovery
#      and exits 42                               failed, no compile, no PASS
#   9  sed -n exits 42 for the system-include   → exit 2, include scan failed
#      scan only, then for the quote-include      (either include scan alone
#      scan only                                  fails → exit 2)
#  10  embediq_endpoint.h unreadable            → exit 2, include scan failed
#                                                 naming it (SKIP as root)
#  11  a header with only a comment added       → exit 0, 136 of 136, three
#                                                 PASS lines (an empty scan is
#                                                 a valid result)
#  12  "../../external.h" appended to           → exit 1, 132 of 132, quote
#      embediq_endpoint.h                         include resolves outside,
#                                                 naming embediq_endpoint.h
#  13  a symbolic link to ../../external.h      → exit 1, 132 of 132, quote
#      included by embediq_endpoint.h             include resolves outside,
#                                                 naming embediq_endpoint.h
#  14  a header in hal/ including               → exit 0, 136 of 136, three
#      "../embediq_config.h"                      PASS lines
#  15  embediq_shadow.h in core/include and a   → exit 1, 140 of 140, the
#      link of that name in hal/ to an outside    including header's own
#      file, included from a header in hal/       directory is searched first:
#                                                 resolves outside, naming
#                                                 hal/embediq_shadow_probe.h
#  16  --root given as a symbolic link to the   → exit 0, 132 of 132, three
#      scratch copy                               PASS lines (both sides of the
#                                                 comparison are canonical)
#
# The scratch root also holds an empty external.h next to core/ and generated/,
# a file outside both. A command shim is an executable in its own scratch
# directory, put first on PATH for one checker run only; the real sed is
# located first.
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
trap 'rm -rf "$SCRATCH" "$SCRATCH-link"' EXIT
COPY="$SCRATCH/copy"
mkdir -p "$COPY/core" || { echo "SELF-TEST: ERROR (cannot copy the headers)"; exit 2; }
cp -R "$ROOT/core/include" "$COPY/core/include" || { echo "SELF-TEST: ERROR (cannot copy the headers)"; exit 2; }
cp -R "$ROOT/generated" "$COPY/generated" || { echo "SELF-TEST: ERROR (cannot copy the headers)"; exit 2; }
: > "$COPY/external.h" || { echo "SELF-TEST: ERROR (cannot copy the headers)"; exit 2; }
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
grep -qF "PASS  (compile) 132 of 132 standalone compiles" "$LOG" || ok=0
grep -qF "PASS  (quote includes)" "$LOG" || ok=0
grep -qx "RESULT: PASS" "$LOG" || ok=0
result "1 unmodified copy" "$ok"

# --- case 2: <stdint.h> removed from embediq_msg.h.
grep -v '^#include <stdint.h>' "$ROOT/core/include/embediq_msg.h" > "$COPY/core/include/embediq_msg.h"
bash "$CHECK" --root "$COPY" > "$LOG" 2>&1; rc=$?
ok=1
[ "$rc" -eq 1 ] || ok=0
grep -qF "FAIL  (compile) 124 of 132" "$LOG" || ok=0
[ "$(named compile)" = "embediq_msg.h embediq_test.h " ] || ok=0
result "2 stdint.h removed from embediq_msg.h" "$ok"
restore embediq_msg.h

# --- case 3: <pthread.h> added to embediq_endpoint.h.
{ echo '#include <pthread.h>'; cat "$ROOT/core/include/embediq_endpoint.h"; } > "$COPY/core/include/embediq_endpoint.h"
bash "$CHECK" --root "$COPY" > "$LOG" 2>&1; rc=$?
ok=1
[ "$rc" -eq 1 ] || ok=0
grep -qF "132 of 132 standalone compiles" "$LOG" || ok=0
grep -qF "FAIL  (system includes)" "$LOG" || ok=0
[ "$(named include)" = "embediq_endpoint.h " ] || ok=0
grep -E "^  include  " "$LOG" | grep -qF "pthread.h" || ok=0
result "3 pthread.h added to embediq_endpoint.h" "$ok"
restore embediq_endpoint.h

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

# --- case 6: a quote include of a system header appended to embediq_endpoint.h.
{ cat "$ROOT/core/include/embediq_endpoint.h"; echo '#include "pthread.h"'; } > "$COPY/core/include/embediq_endpoint.h"
bash "$CHECK" --root "$COPY" > "$LOG" 2>&1; rc=$?
ok=1
[ "$rc" -eq 1 ] || ok=0
grep -qF "PASS  (compile) 132 of 132 standalone compiles" "$LOG" || ok=0
grep -qF "PASS  (system includes)" "$LOG" || ok=0
grep -qF "FAIL  (quote includes) 1 quote include(s)" "$LOG" || ok=0
[ "$(named include)" = "embediq_endpoint.h " ] || ok=0
grep -E "^  include  " "$LOG" | grep -qF '"pthread.h"' || ok=0
result "6 \"pthread.h\" appended to embediq_endpoint.h" "$ok"
restore embediq_endpoint.h

# --- case 7: a warning in embediq_endpoint.h fails the compile.
{ cat "$ROOT/core/include/embediq_endpoint.h"
  echo 'static inline int standalone_probe(int a) { int unused; return a; }'; } > "$COPY/core/include/embediq_endpoint.h"
bash "$CHECK" --root "$COPY" > "$LOG" 2>&1; rc=$?
ok=1
[ "$rc" -eq 1 ] || ok=0
grep -qF "FAIL  (compile) 128 of 132" "$LOG" || ok=0
[ "$(named compile)" = "embediq_endpoint.h " ] || ok=0
result "7 unused variable in embediq_endpoint.h" "$ok"
restore embediq_endpoint.h

# --- case 8: header discovery prints a partial list and fails.
ok=1
for dir in "$(shim find 'echo ./embediq_endpoint.h; echo "find: failing" >&2; exit 42')" \
           "$(shim sort 'cat > /dev/null; exit 42')"; do
    PATH="$dir:$PATH" bash "$CHECK" --root "$COPY" > "$LOG" 2>&1; rc=$?
    [ "$rc" -eq 2 ] || ok=0
    grep -qx "RESULT: ERROR (header discovery failed: exit 42)" "$LOG" || ok=0
    grep -q "RESULT: PASS" "$LOG" && ok=0
    grep -q "standalone compiles" "$LOG" && ok=0
done
result "8 find or sort fails during header discovery" "$ok"

# --- case 9: one include scan fails — the system-include scan (its expression
# holds '<'), then the quote-include scan (its expression holds '"'). The second
# shim overwrites the first, so they never share PATH.
ok=1
for pattern in "'<'" "'\"'"; do
    dir="$(shim sed "if [ \"\$1\" = \"-n\" ]; then case \"\$2\" in *$pattern*) exit 42;; esac; fi; exec \"$REAL_SED\" \"\$@\"")"
    PATH="$dir:$PATH" bash "$CHECK" --root "$COPY" > "$LOG" 2>&1; rc=$?
    [ "$rc" -eq 2 ] || ok=0
    grep -qF "RESULT: ERROR (include scan failed:" "$LOG" || ok=0
    grep -qF "exit 42)" "$LOG" || ok=0
    grep -q "RESULT: PASS" "$LOG" && ok=0
done
result "9 sed fails during the system-include scan, then the quote-include scan" "$ok"

# --- case 10: a header that cannot be read.
if [ "$(id -u)" -eq 0 ]; then
    echo "SKIP  10 unreadable embediq_endpoint.h  (running as root)"
else
    chmod 000 "$COPY/core/include/embediq_endpoint.h"
    bash "$CHECK" --root "$COPY" > "$LOG" 2>&1; rc=$?
    chmod 644 "$COPY/core/include/embediq_endpoint.h"
    ok=1
    [ "$rc" -eq 2 ] || ok=0
    grep -qF "RESULT: ERROR (include scan failed: embediq_endpoint.h:" "$LOG" || ok=0
    grep -q "RESULT: PASS" "$LOG" && ok=0
    result "10 unreadable embediq_endpoint.h" "$ok"
    restore embediq_endpoint.h
fi

# --- case 11: a header with no includes — an empty scan is a valid result.
echo '/* no includes */' > "$COPY/core/include/embediq_probe_empty.h"
bash "$CHECK" --root "$COPY" > "$LOG" 2>&1; rc=$?
ok=1
[ "$rc" -eq 0 ] || ok=0
grep -qF "PASS  (compile) 136 of 136 standalone compiles" "$LOG" || ok=0
grep -qF "PASS  (system includes)" "$LOG" || ok=0
grep -qF "PASS  (quote includes)" "$LOG" || ok=0
grep -qx "RESULT: PASS" "$LOG" || ok=0
result "11 header with no includes" "$ok"
rm -f "$COPY/core/include/embediq_probe_empty.h"

# The listed quote-include line of header $1 says it resolves outside.
outside() { grep -E "^  include  $1  " "$LOG" | grep -qF "resolves outside"; }

# --- case 12: a relative quote include that leaves core/include.
{ cat "$ROOT/core/include/embediq_endpoint.h"; echo '#include "../../external.h"'; } > "$COPY/core/include/embediq_endpoint.h"
bash "$CHECK" --root "$COPY" > "$LOG" 2>&1; rc=$?
ok=1
[ "$rc" -eq 1 ] || ok=0
grep -qF "PASS  (compile) 132 of 132 standalone compiles" "$LOG" || ok=0
grep -qF "FAIL  (quote includes) 1 quote include(s)" "$LOG" || ok=0
[ "$(named include)" = "embediq_endpoint.h " ] || ok=0
outside embediq_endpoint.h || ok=0
result "12 \"../../external.h\" appended to embediq_endpoint.h" "$ok"
restore embediq_endpoint.h

# --- case 13: a symbolic link in core/include whose target is outside.
ln -s ../../external.h "$COPY/core/include/embediq_link_probe.h"
{ cat "$ROOT/core/include/embediq_endpoint.h"; echo '#include "embediq_link_probe.h"'; } > "$COPY/core/include/embediq_endpoint.h"
bash "$CHECK" --root "$COPY" > "$LOG" 2>&1; rc=$?
ok=1
[ "$rc" -eq 1 ] || ok=0
grep -qF "PASS  (compile) 132 of 132 standalone compiles" "$LOG" || ok=0
grep -qF "FAIL  (quote includes) 1 quote include(s)" "$LOG" || ok=0
[ "$(named include)" = "embediq_endpoint.h " ] || ok=0
outside embediq_endpoint.h || ok=0
result "13 symbolic link to an outside file included by embediq_endpoint.h" "$ok"
rm -f "$COPY/core/include/embediq_link_probe.h"
restore embediq_endpoint.h

# --- case 14: a relative quote include that stays inside core/include.
echo '#include "../embediq_config.h"' > "$COPY/core/include/hal/embediq_rel_probe.h"
bash "$CHECK" --root "$COPY" > "$LOG" 2>&1; rc=$?
ok=1
[ "$rc" -eq 0 ] || ok=0
grep -qF "PASS  (compile) 136 of 136 standalone compiles" "$LOG" || ok=0
grep -qF "PASS  (system includes)" "$LOG" || ok=0
grep -qF "PASS  (quote includes)" "$LOG" || ok=0
grep -qx "RESULT: PASS" "$LOG" || ok=0
result "14 \"../embediq_config.h\" from a header in hal/" "$ok"
rm -f "$COPY/core/include/hal/embediq_rel_probe.h"

# --- case 15: the including header's own directory is searched first.
: > "$COPY/core/include/embediq_shadow.h"
ln -s ../../../external.h "$COPY/core/include/hal/embediq_shadow.h"
echo '#include "embediq_shadow.h"' > "$COPY/core/include/hal/embediq_shadow_probe.h"
bash "$CHECK" --root "$COPY" > "$LOG" 2>&1; rc=$?
ok=1
[ "$rc" -eq 1 ] || ok=0
grep -qF "PASS  (compile) 140 of 140 standalone compiles" "$LOG" || ok=0
grep -qF "FAIL  (quote includes) 1 quote include(s)" "$LOG" || ok=0
outside hal/embediq_shadow_probe.h || ok=0
result "15 the header's own directory is searched first" "$ok"
rm -f "$COPY/core/include/embediq_shadow.h" "$COPY/core/include/hal/embediq_shadow.h" \
      "$COPY/core/include/hal/embediq_shadow_probe.h"

# --- case 16: --root given as a symbolic link to the scratch copy.
ln -s "$COPY" "$SCRATCH-link"
bash "$CHECK" --root "$SCRATCH-link" > "$LOG" 2>&1; rc=$?
ok=1
[ "$rc" -eq 0 ] || ok=0
grep -qF "PASS  (compile) 132 of 132 standalone compiles" "$LOG" || ok=0
grep -qF "PASS  (system includes)" "$LOG" || ok=0
grep -qF "PASS  (quote includes)" "$LOG" || ok=0
grep -qx "RESULT: PASS" "$LOG" || ok=0
result "16 --root given as a symbolic link" "$ok"
rm -f "$SCRATCH-link"

if [ "$failures" -ne 0 ]; then
    echo "SELF-TEST: FAIL ($failures of 16 cases failed)"
    exit 1
fi
echo "SELF-TEST: PASS (16 cases)"
