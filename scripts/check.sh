#!/usr/bin/env bash
# scripts/check.sh — local CI runner
#
# Runs the generated-file check, the host build, the tests and the static checks
# (tools/ci/static_checks.sh — the same list CI runs). CI additionally runs the
# Python SDK unit tests, the API compatibility compile, the MISRA scan, the
# ThreadSanitizer job and the release-symbols job.
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

# ── Generated files ────────────────────────────────────────────────────
run_step "Generated files match their sources" python3 tools/ci/check_generated_artifacts.py

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
run_step "Static checks (tools/ci/static_checks.sh)" \
    bash tools/ci/static_checks.sh

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
