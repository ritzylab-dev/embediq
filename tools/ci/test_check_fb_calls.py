#!/usr/bin/env python3
"""
tools/ci/test_check_fb_calls.py — tests for check_fb_calls.py per-rule allowances

Verifies that check_fb_calls.py:
  - Allows exactly the (path, rule) pairs listed in RULE_ALLOW and nothing else.
  - Still reports a rule that is not allowed, inside an allowed host program.
  - Matches RULE_ALLOW paths by equality: a main.c at any other path is checked.
  - Reports stdio in a Functional Block that sits next to an allowed host program.
  - Exits 0 when the only hits are allowed ones, and prints one 'allowed:' line each.

Each test builds a throwaway tree and runs the checker on it with --root.

Run: python3 tools/ci/test_check_fb_calls.py
All output lines start with PASS or FAIL. Exit 0 iff all pass.

@author  Ritesh Anand
@company embediq.com | ritzylab.com

SPDX-License-Identifier: Apache-2.0
"""

import os
import subprocess
import sys
import tempfile

CHECKER = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                       "check_fb_calls.py")

HOST_MAIN = "examples/gateway/main.c"   # listed in RULE_ALLOW

_failed = []


def _write(base, rel_path, content):
    full = os.path.join(base, *rel_path.split('/'))
    os.makedirs(os.path.dirname(full), exist_ok=True)
    with open(full, 'w') as fh:
        fh.write(content)


def _run(repo_root):
    result = subprocess.run(
        [sys.executable, CHECKER, '--root', repo_root],
        capture_output=True, text=True
    )
    return result.returncode, result.stdout + result.stderr


def _check(name, condition, detail=""):
    if condition:
        print(f"PASS: {name}")
    else:
        print(f"FAIL: {name}" + (f" — {detail}" if detail else ""))
        _failed.append(name)


# ── Test 1 — host program: print and sleep are allowed, exit 0 ──────────────

def test_host_program_may_print_and_sleep():
    with tempfile.TemporaryDirectory() as tmp:
        _write(tmp, HOST_MAIN,
               'int main(void) {\n'
               '    printf("hello\\n");\n'
               '    embediq_osal_delay_ms(10u);\n'
               '    return 0;\n'
               '}\n')
        rc, out = _run(tmp)
        _check("host_program_may_print_and_sleep: exits 0",
               rc == 0, f"got rc={rc}, output: {out!r}")
        _check("host_program_may_print_and_sleep: notes the stdio allowance",
               f"allowed: {HOST_MAIN}:2 R-stdio" in out, f"output: {out!r}")
        _check("host_program_may_print_and_sleep: notes the delay allowance",
               f"allowed: {HOST_MAIN}:3 R-sub-03a" in out, f"output: {out!r}")


# ── Test 2 — host program: a rule it is not allowed is still reported ───────

def test_host_program_other_rules_still_apply():
    with tempfile.TemporaryDirectory() as tmp:
        _write(tmp, HOST_MAIN,
               'int main(void) {\n'
               '    printf("hello\\n");\n'
               '    void *p = malloc(4);\n'
               '    return 0;\n'
               '}\n')
        rc, out = _run(tmp)
        _check("host_program_other_rules_still_apply: exits 1",
               rc == 1, f"got rc={rc}")
        _check("host_program_other_rules_still_apply: reports allocation",
               "main.c:3: [I-07/R-02]" in out, f"output: {out!r}")
        _check("host_program_other_rules_still_apply: stdio still allowed",
               f"allowed: {HOST_MAIN}:2 R-stdio" in out, f"output: {out!r}")


# ── Test 3 — a main.c at an unlisted path gets no allowance ─────────────────

def test_unlisted_main_is_checked():
    with tempfile.TemporaryDirectory() as tmp:
        _write(tmp, "examples/other/main.c",
               'int main(void) { printf("x\\n"); return 0; }\n')
        _write(tmp, "examples/gateway/sub/main.c",
               'int main(void) { printf("x\\n"); return 0; }\n')
        rc, out = _run(tmp)
        _check("unlisted_main_is_checked: exits 1",
               rc == 1, f"got rc={rc}")
        _check("unlisted_main_is_checked: reports examples/other/main.c",
               "other/main.c:1: [R-stdio]" in out, f"output: {out!r}")
        _check("unlisted_main_is_checked: reports a nested main.c",
               "sub/main.c:1: [R-stdio]" in out, f"output: {out!r}")
        _check("unlisted_main_is_checked: no allowance printed",
               "allowed:" not in out, f"output: {out!r}")


# ── Test 4 — an FB beside an allowed host program may not print ─────────────

def test_fb_beside_host_program_may_not_print():
    with tempfile.TemporaryDirectory() as tmp:
        _write(tmp, HOST_MAIN,
               'int main(void) { printf("x\\n"); return 0; }\n')
        _write(tmp, "examples/gateway/fb_demo.c",
               'static void run(void) { printf("x\\n"); }\n'
               'static void err(void) { perror("x"); }\n')
        rc, out = _run(tmp)
        _check("fb_beside_host_program_may_not_print: exits 1",
               rc == 1, f"got rc={rc}")
        _check("fb_beside_host_program_may_not_print: reports printf",
               "fb_demo.c:1: [R-stdio]" in out, f"output: {out!r}")
        _check("fb_beside_host_program_may_not_print: reports perror",
               "fb_demo.c:2: [R-stdio]" in out, f"output: {out!r}")


# ── Test 5 — a clean tree exits 0 with no allowance lines ───────────────────

def test_clean_tree_exits_zero():
    with tempfile.TemporaryDirectory() as tmp:
        _write(tmp, "examples/gateway/fb_demo.c",
               'static void run(void) { embediq_obs_emit(0x72u, 0u, 0xFFu, 1u, 2u); }\n')
        rc, out = _run(tmp)
        _check("clean_tree_exits_zero: exits 0",
               rc == 0, f"got rc={rc}, output: {out!r}")
        _check("clean_tree_exits_zero: no allowance printed",
               "allowed:" not in out, f"output: {out!r}")


if __name__ == '__main__':
    test_host_program_may_print_and_sleep()
    test_host_program_other_rules_still_apply()
    test_unlisted_main_is_checked()
    test_fb_beside_host_program_may_not_print()
    test_clean_tree_exits_zero()

    print()
    if _failed:
        print(f"FAILED: {len(_failed)} check(s): {', '.join(_failed)}")
        sys.exit(1)
    else:
        print("All 5 tests passed.")
        sys.exit(0)
