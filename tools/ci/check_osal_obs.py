#!/usr/bin/env python3
# check_osal_obs.py — OSAL Observatory obligation checker + R-osal-01 lint
# Rule: R-osal-01 (CODING_RULES.md) — never negate embediq_err_t; OSAL failure
# paths must call EMBEDIQ_OSAL_OBS_* macros.
# Phase: warning-only in LIB-2. Hard error (--strict) enabled at XOBS-1.
# Usage: python3 tools/ci/check_osal_obs.py [--root <repo_root>] [--strict]
#
# @author  Ritesh Anand
# @company embediq.com | ritzylab.com
#
# SPDX-License-Identifier: Apache-2.0

"""
Scans every .c file under osal/ and verifies:
1. Any function returning a failure indicator (NULL, false, non-zero, or
   embediq_err_t non-OK) calls the corresponding EMBEDIQ_OSAL_OBS_* macro.
2. No line matches the !fn() anti-pattern: if (!embediq_osal_...)

Exemption: a function whose body contains the marker comment
    /* osal-obs: refusal is an expected outcome */
is exempt from the obligation for its `return false` paths — for calls such
as embediq_osal_mutex_try() whose refusal is a normal result, not a failure.
The exemption is decided per function; use it only where that holds.

Exit 0 in warning-only mode (default). Exit 1 with --strict on violations.
"""

import argparse
import os
import re
import sys

# Pattern for the !fn() anti-pattern (R-osal-01)
_NEGATE_PATTERN = re.compile(r'if\s*\(\s*!embediq_osal_')

# Patterns indicating a failure return path
_FAILURE_RETURN_PATTERNS = [
    re.compile(r'\breturn\s+NULL\b'),
    re.compile(r'\breturn\s+false\b'),
    re.compile(r'\breturn\s+EMBEDIQ_ERR_'),
    re.compile(r'\breturn\s+-\d'),
]

# Pattern that satisfies the observation obligation
_OSAL_OBS_PATTERN = re.compile(r'EMBEDIQ_OSAL_OBS_')

# Marker that exempts one function's `return false` paths (see module doc)
_REFUSAL_MARKER = '/* osal-obs: refusal is an expected outcome */'
_RETURN_FALSE_PATTERN = re.compile(r'\breturn\s+false\b')


def _refusal_exempt_lines(lines):
    """Return the 1-based line numbers inside functions that carry the
    refusal marker. A function body runs from a line starting with '{' to
    the next line starting with '}' (the repository's C style)."""
    exempt = set()
    start = None
    for lineno, line in enumerate(lines, 1):
        if line.startswith('{'):
            start = lineno
        elif line.startswith('}') and start is not None:
            body = lines[start - 1:lineno]
            if any(_REFUSAL_MARKER in b for b in body):
                exempt.update(range(start, lineno + 1))
            start = None
    return exempt


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
    has_osal_obs = bool(_OSAL_OBS_PATTERN.search(file_text))

    # Check 1: !fn() anti-pattern
    for lineno, line in enumerate(lines, 1):
        if _NEGATE_PATTERN.search(line):
            violations.append({
                'file': rel_path,
                'line': lineno,
                'text': line.strip(),
                'kind': 'negate',
            })

    # Check 2: failure return paths without OSAL_OBS_* macro in file
    if not has_osal_obs:
        exempt = _refusal_exempt_lines(lines)
        for lineno, line in enumerate(lines, 1):
            if lineno in exempt and _RETURN_FALSE_PATTERN.search(line):
                continue
            for pat in _FAILURE_RETURN_PATTERNS:
                if pat.search(line):
                    violations.append({
                        'file': rel_path,
                        'line': lineno,
                        'text': line.strip(),
                        'kind': 'missing_obs',
                    })
                    break

    return violations


def _format_violation(v, strict):
    tag = 'VIOLATION' if strict else 'WARNING'
    if v['kind'] == 'negate':
        return (
            f'{tag}: {v["file"]}:{v["line"]}:\n'
            f'  Issue: negated embediq_err_t return value — '
            f'"{v["text"]}"\n'
            f'  Rule: R-osal-01 — never negate embediq_err_t — '
            f'CODING_RULES.md\n'
            f'  Why: embediq_err_t is int32_t; EMBEDIQ_ERR_TIMEOUT = -2 '
            f'is truthy; !fn() silently treats timeout as success\n'
            f'  Fix: use explicit comparison: '
            f'if (fn(...) == EMBEDIQ_OK)\n'
            f'  See-also: CODING_RULES.md — OSAL Rules (R-osal-01)'
        )
    else:
        return (
            f'{tag}: {v["file"]}:{v["line"]}:\n'
            f'  Issue: OSAL failure return path without '
            f'EMBEDIQ_OSAL_OBS_* macro — "{v["text"]}"\n'
            f'  Rule: R-osal-01 — OSAL observation obligation — '
            f'CODING_RULES.md\n'
            f'  Why: OSAL failure paths must be observable; silent '
            f'failures manifest only on hardware under RTOS timing\n'
            f'  Fix: call the corresponding EMBEDIQ_OSAL_OBS_* macro '
            f'before the failure return\n'
            f'  See-also: PM/CROSS_LAYER_OBSERVABILITY_PLAN.md — XOBS-1'
        )


def run(repo_root, strict=False):
    osal_dir = os.path.join(repo_root, 'osal')
    all_violations = []

    if not os.path.isdir(osal_dir):
        return 0

    for dirpath, _dirnames, filenames in os.walk(osal_dir):
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
        print(f'\ncheck_osal_obs.py: {count} violation(s) found [{mode}]')

    if strict and all_violations:
        return 1
    return 0


if __name__ == '__main__':
    parser = argparse.ArgumentParser(
        description='OSAL Observatory obligation checker + R-osal-01 lint')
    parser.add_argument('--root', default=None,
                        help='Repository root (default: auto-detect)')
    parser.add_argument('--strict', action='store_true',
                        help='Exit 1 on violations (for XOBS-1 promotion)')
    args = parser.parse_args()

    root = args.root or os.path.normpath(
        os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
    sys.exit(run(root, strict=args.strict))
