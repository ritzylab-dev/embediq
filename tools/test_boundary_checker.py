#!/usr/bin/env python3
"""
tools/test_boundary_checker.py — TDD tests for boundary_checker.py

Verifies that boundary_checker.py:
  - Exits 1 and reports VIOLATION when a cross-layer include is found.
  - Exits 0 when all includes are within each layer's allowed set.
  - Allows same-layer includes unconditionally.
  - Allows platform/posix/ to include from osal/posix/.
  - Correctly resolves relative path includes (../../...).

Run: python3 tools/test_boundary_checker.py
All output lines start with PASS or FAIL. Exit 0 iff all pass.
"""

import os
import sys
import subprocess
import tempfile

CHECKER = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                       "boundary_checker.py")

_failed = []


def _write(base, rel_path, content):
    full = os.path.join(base, *rel_path.replace('/', os.sep).split(os.sep))
    os.makedirs(os.path.dirname(full), exist_ok=True)
    with open(full, 'w') as fh:
        fh.write(content)


def _run(repo_root):
    result = subprocess.run(
        [sys.executable, CHECKER, repo_root],
        capture_output=True, text=True
    )
    return result.returncode, result.stdout + result.stderr


def _check(name, condition, detail=""):
    if condition:
        print(f"PASS: {name}")
    else:
        print(f"FAIL: {name}" + (f" — {detail}" if detail else ""))
        _failed.append(name)


# ── Test 1 — components/ including from osal/posix/ is a violation ──────────

def test_violation_components_includes_osal():
    with tempfile.TemporaryDirectory() as tmp:
        _write(tmp, "core/include/embediq_fb.h",       "/* stub */\n")
        _write(tmp, "osal/posix/embediq_osal_posix.h", "/* stub */\n")
        _write(tmp, "components/fb_sensor/fb_sensor.c",
               '#include "../../osal/posix/embediq_osal_posix.h"\n'
               '#include "embediq_fb.h"\n')
        rc, out = _run(tmp)
        _check("violation_components_includes_osal: exits 1",
               rc == 1, f"got rc={rc}")
        _check("violation_components_includes_osal: prints VIOLATION",
               "VIOLATION" in out, f"output: {out!r}")


# ── Test 2 — core/src/ including from platform/posix/ is a violation ────────

def test_violation_core_src_includes_platform():
    with tempfile.TemporaryDirectory() as tmp:
        _write(tmp, "core/include/embediq_fb.h",       "/* stub */\n")
        _write(tmp, "platform/posix/fb_timer.h",       "/* stub */\n")
        _write(tmp, "core/src/registry/fb_engine.c",
               '#include "embediq_fb.h"\n'
               '#include "../../../platform/posix/fb_timer.h"\n')
        rc, out = _run(tmp)
        _check("violation_core_src_includes_platform: exits 1",
               rc == 1, f"got rc={rc}")
        _check("violation_core_src_includes_platform: prints VIOLATION",
               "VIOLATION" in out, f"output: {out!r}")


# ── Test 3 — clean repo exits 0 ─────────────────────────────────────────────

def test_clean_repo_exits_zero():
    with tempfile.TemporaryDirectory() as tmp:
        _write(tmp, "core/include/embediq_fb.h",  "/* stub */\n")
        _write(tmp, "core/include/embediq_osal.h", "/* stub */\n")
        _write(tmp, "components/fb_sensor/fb_sensor.c",
               '#include "embediq_fb.h"\n'
               '#include <stdint.h>\n')
        _write(tmp, "core/src/bus/message_bus.c",
               '#include "embediq_osal.h"\n'
               '#include "embediq_fb.h"\n')
        rc, out = _run(tmp)
        _check("clean_repo_exits_zero: exits 0",
               rc == 0, f"got rc={rc}. output: {out!r}")


# ── Test 4 — platform/posix/ may include osal/posix/ (allowed) ──────────────

def test_platform_posix_can_include_osal_posix():
    with tempfile.TemporaryDirectory() as tmp:
        _write(tmp, "core/include/embediq_osal.h",     "/* stub */\n")
        _write(tmp, "osal/posix/embediq_osal_posix.h", "/* stub */\n")
        _write(tmp, "platform/posix/fb_timer.c",
               '#include "embediq_osal.h"\n'
               '#include "../../osal/posix/embediq_osal_posix.h"\n')
        rc, out = _run(tmp)
        _check("platform_posix_can_include_osal_posix: exits 0",
               rc == 0, f"got rc={rc}. output: {out!r}")


# ── Test 5 — same-layer includes (examples→examples) are allowed ────────────

def test_same_layer_include_allowed():
    with tempfile.TemporaryDirectory() as tmp:
        _write(tmp, "core/include/embediq_fb.h",          "/* stub */\n")
        _write(tmp, "examples/thermostat/thermo_msgs.h",  "/* stub */\n")
        _write(tmp, "examples/thermostat/fb_sensor.c",
               '#include "embediq_fb.h"\n'
               '#include "thermo_msgs.h"\n')
        rc, out = _run(tmp)
        _check("same_layer_include_allowed: exits 0",
               rc == 0, f"got rc={rc}. output: {out!r}")


# ── Test 6 — examples/ must NOT include from osal/ ──────────────────────────

def test_violation_examples_includes_osal():
    with tempfile.TemporaryDirectory() as tmp:
        _write(tmp, "core/include/embediq_fb.h",       "/* stub */\n")
        _write(tmp, "osal/posix/embediq_osal_posix.h", "/* stub */\n")
        _write(tmp, "examples/thermostat/main.c",
               '#include "embediq_fb.h"\n'
               '#include "../../osal/posix/embediq_osal_posix.h"\n')
        rc, out = _run(tmp)
        _check("violation_examples_includes_osal: exits 1",
               rc == 1, f"got rc={rc}")
        _check("violation_examples_includes_osal: prints VIOLATION",
               "VIOLATION" in out, f"output: {out!r}")


# ── Entry point ──────────────────────────────────────────────────────────────

if __name__ == "__main__":
    test_violation_components_includes_osal()
    test_violation_core_src_includes_platform()
    test_clean_repo_exits_zero()
    test_platform_posix_can_include_osal_posix()
    test_same_layer_include_allowed()
    test_violation_examples_includes_osal()

    print()
    if _failed:
        print(f"FAILED: {len(_failed)} test(s): {', '.join(_failed)}")
        sys.exit(1)
    else:
        print(f"All {6 - len(_failed)} tests passed.")
        sys.exit(0)
