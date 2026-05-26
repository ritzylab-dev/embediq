#!/usr/bin/env python3
# check_misra.py — MISRA-C:2012 compliance scan (cppcheck wrapper)
#
# Wraps cppcheck --addon=misra across all C source directories.
# Reports violations as warnings. Exits 0 always (warning-only).
# Pass --strict to exit non-zero on any violation (future gate upgrade).
#
# This script implements no MISRA rule logic. All rule checking is delegated
# to cppcheck. See docs/MISRA_COMPLIANCE.md for the project's MISRA posture
# and intentional deviations.
#
# @author  Ritesh Anand
# @company embediq.com | ritzylab.com
#
# SPDX-License-Identifier: Apache-2.0
"""
check_misra.py — MISRA-C:2012 compliance scan (cppcheck wrapper).

Iterates over all .c files under the configured scan directories and runs
cppcheck with the MISRA addon. Parses stderr for `[misra-cN.M]` violation
lines and prints a summary report.

Default behaviour: warning-only (exit 0 regardless of violations).
With --strict: exit 1 on any violation (reserved for future gate upgrade).

If cppcheck is not on PATH, prints a hint and exits 0.

Usage:
  python3 tools/ci/check_misra.py [--strict]
"""

import re
import shutil
import subprocess
import sys
from collections import Counter
from pathlib import Path


SCAN_DIRS = [
    'core/src',
    'fbs/drivers',
    'fbs/services',
    'hal/posix',
    'osal/posix',
]

MISRA_LINE_RE = re.compile(r'\[misra-c2012-(\d+\.\d+)\]')


def cppcheck_version() -> str | None:
    try:
        result = subprocess.run(
            ['cppcheck', '--version'],
            capture_output=True, text=True, check=False,
        )
        return result.stdout.strip() or result.stderr.strip()
    except FileNotFoundError:
        return None


def scan_file(path: Path) -> list[str]:
    cmd = [
        'cppcheck',
        '--addon=misra',
        '--error-exitcode=0',
        '--suppress=missingIncludeSystem',
        '--quiet',
        '--std=c11',
        str(path),
    ]
    result = subprocess.run(cmd, capture_output=True, text=True, check=False)
    violations = []
    for line in result.stderr.splitlines():
        if '[misra-' in line:
            violations.append(line.strip())
    return violations


def main() -> int:
    strict = '--strict' in sys.argv[1:]

    version = cppcheck_version()
    if version is None:
        print(
            'WARNING: cppcheck not found — MISRA scan skipped. '
            'Install: sudo apt-get install cppcheck'
        )
        return 0

    repo_root = Path(__file__).resolve().parent.parent.parent

    files: list[Path] = []
    for scan_dir in SCAN_DIRS:
        dir_path = repo_root / scan_dir
        if not dir_path.exists():
            continue
        files.extend(sorted(dir_path.rglob('*.c')))

    print(f'MISRA-C:2012 scan — {version} — {len(files)} files scanned')
    print(f'Scan dirs: {", ".join(SCAN_DIRS)}')
    print()

    all_violations: list[tuple[Path, str]] = []
    for src_file in files:
        for v in scan_file(src_file):
            all_violations.append((src_file, v))

    rule_violations = [(p, v) for p, v in all_violations if MISRA_LINE_RE.search(v)]
    config_warnings = [(p, v) for p, v in all_violations if '[misra-config]' in v]
    other = [(p, v) for p, v in all_violations
             if not MISRA_LINE_RE.search(v) and '[misra-config]' not in v]

    if all_violations:
        print(
            f'cppcheck reported {len(all_violations)} MISRA line(s): '
            f'{len(rule_violations)} rule violation(s), '
            f'{len(config_warnings)} config warning(s), '
            f'{len(other)} other.'
        )
        print()

        if rule_violations:
            print('MISRA rule violations:')
            for src_file, v in rule_violations:
                rel = src_file.relative_to(repo_root)
                print(f'  {rel}: {v}')
            print()
            rule_counts = Counter()
            for _, v in rule_violations:
                m = MISRA_LINE_RE.search(v)
                if m:
                    rule_counts[m.group(1)] += 1
            print('Top rules by frequency:')
            for rule, count in rule_counts.most_common(5):
                print(f'  Rule {rule}: {count} occurrence(s)')
            print()

        if config_warnings:
            print(
                f'{len(config_warnings)} [misra-config] warning(s) — '
                'cppcheck could not fully resolve macros/includes for some '
                'files. These are analysis-incompleteness notes, not MISRA '
                'rule violations. Add -I include paths or -D defines to the '
                'CI invocation to resolve.'
            )
            print()

        print(
            'See docs/MISRA_COMPLIANCE.md for intentional deviations and '
            'rationale.'
        )
    else:
        print('No MISRA violations reported.')

    if strict and rule_violations:
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
