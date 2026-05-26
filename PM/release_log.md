# EmbedIQ — Release Log

## v0.1.2 — 2026-03-26 UTC

- Tag SHA:       007ef632ef96d7a5a19d1a7782572c952cb4aa2d
- main HEAD SHA: 398a52d0cebd4668a424ec080d5fb88bd446a697
- Match:         **MISMATCH** — tag points to old main HEAD (v0.1.1 era), not the PR #65 merge commit
- dev post-release state: 20 commits ahead of v0.1.2 tag (PRs #55–#66 including gate_protocol.md)
- Verified by:   Gate 15 agent pass

**Action required:** Tag v0.1.2 must be deleted and re-applied to the correct commit (398a52d).
Human action:
```
git tag -d v0.1.2 && git push origin :refs/tags/v0.1.2
git checkout main && git pull origin main
git tag -a v0.1.2 -m "v0.1.2 — Phase 1 review fixes (PRs #55–#63)"
git push origin v0.1.2
```
Then re-run Gate 15 to confirm match.

## v0.1.3 — 2026-04-01 UTC

- Tag SHA:       0e33684334b803bc76b851d970afd501608322de
- main HEAD SHA: 0e33684334b803bc76b851d970afd501608322de
- Match:         ✅ CONFIRMED
- dev post-release state: clean — dev equals main at tag commit (0 commits ahead)
- Verified by:   Gate 15 agent pass

Gate evidence:
- PR #74: docs refresh (BUILD_STATUS, HANDOFF, AGENTS, examples, CODING_RULES) → dev ✅
- PR #75: Gate 14B contrib_sim pass — 17/17 fresh clone → dev ✅
- PR #77: merge main into dev — 4 conflict files resolved (dev wins all) → dev ✅
- PR #76: dev→main Phase 1 promotion → main ✅
- Tag v0.1.3 on 0e33684 (Merge PR #76) ✅

What shipped:
- Industrial edge gateway example (6 FBs, edge-to-cloud pipeline)
- Final Decision Set v2.0 (Decisions A–K): safety_class, AI event constants, TLV types, SBOM, compliance table
- Observatory .iqtrace v1.1 — 5 new TLV types, 25 event constants, libembediq_obs INTERFACE target
- AI-first: 4 Phase-1 AI constants (0x17–0x1A), EU AI Act Art.12/13 logging built in
- New docs: COMPLIANCE.md, docs/MIGRATION.md, AI_FIRST_ARCHITECTURE.md

---

## v0.1.2 — CONFIRMED — 2026-03-26 UTC

- Tag SHA:       398a52d0cebd4668a424ec080d5fb88bd446a697
- main HEAD SHA: 398a52d0cebd4668a424ec080d5fb88bd446a697
- Match:         ✅ CONFIRMED — tag corrected by human after Gate 15 mismatch finding
- dev post-release state: PRs #64 #66 #67 ahead of main (process/docs only — no code)
- Verified by:   Gate 15 agent pass — second run

## v0.2.1 — 2026-04-10 UTC

- Tag SHA:       529ec0d3853c77dccff4cb5eecaeea913eb33eab
- main HEAD SHA: 529ec0d3853c77dccff4cb5eecaeea913eb33eab
- Match:         ✅ CONFIRMED
- dev post-release state: clean — dev equals main at tag commit (0 commits ahead)
- Verified by:   Gate 15 agent pass

What shipped (docs-only release):
- PR #101: Gate 15 release log for v0.2.0
- PR #102: Open-core licensing model — five public docs updated
- PR #103: README.md Layer 2 diagram box width fix
- PR #104: Gate 14B contrib_sim 23/23 pass

## v0.2.0 — 2026-04-10 UTC

- Tag SHA:       350423650605c14982802c6ebee21275f176442f
- main HEAD SHA: 350423650605c14982802c6ebee21275f176442f
- Match:         ✅ CONFIRMED
- dev post-release state: clean — dev equals main at tag commit (0 commits ahead)
- Verified by:   Gate 15 agent pass

Gate evidence:
- PRs #92–#98: Phase 2 Stage 1 (XOBS-1 through XOBS-4 + test harness) → dev ✅
- PR #99: Gate 14B contrib_sim pass — 23/23 fresh clone → dev ✅
- PR #100: dev→main Phase 2 Stage 1 promotion → main ✅
- Tag v0.2.0 on 3504236 (Merge PR #100) ✅

What shipped:
- Cross-layer observability: OSAL fault (0x66), HAL fault (0x67), queue depth (0x42), WDG checkin (0x50)
- Observatory feedback storm latch in hal_obs_stream_posix.c
- OSAL/HAL CI obligation scripts promoted to --strict
- Bus queue depth warning with configurable EMBEDIQ_QUEUE_WARN_THRESHOLD (75%)
- Test harness: bus_inject(), EMBEDIQ_TEST_SOURCE_ID=0xFEu, scenario runner
- Behavioral test coverage: test_osal_obs.c (3 tests), test_hal_obs.c (8 tests)
- 23 tests total (was 20 at v0.1.3)

## v0.2.2 — 2026-04-10 UTC
- Tag SHA:       4561eb6a1c4edbad7c16a69b6f5d59a84ca190fd
- main HEAD SHA: 4561eb6a1c4edbad7c16a69b6f5d59a84ca190fd
- Match:         ✅
- dev post-release state: clean
- Verified by:   Gate 15 agent pass

## v0.2.3 — 2026-04-17 UTC
- Tag SHA:       b8afa8ac3196d8e9d3e5911f9bdbfc8d424d36bc
- main HEAD SHA: b8afa8ac3196d8e9d3e5911f9bdbfc8d424d36bc
- Match:         ✅
- dev post-release state: clean
- Verified by:   Gate 15 agent pass

## v0.3.0 — 2026-05-26 UTC
- Tag SHA (v0.3.0^{}):  1610a63273c620668eae17dc7bdd70ccec1b30f5
- main HEAD SHA:         1610a63273c620668eae17dc7bdd70ccec1b30f5
- Match:                 ✅ CONFIRMED
- dev post-release:      clean — git log v0.3.0..origin/dev empty
- Verified by:           Gate 15 agent pass

What shipped (PRs #126–#137 above v0.2.4):
- PR #126: docs(arch): flat FSM philosophy and thread model sections (Item 2)
- PR #127: feat(ci): MISRA-C:2012 scan via cppcheck, warning-only (Item 1)
- PR #128: feat(item4-pr-a): contract alignment — nvm.h, hal atomicity, obs events
- PR #129: feat(item4-pr-b): config.iq schema + generator + CI drift-check
- PR #130: feat(item4-pr-c): embediq_nvs_gen.py build-time NVM blob generator
- PR #131: feat(item4-pr-d): fb_nvm blob header, CRC-32, factory-key mutability
- PR #132: feat(bridge): Item 5a — fb_bridge daemon + External FB C API + unit tests
- PR #133: fix(ci): allowlist core/src/bridge/ in check_fb_calls.py
- PR #134: feat(bridge): Item 5b — Python generator + embediq-python SDK + 30 tests
- PR #135: feat(bridge): Item 5c — integration test, CI steps, BRIDGE.md docs
- PR #136: feat(bridge): Item 5d — telemetry_observer.py example
- PR #137: chore: Gate 14B contrib_sim — 26 tests pass, SHA verified
