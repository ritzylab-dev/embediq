#!/usr/bin/env bash
#
# tools/ci/check_core_headers.sh — every core header compiles standalone, includes only C freestanding headers and quote-includes only core and generated headers
#
# For each header under <root>/core/include (recursive, sorted) a one-line
# wrapper `#include "<header>"` is compiled with
#   -std=c11 -Wall -Wextra -Werror -fsyntax-only
#   -I<root>/core/include -I<root>/core/include/hal -I<root>/generated
# in four configurations: no defines; -DEMBEDIQ_PLATFORM_HOST;
# -DEMBEDIQ_PLATFORM_HOST -DEMBEDIQ_TEST_HOOKS; -DEMBEDIQ_TEST_HOOKS.
# Second rule: every `#include <...>` in a core header is <stdint.h>,
# <stddef.h> or <stdbool.h>.
# Third rule: every `#include "..."` in a core header names an existing file in
# the header's own directory, <root>/core/include, <root>/core/include/hal or
# <root>/generated.
# Rules two and three read a directive that starts its line with `#include`;
# one preceded or interrupted by a comment is not read by them and is checked
# by the standalone compile only.
#
# Failures are listed one per line:
#   "  compile  <header>  [<configuration>]  <first error line>"
#   "  include  <header>  <system header>"
#   "  include  <header>  \"<name>\""
#
# Usage:   tools/ci/check_core_headers.sh [--root <dir>]
#          (default root: the repository, located from this script's path)
# Env:     CC (default: cc)
# Exit:    0 = PASS, 1 = FAIL, 2 = tool error (compiler not found, no headers,
#          header discovery failed, an include scan failed — a header that
#          cannot be read included); a tool error never prints a result
#
# @author  Ritesh Anand
# @company embediq.com | ritzylab.com
#
# SPDX-License-Identifier: Apache-2.0

set -uo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
if [ "${1:-}" = "--root" ]; then
    [ $# -eq 2 ] || { echo "usage: check_core_headers.sh [--root <dir>]"; echo "RESULT: ERROR (usage)"; exit 2; }
    ROOT="$2"
elif [ $# -ne 0 ]; then
    echo "usage: check_core_headers.sh [--root <dir>]"; echo "RESULT: ERROR (usage)"; exit 2
fi
CC="${CC:-cc}"

if ! command -v "$CC" > /dev/null 2>&1; then
    echo "RESULT: ERROR (compiler not found: $CC)"
    exit 2
fi

INC="$ROOT/core/include"
HEADERS=()
if [ -d "$INC" ]; then
    # A checked command substitution: pipefail makes a failure of cd, find, sed
    # or sort its status, and a partial list from a failed discovery is never used.
    header_list="$(cd "$INC" && find . -name '*.h' -type f | sed 's|^\./||' | LC_ALL=C sort)"
    rc=$?
    if [ "$rc" -ne 0 ]; then echo "RESULT: ERROR (header discovery failed: exit $rc)"; exit 2; fi
    while IFS= read -r h; do [ -n "$h" ] && HEADERS+=("$h"); done <<< "$header_list"
fi
if [ "${#HEADERS[@]}" -eq 0 ]; then
    echo "RESULT: ERROR (no headers found)"
    exit 2
fi

CONFIG_NAMES=("no defines" "PLATFORM_HOST" "PLATFORM_HOST+TEST_HOOKS" "TEST_HOOKS")
CONFIG_FLAGS=("" "-DEMBEDIQ_PLATFORM_HOST" "-DEMBEDIQ_PLATFORM_HOST -DEMBEDIQ_TEST_HOOKS" "-DEMBEDIQ_TEST_HOOKS")

total=0
passed=0
compile_fail=()
for h in "${HEADERS[@]}"; do
    for i in 0 1 2 3; do
        total=$((total + 1))
        # shellcheck disable=SC2086  # the configuration flags are word-split on purpose
        out="$(printf '#include "%s"\n' "$h" | "$CC" -std=c11 -Wall -Wextra -Werror -fsyntax-only \
                 -I"$INC" -I"$INC/hal" -I"$ROOT/generated" ${CONFIG_FLAGS[$i]} -x c - 2>&1)"
        if [ $? -eq 0 ]; then
            passed=$((passed + 1))
        else
            first="$(printf '%s\n' "$out" | grep -m1 -E 'error' || printf '%s\n' "$out" | head -1)"
            compile_fail+=("  compile  $h  [${CONFIG_NAMES[$i]}]  $first")
        fi
    done
done

# Every include scan: status is sed's status, so a header that cannot be read
# is a tool error, while empty output with status 0 is a header with no such include.
scan_includes() { sed -n "$1" "$2"; }      # $1 = sed expression, $2 = header path

include_fail=()
for h in "${HEADERS[@]}"; do
    scan="$(scan_includes 's/^[[:space:]]*#[[:space:]]*include[[:space:]]*<\([^>]*\)>.*/\1/p' "$INC/$h")"
    rc=$?
    if [ "$rc" -ne 0 ]; then echo "RESULT: ERROR (include scan failed: $h: exit $rc)"; exit 2; fi
    while IFS= read -r sys; do
        [ -n "$sys" ] || continue
        case "$sys" in
            stdint.h|stddef.h|stdbool.h) ;;
            *) include_fail+=("  include  $h  <$sys>") ;;
        esac
    done <<< "$scan"
done

quote_fail=()
for h in "${HEADERS[@]}"; do
    scan="$(scan_includes 's/^[[:space:]]*#[[:space:]]*include[[:space:]]*"\([^"]*\)".*/\1/p' "$INC/$h")"
    rc=$?
    if [ "$rc" -ne 0 ]; then echo "RESULT: ERROR (include scan failed: $h: exit $rc)"; exit 2; fi
    dir="$(dirname "$INC/$h")"
    while IFS= read -r name; do
        [ -n "$name" ] || continue
        found=0
        for base in "$dir" "$INC" "$INC/hal" "$ROOT/generated"; do
            if [ -f "$base/$name" ]; then found=1; break; fi
        done
        [ "$found" -eq 1 ] || quote_fail+=("  include  $h  \"$name\"")
    done <<< "$scan"
done

result=PASS
if [ "$passed" -eq "$total" ]; then
    echo "PASS  (compile) $passed of $total standalone compiles"
else
    echo "FAIL  (compile) $passed of $total standalone compiles"
    printf '%s\n' "${compile_fail[@]}"
    result=FAIL
fi
if [ "${#include_fail[@]}" -eq 0 ]; then
    echo "PASS  (system includes) every system include is <stdint.h>, <stddef.h> or <stdbool.h>"
else
    echo "FAIL  (system includes) ${#include_fail[@]} system include(s) outside <stdint.h>, <stddef.h>, <stdbool.h>:"
    printf '%s\n' "${include_fail[@]}"
    result=FAIL
fi
if [ "${#quote_fail[@]}" -eq 0 ]; then
    echo "PASS  (quote includes) every quote include resolves to a core or generated header"
else
    echo "FAIL  (quote includes) ${#quote_fail[@]} quote include(s) resolve to no core or generated header:"
    printf '%s\n' "${quote_fail[@]}"
    result=FAIL
fi
echo "RESULT: $result"
[ "$result" = PASS ]
