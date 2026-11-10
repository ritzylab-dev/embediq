#!/usr/bin/env python3
"""
tools/boundary_checker.py — Layer boundary include checker for EmbedIQ

Usage: python3 tools/boundary_checker.py [repo_root]
  repo_root defaults to the repository root (parent of tools/).

Checks that no .c or .h file includes headers from a layer it is not
permitted to access (AGENTS.md §3 layer dependency rule).

Layer rules (a file in layer X may only include from allowed layers):
  core/src/        → allowed: core/include/ (NO core/include/hal/)
  osal/posix/      → allowed: core/include/ (NO core/include/hal/)
  osal/freertos/   → allowed: core/include/ (NO core/include/hal/)
  components/      → allowed: nothing outside components/ (no embediq_*.h)
  examples/        → allowed: core/include/, core/include/hal/, fbs/drivers/, fbs/services/
  tests/           → allowed: core/include/, core/include/hal/, examples/, fbs/drivers/, fbs/services/
  hal/posix/, hal/freertos/, hal/esp32/, hal/stm32/
                   → allowed: core/include/hal/; and from core/include/ only
                     (a) headers under core/include/ops/,
                     (b) embediq_config.h, embediq_obs.h, embediq_platform.h,
                     (c) the scoped exception: hal/posix/ops/hal_provisioning_posix.c
                         may include embediq_cfg.h and embediq_nvm.h
  fbs/drivers/     → allowed: core/include/, core/include/hal/, components/, generated/
  fbs/services/    → allowed: core/include/ (NO core/include/hal/ — service FBs are platform-agnostic),
                     components/, generated/
  fbs/bridge/      → allowed: core/include/ (NO core/include/hal/), components/, generated/

Rules:
  - A header in the same source location as the including file is always allowed.
  - Angle-bracket includes are never inspected. A quote include that resolves
    to no first-party header is ignored, except in fbs/drivers, fbs/services
    and fbs/bridge, where it is rejected unless it is a generated header. A
    generated header is a file inside generated/, identified by the location
    the include resolves to, never by its name.
  - platform/ is recognised only as a target that core/src must not include;
    files under it are not checked.
  - Strict layers: fbs/drivers/, fbs/services/, fbs/bridge/.
  - Include resolution: a name containing '/' is looked up relative to the
    including file's directory, then under core/include/, then under
    generated/; a bare name through the header index (first-party headers),
    then under generated/.
  - components/ utility libraries (e.g. embediq_crc — LIB-3) carry zero
    framework dependencies and exist to be consumed by the layers above them;
    Driver and Service FBs may include them directly.
  - Directories whose name starts with "build", .git and __pycache__ are
    skipped. A file that cannot be opened or a directory that cannot be listed
    is a violation ("cannot read").

On success prints one line: boundary_checker.py: OK — <n> file(s) checked, 0 violations.

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
    if p.startswith('core/include/hal/'):  return 'core/include/hal'
    if p.startswith('core/include/'):      return 'core/include'
    if p.startswith('core/src/'):          return 'core/src'
    if p.startswith('osal/posix/'):        return 'osal/posix'
    if p.startswith('osal/freertos/'):     return 'osal/freertos'
    if p.startswith('platform/posix/'):    return 'platform/posix'
    if p.startswith('platform/esp32/'):    return 'platform/esp32'
    if p.startswith('components/'):        return 'components'
    if p.startswith('examples/'):          return 'examples'
    if p.startswith('tests/'):             return 'tests'
    if p.startswith('hal/posix/'):         return 'hal/posix'
    if p.startswith('hal/freertos/'):      return 'hal/freertos'
    if p.startswith('hal/esp32/'):         return 'hal/esp32'
    if p.startswith('hal/stm32/'):         return 'hal/stm32'
    if p.startswith('fbs/drivers/'):       return 'fbs/drivers'
    if p.startswith('fbs/services/'):      return 'fbs/services'
    if p.startswith('fbs/bridge/'):        return 'fbs/bridge'
    return None  # build/, generated/, tools/, docs/, third_party/ — not checked


# Allowed cross-layer include sets (same-layer is always permitted separately)
_HAL_LAYERS = ('hal/posix', 'hal/freertos', 'hal/esp32', 'hal/stm32')
_ALLOWED = {
    'core/src':        {'core/include'},
    'osal/posix':      {'core/include'},
    'osal/freertos':   {'core/include'},
    'components':      set(),  # stdlib + own third_party/<name>/ only; no embediq_*.h
    'examples':        {'core/include', 'core/include/hal', 'fbs/drivers', 'fbs/services'},
    'tests':           {'core/include', 'core/include/hal', 'examples', 'fbs/drivers', 'fbs/services'},
    # HAL implementation layers — HAL contracts; core/include headers only as
    # the header-level rule below allows.
    'hal/posix':       {'core/include/hal'},
    'hal/freertos':    {'core/include/hal'},
    'hal/esp32':       {'core/include/hal'},
    'hal/stm32':       {'core/include/hal'},
    # Driver FBs — may use all framework contracts (incl. HAL), component
    # utility libraries (LIB-3), and generated contracts; but not impl paths.
    'fbs/drivers':     {'core/include', 'core/include/hal', 'components'},
    # Service FBs and the bridge — platform-agnostic: must NOT include HAL
    # contracts, but may use component utility libraries and generated contracts.
    'fbs/services':    {'core/include', 'components'},
    'fbs/bridge':      {'core/include', 'components'},
}

# Layers where a quote include that resolves to no first-party header is a
# violation unless it resolves to a file inside generated/.
_VENDOR_BSP_STRICT = {'fbs/drivers', 'fbs/services', 'fbs/bridge'}

# Header-level rule for HAL layers: the core/include headers a HAL source may
# include besides core/include/ops/.
_HAL_CORE_HEADERS = {'embediq_config.h', 'embediq_obs.h', 'embediq_platform.h'}

# Scoped exception. The POSIX provisioning HAL reads and writes provisioning state through the configuration and NVM interfaces — a HAL calling
# a Driver FB's interface, which the layer rule forbids. It is tolerated for this one file and these two headers only; the same include anywhere
# else is rejected. Removal criterion: provisioning state reaches the HAL through its ops-table arguments, and this file includes neither header.
_HAL_EXCEPTIONS = {
    ('hal/posix/ops/hal_provisioning_posix.c', 'embediq_cfg.h'),
    ('hal/posix/ops/hal_provisioning_posix.c', 'embediq_nvm.h'),
}

_HEADER_EXT = ('.h', '.hpp')
_SOURCE_EXT = ('.c', '.h', '.cpp', '.hpp')


def _skip_dir(name):
    return name.startswith('build') or name in ('.git', '__pycache__')


def _rel(path, repo_root):
    return os.path.relpath(path, repo_root).replace(os.sep, '/')


# ── Header index (basename → (layer, repo-relative path)) ────────────────────

def _build_header_index(repo_root, errors):
    """
    Return dict: header_basename → (layer, repo-relative path).

    core/include/ and core/include/hal/ direct children are indexed first so
    they win when the same header name exists in multiple layers (e.g. shim +
    canonical). Every other first-party header follows, first found wins.
    """
    idx = {}

    core_inc = os.path.join(repo_root, 'core', 'include')
    for sub, layer in ((core_inc, 'core/include'),
                       (os.path.join(core_inc, 'hal'), 'core/include/hal')):
        if os.path.isdir(sub):
            for fname in sorted(os.listdir(sub)):
                full = os.path.join(sub, fname)
                if fname.endswith(_HEADER_EXT) and os.path.isfile(full):
                    idx[fname] = (layer, _rel(full, repo_root))

    def onerror(err):
        errors.append(f'VIOLATION: {_rel(err.filename, repo_root)}: cannot read')

    for dirpath, dirnames, filenames in os.walk(repo_root, onerror=onerror):
        dirnames[:] = sorted(d for d in dirnames if not _skip_dir(d))
        for fname in sorted(filenames):
            if not fname.endswith(_HEADER_EXT) or fname in idx:
                continue
            rel = _rel(os.path.join(dirpath, fname), repo_root)
            layer = _layer_of(rel)
            if layer:
                idx[fname] = (layer, rel)
    return idx


# ── Include resolution ───────────────────────────────────────────────────────

def _resolve(inc, file_dir, repo_root, header_index):
    """Return the absolute path of the file an include resolves to, or None."""
    if '/' in inc or '\\' in inc:
        for base in (file_dir,
                     os.path.join(repo_root, 'core', 'include'),
                     os.path.join(repo_root, 'generated')):
            cand = os.path.normpath(os.path.join(base, inc))
            if os.path.isfile(cand):
                return cand
        return None
    hit = header_index.get(inc)
    if hit:
        return os.path.join(repo_root, hit[1])
    cand = os.path.join(repo_root, 'generated', inc)
    return cand if os.path.isfile(cand) else None


def _is_generated(path, repo_root):
    gen = os.path.realpath(os.path.join(repo_root, 'generated'))
    real = os.path.realpath(path)
    return real.startswith(gen + os.sep)


# ── Per-file include check ───────────────────────────────────────────────────

_INCLUDE_RE = re.compile(r'^\s*#\s*include\s+"([^"]+)"')


def _check_file(abs_path, rel_path, repo_root, file_layer, header_index):
    """Return a list of violation strings for the given file."""
    allowed  = _ALLOWED[file_layer]
    file_dir = os.path.dirname(abs_path)
    violations = []

    try:
        with open(abs_path, 'r', errors='replace') as fh:
            lines = fh.readlines()
    except OSError:
        return [f'VIOLATION: {rel_path}: cannot read']

    for lineno, line in enumerate(lines, 1):
        m = _INCLUDE_RE.match(line)
        if not m:
            continue
        inc = m.group(1)

        resolved = _resolve(inc, file_dir, repo_root, header_index)
        inc_rel   = _rel(resolved, repo_root) if resolved else None
        inc_layer = _layer_of(inc_rel) if inc_rel else None

        if inc_layer is None:
            # Resolves to no first-party header (vendor, third_party/, generated/,
            # or nothing at all).
            if file_layer in _VENDOR_BSP_STRICT:
                # generated/ headers are auto-generated framework contracts
                # (catalogs, config schema) — sanctioned for FB use, decided
                # by where the include resolves, never by its name.
                if resolved and _is_generated(resolved, repo_root):
                    continue
                violations.append(
                    f'VIOLATION: {rel_path}:{lineno}: '
                    f'{file_layer} must not include unrecognised headers '
                    f'("{inc}") — possible vendor BSP or unknown include'
                )
            continue

        if inc_layer == file_layer:
            continue  # same-layer include always allowed

        if file_layer in _HAL_LAYERS and inc_layer == 'core/include':
            base = os.path.basename(inc_rel)
            if (inc_rel.startswith('core/include/ops/')
                    or base in _HAL_CORE_HEADERS
                    or (rel_path, base) in _HAL_EXCEPTIONS):
                continue
            violations.append(
                f'VIOLATION: {rel_path}:{lineno}: '
                f'{file_layer} must not include {inc_rel} ("{inc}") — a HAL '
                f'source may include from core/include/ only (a) headers under '
                f'core/include/ops/ and (b) embediq_config.h, embediq_obs.h, '
                f'embediq_platform.h'
            )
            continue

        if inc_layer not in allowed:
            if file_layer == 'components':
                violations.append(
                    f'VIOLATION: {rel_path}:{lineno}:\n'
                    f'  Issue: components/ includes framework header '
                    f'from {inc_layer} ("{inc}")\n'
                    f'  Rule: R-lib-2 (emit through ops table only), '
                    f'R-lib-4 (ops table required) — CODING_RULES.md\n'
                    f'  Why: components/ must not include embediq_*.h '
                    f'directly — the ops table is the only framework '
                    f'interface\n'
                    f'  Fix: remove the include; access framework '
                    f'services through the ops table function pointers\n'
                    f'  See-also: ARCHITECTURE.md — Library Architecture '
                    f'(D-LIB-1, D-LIB-4)'
                )
            else:
                violations.append(
                    f'VIOLATION: {rel_path}:{lineno}: '
                    f'{file_layer} must not include from {inc_layer} '
                    f'("{inc}")'
                )

    return violations

# ── Main walker ──────────────────────────────────────────────────────────────

def run(repo_root):
    all_violations = []
    header_index   = _build_header_index(repo_root, all_violations)
    checked        = 0

    def onerror(err):
        all_violations.append(f'VIOLATION: {_rel(err.filename, repo_root)}: cannot read')

    for dirpath, dirnames, filenames in os.walk(repo_root, onerror=onerror):
        dirnames[:] = sorted(d for d in dirnames if not _skip_dir(d))
        for fname in sorted(filenames):
            if not fname.endswith(_SOURCE_EXT):
                continue
            abs_path   = os.path.join(dirpath, fname)
            rel_path   = _rel(abs_path, repo_root)
            file_layer = _layer_of(rel_path)
            if file_layer not in _ALLOWED:
                continue  # layer not under our rules
            checked += 1
            all_violations.extend(_check_file(abs_path, rel_path, repo_root,
                                              file_layer, header_index))

    # A directory that cannot be listed is reported by both walks once each.
    all_violations = sorted(set(all_violations))
    for v in all_violations:
        print(v)

    if all_violations:
        return 1
    print(f'boundary_checker.py: OK — {checked} file(s) checked, 0 violations.')
    return 0


if __name__ == '__main__':
    root = sys.argv[1] if len(sys.argv) > 1 else os.path.normpath(
        os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
    sys.exit(run(root))
