#!/usr/bin/env bash
#
# tests/compat/check_wdg_signature_conflict.sh — the public watchdog header
# rejects a definition with the same name and a different signature
#
# A linker, and a name-only symbol check, accept a definition of
# embediq_wdg_register whatever its parameter and return types. The guarantee
# that a definition agrees with the header comes from the compiler, and only
# in a translation unit that includes the header. This script proves both
# sides of that with the compiler the build uses:
#
#   negative  wdg_signature_conflict.c (includes embediq_wdg.h, defines the
#             function with no return value) must FAIL to compile, and the
#             compiler's output must contain "conflicting types";
#   positive  wdg_public_client.c (includes the header, declares nothing
#             itself) must compile cleanly — so a failure of the negative is
#             caused by the signature, not by a broken header or include path.
#
# Usage:   check_wdg_signature_conflict.sh <compiler> <include dir>...
# Exit:    0 when both controls give their expected result, 1 otherwise
#          (a missing compiler or source, or no include directory, is a
#          failure, never a pass).
#
# @author  Ritesh Anand
# @company embediq.com | ritzylab.com
#
# SPDX-License-Identifier: Apache-2.0

set -u

if [ "$#" -lt 2 ]; then
    echo "FAIL  usage: $0 <compiler> <include dir>..."
    exit 1
fi

CC_BIN="$1"
shift

HERE="$(cd "$(dirname "$0")" && pwd -P)"
CONFLICT="$HERE/wdg_signature_conflict.c"
CLIENT="$HERE/wdg_public_client.c"

if ! command -v "$CC_BIN" > /dev/null 2>&1; then
    echo "FAIL  compiler not found: $CC_BIN"
    exit 1
fi
for src in "$CONFLICT" "$CLIENT"; do
    if [ ! -r "$src" ]; then
        echo "FAIL  source not readable: $src"
        exit 1
    fi
done

INCLUDES=()
for dir in "$@"; do
    INCLUDES+=("-I$dir")
done

OUT="$(mktemp "${TMPDIR:-/tmp}/embediq-wdg-conflict.XXXXXX")" || {
    echo "FAIL  cannot create a scratch file"
    exit 1
}
trap 'rm -f "$OUT"' EXIT

status=0

# --- negative control: the conflicting definition must not compile.
"$CC_BIN" -std=c11 -fsyntax-only "${INCLUDES[@]}" "$CONFLICT" > "$OUT" 2>&1
rc=$?
if [ "$rc" -eq 0 ]; then
    echo "FAIL  negative: the conflicting definition compiled (exit 0) — the expected"
    echo "      'conflicting types' error did not occur"
    status=1
elif ! grep -q "conflicting types" "$OUT"; then
    echo "FAIL  negative: the compiler failed (exit $rc) without 'conflicting types':"
    sed 's/^/      /' "$OUT"
    status=1
else
    echo "PASS  negative: the conflicting definition is rejected (conflicting types)"
fi

# --- positive control: a client of the public header compiles.
"$CC_BIN" -std=c11 -fsyntax-only "${INCLUDES[@]}" "$CLIENT" > "$OUT" 2>&1
rc=$?
if [ "$rc" -ne 0 ]; then
    echo "FAIL  positive: the public-header client did not compile (exit $rc):"
    sed 's/^/      /' "$OUT"
    status=1
else
    echo "PASS  positive: the public-header client compiles"
fi

if [ "$status" -ne 0 ]; then
    echo "RESULT: FAIL"
    exit 1
fi
echo "RESULT: PASS"
