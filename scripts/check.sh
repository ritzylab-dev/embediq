#!/usr/bin/env bash
# scripts/check.sh — local CI runner
#
# Runs every check in .github/workflows/ci.yml in the same order.
# Run this before pushing to catch failures before GitHub Actions does.
#
# Usage:
#   bash scripts/check.sh
#
# Exit code: 0 = all checks pass, 1 = one or more checks failed.
#
# @author  Ritesh Anand
# @company embediq.com | ritzylab.com
#
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

REPO_ROOT="$(git rev-parse --show-toplevel)"
cd "$REPO_ROOT"

PASS=0
FAIL=0
FAILED_STEPS=()

run_step() {
    local name="$1"
    shift
    printf '\n── %s\n' "$name"
    if "$@"; then
        printf '   ✅ PASS\n'
        PASS=$((PASS + 1))
    else
        printf '   ❌ FAIL\n'
        FAIL=$((FAIL + 1))
        FAILED_STEPS+=("$name")
    fi
}

# ── Generate message catalogs ──────────────────────────────────────────
run_step "Generate message catalogs" bash -c '
    python3 tools/messages_iq/generate.py messages/core.iq \
        --out generated/ --output-name embediq_msg_catalog.h &&
    python3 tools/messages_iq/generate.py messages/thermostat.iq \
        --out generated/ --output-name thermostat_msg_catalog.h
'

# ── Drift check ────────────────────────────────────────────────────────
run_step "Generated header drift check" bash -c '
    mkdir -p /tmp/iq_fresh &&
    python3 tools/messages_iq/generate.py messages/core.iq \
        --out /tmp/iq_fresh/ --output-name embediq_msg_catalog.h &&
    python3 tools/messages_iq/generate.py messages/thermostat.iq \
        --out /tmp/iq_fresh/ --output-name thermostat_msg_catalog.h &&
    diff generated/embediq_msg_catalog.h /tmp/iq_fresh/embediq_msg_catalog.h &&
    diff generated/thermostat_msg_catalog.h /tmp/iq_fresh/thermostat_msg_catalog.h
'

# ── Build ──────────────────────────────────────────────────────────────
run_step "Build (host platform)" bash -c '
    cmake -B build -DEMBEDIQ_PLATFORM=host -DCMAKE_BUILD_TYPE=Debug &&
    cmake --build build
'

# ── Tests ──────────────────────────────────────────────────────────────
run_step "ctest" bash -c '
    ctest --test-dir build --output-on-failure
'

# ── Static checks ──────────────────────────────────────────────────────
run_step "validator.py (I-08)" \
    python3 tools/validator.py

run_step "check_lib_obs.py --strict (R-lib-1)" \
    python3 tools/ci/check_lib_obs.py --strict

run_step "check_osal_obs.py --strict (XOBS-1, R-osal-01)" \
    python3 tools/ci/check_osal_obs.py --strict

run_step "check_hal_obs.py --strict (XOBS-2)" \
    python3 tools/ci/check_hal_obs.py --strict

run_step "check_fb_calls.py (I-07, R-02, R-sub-03)" \
    python3 tools/ci/check_fb_calls.py

run_step "boundary_checker.py" \
    python3 tools/boundary_checker.py

run_step "component_globals_check.py (R-lib-4)" \
    python3 tools/ci/component_globals_check.py

run_step "component_state_check.py (R-lib-4)" \
    python3 tools/ci/component_state_check.py

run_step "vendoring_check.py (R-lib-3)" \
    python3 tools/ci/vendoring_check.py

run_step "vendoring_date_check.py (R-lib-3)" \
    python3 tools/ci/vendoring_date_check.py

run_step "ops_version_check.py (R-lib-4, D-LIB-5)" \
    python3 tools/ci/ops_version_check.py

run_step "licence_check.py (SPDX)" \
    python3 tools/ci/licence_check.py

run_step "binary_blob_check.py" \
    python3 tools/ci/binary_blob_check.py

# ── Summary ────────────────────────────────────────────────────────────
printf '\n══════════════════════════════════════════\n'
printf 'check.sh summary: %d passed, %d failed\n' "$PASS" "$FAIL"
if [ ${#FAILED_STEPS[@]} -gt 0 ]; then
    printf 'Failed steps:\n'
    for step in "${FAILED_STEPS[@]}"; do
        printf '  ❌ %s\n' "$step"
    done
    printf '\nFix all failures before pushing.\n'
    exit 1
fi
printf 'All checks passed. Safe to push.\n'
exit 0
