# EmbedIQ CLI — Command Specification

> **Status:** Contract frozen Phase 2. Implementation target: Phase 2.
> This document specifies the `embediq` command-line tool that will ship
> alongside the framework. No implementation exists yet.

---

## Overview

The `embediq` CLI is the developer-facing entry point for EmbedIQ projects.
It handles project scaffolding, component management, building, flashing,
monitoring, and validation in a single tool — no separate scripts needed.

---

## Commands

### `embediq init <project-name>`

Scaffold a new EmbedIQ project in a directory named `<project-name>`.

Creates:
```
<project-name>/
  CMakeLists.txt          — pre-wired for EmbedIQ framework
  embediq.toml            — project config (see Config file below)
  AGENTS.md               — project-specific agents stub
  src/
    fb_hello_world.c      — minimal example FB
    main.c                — calls embediq_engine_boot() + dispatch_boot()
```

### `embediq add <fb-name>`

Add a Functional Block from the EmbedIQ component registry to the project.

- Downloads the FB source and its contract headers.
- Places source in `components/<fb-name>/`.
- Updates `CMakeLists.txt` with the new target.
- Updates `embediq.toml` `[fbs]` table.

Example: `embediq add fb_nvm`

### `embediq build [--target host|esp32|stm32] [--type Debug|Release]`

Build the project for the specified target.

| Flag       | Default | Description                                          |
| ---------- | ------- | ---------------------------------------------------- |
| `--target` | `host`  | Build target: `host` (macOS/Linux), `esp32`, `stm32` |
| `--type`   | `Debug` | CMake build type: `Debug` or `Release`               |

Internally runs:
```
cmake -B build -DCMAKE_BUILD_TYPE=<type> -DEMBEDIQ_TARGET=<target>
cmake --build build -j$(nproc)
```

### `embediq flash --target esp32 --port /dev/cu.usbserial-XXXX`

Flash firmware to connected hardware.

| Flag       | Required | Description                         |
| ---------- | -------- | ----------------------------------- |
| `--target` | yes      | Hardware target: `esp32` or `stm32` |
| `--port`   | yes      | Serial port path                    |

- ESP32: uses `esptool.py` via `idf.py flash`
- STM32: uses OpenOCD

### `embediq monitor [--port /dev/cu.usbserial-XXXX] [--level 0|1|2]`

Connect to the Observatory event stream.

| Flag      | Default | Description                                          |
| --------- | ------- | ---------------------------------------------------- |
| `--port`  | stdout  | Serial port (hardware) or stdout pipe (host)         |
| `--level` | `1`     | Observatory verbosity: 0=silent, 1=normal, 2=verbose |

- **Host build:** reads from the process stdout pipe of the running firmware.
- **Hardware:** reads from the target UART at the Observatory baud rate.

Output is a live, human-readable event stream. Press Ctrl-C to exit.

Example output:
```
[  0.001s] BOOT    fb_timer         phase=PLATFORM
[  0.003s] BOOT    fb_watchdog      phase=INFRASTRUCTURE
[  1.000s] MSG     fb_temp_sensor   → fb_temp_controller  MSG_TEMP_READING  seq=1
[  1.001s] FSM     fb_temp_ctrl     state_0 → state_1
```

### `embediq validate`

Run all static checks on the project.

Runs in order:
1. `python3 tools/validator.py` — no hardcoded sizing constants
2. `python3 tools/boundary_checker.py` — no layer boundary violations

Exit 0 only if **both** pass. Any failure prints the checker output and
exits 1 with a descriptive error message.

---

## Config file: `embediq.toml`

Located at the project root. Created by `embediq init`.

```toml
[project]
name   = "my-project"
target = "host"          # default build target

[fbs]
fb_nvm    = "1.0"        # component name = semver
fb_timer  = "1.0"
```

`embediq add` appends to `[fbs]` automatically.
`embediq build` reads `target` as the default if `--target` is not passed.

---

## Notes

- The CLI is a thin orchestration layer. It does not embed build logic —
  it delegates to CMake, esptool, OpenOCD, and the Python tools.
- All commands exit 0 on success, non-zero on failure.
- `--help` is supported on every command.

---

*EmbedIQ — embediq.com — Apache 2.0*
