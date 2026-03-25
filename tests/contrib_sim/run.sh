#!/usr/bin/env bash
# contrib_sim — fresh-clone simulation gate
#
# Purpose: simulate what a first-time contributor experiences after
#   git clone + cmake + ctest, using the real remote (not a local path).
#
# Usage:
#   bash tests/contrib_sim/run.sh
#
# Exits 0 on full pass. Exits 1 on any failure.
# Updates tests/contrib_sim/last_run.md with the result.
#
# When to run:
#   Before every dev→main promotion PR (Gate 14, Step B).
#   Do NOT add this to CMakeLists.txt or CI — it is a manual gate.

set -euo pipefail

REPO_URL=$(git remote get-url origin)
BRANCH="dev"
WORK_DIR=$(mktemp -d)
REPO_ROOT=$(git rev-parse --show-toplevel)
OUTFILE="$REPO_ROOT/tests/contrib_sim/last_run.md"
TIMESTAMP=$(date -u "+%Y-%m-%d %H:%M UTC")
HEAD_SHA=$(git ls-remote "$REPO_URL" "refs/heads/$BRANCH" | awk '{print $1}')

cleanup() {
    rm -rf "$WORK_DIR"
}
trap cleanup EXIT

{
    echo "# contrib_sim run — $TIMESTAMP"
    echo ""
    echo "**Branch:** $BRANCH"
    echo "**HEAD:** $HEAD_SHA"
    echo "**Clone URL:** $REPO_URL"
    echo ""
    echo "---"
    echo ""
} > "$OUTFILE"

echo "[contrib_sim] Cloning $REPO_URL (branch: $BRANCH)..."
if ! git clone --branch "$BRANCH" --depth 1 "$REPO_URL" "$WORK_DIR/embediq" >> "$OUTFILE" 2>&1; then
    echo "## Result: FAIL — clone failed" >> "$OUTFILE"
    echo "[contrib_sim] FAIL: clone failed. See $OUTFILE"
    exit 1
fi

cd "$WORK_DIR/embediq"

echo "[contrib_sim] Configuring..."
if ! cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug >> "$OUTFILE" 2>&1; then
    echo "## Result: FAIL — cmake configure failed" >> "$OUTFILE"
    echo "[contrib_sim] FAIL: cmake configure failed. See $OUTFILE"
    exit 1
fi

echo "[contrib_sim] Building..."
if ! cmake --build build >> "$OUTFILE" 2>&1; then
    echo "## Result: FAIL — cmake build failed" >> "$OUTFILE"
    echo "[contrib_sim] FAIL: cmake build failed. See $OUTFILE"
    exit 1
fi

echo "[contrib_sim] Running tests..."
if ! ctest --test-dir build --output-on-failure >> "$OUTFILE" 2>&1; then
    echo "## Result: FAIL — ctest failed" >> "$OUTFILE"
    echo "[contrib_sim] FAIL: ctest failed. See $OUTFILE"
    exit 1
fi

{
    echo ""
    echo "---"
    echo ""
    echo "## Result: PASS"
    echo ""
    echo "All steps completed successfully:"
    echo "- git clone: ok"
    echo "- cmake configure: ok"
    echo "- cmake build: ok"
    echo "- ctest: ok"
} >> "$OUTFILE"

echo "[contrib_sim] PASS. Result written to tests/contrib_sim/last_run.md"
