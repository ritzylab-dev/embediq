#!/usr/bin/env python3
# vendoring_date_check.py — Third-party anomaly assessment staleness checker
# Rule: R-lib-3 (CODING_RULES.md) — anomaly assessments must be reviewed
# annually. >1 year = warning. >2 years + --strict = hard error.
# Usage: python3 tools/ci/vendoring_date_check.py [--root <repo>] [--strict]
#
# @author  Ritesh Anand
# @company embediq.com | ritzylab.com
#
# SPDX-License-Identifier: Apache-2.0

import argparse
import datetime
import os
import re
import sys

_DATE_PATTERN = re.compile(
    r'^\s*\|\s*anomaly_assessment_date\s*\|\s*(\d{4}-\d{2}-\d{2})\s*\|',
    re.MULTILINE)


def run(repo_root, strict=False):
    tp_dir = os.path.join(repo_root, 'third_party')
    violations = []
    today = datetime.date.today()

    if not os.path.isdir(tp_dir):
        return 0

    for entry in sorted(os.listdir(tp_dir)):
        full = os.path.join(tp_dir, entry)
        if not os.path.isdir(full):
            continue
        vmd = os.path.join(full, 'VENDORING.md')
        if not os.path.isfile(vmd):
            continue

        try:
            with open(vmd, 'r', errors='replace') as fh:
                content = fh.read()
        except (IOError, OSError):
            continue

        m = _DATE_PATTERN.search(content)
        if not m:
            violations.append(
                f'WARNING: third_party/{entry}/VENDORING.md:\n'
                f'  Issue: anomaly_assessment_date not found or unparseable\n'
                f'  Rule: R-lib-3 (CODING_RULES.md) — anomaly assessments must '
                f'be reviewed annually\n'
                f'  Why: security posture degrades silently; a missing date means '
                f'no assessment was ever recorded\n'
                f'  Fix: add anomaly_assessment_date in YYYY-MM-DD format\n'
                f'  See-also: third_party/README.md — anomaly_assessment_date policy'
            )
            continue

        try:
            date_val = datetime.date.fromisoformat(m.group(1))
        except ValueError:
            violations.append(
                f'WARNING: third_party/{entry}/VENDORING.md:\n'
                f"  Issue: anomaly_assessment_date '{m.group(1)}' is unparseable\n"
                f'  Rule: R-lib-3 (CODING_RULES.md) — anomaly assessments must '
                f'be reviewed annually\n'
                f'  Why: security posture degrades silently; an unparseable date '
                f'cannot be age-checked\n'
                f'  Fix: use YYYY-MM-DD format for anomaly_assessment_date\n'
                f'  See-also: third_party/README.md — anomaly_assessment_date policy'
            )
            continue

        age_days = (today - date_val).days
        if age_days > 730 and strict:
            violations.append(
                f'VIOLATION: third_party/{entry}/VENDORING.md:\n'
                f"  Issue: anomaly_assessment_date '{m.group(1)}' is {age_days} "
                f'days old (>730 day limit)\n'
                f'  Rule: R-lib-3 (CODING_RULES.md) — anomaly assessments must '
                f'be reviewed annually\n'
                f'  Why: security posture degrades silently; a year-old assessment '
                f'may miss newly disclosed CVEs against the pinned version\n'
                f'  Fix: re-review anomaly_list_url, update anomaly_assessment_date '
                f'to today, and document findings in the Notes section of VENDORING.md\n'
                f'  See-also: third_party/README.md — anomaly_assessment_date policy'
            )
        elif age_days > 365:
            violations.append(
                f'WARNING: third_party/{entry}/VENDORING.md:\n'
                f"  Issue: anomaly_assessment_date '{m.group(1)}' is {age_days} "
                f'days old (>365 day limit)\n'
                f'  Rule: R-lib-3 (CODING_RULES.md) — anomaly assessments must '
                f'be reviewed annually\n'
                f'  Why: security posture degrades silently; a year-old assessment '
                f'may miss newly disclosed CVEs against the pinned version\n'
                f'  Fix: re-review anomaly_list_url, update anomaly_assessment_date '
                f'to today, and document findings in the Notes section of VENDORING.md\n'
                f'  See-also: third_party/README.md — anomaly_assessment_date policy'
            )

    for v in violations:
        print(v)

    if violations:
        count = len(violations)
        mode = 'STRICT' if strict else 'WARNING-ONLY'
        print(f'\nvendoring_date_check.py: {count} issue(s) found [{mode}]')

    has_hard = any(v.startswith('VIOLATION:') for v in violations)
    if strict and has_hard:
        return 1
    return 0


if __name__ == '__main__':
    parser = argparse.ArgumentParser(
        description='Third-party anomaly assessment staleness checker (R-lib-3)')
    parser.add_argument('--root', default=None)
    parser.add_argument('--strict', action='store_true')
    args = parser.parse_args()
    root = args.root or os.path.normpath(
        os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
    sys.exit(run(root, strict=args.strict))
