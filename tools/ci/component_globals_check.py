#!/usr/bin/env python3
# component_globals_check.py — Component OSAL handle file-scope checker
# Rule: R-lib-4 (CODING_RULES.md) — components must not store OSAL handles
# at file scope. Hidden RTOS state breaks bare-metal portability.
# Usage: python3 tools/ci/component_globals_check.py [--root <repo>] [--strict]
#
# @author  Ritesh Anand
# @company embediq.com | ritzylab.com
#
# SPDX-License-Identifier: Apache-2.0

import argparse
import os
import re
import sys

_HANDLE_TYPES = [
    'EmbedIQ_Task_t', 'EmbedIQ_Queue_t', 'EmbedIQ_Signal_t',
    'EmbedIQ_Mutex_t', 'embediq_sem_t', 'EmbedIQ_Timer_t',
]

_PATTERN = re.compile(
    r'^\s*(static\s+)?(' + '|'.join(re.escape(t) for t in _HANDLE_TYPES) + r')\s+')


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
                        m = _PATTERN.match(line)
                        if m:
                            handle_type = m.group(2)
                            tag = 'VIOLATION' if strict else 'WARNING'
                            violations.append(
                                f'{tag}: {rel_path}:{lineno}:\n'
                                f'  Issue: components/ file stores OSAL handle '
                                f"'{handle_type}' at file scope\n"
                                f'  Rule: R-lib-4 (CODING_RULES.md) — components '
                                f'are pure utilities; no hidden OSAL state\n'
                                f'  Why: file-scope OSAL handles mean the component '
                                f'owns RTOS resources — it is no longer caller-allocated '
                                f'and cannot be safely used on bare metal\n'
                                f'  Fix: remove the file-scope handle; pass it as a '
                                f'parameter or use a caller-allocated context struct '
                                f'instead\n'
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
        print(f'\ncomponent_globals_check.py: {count} violation(s) found [{mode}]')

    if strict and violations:
        return 1
    return 0


if __name__ == '__main__':
    parser = argparse.ArgumentParser(
        description='Component OSAL handle file-scope checker (R-lib-4)')
    parser.add_argument('--root', default=None)
    parser.add_argument('--strict', action='store_true')
    args = parser.parse_args()
    root = args.root or os.path.normpath(
        os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
    sys.exit(run(root, strict=args.strict))
