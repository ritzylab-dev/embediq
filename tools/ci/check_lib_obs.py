#!/usr/bin/env python3
# check_lib_obs.py — Library Observatory obligation checker
# Rule: R-lib-1 (CODING_RULES.md) — every library with a resource acquisition
# path must emit EMBEDIQ_OBS_EVT_LIB_INIT (0x40) at init.
# Phase: warning-only in LIB-2. Hard error (--strict) enabled in LIB-5.
# Usage: python3 tools/ci/check_lib_obs.py [--root <repo_root>] [--strict]
#
# @author  Ritesh Anand
# @company embediq.com | ritzylab.com
#
# SPDX-License-Identifier: Apache-2.0

"""
Scans every .c file under components/ and verifies that any file containing
a resource acquisition path (malloc, RTOS object creation, or peripheral init)
calls EMBEDIQ_OBS_EVT_LIB_INIT.

Exit 0 in warning-only mode (default). Exit 1 with --strict on violations.
"""

import argparse
import os
import re
import sys

# Patterns that indicate a resource acquisition path
_RESOURCE_PATTERNS = [
    re.compile(r'\bmalloc\s*\('),
    re.compile(r'\bcalloc\s*\('),
    re.compile(r'\brealloc\s*\('),
    re.compile(r'\bembediq_osal_task_create\s*\('),
    re.compile(r'\bembediq_osal_queue_create\s*\('),
    re.compile(r'\bembediq_osal_signal_create\s*\('),
    re.compile(r'\bembediq_osal_mutex_create\s*\('),
    re.compile(r'\bembediq_osal_sem_create\s*\('),
    re.compile(r'\bhal_\w+_init\s*\('),
    re.compile(r'\bhal_\w+_open\s*\('),
]

# Pattern that satisfies the obligation
_LIB_INIT_PATTERN = re.compile(r'EMBEDIQ_OBS_EVT_LIB_INIT')


def _find_resource_lines(filepath):
    """Return list of (lineno, line, pattern_desc) for resource acquisition lines."""
    findings = []
    try:
        with open(filepath, 'r', errors='replace') as fh:
            for lineno, line in enumerate(fh, 1):
                for pat in _RESOURCE_PATTERNS:
                    if pat.search(line):
                        findings.append((lineno, line.strip(), pat.pattern))
                        break
    except (IOError, OSError):
        pass
    return findings


def _has_lib_init(filepath):
    """Return True if the file contains EMBEDIQ_OBS_EVT_LIB_INIT."""
    try:
        with open(filepath, 'r', errors='replace') as fh:
            for line in fh:
                if _LIB_INIT_PATTERN.search(line):
                    return True
    except (IOError, OSError):
        pass
    return False


def run(repo_root, strict=False):
    components_dir = os.path.join(repo_root, 'components')
    violations = []

    if not os.path.isdir(components_dir):
        return 0

    for dirpath, _dirnames, filenames in os.walk(components_dir):
        for fname in filenames:
            if not fname.endswith('.c'):
                continue
            abs_path = os.path.join(dirpath, fname)
            rel_path = os.path.relpath(abs_path, repo_root)

            resource_lines = _find_resource_lines(abs_path)
            if not resource_lines:
                continue

            if _has_lib_init(abs_path):
                continue

            first_line = resource_lines[0]
            violations.append(
                f'{"VIOLATION" if strict else "WARNING"}: {rel_path}:{first_line[0]}:\n'
                f'  Issue: file contains resource acquisition '
                f'({first_line[2]}) but does not emit '
                f'EMBEDIQ_OBS_EVT_LIB_INIT\n'
                f'  Rule: R-lib-1 — Observatory obligation — '
                f'CODING_RULES.md\n'
                f'  Why: silent library init failure is the hardest '
                f'embedded failure mode to diagnose\n'
                f'  Fix: emit EMBEDIQ_OBS_EVT_LIB_INIT (0x40) at init '
                f'via the ops table obs_emit pointer\n'
                f'  See-also: ARCHITECTURE.md — Library Architecture '
                f'(D-LIB-2)'
            )

    for v in sorted(violations):
        print(v)

    if violations:
        count = len(violations)
        mode = 'STRICT' if strict else 'WARNING-ONLY'
        print(f'\ncheck_lib_obs.py: {count} violation(s) found [{mode}]')

    if strict and violations:
        return 1
    return 0


if __name__ == '__main__':
    parser = argparse.ArgumentParser(
        description='Library Observatory obligation checker (R-lib-1)')
    parser.add_argument('--root', default=None,
                        help='Repository root (default: auto-detect)')
    parser.add_argument('--strict', action='store_true',
                        help='Exit 1 on violations (for LIB-5 promotion)')
    args = parser.parse_args()

    root = args.root or os.path.normpath(
        os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
    sys.exit(run(root, strict=args.strict))
