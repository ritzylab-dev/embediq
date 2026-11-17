#!/usr/bin/env bash
#
# tools/ci/static_checks.sh — the one list of static checks, run by CI and by scripts/check.sh
#
# Runs every check below from the repository root, whatever the current
# directory, and always runs all of them. A BLOCKING check that exits non-zero
# fails the run; an ADVISORY check is reported and never fails it. Ends with a
# summary table (check, class, result).
#
# Usage:   bash tools/ci/static_checks.sh
# Exit:    0 = every blocking check passed, 1 = a blocking check failed,
#          2 = the repository root cannot be entered.
#
# @author  Ritesh Anand
# @company embediq.com | ritzylab.com
#
# SPDX-License-Identifier: Apache-2.0

set -u

cd "$(dirname "$0")/../.." || { echo "static_checks: cannot enter repository root"; exit 2; }

NAMES=()
CLASSES=()
RESULTS=()
blocking_failed=0

# check CLASS COMMAND... — runs one check un-piped and records its result.
check() {
    local class="$1"; shift
    local name="$*" rc res
    echo "== $name"
    "$@"
    rc=$?
    if [ "$class" = BLOCKING ]; then
        if [ "$rc" -eq 0 ]; then res="PASS"; else res="FAIL (exit $rc)"; blocking_failed=1; fi
    else
        if [ "$rc" -eq 0 ]; then res="ADVISORY ok"; else res="ADVISORY (exit $rc)"; fi
    fi
    echo "$res"
    echo
    NAMES+=("$name")
    CLASSES+=("$class")
    RESULTS+=("$res")
}

check BLOCKING python3 tools/validator.py
check BLOCKING python3 tools/ci/check_lib_obs.py --strict
check BLOCKING python3 tools/ci/check_osal_obs.py --strict
check BLOCKING python3 tools/ci/check_hal_obs.py --strict
check BLOCKING python3 tools/ci/check_fb_calls.py
check BLOCKING python3 tools/ci/test_check_fb_calls.py
check BLOCKING python3 tools/boundary_checker.py
check BLOCKING python3 tools/test_boundary_checker.py
check BLOCKING python3 tools/ci/licence_check.py --strict
check BLOCKING python3 tools/ci/test_licence_check.py
check BLOCKING python3 tools/ci/check_message_ids.py
check BLOCKING python3 tools/ci/test_check_message_ids.py
check BLOCKING bash tools/ci/check_core_headers.sh
check BLOCKING bash tools/ci/test_check_core_headers.sh
check BLOCKING python3 tools/ci/test_tsan_summarise.py

check ADVISORY python3 tools/ci/component_globals_check.py
check ADVISORY python3 tools/ci/component_state_check.py
check ADVISORY python3 tools/ci/vendoring_check.py
check ADVISORY python3 tools/ci/vendoring_date_check.py
check ADVISORY python3 tools/ci/ops_version_check.py
check ADVISORY python3 tools/ci/binary_blob_check.py

echo "== Summary"
printf '%-48s  %-8s  %s\n' "Check" "Class" "Result"
for i in "${!NAMES[@]}"; do
    printf '%-48s  %-8s  %s\n' "${NAMES[$i]}" "${CLASSES[$i]}" "${RESULTS[$i]}"
done

if [ "$blocking_failed" -ne 0 ]; then
    echo "static_checks: FAIL — at least one blocking check failed"
    exit 1
fi
echo "static_checks: PASS — every blocking check passed"
exit 0
