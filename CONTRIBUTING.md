# CONTRIBUTING.md — How to Contribute to EmbedIQ

Welcome. Contributions are what make EmbedIQ better for everyone.

**Before you start:** Read [AGENTS.md](AGENTS.md), [ARCHITECTURE.md](ARCHITECTURE.md), and [CODING_RULES.md](CODING_RULES.md). Every PR is reviewed against these documents.

---

## Before your first PR

1. **Sign the CLA.** A bot will prompt you on your first PR. No CLA, no merge. See [COMMERCIAL_BOUNDARY.md](COMMERCIAL_BOUNDARY.md) for why.
2. **Open an issue first** for anything larger than a bug fix. Discuss the design before writing code. Architecture decisions made in PRs are harder to undo than architecture decisions made in issues.
3. **Check the roadmap.** If your contribution is something already planned for a future phase, coordinate — it may already be in progress.

---

## What we welcome

| Contribution type    | Where it goes     | Notes                                             |
| -------------------- | ----------------- | ------------------------------------------------- |
| Bug fixes            | Anywhere          | Always welcome. Small and focused.                |
| New Driver FBs       | `fbs/drivers/` + `hal/<target>/` | Portable FB in `fbs/drivers/`, HAL implementation in `hal/<target>/`. Must follow the five-layer pattern below. |
| New Component FBs    | `components/`     | Must have full host test coverage                 |
| OSAL implementations | `osal/`           | For new RTOS targets (Zephyr, bare-metal, RISC-V) |
| BSP implementations  | `platform/bsp/`   | For new hardware targets                          |
| Example applications | `examples/`       | Must run on host without hardware                 |
| Documentation        | `docs/`           | Corrections, clarifications, translations         |
| messages.iq schemas  | `messages/`       | New message types for shared use                  |

---

## What we do not accept

- Code that violates any binding invariant (I-01 through I-12) — CI will reject it
- Code that requires hardware to test — every contribution must pass host tests
- GPL or LGPL licensed code — Apache 2.0 only in Core/Shell 1/Shell 2
- Dynamic allocation (`malloc`/`free`) in Shell 1 or Core
- Cross-FB function calls — messages only at FB boundaries
- Changes to Core header contracts (`core/include/*.h`) without an architecture decision record

---

## The Five-Layer PR Pattern

Every non-trivial contribution follows this pattern. Each layer gates the next.

```
Layer 1 — Contracts
  Define or update the interface header first.
  Core headers are the source of truth. Everything else implements them.
  Human review required before implementation starts.

Layer 2 — Implementation
  Implement against the contracts from Layer 1.
  No new public API beyond what Layer 1 defined.

Layer 3 — Host Tests
  Tests run on host without hardware. CI runs these on every PR.
  Coverage: normal path, error path, edge cases.
  A contribution with no host tests will not merge.

Layer 4 — Integration
  Run the full example suite with your changes.
  Confirm Observatory output is correct.
  Human review of integration output required.

Layer 5 — Target Tests  (optional for most contributions)
  Flash to real hardware and verify.
  Required for Platform FB contributions and BSP additions.
  Human gate — cannot be automated in CI.
```

---

## AI-generated contributions

AI-assisted contributions are welcome and expected. The framework is designed to be AI-codegen friendly.

**Rules for AI-generated code:**

1. The contributor is responsible for the correctness of AI-generated code — the same as any other code
2. AI-generated code must pass the same five-layer pattern as human-written code
3. If you used an AI agent to generate the code, say so in the PR description
4. The AI coding checklist in [CODING_RULES.md](CODING_RULES.md) must pass before submission

**For best results with AI agents:** Point your agent at [AGENTS.md](AGENTS.md) and [CODING_RULES.md](CODING_RULES.md) at the start of every session. These files are specifically designed as AI agent primers.

---

## Contribution boundary rules

EmbedIQ has a clear public/private boundary. Contributions must respect it.

**In scope for community contributions:**
- Everything in `core/`, `osal/`, `platform/`, `components/`, `examples/`, `tools/`, `docs/`

**Not in scope for community contributions:**
- EmbedIQ Studio codebase (commercial product, closed source)
- EmbedIQ Cloud backend (commercial product, closed source)
- Anything in `studio/` or `cloud/` if present

---

## Style guide

- **Language:** C11 for Core and Shell 1. C++ opt-in wrapper only in Shell 3+.
- **Naming:** `embediq_` prefix for all public API. `fb_` prefix for Functional Block files.
- **Headers:** One interface header per module. One or more implementation files.
- **Comments:** Document the *why*, not the *what*. Code explains what. Comments explain why a decision was made.
- **No magic numbers:** All sizing in `embediq_config.h`. `validator.py` will catch hardcoded values.
- **Formatting:** `clang-format` with the project `.clang-format` file. CI will check.

---

## Review process

1. Open a PR against `dev`
2. CI runs automatically: build, lint, host tests, binary analysis, license check
3. CLA bot checks signature
4. Maintainer reviews within 5 business days for small PRs, 10 for large
5. Changes requested → address them → re-review
6. Approved + CI green → merged

---

## Getting help

- **GitHub Discussions** — architecture questions, design proposals, use case questions
- **GitHub Issues** — bug reports, specific feature requests
- **Discord** — [discord.gg/embediq](https://discord.gg/embediq) — community chat, quick questions

For security vulnerabilities: email security@embediq.com. Do not open a public issue.

---

*Apache 2.0 · embediq.com · © 2026 Ritzy Lab*
