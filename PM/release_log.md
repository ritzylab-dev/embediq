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

## v0.1.2 — CONFIRMED — 2026-03-26 UTC

- Tag SHA:       398a52d0cebd4668a424ec080d5fb88bd446a697
- main HEAD SHA: 398a52d0cebd4668a424ec080d5fb88bd446a697
- Match:         ✅ CONFIRMED — tag corrected by human after Gate 15 mismatch finding
- dev post-release state: PRs #64 #66 #67 ahead of main (process/docs only — no code)
- Verified by:   Gate 15 agent pass — second run
