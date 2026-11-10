#!/usr/bin/env python3
"""
tools/test_boundary_checker.py — TDD tests for boundary_checker.py

Verifies that boundary_checker.py:
  - Exits 1 and reports VIOLATION when a cross-layer include is found.
  - Exits 0 when all includes are within each layer's allowed set.
  - Allows same-layer includes unconditionally.
  - Correctly resolves relative path includes (../../...).
  - hal/*/ files may include core/include/hal/ contracts, core/include/ops/ ops
    tables and embediq_config.h, embediq_obs.h, embediq_platform.h; every other
    core/include/ header is rejected, except the provisioning HAL's two named
    configuration and NVM headers.
  - fbs/drivers/ files may include core/include/ (incl. hal/ contracts) and C stdlib.
  - fbs/drivers/ files must not reach into hal/posix/, hal/esp32/ etc. or vendor BSP headers.
  - fbs/services/ files may include core/include/ framework headers and C stdlib.
  - fbs/services/ files must NOT include core/include/hal/ or any hal/ path.

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
        _write(tmp, "components/fb_sensor/fb_sensor.h", "/* stub */\n")
        _write(tmp, "components/fb_sensor/fb_sensor.c",
               '#include "fb_sensor.h"\n'
               '#include <stdint.h>\n')
        _write(tmp, "core/src/bus/message_bus.c",
               '#include "embediq_osal.h"\n'
               '#include "embediq_fb.h"\n')
        rc, out = _run(tmp)
        _check("clean_repo_exits_zero: exits 0",
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


# ── Test 11 — fbs/drivers/ may include core/include/ framework headers ────────

def test_drivers_may_include_core():
    with tempfile.TemporaryDirectory() as tmp:
        _write(tmp, "core/include/embediq_fb.h", "/* stub */\n")
        _write(tmp, "fbs/drivers/fb_timer.c",
               '#include "embediq_fb.h"\n')
        rc, out = _run(tmp)
        _check("drivers_may_include_core: exits 0",
               rc == 0, f"got rc={rc}. output: {out!r}")


# ── Test 12 — fbs/drivers/ may include core/include/hal/ contracts ───────────

def test_drivers_may_include_hal_contract():
    with tempfile.TemporaryDirectory() as tmp:
        _write(tmp, "core/include/hal/hal_timer.h", "/* stub */\n")
        _write(tmp, "fbs/drivers/fb_timer.c",
               '#include "../../core/include/hal/hal_timer.h"\n')
        rc, out = _run(tmp)
        _check("drivers_may_include_hal_contract: exits 0",
               rc == 0, f"got rc={rc}. output: {out!r}")


# ── Test 13 — fbs/drivers/ must NOT include hal/posix/ (impl, not contract) ──

def test_drivers_must_not_include_hal_impl():
    with tempfile.TemporaryDirectory() as tmp:
        _write(tmp, "hal/posix/hal_timer_posix.h", "/* stub */\n")
        _write(tmp, "fbs/drivers/fb_timer.c",
               '#include "../../hal/posix/hal_timer_posix.h"\n')
        rc, out = _run(tmp)
        _check("drivers_must_not_include_hal_impl: exits 1",
               rc == 1, f"got rc={rc}")
        _check("drivers_must_not_include_hal_impl: prints VIOLATION",
               "VIOLATION" in out, f"output: {out!r}")


# ── Test 14 — fbs/drivers/ must NOT include vendor BSP headers ───────────────

def test_drivers_must_not_include_vendor_bsp():
    with tempfile.TemporaryDirectory() as tmp:
        # esp_uart.h is not created anywhere — it is an unknown/vendor header
        _write(tmp, "fbs/drivers/fb_uart.c",
               '#include "esp_uart.h"\n')
        rc, out = _run(tmp)
        _check("drivers_must_not_include_vendor_bsp: exits 1",
               rc == 1, f"got rc={rc}")
        _check("drivers_must_not_include_vendor_bsp: prints VIOLATION",
               "VIOLATION" in out, f"output: {out!r}")


# ── Test 15 — fbs/services/ may include core/include/ framework headers ──────

def test_fbs_services_may_include_core_framework():
    with tempfile.TemporaryDirectory() as tmp:
        _write(tmp, "core/include/embediq_bus.h", "/* stub */\n")
        _write(tmp, "fbs/services/fb_cloud_mqtt.c",
               '#include "embediq_bus.h"\n')
        rc, out = _run(tmp)
        _check("fbs_services_may_include_core_framework: exits 0",
               rc == 0, f"got rc={rc}. output: {out!r}")


# ── Test 16 — fbs/services/ must NOT include core/include/hal/ contracts ─────

def test_fbs_services_must_not_include_hal_contract():
    with tempfile.TemporaryDirectory() as tmp:
        _write(tmp, "core/include/hal/hal_timer.h", "/* stub */\n")
        _write(tmp, "fbs/services/fb_cloud_mqtt.c",
               '#include "../../core/include/hal/hal_timer.h"\n')
        rc, out = _run(tmp)
        _check("fbs_services_must_not_include_hal_contract: exits 1",
               rc == 1, f"got rc={rc}")
        _check("fbs_services_must_not_include_hal_contract: prints VIOLATION",
               "VIOLATION" in out, f"output: {out!r}")


# ── HAL allow set, header-level rule and the scoped exception ───────────────

HAL_CONTRACT = "core/include/hal/hal_timer.h"


def _tree(tmp, files):
    for rel, content in files.items():
        _write(tmp, rel, content)


def _expect(name, files, want_rc, violating=None):
    """Run the checker on a tree; assert the exit code and, when a violation is
    expected, a VIOLATION line that names the violating file."""
    with tempfile.TemporaryDirectory() as tmp:
        _tree(tmp, files)
        rc, out = _run(tmp)
        _check(f"{name}: exits {want_rc}", rc == want_rc,
               f"got rc={rc}. output: {out!r}")
        if violating:
            _check(f"{name}: VIOLATION names {violating}",
                   f"VIOLATION: {violating}:" in out, f"output: {out!r}")


def test_hal_allowed_core_headers():
    _expect("hal_allowed_core_headers", {
        "core/include/embediq_config.h":   "/* stub */\n",
        "core/include/embediq_obs.h":      "/* stub */\n",
        "core/include/embediq_platform.h": "/* stub */\n",
        "core/include/ops/embediq_tls.h":  "/* stub */\n",
        HAL_CONTRACT:                      "/* stub */\n",
        "hal/posix/x.c": '#include "embediq_config.h"\n'
                         '#include "embediq_obs.h"\n'
                         '#include "embediq_platform.h"\n'
                         '#include "ops/embediq_tls.h"\n'
                         '#include "hal_timer.h"\n',
    }, 0)


def test_hal_posix_framework_header_rejected():
    _expect("hal_posix_framework_header_rejected", {
        "core/include/embediq_bus.h": "/* stub */\n",
        "hal/posix/x.c": '#include "embediq_bus.h"\n',
    }, 1, "hal/posix/x.c")


def test_hal_freertos_framework_header_rejected():
    _expect("hal_freertos_framework_header_rejected", {
        "core/include/embediq_bus.h": "/* stub */\n",
        "hal/freertos/x.c": '#include "embediq_bus.h"\n',
    }, 1, "hal/freertos/x.c")


def test_hal_relative_path_framework_header_rejected():
    _expect("hal_relative_path_framework_header_rejected", {
        "core/include/embediq_bus.h": "/* stub */\n",
        "hal/posix/x.c": '#include "../../core/include/embediq_bus.h"\n',
    }, 1, "hal/posix/x.c")


def test_provisioning_hal_exception():
    _expect("provisioning_hal_exception", {
        "core/include/embediq_cfg.h": "/* stub */\n",
        "core/include/embediq_nvm.h": "/* stub */\n",
        "hal/posix/ops/hal_provisioning_posix.c": '#include "embediq_cfg.h"\n'
                                                  '#include "embediq_nvm.h"\n',
    }, 0)


def test_exception_is_file_scoped():
    _expect("exception_is_file_scoped", {
        "core/include/embediq_cfg.h": "/* stub */\n",
        "hal/posix/x.c": '#include "embediq_cfg.h"\n',
    }, 1, "hal/posix/x.c")


def test_hal_ops_header_by_bare_name():
    _expect("hal_ops_header_by_bare_name", {
        "core/include/ops/embediq_tls.h": "/* stub */\n",
        "hal/posix/x.c": '#include "embediq_tls.h"\n',
    }, 0)


# ── Functional Block, OSAL, core and components layers ──────────────────────

def test_services_hal_contract_path_form_rejected():
    _expect("services_hal_contract_path_form_rejected", {
        HAL_CONTRACT: "/* stub */\n",
        "fbs/services/x.c": '#include "hal/hal_timer.h"\n',
    }, 1, "fbs/services/x.c")


def test_drivers_hal_contract_path_form_allowed():
    _expect("drivers_hal_contract_path_form_allowed", {
        HAL_CONTRACT: "/* stub */\n",
        "fbs/drivers/x.c": '#include "hal/hal_timer.h"\n',
    }, 0)


def test_bridge_hal_contract_rejected():
    _expect("bridge_hal_contract_rejected", {
        HAL_CONTRACT: "/* stub */\n",
        "fbs/bridge/x.c": '#include "hal_timer.h"\n',
    }, 1, "fbs/bridge/x.c")


def test_osal_hal_contract_rejected():
    _expect("osal_hal_contract_rejected", {
        HAL_CONTRACT: "/* stub */\n",
        "osal/posix/x.c": '#include "hal_timer.h"\n',
    }, 1, "osal/posix/x.c")


def test_build_directories_skipped():
    with tempfile.TemporaryDirectory() as tmp:
        _tree(tmp, {
            "core/include/embediq_bus.h": "/* stub */\n",
            "hal/posix/build_tsan/x.c": '#include "embediq_bus.h"\n',
        })
        rc, out = _run(tmp)
        _check("build_directories_skipped: exits 0", rc == 0,
               f"got rc={rc}. output: {out!r}")
        _check("build_directories_skipped: file not reported",
               "build_tsan" not in out, f"output: {out!r}")


def test_core_src_hal_contract_rejected():
    _expect("core_src_hal_contract_rejected", {
        HAL_CONTRACT: "/* stub */\n",
        "core/src/x.c": '#include "hal_timer.h"\n',
    }, 1, "core/src/x.c")


def test_components_framework_header_rejected():
    _expect("components_framework_header_rejected", {
        "core/include/embediq_fb.h": "/* stub */\n",
        "components/c/c.c": '#include "embediq_fb.h"\n',
    }, 1, "components/c/c.c")


def test_examples_bridge_header_rejected():
    _expect("examples_bridge_header_rejected", {
        "fbs/bridge/fb_bridge.h": "/* stub */\n",
        "examples/x/main.c": '#include "fb_bridge.h"\n',
    }, 1, "examples/x/main.c")


def test_bridge_unresolved_include_rejected():
    _expect("bridge_unresolved_include_rejected", {
        "fbs/bridge/x.c": '#include "vendor_bsp.h"\n',
    }, 1, "fbs/bridge/x.c")


# ── Generated-header identity: decided by location, never by name ───────────

GEN_TREE = {
    "generated/embediq_msg_catalog.h":       "/* generated */\n",
    "third_party/bsp/embediq_msg_catalog.h": "/* vendor */\n",
}


def test_generated_header_identity():
    for layer in ("fbs/services", "fbs/drivers", "fbs/bridge"):
        src = f"{layer}/x.c"
        _expect(f"generated_bare[{layer}]", dict(GEN_TREE, **{
            src: '#include "embediq_msg_catalog.h"\n'}), 0)
        _expect(f"generated_relative[{layer}]", dict(GEN_TREE, **{
            src: '#include "../../generated/embediq_msg_catalog.h"\n'}), 0)
        _expect(f"vendor_with_generated_name[{layer}]", dict(GEN_TREE, **{
            src: '#include "../../third_party/bsp/embediq_msg_catalog.h"\n'}),
            1, src)
        _expect(f"unresolved_with_generated_name[{layer}]", dict(GEN_TREE, **{
            src: '#include "../nowhere/embediq_msg_catalog.h"\n'}), 1, src)
    _expect("hal_vendor_include_ignored", dict(GEN_TREE, **{
        "hal/posix/x.c": '#include "../../third_party/bsp/embediq_msg_catalog.h"\n'}),
        0)


# ── Robustness: unreadable files fail; success prints one OK line ───────────

def test_unreadable_file_fails():
    if os.geteuid() == 0:
        print("SKIP  unreadable_file_fails  (running as root)")
        return
    with tempfile.TemporaryDirectory() as tmp:
        _write(tmp, "core/src/x.c", '#include <stdint.h>\n')
        path = os.path.join(tmp, "core", "src", "x.c")
        os.chmod(path, 0)
        try:
            rc, out = _run(tmp)
        finally:
            os.chmod(path, 0o644)
        _check("unreadable_file_fails: exits 1", rc == 1, f"got rc={rc}. output: {out!r}")
        _check("unreadable_file_fails: cannot read line",
               "VIOLATION: core/src/x.c: cannot read" in out, f"output: {out!r}")


def test_clean_fixture_ok_line():
    with tempfile.TemporaryDirectory() as tmp:
        _tree(tmp, {
            "core/include/embediq_fb.h": "/* stub */\n",
            "core/src/a.c":              '#include "embediq_fb.h"\n',
            "examples/x/main.c":         '#include "embediq_fb.h"\n',
        })
        rc, out = _run(tmp)
        _check("clean_fixture_ok_line: exits 0", rc == 0, f"got rc={rc}. output: {out!r}")
        _check("clean_fixture_ok_line: OK line with file count",
               out.strip() == "boundary_checker.py: OK — 2 file(s) checked, 0 violations.",
               f"output: {out!r}")


# ── Entry point ──────────────────────────────────────────────────────────────

if __name__ == "__main__":
    tests = [
        test_violation_components_includes_osal,
        test_violation_core_src_includes_platform,
        test_clean_repo_exits_zero,
        test_same_layer_include_allowed,
        test_violation_examples_includes_osal,
        test_hal_impl_may_include_hal_contract,
        test_hal_impl_may_include_stdlib,
        test_hal_impl_must_not_include_core_framework,
        test_hal_impl_must_not_include_other_platform_hal,
        test_drivers_may_include_core,
        test_drivers_may_include_hal_contract,
        test_drivers_must_not_include_hal_impl,
        test_drivers_must_not_include_vendor_bsp,
        test_fbs_services_may_include_core_framework,
        test_fbs_services_must_not_include_hal_contract,
        test_hal_allowed_core_headers,
        test_hal_posix_framework_header_rejected,
        test_hal_freertos_framework_header_rejected,
        test_hal_relative_path_framework_header_rejected,
        test_provisioning_hal_exception,
        test_exception_is_file_scoped,
        test_hal_ops_header_by_bare_name,
        test_services_hal_contract_path_form_rejected,
        test_drivers_hal_contract_path_form_allowed,
        test_bridge_hal_contract_rejected,
        test_osal_hal_contract_rejected,
        test_build_directories_skipped,
        test_core_src_hal_contract_rejected,
        test_components_framework_header_rejected,
        test_examples_bridge_header_rejected,
        test_bridge_unresolved_include_rejected,
        test_generated_header_identity,
        test_unreadable_file_fails,
        test_clean_fixture_ok_line,
    ]
    for t in tests:
        t()

    print()
    if _failed:
        print(f"FAILED: {len(_failed)} check(s) in {len(tests)} tests: "
              f"{', '.join(_failed)}")
        sys.exit(1)
    else:
        print(f"All {len(tests)} tests passed.")
        sys.exit(0)
