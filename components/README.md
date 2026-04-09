# EmbedIQ Components

Components are pure, self-contained utility libraries that any EmbedIQ project
can use without modification. They are not Function Blocks and they are not
registered with the framework. They are dependency-free building blocks.

## Component Rules (C1–C6)

Every component in this directory must satisfy all six rules.
CI enforces C1, C2, C3, C4. Violations fail the build.

| Rule | Requirement | Enforced by |
|------|-------------|-------------|
| C1 | Only `<stdint.h>`, `<stddef.h>`, `<stdbool.h>`, `<string.h>` may be included. Zero EmbedIQ framework headers (`embediq_*.h`). Zero platform headers. | boundary_checker.py |
| C2 | Zero global mutable variables. All state is caller-allocated or computed from inputs. | component_globals_check.py |
| C3 | License must be Apache-2.0, MIT, BSD-2-Clause, BSD-3-Clause, ISC, or zlib. No GPL. No LGPL. | licence_check.py |
| C4 | Every component must have a unit test (`test_<name>.c`) that is wired into ctest. | CI: build fails without test |
| C5 | If a component wraps a `third_party/` library, FBs must include only the component header — never the third-party header directly. | boundary_checker.py |
| C6 | Component state structs (`*_state_t`, `*_ctx_t`) must live in `fb_data` when used inside an FB — not as local stack variables inside handler functions. | component_state_check.py |

## C2 Note — Lookup Tables

A table-driven component (CRC, hash) requires one-time table population. This
is the only permitted exception to C2: a file-scope `static` table and a
`static int ready` flag are allowed. The table must be read-only after first
population. For ROM-constrained targets, pre-generate the table at compile time
and declare it `const` — no flag needed, no C2 exception required.

## How to Add a New Component

1. Create `components/<name>/` directory.
2. Write `<name>.h` — C1 compliant (stdlib headers only).
3. Write `<name>.c` — C2 compliant (no global mutable state, or table exception above).
4. Write `test_<name>.c` — verify against published standard test vectors where they exist.
5. Add `LICENSE` — must be C3 compliant.
6. Wire into `CMakeLists.txt` (component library + test executable).
7. Run boundary_checker.py and confirm zero violations.

## Published Components

| Component | Variants | Standard vectors |
|-----------|----------|-----------------|
| `embediq_crc` | CRC-8, CRC-16/CCITT-FALSE, CRC-32/ISO-HDLC | crccalc.com — all three pass |
