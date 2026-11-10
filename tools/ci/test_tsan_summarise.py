#!/usr/bin/env python3
"""
tools/ci/test_tsan_summarise.py — tests for tsan_summarise.py exit codes and input handling

Verifies that tsan_summarise.py:
  - Exits 0 on a readable directory with no finding and no runtime abort.
  - Exits 1 on a data-race report, a runtime CHECK failure, a sanitizer-handled
    crash (ERROR: ThreadSanitizer) and a start-up abort recorded only in
    Testing/Temporary/LastTest.log; lists each abort.
  - Exits 2 on a usage error, an unknown option, a path that is not a
    directory, a directory it cannot list and a report it cannot read — and
    then prints no table, no annotation and writes no job summary.
  - With --github, emits error annotations for findings and aborts.
And that the workflow uploads the reports and the recorded test output.

Each test builds a throwaway directory and runs the script on it. Fixture text
is the minimum the script's parser recognises: the separator, WARNING and FRAME
patterns (tsan_summarise.py, the regexes after "SEPARATOR = ") and the
runtime-abort tokens of its abort scan.

Run: python3 tools/ci/test_tsan_summarise.py
All output lines start with PASS, FAIL or SKIP. Exit 0 iff none fails.

@author  Ritesh Anand
@company embediq.com | ritzylab.com

SPDX-License-Identifier: Apache-2.0
"""

import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPT = os.path.join(HERE, "tsan_summarise.py")
WORKFLOW = os.path.normpath(os.path.join(HERE, "..", "..", ".github",
                                         "workflows", "ci.yml"))

if not os.path.isfile(SCRIPT):
    print("FAIL  tsan_summarise.py not found")
    sys.exit(1)

_failed = []
_run_count = 0

TABLE_MARK = "TSan report files"

RACE_REPORT = (
    "==================\n"
    "WARNING: ThreadSanitizer: data race (pid=123)\n"
    "  Write of size 4 at 0x7b0400000000 by thread T1:\n"
    "    #0 worker /src/tests/unit/test_race.c:10 (test_race+0x1234)\n"
    "\n"
    "  Previous write of size 4 at 0x7b0400000000 by thread T2:\n"
    "    #0 worker /src/tests/unit/test_race.c:10 (test_race+0x1234)\n"
    "\n"
    "  Location is global 'counter' of size 4 at 0x7b0400000000 (test_race+0x5678)\n"
    "\n"
    "SUMMARY: ThreadSanitizer: data race /src/tests/unit/test_race.c:10 in worker\n"
    "==================\n"
)

CHECK_ABORT = (
    "ThreadSanitizer: CHECK failed: tsan_rtl.cpp:42 \"((x)) != (0)\" (0x0, 0x0)\n"
    "    #0 obs_emit /src/core/src/observatory/obs.c:120 (test_obs+0x10)\n"
)

SEGV_ABORT = "==123==ERROR: ThreadSanitizer: SEGV on unknown address 0x000000000000\n"

STARTUP_ABORT = "FATAL: ThreadSanitizer: unexpected memory mapping 0x5555-0x5556\n"


def _write(base, rel_path, content):
    full = os.path.join(base, *rel_path.split('/'))
    os.makedirs(os.path.dirname(full), exist_ok=True)
    with open(full, 'w') as fh:
        fh.write(content)


def _run(args, summary=None):
    env = dict(os.environ)
    env.pop("GITHUB_STEP_SUMMARY", None)
    if summary is not None:
        env["GITHUB_STEP_SUMMARY"] = summary
    result = subprocess.run([sys.executable, SCRIPT] + args,
                            capture_output=True, text=True, env=env)
    return result.returncode, result.stdout + result.stderr


def _check(name, condition, detail=""):
    if condition:
        print(f"PASS: {name}")
    else:
        print(f"FAIL: {name}" + (f" — {detail}" if detail else ""))
        _failed.append(name)


def _test(fn):
    global _run_count
    _run_count += 1
    fn()


# ── Case 1 — empty directory → exit 0 ───────────────────────────────────────

def test_empty_directory_exits_zero():
    with tempfile.TemporaryDirectory() as tmp:
        rc, out = _run([tmp])
        _check("empty_directory: exits 0", rc == 0, f"got rc={rc}, output: {out!r}")


# ── Case 2 — one data-race report → exit 1, row names the race ──────────────

def test_data_race_exits_one():
    with tempfile.TemporaryDirectory() as tmp:
        _write(tmp, "tsan.101", RACE_REPORT)
        rc, out = _run([tmp])
        _check("data_race: exits 1", rc == 1, f"got rc={rc}")
        rows = [l for l in out.splitlines() if l.startswith("| 1 |")]
        _check("data_race: table row names a data race",
               any("data race" in r for r in rows), f"output: {out!r}")


# ── Case 3 — runtime CHECK failure with a located frame → exit 1 ────────────

def test_check_failed_abort_exits_one():
    with tempfile.TemporaryDirectory() as tmp:
        _write(tmp, "tsan.102", CHECK_ABORT)
        rc, out = _run([tmp])
        _check("check_failed_abort: exits 1", rc == 1, f"got rc={rc}, output: {out!r}")
        _check("check_failed_abort: abort listed",
               "CHECK failed" in out and "RUNTIME ABORTS" in out, f"output: {out!r}")


# ── Case 4 — sanitizer-handled crash → exit 1, listed ───────────────────────

def test_handled_crash_exits_one():
    with tempfile.TemporaryDirectory() as tmp:
        _write(tmp, "tsan.103", SEGV_ABORT)
        rc, out = _run([tmp])
        _check("handled_crash: exits 1", rc == 1, f"got rc={rc}, output: {out!r}")
        _check("handled_crash: abort listed",
               "RUNTIME ABORTS" in out and "SEGV on unknown address" in out,
               f"output: {out!r}")


# ── Case 4b — start-up abort recorded only in ctest's LastTest.log ──────────

def test_startup_abort_in_last_test_log():
    with tempfile.TemporaryDirectory() as tmp:
        _write(tmp, "Testing/Temporary/LastTest.log",
               "1/3 Test: unit_bus\nOutput:\n" + STARTUP_ABORT + "Test Passed.\n")
        rc, out = _run([tmp])
        _check("startup_abort_in_last_test_log: exits 1", rc == 1,
               f"got rc={rc}, output: {out!r}")
        _check("startup_abort_in_last_test_log: abort listed",
               "RUNTIME ABORTS" in out and "unexpected memory mapping" in out,
               f"output: {out!r}")
    with tempfile.TemporaryDirectory() as tmp:
        _write(tmp, "Testing/Temporary/LastTest.log",
               "1/3 Test: unit_bus\nOutput:\nall good\nTest Passed.\n")
        rc, out = _run([tmp])
        _check("clean_last_test_log: exits 0", rc == 0, f"got rc={rc}, output: {out!r}")


# ── Case 5 — no arguments → exit 2 ──────────────────────────────────────────

def test_no_arguments_exits_two():
    rc, out = _run([])
    _check("no_arguments: exits 2", rc == 2, f"got rc={rc}, output: {out!r}")


# ── Case 6 — a report path that cannot be read → exit 2, no table ───────────

def test_unreadable_report_exits_two():
    with tempfile.TemporaryDirectory() as tmp:
        os.mkdir(os.path.join(tmp, "tsan.1"))
        rc, out = _run([tmp])
        _check("unreadable_report: exits 2", rc == 2, f"got rc={rc}, output: {out!r}")
        _check("unreadable_report: cannot read line",
               "tsan_summarise: cannot read" in out, f"output: {out!r}")
        _check("unreadable_report: no table", TABLE_MARK not in out, f"output: {out!r}")


# ── Case 7 — --github with a finding → ::error::, job summary written ───────

def test_github_finding_is_error():
    with tempfile.TemporaryDirectory() as tmp:
        _write(tmp, "r/tsan.104", RACE_REPORT)
        summary = os.path.join(tmp, "summary.md")
        rc, out = _run(["--github", os.path.join(tmp, "r")], summary=summary)
        _check("github_finding: ::error:: annotation", "::error::" in out,
               f"output: {out!r}")
        text = ""
        if os.path.isfile(summary):
            with open(summary) as fh:
                text = fh.read()
        _check("github_finding: job summary heading",
               "## ThreadSanitizer inventory" in text, f"summary: {text!r}")


# ── Case 8 — --github with a located abort → ::error file= ──────────────────

def test_github_abort_is_error():
    with tempfile.TemporaryDirectory() as tmp:
        _write(tmp, "tsan.105", CHECK_ABORT)
        rc, out = _run(["--github", tmp])
        _check("github_abort: ::error file= annotation", "::error file=" in out,
               f"output: {out!r}")


# ── Case 9 — --github, empty directory → exit 0, no ::error ─────────────────

def test_github_empty_directory():
    with tempfile.TemporaryDirectory() as tmp:
        rc, out = _run(["--github", tmp])
        _check("github_empty_directory: exits 0", rc == 0, f"got rc={rc}, output: {out!r}")
        _check("github_empty_directory: no ::error", "::error" not in out,
               f"output: {out!r}")


# ── Case 10 — a path that does not exist → exit 2, no inventory ─────────────

def test_missing_directory_exits_two():
    with tempfile.TemporaryDirectory() as tmp:
        missing = os.path.join(tmp, "does-not-exist")
        rc, out = _run([missing])
        _check("missing_directory: exits 2", rc == 2, f"got rc={rc}, output: {out!r}")
        _check("missing_directory: not a directory line",
               "tsan_summarise: not a directory" in out, f"output: {out!r}")
        _check("missing_directory: no table", TABLE_MARK not in out, f"output: {out!r}")
        summary = os.path.join(tmp, "summary.md")
        rc, out = _run(["--github", missing], summary=summary)
        _check("missing_directory --github: exits 2", rc == 2, f"got rc={rc}")
        _check("missing_directory --github: no job summary written",
               not os.path.exists(summary), "summary file was created")


# ── Case 11 — a regular file given as the directory → exit 2 ────────────────

def test_regular_file_exits_two():
    with tempfile.TemporaryDirectory() as tmp:
        path = os.path.join(tmp, "plain")
        _write(tmp, "plain", RACE_REPORT)
        rc, out = _run([path])
        _check("regular_file: exits 2", rc == 2, f"got rc={rc}, output: {out!r}")
        _check("regular_file: not a directory line",
               "tsan_summarise: not a directory" in out, f"output: {out!r}")
        _check("regular_file: no table", TABLE_MARK not in out, f"output: {out!r}")


# ── Case 12 — a directory that cannot be listed → exit 2 ────────────────────

def test_unlistable_directory_exits_two():
    if os.geteuid() == 0:
        print("SKIP  unlistable_directory  (running as root)")
        return
    with tempfile.TemporaryDirectory() as tmp:
        locked = os.path.join(tmp, "locked")
        _write(tmp, "locked/tsan.106", RACE_REPORT)
        os.chmod(locked, 0)
        try:
            rc, out = _run([locked])
        finally:
            os.chmod(locked, 0o755)
        _check("unlistable_directory: exits 2", rc == 2, f"got rc={rc}, output: {out!r}")
        _check("unlistable_directory: cannot read line",
               "tsan_summarise: cannot read" in out, f"output: {out!r}")
        _check("unlistable_directory: no table", TABLE_MARK not in out, f"output: {out!r}")


# ── Case 13 — an unknown option → exit 2 with the usage line ────────────────

def test_unknown_option_exits_two():
    with tempfile.TemporaryDirectory() as tmp:
        rc, out = _run(["--bogus", tmp])
        _check("unknown_option: exits 2", rc == 2, f"got rc={rc}, output: {out!r}")
        _check("unknown_option: usage line", "usage: tsan_summarise.py" in out,
               f"output: {out!r}")
        rc, out = _run(["--github", "--bogus", tmp])
        _check("unknown_option with --github: exits 2", rc == 2,
               f"got rc={rc}, output: {out!r}")


# ── Case 14 — the workflow uploads the reports and the recorded test output ─

def test_workflow_preserves_evidence():
    name = "workflow_preserves_evidence"
    if not os.path.isfile(WORKFLOW):
        _check(f"{name}: ci.yml found", False, WORKFLOW)
        return
    with open(WORKFLOW) as fh:
        lines = fh.read().splitlines()
    start = next((i for i, l in enumerate(lines)
                  if l.strip() == "- name: Upload TSan reports"), None)
    if start is None:
        _check(f"{name}: upload step found", False, "no 'Upload TSan reports' step")
        return
    end = next((i for i in range(start + 1, len(lines))
                if lines[i].startswith("      - ")), len(lines))
    seg = lines[start:end]
    text = "\n".join(seg)
    _check(f"{name}: if: always()", "if: always()" in text, text)
    _check(f"{name}: uploads build_tsan/tsan.*", "build_tsan/tsan.*" in text, text)
    _check(f"{name}: uploads LastTest.log",
           "build_tsan/Testing/Temporary/LastTest.log" in text, text)

    def indent(l):
        return len(l) - len(l.lstrip(" "))
    with_l = [l for l in seg if l.strip() == "with:"]
    uses_l = [l for l in seg if l.strip() == "uses: actions/upload-artifact@v4"]
    _check(f"{name}: with: aligned with uses:",
           len(with_l) == 1 and len(uses_l) == 1
           and indent(with_l[0]) == indent(uses_l[0]), text)


# ── Entry point ──────────────────────────────────────────────────────────────

if __name__ == "__main__":
    for t in (test_empty_directory_exits_zero,
              test_data_race_exits_one,
              test_check_failed_abort_exits_one,
              test_handled_crash_exits_one,
              test_startup_abort_in_last_test_log,
              test_no_arguments_exits_two,
              test_unreadable_report_exits_two,
              test_github_finding_is_error,
              test_github_abort_is_error,
              test_github_empty_directory,
              test_missing_directory_exits_two,
              test_regular_file_exits_two,
              test_unlistable_directory_exits_two,
              test_unknown_option_exits_two,
              test_workflow_preserves_evidence):
        _test(t)

    print()
    if _failed:
        print(f"FAILED: {len(_failed)} check(s) in {_run_count} tests: "
              f"{', '.join(_failed)}")
        sys.exit(1)
    print(f"All {_run_count} tests passed.")
    sys.exit(0)
