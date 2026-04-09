#!/usr/bin/env python3
# binary_blob_check.py — Binary blob detection in source directories
# Rule: CODING_RULES.md — no compiled objects in source dirs.
# Pre-compiled blobs bypass code review and break supply chain audits.
# Usage: python3 tools/ci/binary_blob_check.py [--root <repo>] [--strict]
#
# @author  Ritesh Anand
# @company embediq.com | ritzylab.com
#
# SPDX-License-Identifier: Apache-2.0

import argparse
import os
import sys

_SOURCE_DIRS = ['core', 'osal', 'hal', 'fbs', 'components', 'third_party',
                'tools', 'tests']

_SAFE_EXTS = {
    '.gitkeep', '.md', '.txt', '.py', '.sh', '.cmake', '.c', '.h',
    '.cpp', '.hpp', '.iq', '.json', '.yml', '.yaml', '.ld', '.cfg',
    '.clang-format', '.clang-tidy',
}

_SAFE_NAMES = {'CMakeLists.txt', 'LICENSE', 'NOTICE', 'Makefile',
               '.gitkeep', '.gitignore'}

_BINARY_EXTS = {'.o', '.a', '.so', '.dylib', '.dll', '.elf', '.bin',
                '.hex', '.axf', '.out', '.lib'}


def run(repo_root, strict=False):
    violations = []

    for src_dir in _SOURCE_DIRS:
        base = os.path.join(repo_root, src_dir)
        if not os.path.isdir(base):
            continue
        for dirpath, dirnames, filenames in os.walk(base):
            dirnames[:] = [d for d in dirnames
                           if d not in {'build', '.git', '__pycache__'}]
            for fname in filenames:
                abs_path = os.path.join(dirpath, fname)
                rel_path = os.path.relpath(abs_path, repo_root)

                if fname in _SAFE_NAMES:
                    continue

                _, ext = os.path.splitext(fname)
                ext_lower = ext.lower()

                if ext_lower in _SAFE_EXTS:
                    continue

                tag = 'VIOLATION' if strict else 'WARNING'

                # Known binary extensions
                if ext_lower in _BINARY_EXTS:
                    violations.append(
                        f'{tag}: {rel_path}:\n'
                        f'  Issue: binary file committed to source directory\n'
                        f'  Rule: CODING_RULES.md — no compiled objects in '
                        f'source dirs; use third_party/ + VENDORING.md\n'
                        f'  Why: binary blobs bypass code review, cannot be '
                        f'audited for licence or security, and make the repo '
                        f'unverifiable for regulated product supply chains\n'
                        f'  Fix: remove the file; if it is a vendored library, '
                        f'add source to third_party/<name>/ with a VENDORING.md '
                        f'instead\n'
                        f'  See-also: third_party/README.md — vendoring process'
                    )
                    continue

                # Unknown extension — check if binary content
                try:
                    with open(abs_path, 'rb') as fh:
                        chunk = fh.read(8192)
                    chunk.decode('utf-8')
                except UnicodeDecodeError:
                    violations.append(
                        f'{tag}: {rel_path}:\n'
                        f'  Issue: binary file committed to source directory\n'
                        f'  Rule: CODING_RULES.md — no compiled objects in '
                        f'source dirs; use third_party/ + VENDORING.md\n'
                        f'  Why: binary blobs bypass code review, cannot be '
                        f'audited for licence or security, and make the repo '
                        f'unverifiable for regulated product supply chains\n'
                        f'  Fix: remove the file; if it is a vendored library, '
                        f'add source to third_party/<name>/ with a VENDORING.md '
                        f'instead\n'
                        f'  See-also: third_party/README.md — vendoring process'
                    )
                except (IOError, OSError):
                    pass

    for v in sorted(violations):
        print(v)

    if violations:
        count = len(violations)
        mode = 'STRICT' if strict else 'WARNING-ONLY'
        print(f'\nbinary_blob_check.py: {count} violation(s) found [{mode}]')

    if strict and violations:
        return 1
    return 0


if __name__ == '__main__':
    parser = argparse.ArgumentParser(
        description='Binary blob detection in source directories')
    parser.add_argument('--root', default=None)
    parser.add_argument('--strict', action='store_true')
    args = parser.parse_args()
    root = args.root or os.path.normpath(
        os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
    sys.exit(run(root, strict=args.strict))
