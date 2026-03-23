#!/usr/bin/env python3
"""
tools/test_boundary_checker.py — TDD tests for boundary_checker.py

Verifies that boundary_checker.py:
  - Exits 1 and reports VIOLATION when a cross-layer include is found.
  - Exits 0 when all includes are within each layer's allowed set.
  - Allows same-layer includes unconditionally.
  - Allows platform/posix/ to include from osal/posix/.
  - Correctly resolves relative path includes (../../...).
  - hal/*/ files may only include core/include/hal/ contracts and C stdlib.
  - hal/*/ files must not include framework headers or cross-platform HAL headers.
  - drivers/ files may include core/include/ (incl. hal/ contracts) and C stdlib.
  - drivers/ files must not reach into hal/posix/, hal/esp32/ etc. or vendor BSP headers.

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


# ── Test 7 — hal/posix/ may include core/include/hal/ contracts ──────────────

def test_hal_impl_may_include_hal_contract():
    with tempfile.TemporaryDirectory() as tmp:
        _write(tmp, "core/include/hal/hal_timer.h", "/* stub */\n")
        _write(tmp, "hal/posix/hal_timer_posix.c",
               '#include "../../core/include/hal/hal_timer.h"\n')
        rc, out = _run(tmp)
        _check("hal_impl_may_include_hal_contract: exits 0",
               rc == 0, f"got rc={rc}. output: {out!r}")


# ── Test 8 — hal/posix/ may include C stdlib (angle-bracket) ─────────────────

def test_hal_impl_may_include_stdlib():
    with tempfile.TemporaryDirectory() as tmp:
        _write(tmp, "hal/posix/hal_uart_posix.c",
               '#include <stdint.h>\n')
        rc, out = _run(tmp)
        _check("hal_impl_may_include_stdlib: exits 0",
               rc == 0, f"got rc={rc}. output: {out!r}")


# ── Test 9 — hal/posix/ must NOT include core framework headers ───────────────

def test_hal_impl_must_not_include_core_framework():
    with tempfile.TemporaryDirectory() as tmp:
        _write(tmp, "core/include/embediq_bus.h",  "/* stub */\n")
        _write(tmp, "hal/posix/hal_timer_posix.c",
               '#include "embediq_bus.h"\n')
        rc, out = _run(tmp)
        _check("hal_impl_must_not_include_core_framework: exits 1",
               rc == 1, f"got rc={rc}")
        _check("hal_impl_must_not_include_core_framework: prints VIOLATION",
               "VIOLATION" in out, f"output: {out!r}")


# ── Test 10 — hal/posix/ must NOT include from hal/esp32/ ────────────────────

def test_hal_impl_must_not_include_other_platform_hal():
    with tempfile.TemporaryDirectory() as tmp:
        _write(tmp, "hal/esp32/hal_timer_esp32.h", "/* stub */\n")
        _write(tmp, "hal/posix/hal_timer_posix.c",
               '#include "../esp32/hal_timer_esp32.h"\n')
        rc, out = _run(tmp)
        _check("hal_impl_must_not_include_other_platform_hal: exits 1",
               rc == 1, f"got rc={rc}")
        _check("hal_impl_must_not_include_other_platform_hal: prints VIOLATION",
               "VIOLATION" in out, f"output: {out!r}")


# ── Test 11 — drivers/ may include core/include/ framework headers ────────────

def test_drivers_may_include_core():
    with tempfile.TemporaryDirectory() as tmp:
        _write(tmp, "core/include/embediq_fb.h", "/* stub */\n")
        _write(tmp, "drivers/fb_timer.c",
               '#include "embediq_fb.h"\n')
        rc, out = _run(tmp)
        _check("drivers_may_include_core: exits 0",
               rc == 0, f"got rc={rc}. output: {out!r}")


# ── Test 12 — drivers/ may include core/include/hal/ contracts ───────────────

def test_drivers_may_include_hal_contract():
    with tempfile.TemporaryDirectory() as tmp:
        _write(tmp, "core/include/hal/hal_timer.h", "/* stub */\n")
        _write(tmp, "drivers/fb_timer.c",
               '#include "../core/include/hal/hal_timer.h"\n')
        rc, out = _run(tmp)
        _check("drivers_may_include_hal_contract: exits 0",
               rc == 0, f"got rc={rc}. output: {out!r}")


# ── Test 13 — drivers/ must NOT include hal/posix/ (impl, not contract) ──────

def test_drivers_must_not_include_hal_impl():
    with tempfile.TemporaryDirectory() as tmp:
        _write(tmp, "hal/posix/hal_timer_posix.h", "/* stub */\n")
        _write(tmp, "drivers/fb_timer.c",
               '#include "../hal/posix/hal_timer_posix.h"\n')
        rc, out = _run(tmp)
        _check("drivers_must_not_include_hal_impl: exits 1",
               rc == 1, f"got rc={rc}")
        _check("drivers_must_not_include_hal_impl: prints VIOLATION",
               "VIOLATION" in out, f"output: {out!r}")


# ── Test 14 — drivers/ must NOT include vendor BSP headers ───────────────────

def test_drivers_must_not_include_vendor_bsp():
    with tempfile.TemporaryDirectory() as tmp:
        # esp_uart.h is not created anywhere — it is an unknown/vendor header
        _write(tmp, "drivers/fb_uart.c",
               '#include "esp_uart.h"\n')
        rc, out = _run(tmp)
        _check("drivers_must_not_include_vendor_bsp: exits 1",
               rc == 1, f"got rc={rc}")
        _check("drivers_must_not_include_vendor_bsp: prints VIOLATION",
               "VIOLATION" in out, f"output: {out!r}")


# ── Entry point ──────────────────────────────────────────────────────────────

if __name__ == "__main__":
    # Accept and ignore -v flag (output is always verbose)
    test_violation_components_includes_osal()
    test_violation_core_src_includes_platform()
    test_clean_repo_exits_zero()
    test_platform_posix_can_include_osal_posix()
    test_same_layer_include_allowed()
    test_violation_examples_includes_osal()
    test_hal_impl_may_include_hal_contract()
    test_hal_impl_may_include_stdlib()
    test_hal_impl_must_not_include_core_framework()
    test_hal_impl_must_not_include_other_platform_hal()
    test_drivers_may_include_core()
    test_drivers_may_include_hal_contract()
    test_drivers_must_not_include_hal_impl()
    test_drivers_must_not_include_vendor_bsp()

    print()
    if _failed:
        print(f"FAILED: {len(_failed)} test(s): {', '.join(_failed)}")
        sys.exit(1)
    else:
        print("All 14 tests passed.")
        sys.exit(0)
