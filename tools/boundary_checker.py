#!/usr/bin/env python3
"""
tools/boundary_checker.py — Layer boundary include checker for EmbedIQ

Usage: python3 tools/boundary_checker.py [repo_root]
  repo_root defaults to the repository root (parent of tools/).

Checks that no .c or .h file includes headers from a layer it is not
permitted to access (AGENTS.md §3 layer dependency rule).

Layer rules (a file in layer X may only include from allowed layers):
  core/src/        → allowed: core/include/
  osal/posix/      → allowed: core/include/
  osal/freertos/   → allowed: core/include/
  platform/posix/  → allowed: core/include/, osal/posix/
  platform/esp32/  → allowed: core/include/, osal/freertos/
  components/      → allowed: core/include/
  examples/        → allowed: core/include/, components/
  tests/           → allowed: core/include/, examples/  (integration tests need example headers)

Rules:
  - Same-layer includes are always allowed.
  - System headers (#include <...>) are always allowed.
  - Headers in unknown layers (build/, generated/, tools/) are skipped.

Exit codes: 0 = clean, 1 = violations found.

@author  Ritesh Anand
@company embediq.com | ritzylab.com

SPDX-License-Identifier: Apache-2.0
"""

import os
import sys
import re

# ── Layer detection ───────────────────────────────────────────────────────────

def _layer_of(rel_path):
    """Return the canonical layer name for a repo-relative path, or None."""
    p = rel_path.replace(os.sep, '/')
    if p.startswith('core/include/'):  return 'core/include'
    if p.startswith('core/src/'):      return 'core/src'
    if p.startswith('osal/posix/'):    return 'osal/posix'
    if p.startswith('osal/freertos/'): return 'osal/freertos'
    if p.startswith('platform/posix/'): return 'platform/posix'
    if p.startswith('platform/esp32/'): return 'platform/esp32'
    if p.startswith('components/'):    return 'components'
    if p.startswith('examples/'):      return 'examples'
    if p.startswith('tests/'):         return 'tests'
    return None  # build/, generated/, tools/, docs/ — not checked


# Allowed cross-layer include sets (same-layer is always permitted separately)
_ALLOWED = {
    'core/src':        {'core/include'},
    'osal/posix':      {'core/include'},
    'osal/freertos':   {'core/include'},
    'platform/posix':  {'core/include', 'osal/posix'},
    'platform/esp32':  {'core/include', 'osal/freertos'},
    'components':      {'core/include'},
    'examples':        {'core/include', 'components'},
    'tests':           {'core/include', 'examples'},
}

# ── Header index (basename → layer) ─────────────────────────────────────────

def _build_header_index(repo_root):
    """
    Return dict: header_basename → layer.

    core/include/ is indexed first so it wins when the same header name
    exists in multiple layers (e.g. shim + canonical).
    """
    idx = {}

    # Pass 1 — core/include/ wins all ties
    core_inc = os.path.join(repo_root, 'core', 'include')
    if os.path.isdir(core_inc):
        for fname in os.listdir(core_inc):
            if fname.endswith(('.h', '.hpp')):
                idx[fname] = 'core/include'

    # Pass 2 — everything else (first-found wins after core/include)
    skip = {'build', '.git', '__pycache__'}
    for dirpath, dirnames, filenames in os.walk(repo_root):
        dirnames[:] = [d for d in dirnames if d not in skip]
        for fname in filenames:
            if not fname.endswith(('.h', '.hpp')):
                continue
            if fname in idx:
                continue  # already indexed (core/include wins)
            full  = os.path.join(dirpath, fname)
            rel   = os.path.relpath(full, repo_root)
            layer = _layer_of(rel)
            if layer:
                idx[fname] = layer
    return idx

# ── Per-file include check ───────────────────────────────────────────────────

_INCLUDE_RE = re.compile(r'^\s*#\s*include\s+"([^"]+)"')


def _check_file(abs_path, rel_path, repo_root, file_layer, header_index):
    """Return a list of violation strings for the given file."""
    allowed  = _ALLOWED[file_layer]
    file_dir = os.path.dirname(abs_path)
    violations = []

    try:
        with open(abs_path, 'r', errors='replace') as fh:
            for lineno, line in enumerate(fh, 1):
                m = _INCLUDE_RE.match(line)
                if not m:
                    continue
                inc = m.group(1)

                # Resolve which layer the included file belongs to
                if '/' in inc or '\\' in inc:
                    # Path includes a separator → resolve relative to file dir
                    resolved     = os.path.normpath(os.path.join(file_dir, inc))
                    inc_from_root = os.path.relpath(resolved, repo_root)
                    inc_layer    = _layer_of(inc_from_root)
                else:
                    # Simple basename → look up in the pre-built index
                    inc_layer = header_index.get(os.path.basename(inc))

                if inc_layer is None:
                    continue  # unknown layer (generated/, tools/, etc.) — skip

                if inc_layer == file_layer:
                    continue  # same-layer include always allowed

                if inc_layer not in allowed:
                    violations.append(
                        f'VIOLATION: {rel_path}:{lineno}: '
                        f'{file_layer} must not include from {inc_layer} '
                        f'("{inc}")'
                    )
    except (IOError, OSError):
        pass

    return violations

# ── Main walker ──────────────────────────────────────────────────────────────

def run(repo_root):
    header_index   = _build_header_index(repo_root)
    all_violations = []
    skip           = {'build', '.git', '__pycache__'}

    for dirpath, dirnames, filenames in os.walk(repo_root):
        dirnames[:] = [d for d in dirnames if d not in skip]
        for fname in filenames:
            if not fname.endswith(('.c', '.h', '.cpp', '.hpp')):
                continue
            abs_path   = os.path.join(dirpath, fname)
            rel_path   = os.path.relpath(abs_path, repo_root)
            file_layer = _layer_of(rel_path)
            if file_layer not in _ALLOWED:
                continue  # layer not under our rules
            violations = _check_file(abs_path, rel_path, repo_root,
                                     file_layer, header_index)
            all_violations.extend(violations)

    for v in sorted(all_violations):
        print(v)

    return 1 if all_violations else 0


if __name__ == '__main__':
    root = sys.argv[1] if len(sys.argv) > 1 else os.path.normpath(
        os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
    sys.exit(run(root))
