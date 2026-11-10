#!/usr/bin/env python3
"""
tools/ci/test_licence_check.py — tests for licence_check.py first-party scope and strict mode

Verifies that licence_check.py:
  - Fails a first-party file with no SPDX identifier under --strict, and only
    warns without it.
  - Does not check files under a third_party/ directory, and says so once.
  - Fails a first-party file whose identifier is not Apache-2.0, comparing the
    whole expression (`Apache-2.0 OR MIT` fails); a closing `*/` on the same
    line is not part of it.
  - Skips only build, build_* and build-* directories (a `builder` directory is
    checked).
  - Under --strict, fails a file it cannot read and a directory it cannot list.
  - Passes a clean tree without printing the third_party notice.

Each test builds a throwaway tree and runs the checker on it with --root.

Run: python3 tools/ci/test_licence_check.py
All output lines start with PASS, FAIL or SKIP. Exit 0 iff none fails.

@author  Ritesh Anand
@company embediq.com | ritzylab.com

SPDX-License-Identifier: Apache-2.0
"""

import os
import subprocess
import sys
import tempfile

CHECKER = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                       "licence_check.py")

if not os.path.isfile(CHECKER):
    print("FAIL  licence_check.py not found")
    sys.exit(1)

NOTICE = "third_party/: not checked"
GOOD = "/* SPDX-License-Identifier: Apache-2.0 */\nint x;\n"
NONE = "int x;\n"
MIT = "/* SPDX-License-Identifier: MIT */\nint x;\n"

_failed = []


def _write(base, rel_path, content):
    full = os.path.join(base, *rel_path.split('/'))
    os.makedirs(os.path.dirname(full), exist_ok=True)
    with open(full, 'w') as fh:
        fh.write(content)


def _run(repo_root, strict=True):
    cmd = [sys.executable, CHECKER, '--root', repo_root]
    if strict:
        cmd.append('--strict')
    result = subprocess.run(cmd, capture_output=True, text=True)
    return result.returncode, result.stdout + result.stderr


def _check(name, condition, detail=""):
    if condition:
        print(f"PASS: {name}")
    else:
        print(f"FAIL: {name}" + (f" — {detail}" if detail else ""))
        _failed.append(name)


# ── Test 1 — missing identifier: fails under --strict, warns without ────────

def test_missing_identifier():
    with tempfile.TemporaryDirectory() as tmp:
        _write(tmp, "core/src/a.c", NONE)
        rc, out = _run(tmp)
        _check("missing_identifier --strict: exits 1", rc == 1, f"got rc={rc}")
        _check("missing_identifier --strict: names the file",
               "core/src/a.c" in out, f"output: {out!r}")
        rc, out = _run(tmp, strict=False)
        _check("missing_identifier warning-only: exits 0", rc == 0, f"got rc={rc}")


# ── Test 2 — the same file under third_party/ is not checked ────────────────

def test_third_party_not_checked():
    with tempfile.TemporaryDirectory() as tmp:
        _write(tmp, "core/src/a.c", GOOD)
        _write(tmp, "third_party/vendor/v.c", NONE)
        _write(tmp, "components/lib/third_party/w.c", NONE)
        rc, out = _run(tmp)
        _check("third_party_not_checked: exits 0", rc == 0,
               f"got rc={rc}. output: {out!r}")
        _check("third_party_not_checked: notice printed exactly once",
               out.count(NOTICE) == 1, f"output: {out!r}")


# ── Test 3 — a non-Apache identifier in first-party code fails ──────────────

def test_wrong_identifier():
    with tempfile.TemporaryDirectory() as tmp:
        _write(tmp, "fbs/drivers/a.c", MIT)
        rc, out = _run(tmp)
        _check("wrong_identifier: exits 1", rc == 1, f"got rc={rc}. output: {out!r}")


# ── Test 4 — an unreadable first-party file fails under --strict ────────────

def test_unreadable_file():
    if os.geteuid() == 0:
        print("SKIP  unreadable_file  (running as root)")
        return
    with tempfile.TemporaryDirectory() as tmp:
        _write(tmp, "core/src/a.c", GOOD)
        path = os.path.join(tmp, "core", "src", "a.c")
        os.chmod(path, 0)
        try:
            rc, out = _run(tmp)
        finally:
            os.chmod(path, 0o644)
        _check("unreadable_file: exits 1", rc == 1, f"got rc={rc}. output: {out!r}")
        _check("unreadable_file: cannot read", "cannot read" in out, f"output: {out!r}")


# ── Test 5 — an unlistable first-party directory fails under --strict ───────

def test_unlistable_directory():
    if os.geteuid() == 0:
        print("SKIP  unlistable_directory  (running as root)")
        return
    with tempfile.TemporaryDirectory() as tmp:
        _write(tmp, "core/src/a.c", GOOD)
        locked = os.path.join(tmp, "core", "src")
        os.chmod(locked, 0)
        try:
            rc, out = _run(tmp)
        finally:
            os.chmod(locked, 0o755)
        _check("unlistable_directory: exits 1", rc == 1, f"got rc={rc}. output: {out!r}")
        _check("unlistable_directory: cannot read", "cannot read" in out,
               f"output: {out!r}")


# ── Test 6 — a clean tree passes, no notice ─────────────────────────────────

def test_clean_tree():
    with tempfile.TemporaryDirectory() as tmp:
        _write(tmp, "core/src/a.c", GOOD)
        _write(tmp, "core/include/a.h", GOOD)
        rc, out = _run(tmp)
        _check("clean_tree: exits 0", rc == 0, f"got rc={rc}. output: {out!r}")
        _check("clean_tree: no third_party notice", NOTICE not in out,
               f"output: {out!r}")


# ── Test 7 — the whole expression is the identifier ─────────────────────────

def test_compound_expression_rejected():
    with tempfile.TemporaryDirectory() as tmp:
        _write(tmp, "core/src/a.c", "/* SPDX-License-Identifier: Apache-2.0 OR MIT */\nint x;\n")
        rc, out = _run(tmp)
        _check("compound_expression: exits 1", rc == 1, f"got rc={rc}. output: {out!r}")


def test_comment_forms_accepted():
    for n, line in enumerate((" * SPDX-License-Identifier: Apache-2.0 */",
                              "// SPDX-License-Identifier: Apache-2.0"), 1):
        with tempfile.TemporaryDirectory() as tmp:
            _write(tmp, "core/src/a.c", line + "\nint x;\n")
            rc, out = _run(tmp)
            _check(f"comment_form_{n} {line!r}: exits 0", rc == 0,
                   f"got rc={rc}. output: {out!r}")


# ── Test 8 — only build, build_* and build-* directories are skipped ────────

def test_build_directory_names():
    with tempfile.TemporaryDirectory() as tmp:
        _write(tmp, "core/src/builder/a.c", NONE)
        rc, out = _run(tmp)
        _check("builder_directory_checked: exits 1", rc == 1, f"got rc={rc}. output: {out!r}")
    with tempfile.TemporaryDirectory() as tmp:
        _write(tmp, "core/src/build_x/a.c", NONE)
        rc, out = _run(tmp)
        _check("build_x_directory_skipped: exits 0", rc == 0, f"got rc={rc}. output: {out!r}")


# ── Entry point ──────────────────────────────────────────────────────────────

if __name__ == "__main__":
    tests = [
        test_missing_identifier,
        test_third_party_not_checked,
        test_wrong_identifier,
        test_unreadable_file,
        test_unlistable_directory,
        test_clean_tree,
        test_compound_expression_rejected,
        test_comment_forms_accepted,
        test_build_directory_names,
    ]
    for t in tests:
        t()

    print()
    if _failed:
        print(f"FAILED: {len(_failed)} check(s) in {len(tests)} tests: "
              f"{', '.join(_failed)}")
        sys.exit(1)
    print(f"All {len(tests)} tests passed.")
    sys.exit(0)
