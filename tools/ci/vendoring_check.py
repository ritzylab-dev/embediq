#!/usr/bin/env python3
# vendoring_check.py — Third-party VENDORING.md completeness checker
# Rule: R-lib-3 (CODING_RULES.md) — every third_party/ subdirectory must have
# a VENDORING.md with all 7 mandatory fields.
# Usage: python3 tools/ci/vendoring_check.py [--root <repo>] [--strict]
#
# @author  Ritesh Anand
# @company embediq.com | ritzylab.com
#
# SPDX-License-Identifier: Apache-2.0

import argparse
import os
import re
import sys

_REQUIRED_FIELDS = [
    'name', 'version', 'license', 'soup_class', 'source_url',
    'anomaly_list_url', 'anomaly_assessment_date',
]


def _check_vendoring(repo_root, name, strict):
    violations = []
    tag = 'VIOLATION' if strict else 'WARNING'
    vmd_path = os.path.join(repo_root, 'third_party', name, 'VENDORING.md')
    rel_path = os.path.relpath(vmd_path, repo_root)

    if not os.path.isfile(vmd_path):
        violations.append(
            f'{tag}: third_party/{name}/:\n'
            f'  Issue: VENDORING.md is missing\n'
            f'  Rule: R-lib-3 (CODING_RULES.md) — every vendored library must '
            f'have a 7-field VENDORING.md\n'
            f'  Why: without VENDORING.md, security reviewers cannot identify '
            f'the library version, license, or anomaly exposure — a '
            f'supplier-of-record gap for regulated products\n'
            f'  Fix: add VENDORING.md with all 7 fields. See third_party/README.md '
            f'for the template.\n'
            f'  See-also: third_party/README.md — VENDORING.md field definitions'
        )
        return violations

    try:
        with open(vmd_path, 'r', errors='replace') as fh:
            content = fh.read()
    except (IOError, OSError):
        return violations

    for field in _REQUIRED_FIELDS:
        pat = re.compile(r'^\s*\|\s*' + re.escape(field) + r'\s*\|', re.MULTILINE)
        if not pat.search(content):
            violations.append(
                f'{tag}: {rel_path}:\n'
                f"  Issue: missing required field '{field}'\n"
                f'  Rule: R-lib-3 (CODING_RULES.md) — every vendored library must '
                f'have a 7-field VENDORING.md\n'
                f'  Why: without VENDORING.md, security reviewers cannot identify '
                f'the library version, license, or anomaly exposure — a '
                f'supplier-of-record gap for regulated products\n'
                f'  Fix: add VENDORING.md with all 7 fields. See third_party/README.md '
                f'for the template.\n'
                f'  See-also: third_party/README.md — VENDORING.md field definitions'
            )

    return violations


def run(repo_root, strict=False):
    tp_dir = os.path.join(repo_root, 'third_party')
    all_violations = []

    if not os.path.isdir(tp_dir):
        return 0

    for entry in sorted(os.listdir(tp_dir)):
        full = os.path.join(tp_dir, entry)
        if not os.path.isdir(full):
            continue
        all_violations.extend(_check_vendoring(repo_root, entry, strict))

    for v in sorted(all_violations):
        print(v)

    if all_violations:
        count = len(all_violations)
        mode = 'STRICT' if strict else 'WARNING-ONLY'
        print(f'\nvendoring_check.py: {count} violation(s) found [{mode}]')

    if strict and all_violations:
        return 1
    return 0


if __name__ == '__main__':
    parser = argparse.ArgumentParser(
        description='Third-party VENDORING.md completeness checker (R-lib-3)')
    parser.add_argument('--root', default=None)
    parser.add_argument('--strict', action='store_true')
    args = parser.parse_args()
    root = args.root or os.path.normpath(
        os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
    sys.exit(run(root, strict=args.strict))
