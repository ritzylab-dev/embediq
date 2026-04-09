#!/usr/bin/env python3
# ops_version_check.py — Ops table ABI version field checker
# Rule: R-lib-4 + D-LIB-5 — every ops table header in core/include/ops/
# must define EMBEDIQ_*_OPS_VERSION and have uint32_t version as first field.
# Usage: python3 tools/ci/ops_version_check.py [--root <repo>] [--strict]
#
# @author  Ritesh Anand
# @company embediq.com | ritzylab.com
#
# SPDX-License-Identifier: Apache-2.0

import argparse
import os
import re
import sys

_VERSION_MACRO = re.compile(r'#define\s+EMBEDIQ_\w+_OPS_VERSION\s+')
_STRUCT_START = re.compile(r'typedef\s+struct\s*\{')
_VERSION_FIELD = re.compile(r'\buint32_t\s+version\b')
_FN_POINTER = re.compile(r'\(\s*\*')


def _check_header(abs_path, rel_path, strict):
    violations = []
    tag = 'VIOLATION' if strict else 'WARNING'

    try:
        with open(abs_path, 'r', errors='replace') as fh:
            lines = fh.readlines()
    except (IOError, OSError):
        return violations

    content = ''.join(lines)

    # Check (a): EMBEDIQ_*_OPS_VERSION macro
    if not _VERSION_MACRO.search(content):
        violations.append(
            f'{tag}: {rel_path}:\n'
            f'  Issue: ops table header missing EMBEDIQ_*_OPS_VERSION macro\n'
            f'  Rule: R-lib-4 (CODING_RULES.md) — ops tables must have '
            f'uint32_t version as first field\n'
            f'  Why: ABI version mismatch is silent if the version field '
            f"can't be read at offset 0; a version field placed after a "
            f'function pointer is unreadable at a known offset\n'
            f'  Fix: add `#define EMBEDIQ_<CAP>_OPS_VERSION 1u` and ensure '
            f'`uint32_t version;` is the first field in the struct (before '
            f'any function pointer)\n'
            f'  See-also: ARCHITECTURE.md — Ops Table Contract Rules (§2.3)'
        )

    # Check (b): uint32_t version before first function pointer in struct
    in_struct = False
    found_version = False
    found_fn_ptr_first = False
    for line in lines:
        if _STRUCT_START.search(line):
            in_struct = True
            found_version = False
            found_fn_ptr_first = False
            continue
        if in_struct:
            if line.strip().startswith('}'):
                in_struct = False
                continue
            if _VERSION_FIELD.search(line):
                found_version = True
                continue
            if _FN_POINTER.search(line) and not found_version:
                found_fn_ptr_first = True
                break

    if found_fn_ptr_first:
        violations.append(
            f'{tag}: {rel_path}:\n'
            f'  Issue: uint32_t version is not the first field in the '
            f'struct (appears after a function pointer)\n'
            f'  Rule: R-lib-4 (CODING_RULES.md) — ops tables must have '
            f'uint32_t version as first field\n'
            f'  Why: ABI version mismatch is silent if the version field '
            f"can't be read at offset 0; a version field placed after a "
            f'function pointer is unreadable at a known offset\n'
            f'  Fix: move `uint32_t version;` to be the first field in '
            f'the struct (before any function pointer)\n'
            f'  See-also: ARCHITECTURE.md — Ops Table Contract Rules (§2.3)'
        )

    return violations


def run(repo_root, strict=False):
    ops_dir = os.path.join(repo_root, 'core', 'include', 'ops')
    all_violations = []

    if not os.path.isdir(ops_dir):
        return 0

    for fname in sorted(os.listdir(ops_dir)):
        if not fname.endswith('.h'):
            continue
        abs_path = os.path.join(ops_dir, fname)
        rel_path = os.path.relpath(abs_path, repo_root)
        all_violations.extend(_check_header(abs_path, rel_path, strict))

    for v in sorted(all_violations):
        print(v)

    if all_violations:
        count = len(all_violations)
        mode = 'STRICT' if strict else 'WARNING-ONLY'
        print(f'\nops_version_check.py: {count} violation(s) found [{mode}]')

    if strict and all_violations:
        return 1
    return 0


if __name__ == '__main__':
    parser = argparse.ArgumentParser(
        description='Ops table ABI version field checker (R-lib-4, D-LIB-5)')
    parser.add_argument('--root', default=None)
    parser.add_argument('--strict', action='store_true')
    args = parser.parse_args()
    root = args.root or os.path.normpath(
        os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
    sys.exit(run(root, strict=args.strict))
