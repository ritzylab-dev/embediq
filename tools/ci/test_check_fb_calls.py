#!/usr/bin/env python3
"""
tools/ci/test_check_fb_calls.py — tests for check_fb_calls.py per-rule allowances

Verifies that check_fb_calls.py:
  - Allows exactly the (path, rule) pairs listed in RULE_ALLOW and nothing else.
  - Still reports a rule that is not allowed, inside an allowed host program.
  - Matches RULE_ALLOW paths by equality: a main.c at any other path is checked.
  - Reports stdio in a Functional Block that sits next to an allowed host program.
  - Exits 0 when the only hits are allowed ones, and prints one 'allowed:' line each.
  - Reports a handler that reads msg as an envelope (cast or alias) in examples/
    and fbs/, and not in core/src, an outgoing-message helper, or (void)msg.

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


# Handler bodies that read msg as an EmbedIQ_Msg_t envelope (R-sub-34).
ENVELOPE_READS = [
    ("cast",            'n = ((const EmbedIQ_Msg_t *)msg)->payload_len;'),
    ("const_after",     'n = ((EmbedIQ_Msg_t const *)msg)->payload_len;'),
    ("alias_cast",      'const EmbedIQ_Msg_t *m = (const EmbedIQ_Msg_t *)msg;'),
    ("alias_plain",     'const EmbedIQ_Msg_t *m = msg;'),
]


def _handler(body_lines):
    return ('static void run(void *fb, const void *msg, void *d, void *s)\n{\n'
            + ''.join(f'    {b}\n' for b in body_lines) + '}\n')


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


# ── Test 6 — envelope reads through msg are reported in examples/ ────────────

def test_envelope_read_reported_in_examples():
    with tempfile.TemporaryDirectory() as tmp:
        for name, line in ENVELOPE_READS:
            _write(tmp, f"examples/demo/fb_{name}.c", _handler([line]))
        rc, out = _run(tmp)
        _check("envelope_read_reported_in_examples: exits 1",
               rc == 1, f"got rc={rc}")
        for name, _ in ENVELOPE_READS:
            _check(f"envelope_read_reported_in_examples: {name} form",
                   f"fb_{name}.c:3: [R-sub-34]" in out, f"output: {out!r}")
        _check("envelope_read_reported_in_examples: one report per line",
               out.count("[R-sub-34]") == len(ENVELOPE_READS), f"output: {out!r}")


# ── Test 7 — the same text outside fbs/ and examples/ is not this rule ──────

def test_envelope_read_out_of_scope_in_core():
    with tempfile.TemporaryDirectory() as tmp:
        _write(tmp, "core/src/dispatcher/demo.c",
               _handler([line for _, line in ENVELOPE_READS]))
        rc, out = _run(tmp)
        _check("envelope_read_out_of_scope_in_core: exits 0",
               rc == 0, f"got rc={rc}, output: {out!r}")
        _check("envelope_read_out_of_scope_in_core: not reported",
               "[R-sub-34]" not in out, f"output: {out!r}")


# ── Test 8 — an outgoing-message helper and (void)msg are not reported ──────

def test_outgoing_envelope_and_void_msg_not_reported():
    with tempfile.TemporaryDirectory() as tmp:
        _write(tmp, "fbs/services/fb_demo.c",
               'static EmbedIQ_Msg_t g_msg;\n'
               'static void publish(void)\n{\n'
               '    EmbedIQ_Msg_t *out = &g_msg;\n'
               '    out->payload_len = 4;\n'
               '    out->payload[0] = 1u;\n'
               '}\n'
               + _handler(['(void)msg;']))
        rc, out = _run(tmp)
        _check("outgoing_envelope_and_void_msg_not_reported: exits 0",
               rc == 0, f"got rc={rc}, output: {out!r}")


if __name__ == '__main__':
    test_host_program_may_print_and_sleep()
    test_host_program_other_rules_still_apply()
    test_unlisted_main_is_checked()
    test_fb_beside_host_program_may_not_print()
    test_clean_tree_exits_zero()
    test_envelope_read_reported_in_examples()
    test_envelope_read_out_of_scope_in_core()
    test_outgoing_envelope_and_void_msg_not_reported()

    print()
    if _failed:
        print(f"FAILED: {len(_failed)} check(s): {', '.join(_failed)}")
        sys.exit(1)
    else:
        print("All 8 tests passed.")
        sys.exit(0)
