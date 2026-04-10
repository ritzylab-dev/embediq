#!/usr/bin/env python3
# component_state_check.py — Component OSAL API call checker
# Rule: R-lib-4 (CODING_RULES.md) — components must not call OSAL APIs.
# OSAL calls create hidden RTOS dependencies that break bare-metal portability.
# Usage: python3 tools/ci/component_state_check.py [--root <repo>] [--strict]
#
# @author  Ritesh Anand
# @company embediq.com | ritzylab.com
#
# SPDX-License-Identifier: Apache-2.0

import argparse
import os
import re
import sys

_OSAL_CALL = re.compile(r'\b(embediq_osal_\w+|embediq_sem_\w+)\s*\(')


def run(repo_root, strict=False):
    components_dir = os.path.join(repo_root, 'components')
    violations = []

    if not os.path.isdir(components_dir):
        return 0

    for dirpath, _dirs, filenames in os.walk(components_dir):
        for fname in filenames:
            if not fname.endswith('.c'):
                continue
            abs_path = os.path.join(dirpath, fname)
            rel_path = os.path.relpath(abs_path, repo_root)
            try:
                with open(abs_path, 'r', errors='replace') as fh:
                    for lineno, line in enumerate(fh, 1):
                        m = _OSAL_CALL.search(line)
                        if m:
                            fn_name = m.group(1)
                            tag = 'VIOLATION' if strict else 'WARNING'
                            violations.append(
                                f'{tag}: {rel_path}:{lineno}:\n'
                                f"  Issue: components/ file calls OSAL API "
                                f"'{fn_name}' — OSAL calls forbidden in "
                                f"components\n"
                                f'  Rule: R-lib-4 (CODING_RULES.md) — components '
                                f'must have zero OSAL dependencies\n'
                                f'  Why: OSAL calls require an RTOS; a component '
                                f'calling OSAL cannot run on bare metal and '
                                f'violates the "pure utility" classification\n'
                                f'  Fix: remove the OSAL call; use a callback '
                                f'parameter or caller-provided synchronisation\n'
                                f'  See-also: ARCHITECTURE.md — Library Architecture '
                                f'(§2.1 Component category)'
                            )
            except (IOError, OSError):
                pass

    for v in sorted(violations):
        print(v)

    if violations:
        count = len(violations)
        mode = 'STRICT' if strict else 'WARNING-ONLY'
        print(f'\ncomponent_state_check.py: {count} violation(s) found [{mode}]')

    if strict and violations:
        return 1
    return 0


if __name__ == '__main__':
    parser = argparse.ArgumentParser(
        description='Component OSAL API call checker (R-lib-4)')
    parser.add_argument('--root', default=None)
    parser.add_argument('--strict', action='store_true')
    args = parser.parse_args()
    root = args.root or os.path.normpath(
        os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
    sys.exit(run(root, strict=args.strict))
