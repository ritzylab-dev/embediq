#!/usr/bin/env python3
"""
tools/ci/test_check_message_ids.py — tests for check_message_ids.py duplicate detection

Verifies that check_message_ids.py:
  - Exits 1 when a message ID or a message name is defined more than once,
    within one catalog, across catalogs, or between a catalog and the
    framework header — and names both definitions.
  - Exits 2 on an unparsable catalog, an empty catalog directory, a catalog
    with zero messages, a header with no definitions and a missing header —
    never a silent pass.
  - Exits 0 on a clean fixture and on the repository's own catalogs.

Each test builds a throwaway catalog directory and header and runs the checker
with --messages-dir and --header.

Run: python3 tools/ci/test_check_message_ids.py
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
                       "check_message_ids.py")

if not os.path.isfile(CHECKER):
    print("FAIL  check_message_ids.py not found")
    sys.exit(1)

HEADER = ("#define MSG_H_ONE   0x0500u  /**< one */\n"
          "#define MSG_H_TWO   0x0501u\n")

_failed = []


def _catalog(*messages):
    body = 'version 1\nnamespace "com.example.test"\n\n'
    for name, mid in messages:
        body += f"message {name} id={mid} {{\n    value : u8\n}}\n\n"
    return body


def _write(base, rel_path, content):
    full = os.path.join(base, *rel_path.split('/'))
    os.makedirs(os.path.dirname(full), exist_ok=True)
    with open(full, 'w') as fh:
        fh.write(content)


def _run(tmp, catalogs, header=HEADER, header_path=None):
    msgs = os.path.join(tmp, "messages")
    os.makedirs(msgs, exist_ok=True)
    for name, text in catalogs.items():
        _write(tmp, f"messages/{name}", text)
    if header_path is None:
        header_path = os.path.join(tmp, "msgs.h")
        _write(tmp, "msgs.h", header)
    result = subprocess.run(
        [sys.executable, CHECKER, "--messages-dir", msgs, "--header", header_path],
        capture_output=True, text=True)
    return result.returncode, result.stdout + result.stderr


def _check(name, condition, detail=""):
    if condition:
        print(f"PASS: {name}")
    else:
        print(f"FAIL: {name}" + (f" — {detail}" if detail else ""))
        _failed.append(name)


def _expect_dup(name, catalogs, both, header=HEADER):
    with tempfile.TemporaryDirectory() as tmp:
        rc, out = _run(tmp, catalogs, header=header)
        _check(f"{name}: exits 1", rc == 1, f"got rc={rc}. output: {out!r}")
        for d in both:
            _check(f"{name}: names '{d}'", d in out, f"output: {out!r}")


def _expect_error(name, catalogs, text, header=HEADER, header_path=None):
    with tempfile.TemporaryDirectory() as tmp:
        hp = header_path(tmp) if header_path else None
        rc, out = _run(tmp, catalogs, header=header, header_path=hp)
        _check(f"{name}: exits 2", rc == 2, f"got rc={rc}. output: {out!r}")
        _check(f"{name}: says '{text}'", text in out, f"output: {out!r}")


def test_duplicates():
    _expect_dup("same_id_one_catalog",
                {"a.iq": _catalog(("MSG_A", "0x1400"), ("MSG_B", "0x1400"))},
                ["a.iq: MSG_A = 0x1400", "a.iq: MSG_B = 0x1400"])
    _expect_dup("same_id_two_catalogs",
                {"a.iq": _catalog(("MSG_A", "0x1400")),
                 "b.iq": _catalog(("MSG_B", "0x1400"))},
                ["a.iq: MSG_A = 0x1400", "b.iq: MSG_B = 0x1400"])
    _expect_dup("same_name_two_catalogs",
                {"a.iq": _catalog(("MSG_A", "0x1400")),
                 "b.iq": _catalog(("MSG_A", "0x1401"))},
                ["a.iq: MSG_A = 0x1400", "b.iq: MSG_A = 0x1401"])
    _expect_dup("same_name_one_catalog",
                {"a.iq": _catalog(("MSG_A", "0x1400"), ("MSG_A", "0x1401"))},
                ["a.iq: MSG_A = 0x1400", "a.iq: MSG_A = 0x1401"])
    _expect_dup("catalog_id_equals_header_id",
                {"a.iq": _catalog(("MSG_A", "0x0500"))},
                ["a.iq: MSG_A = 0x0500", "msgs.h: MSG_H_ONE = 0x0500"])
    _expect_dup("catalog_name_equals_header_name",
                {"a.iq": _catalog(("MSG_H_TWO", "0x1400"))},
                ["a.iq: MSG_H_TWO = 0x1400", "msgs.h: MSG_H_TWO = 0x0501"])
    _expect_dup("two_header_definitions_one_id",
                {"a.iq": _catalog(("MSG_A", "0x1400"))},
                ["msgs.h: MSG_H_ONE = 0x0500", "msgs.h: MSG_H_SAME = 0x0500"],
                header=HEADER + "#define MSG_H_SAME  0x0500u\n")


def test_errors():
    _expect_error("unparsable_catalog",
                  {"a.iq": 'version 1\nmessage lowercase id=zz {\n'},
                  "cannot parse")
    _expect_error("empty_messages_directory", {}, "no catalogs found")
    _expect_error("catalog_with_zero_messages",
                  {"a.iq": _catalog(("MSG_A", "0x1400")),
                   "empty.iq": _catalog()},
                  "catalog defines no messages")
    _expect_error("header_without_definitions",
                  {"a.iq": _catalog(("MSG_A", "0x1400"))},
                  "no header definitions found", header="/* nothing */\n")
    _expect_error("missing_header",
                  {"a.iq": _catalog(("MSG_A", "0x1400"))},
                  "cannot read",
                  header_path=lambda tmp: os.path.join(tmp, "absent.h"))


def test_clean_fixture():
    with tempfile.TemporaryDirectory() as tmp:
        rc, out = _run(tmp, {"a.iq": _catalog(("MSG_A", "0x1400"), ("MSG_B", "0x1401")),
                             "b.iq": _catalog(("MSG_C", "0x1402"))})
        _check("clean_fixture: exits 0", rc == 0, f"got rc={rc}. output: {out!r}")
        _check("clean_fixture: OK line",
               "OK — 3 message(s) in 2 catalog(s), 2 header definition(s), "
               "no duplicates." in out, f"output: {out!r}")


def test_repository_tree():
    result = subprocess.run([sys.executable, CHECKER], capture_output=True, text=True)
    out = result.stdout + result.stderr
    _check("repository_tree: exits 0", result.returncode == 0,
           f"got rc={result.returncode}. output: {out!r}")
    _check("repository_tree: OK line",
           "OK — 17 message(s) in 5 catalog(s), 15 header definition(s), "
           "no duplicates." in out, f"output: {out!r}")


if __name__ == "__main__":
    tests = [test_duplicates, test_errors, test_clean_fixture, test_repository_tree]
    for t in tests:
        t()

    print()
    if _failed:
        print(f"FAILED: {len(_failed)} check(s): {', '.join(_failed)}")
        sys.exit(1)
    print(f"All {len(tests)} test groups passed.")
    sys.exit(0)
