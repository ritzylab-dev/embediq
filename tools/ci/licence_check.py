#!/usr/bin/env python3
# licence_check.py — SPDX-License-Identifier presence checker
# Rule: CODING_RULES.md — all .c and .h files must carry
# SPDX-License-Identifier: Apache-2.0.
# Scope: first-party sources only. Every directory named third_party (at any
# depth) is skipped — vendored code keeps its upstream licence notices; this is
# not a dependency licence review. Under --strict a file that cannot be read or
# a directory that cannot be listed is a violation.
# Usage: python3 tools/ci/licence_check.py [--root <repo>] [--strict]
#
# @author  Ritesh Anand
# @company embediq.com | ritzylab.com
#
# SPDX-License-Identifier: Apache-2.0

import argparse
import os
import re
import sys

_SPDX_PATTERN = re.compile(r'SPDX-License-Identifier:\s*(\S+)')
_SKIP_DIRS = {'.git', '__pycache__', 'generated'}
_VENDORED = 'third_party'
_VENDORED_NOTICE = ('third_party/: not checked — vendored code keeps its upstream '
                    'licence notices; this check covers first-party sources only '
                    'and is not a dependency licence review.')


def run(repo_root, strict=False):
    violations = []
    pruned_vendored = False
    tag = 'VIOLATION' if strict else 'WARNING'

    def cannot_read(path, err):
        if strict:
            violations.append(f'VIOLATION: {os.path.relpath(path, repo_root)}: '
                              f'cannot read ({err})')

    def onerror(err):
        cannot_read(err.filename, err.strerror)

    for dirpath, dirnames, filenames in os.walk(repo_root, onerror=onerror):
        if _VENDORED in dirnames:
            pruned_vendored = True
        dirnames[:] = [d for d in dirnames
                       if d not in _SKIP_DIRS and d != _VENDORED
                       and not d.startswith('build')]
        for fname in filenames:
            if not fname.endswith(('.c', '.h')):
                continue
            abs_path = os.path.join(dirpath, fname)
            rel_path = os.path.relpath(abs_path, repo_root)

            try:
                with open(abs_path, 'r', errors='replace') as fh:
                    content = fh.read()
            except OSError as e:
                cannot_read(abs_path, e.strerror)
                continue

            m = _SPDX_PATTERN.search(content)
            if not m:
                violations.append(
                    f'{tag}: {rel_path}:\n'
                    f'  Issue: file missing SPDX-License-Identifier\n'
                    f'  Rule: CODING_RULES.md — all source files must carry '
                    f'SPDX-License-Identifier: Apache-2.0\n'
                    f'  Why: missing SPDX makes automated licence scanners '
                    f'unable to classify the file; downstream packagers and '
                    f'OSS compliance tools treat it as unlicenced\n'
                    f'  Fix: add `SPDX-License-Identifier: Apache-2.0` to '
                    f'the file comment header\n'
                    f'  See-also: https://spdx.dev/ids/'
                )
            elif m.group(1) != 'Apache-2.0':
                violations.append(
                    f'{tag}: {rel_path}:\n'
                    f"  Issue: SPDX-License-Identifier is '{m.group(1)}' "
                    f"(expected 'Apache-2.0')\n"
                    f'  Rule: CODING_RULES.md — all source files must carry '
                    f'SPDX-License-Identifier: Apache-2.0\n'
                    f'  Why: non-Apache licence in core/layer-1/layer-2 violates '
                    f'R-04 (Apache 2.0 only in Core/Layer 1/Layer 2)\n'
                    f'  Fix: change the SPDX identifier to Apache-2.0 or move '
                    f'the file to Layer 3+\n'
                    f'  See-also: https://spdx.dev/ids/'
                )

    if pruned_vendored:
        print(_VENDORED_NOTICE)

    for v in sorted(violations):
        print(v)

    if violations:
        count = len(violations)
        mode = 'STRICT' if strict else 'WARNING-ONLY'
        print(f'\nlicence_check.py: {count} violation(s) found [{mode}]')

    if strict and violations:
        return 1
    return 0


if __name__ == '__main__':
    parser = argparse.ArgumentParser(
        description='SPDX-License-Identifier presence checker')
    parser.add_argument('--root', default=None)
    parser.add_argument('--strict', action='store_true')
    args = parser.parse_args()
    root = args.root or os.path.normpath(
        os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
    sys.exit(run(root, strict=args.strict))
