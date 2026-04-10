#!/usr/bin/env python3
# check_hal_obs.py — HAL Observatory obligation checker
# Rule: HAL observation obligation — every HAL function returning non-HAL_OK
# must call EMBEDIQ_HAL_OBS_EMIT_ERROR before returning.
# Phase: warning-only in LIB-2. Hard error (--strict) enabled at XOBS-2.
# Usage: python3 tools/ci/check_hal_obs.py [--root <repo_root>] [--strict]
#
# @author  Ritesh Anand
# @company embediq.com | ritzylab.com
#
# SPDX-License-Identifier: Apache-2.0

"""
Scans every .c file under hal/ and verifies that any function that can return
a non-HAL_OK status calls EMBEDIQ_HAL_OBS_EMIT_ERROR before returning.

Exit 0 in warning-only mode (default). Exit 1 with --strict on violations.
"""

import argparse
import os
import re
import sys

# Patterns indicating a non-HAL_OK return path
_NON_OK_RETURN_PATTERNS = [
    re.compile(r'\breturn\s+HAL_ERR_'),
    re.compile(r'\breturn\s+HAL_ERROR\b'),
    re.compile(r'\breturn\s+HAL_TIMEOUT\b'),
    re.compile(r'\breturn\s+HAL_BUSY\b'),
    re.compile(r'\breturn\s+-\d'),
    re.compile(r'\breturn\s+NULL\b'),
    re.compile(r'\breturn\s+false\b'),
    re.compile(r'\breturn\s+EMBEDIQ_ERR_'),
]

# Pattern that satisfies the obligation
_HAL_OBS_PATTERN = re.compile(r'EMBEDIQ_HAL_OBS_EMIT_ERROR')


def _scan_file(filepath, repo_root):
    """Return list of violation dicts for this file."""
    violations = []
    rel_path = os.path.relpath(filepath, repo_root)

    try:
        with open(filepath, 'r', errors='replace') as fh:
            lines = fh.readlines()
    except (IOError, OSError):
        return violations

    file_text = ''.join(lines)
    has_hal_obs = bool(_HAL_OBS_PATTERN.search(file_text))

    if has_hal_obs:
        return violations

    for lineno, line in enumerate(lines, 1):
        for pat in _NON_OK_RETURN_PATTERNS:
            if pat.search(line):
                violations.append({
                    'file': rel_path,
                    'line': lineno,
                    'text': line.strip(),
                })
                break

    return violations


def _format_violation(v, strict):
    tag = 'VIOLATION' if strict else 'WARNING'
    return (
        f'{tag}: {v["file"]}:{v["line"]}:\n'
        f'  Issue: HAL function returns non-OK status without '
        f'EMBEDIQ_HAL_OBS_EMIT_ERROR — "{v["text"]}"\n'
        f'  Rule: HAL observation obligation — '
        f'CROSS_LAYER_OBSERVABILITY_PLAN.md XOBS-2\n'
        f'  Why: HAL error paths must be observable; silent HAL '
        f'failures are invisible to the Observatory and to '
        f'post-mortem .iqtrace analysis\n'
        f'  Fix: call EMBEDIQ_HAL_OBS_EMIT_ERROR before the '
        f'error return\n'
        f'  See-also: PM/CROSS_LAYER_OBSERVABILITY_PLAN.md — XOBS-2'
    )


def run(repo_root, strict=False):
    hal_dir = os.path.join(repo_root, 'hal')
    all_violations = []

    if not os.path.isdir(hal_dir):
        return 0

    for dirpath, _dirnames, filenames in os.walk(hal_dir):
        for fname in filenames:
            if not fname.endswith('.c'):
                continue
            abs_path = os.path.join(dirpath, fname)
            all_violations.extend(_scan_file(abs_path, repo_root))

    formatted = [_format_violation(v, strict) for v in all_violations]
    for msg in sorted(formatted):
        print(msg)

    if all_violations:
        count = len(all_violations)
        mode = 'STRICT' if strict else 'WARNING-ONLY'
        print(f'\ncheck_hal_obs.py: {count} violation(s) found [{mode}]')

    if strict and all_violations:
        return 1
    return 0


if __name__ == '__main__':
    parser = argparse.ArgumentParser(
        description='HAL Observatory obligation checker (XOBS-2)')
    parser.add_argument('--root', default=None,
                        help='Repository root (default: auto-detect)')
    parser.add_argument('--strict', action='store_true',
                        help='Exit 1 on violations (for XOBS-2 promotion)')
    args = parser.parse_args()

    root = args.root or os.path.normpath(
        os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
    sys.exit(run(root, strict=args.strict))
