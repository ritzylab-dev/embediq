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
