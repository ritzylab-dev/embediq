# EmbedIQ — Agent Prompt Gate Protocol

**This document governs every coding agent prompt issued by the PM.**
Read this file before writing ANY agent prompt.
Run every applicable gate. Mark N/A with a reason if skipped.
Do not issue the prompt until all gates are checked.

---

## PM Session Startup Checklist (mandatory — first action of every session)

Before any prompt, analysis, or repo work, the PM must output this block completed:

```
SESSION START — <date>

git fetch origin && git show origin/dev:PM/gate_protocol.md read: ✅
origin/dev vs Master Package comparison:   ✅ in sync / ⚠️ dev ahead — MP updated / ⚠️ MP ahead — protocol PR pending #___
project_state.md read:                     ✅  (last updated: <date>)
Working copy for this session: Master Package.
Proceeding.
```

Rules:
- At session start, fetch and read from `origin/dev:PM/gate_protocol.md` — not from memory, not from a feature branch, not from main.
- `origin/dev` is the between-sessions canonical source. Master Package is the in-session working copy. Sync direction is always dev → Master Package at session start.
- If `origin/dev` is ahead of Master Package: update Master Package from dev before proceeding.
- If Master Package is ahead of `origin/dev`: a previous session's protocol PR has not merged yet. File it now before starting new work, or explicitly note the open PR number.
- Any session that modifies gate_protocol.md in the Master Package must file a dedicated PR to dev before the session ends. Branch: `chore/gate-protocol-<topic>`.
- If the user begins a session without seeing this checklist output, they should ask for it before accepting any prompt.

---

## The Gates

| # | Gate | Check |
|---|------|-------|
| 0 | **Agent grounding** | Prompt opens with the mandatory standards preamble. Agent must confirm it has read `CODING_RULES.md` and `ARCHITECTURE.md` and name the three most relevant rules for this specific task before writing any code. No implementation prompt is issued without this gate. See Gate 0 section below. |
| 1 | **Git setup** | Branch created from `dev` (`git checkout dev && git pull origin dev && git checkout -b <branch>`) |
| 2 | **TDD** | Tests written first. New tests must FAIL before implementation is written. |
| 3 | **File placement** | New files land in correct layer directory (`hal/posix/`, `fbs/drivers/`, `fbs/services/`, `core/include/`) |
| 4 | **Layer boundaries** | HAL impl: zero `embediq_*.h`. Driver FB: zero POSIX headers (`<stdio.h>`, `<stdlib.h>`, `<sys/stat.h>` etc). Service FB: zero `hal/` includes. |
| 5 | **No magic numbers** | No hardcoded sizing literals. All array/buffer sizes use named constants from `embediq_config.h`. |
| 6 | **validator.py** | `python3 tools/validator.py` — zero errors before commit. |
| 7 | **Full verification** | `cmake --build build && ctest --test-dir build --output-on-failure && python3 tools/boundary_checker.py` — all clean. |
| 8 | **Staged file check** | `git diff --cached --name-only` — only intended files staged. No `.lock`, no stray edits. If overwriting a file that already exists in the repo, agent must read the existing version first and confirm the overwrite is intentional — never replace an evolved file with a stale scaffold. |
| 9 | **PR target + body quality** | `gh pr create --base dev` always. Verify: `gh pr view <N> --json baseRefName -q .baseRefName` must return `dev`. PR body must contain: (1) what the bug/violation was, (2) what would break and when — especially if host-invisible/hardware-visible, (3) what was changed and why that fixes it. A PR body that only lists changed files or says "tests pass" is incomplete and must be rejected by PM. |
| 10 | **Docs** | If new FB or layer rule added: update `AGENTS.md` and `ARCHITECTURE.md`. If `messages/*.iq` changed: run generator, commit `generated/*.h` in the same PR — never in a follow-up PR. All `.md` files with tables must use perfectly aligned Markdown tables — misaligned columns are a documentation violation. |
| 11 | **Contract before implementation** | HAL contract header (`core/include/hal/hal_*.h`) must exist before the `.c` implementation is written. |
| 12 | **PM boundary** | PM does NOT edit any file inside the git repo directly. All repo changes go through agent prompts only. |
| 13 | **No direct push** | Agent NEVER pushes directly to `dev` or `main`. Every change — no matter how small — goes through a feature branch + PR. Direct push is forbidden. Prompt must include: `git checkout -b <branch>`, `git push origin <branch>`, `gh pr create --base dev`. |
| 14 | **Promotion gate** | dev→main requires three steps in order: (A) mergeability check, (B) contrib_sim pass + `last_run.md` committed to dev, (C) `gh pr create --base main --head dev`. No step may be skipped. |
| 15 | **Release verification** | After human merges and tags, PM runs release verification prompt. Agent confirms tag SHA equals main HEAD SHA and writes result to `PM/release_log.md`. See Gate 15 section below. |

---

## How to Use

Before writing an agent prompt, paste this block and fill it in:

```
GATE CHECK — <task name>
Protocol: gate_protocol.md — <first 8 chars of SHA or last-modified date — read this session>
0.  Agent grounding  ✅   CODING_RULES.md + ARCHITECTURE.md in preamble. Agent confirms 3 rules.
1.  Git setup        ✅/❌  <branch name>, branched from dev
2.  TDD              ✅/❌/N/A  <reason>
3.  File placement   ✅/❌  <new files and their paths>
4.  Layer boundary   ✅/❌  <Layer 1 / Layer 2 / Layer 3 rules confirmed>
5.  No magic numbers ✅/❌/N/A  <confirm or note where literals appear>
6.  validator.py     ✅   included in prompt
7.  Full verify      ✅   cmake + ctest + boundary_checker in prompt
8.  Staged check     ✅   git diff --cached included. No existing file overwritten with scaffold without prior read.
9.  PR target+body   ✅   --base dev + verify command included. PR body: (1) bug, (2) what breaks, (3) fix.
10. Docs             ✅/N/A  <AGENTS.md / ARCHITECTURE.md updated if arch changed. .iq → generator + generated/*.h. All tables aligned.>
11. Contract first   ✅/N/A  <hal_*.h exists before .c>
12. PM boundary      ✅   no direct repo edits by PM
13. No direct push   ✅   prompt contains feature branch + push + gh pr create
14. Promotion gate   ✅/N/A  <N/A for dev PRs — applies only to dev→main promotion>
15. Release verify   ✅/N/A  <N/A for dev PRs — applies only after human merge + tag>
```

**Protocol version header (mandatory):** Every agent prompt must include the `Protocol:` line above. The human can spot-check this against `origin/dev:PM/gate_protocol.md` before pasting. A prompt with no protocol line was written without reading the spec and must be rejected.

**Pre-prompt failure mode check (mandatory):** Before issuing any multi-step agent prompt, the PM must write one sentence identifying the most likely failure mode of the prompt and how the prompt guards against it. If no failure mode can be named, the prompt is not ready.

---

## PM Operating Principles (non-negotiable)

### Working rules
- **Never** use Edit, Write, or Bash on any file inside the git repo
- **Never** run `git` commands on the repo (read-only checks only via Read/Glob/Grep)
- All repo work is routed through agent prompts
- If a gate fails, fix the prompt — do not proceed
- Read this file at the start of every new session (see Session Startup Checklist above)
- **Every PR body must explain WHY, not just WHAT.** The diff shows what changed. The PR body is the permanent git record of why it was wrong and what would break without the fix. Required format: (1) what the bug or violation was, (2) what would break and when — especially if host-invisible/hardware-visible, (3) what was changed and why that fixes it. A PR body that only lists changed files and says "tests pass" is incomplete.
- **Every agent prompt must be one single unbroken copy-pasteable block.** No narrative text, no explanations, no gate check commentary inside or between prompt sections. Gate check and PM commentary go BEFORE the prompt block, never inside it. Prompt block must have clear ================== PROMPT START ================== and ================== PROMPT END ================== markers.

### PM Thinking Rules
- **Never be a people pleaser.** If the evidence points to a different conclusion than what the PM or user expects, say so directly. Agreement without analysis is a failure mode. If challenged, re-examine the first principles — do not capitulate to social pressure.
- **Always think first principle — required output.** Before any significant architectural or naming decision, write one sentence: "Ground constraint: ___ . Convention says ___ . I am [following / departing from] convention because ___ ." If the PM cannot fill in the ground constraint from physical or logical reality — only from "that's how it's been done" — the decision is not ready.
- **Challenge your own conclusions — required output.** Before issuing any multi-step agent prompt, write one sentence: "Most likely failure mode: ___ . Guard: ___ ." This is not optional. A prompt with no named failure mode and no named guard is not ready to issue.
- **Scenario-based stress testing.** For any architectural decision, run at least one adversarial scenario: a fresh-clone failure, a downstream packager, a security researcher, a CI system on a clean runner. If the decision fails any realistic actor, it is not ready.
- **Name things for what they are permanently, not what they are today.** Names must reflect permanent role: `gate_protocol.md` not `13_gate_protocol.md`. This applies to files, variables, functions, branches, and all identifiers.

---

## Gate Violation Response (mandatory procedure)

When any gate is violated — agent merges without human approval, agent pushes directly to dev/main, tag lands on wrong commit, or any other protocol breach:

1. **Stop all work immediately.** No new prompts, no analysis, no repo actions.
2. **Identify what changed that should not have.** State it explicitly: which gate, which action, which commit/tag/branch was affected.
3. **Reverse if reversible.** Tag on wrong commit → delete tag, re-tag. Direct push → revert commit via PR. Agent merge → cannot undo merge, but document it.
4. **Root cause.** Which specific gap in gate_protocol.md allowed this? Name the gap, not the symptom.
5. **Fix the gap.** Update gate_protocol.md in the Master Package. File a dedicated PR to dev.
6. **Resume only after steps 1–5 are complete.**

---

## Gate 0 — Agent Grounding (mandatory before every implementation prompt)

```
BEFORE WRITING ANY CODE — MANDATORY
Step 1: Read CODING_RULES.md completely.
Step 2: Read ARCHITECTURE.md — specifically the section relevant to this task.
Step 3: State your confirmation in this exact format:
  "I have read CODING_RULES.md and ARCHITECTURE.md.
   The three most relevant rules for this task are:
   1. [Rule ID] — [rule name] — [why it applies to this task]
   2. [Rule ID] — [rule name] — [why it applies to this task]
   3. [Rule ID] — [rule name] — [why it applies to this task]"
Do not write any implementation code until you have completed Steps 1–3.
```

Gate 0 applies to all implementation prompts. Does NOT apply to read-only prompts or documentation-only changes.

---

## Gate 13 — Mandatory Prompt Language

Every agent prompt must include this block verbatim:

```
NEVER push directly to dev or main under any circumstances.
NEVER run `gh pr merge` under any circumstances. PR merges are human-only actions.
Steps:
  git checkout dev
  git pull origin dev
  git checkout -b <branch-name>
  [make changes]
  git add <files>
  git commit -m "<message>"
  git push origin <branch-name>
  gh pr create --base dev --head <branch-name> --title "<title>" --body "<body>"
```

**Why this gate exists:** Added 2026-03-24 after agent pushed directly to dev. `gh pr merge` prohibition added 2026-03-26 after agent merged Gate 14B PR without human approval.

---

## Gate 10 — .iq Schema Changes (mandatory)

```
python3 tools/messages_iq/generate.py messages/<name>.iq \
    --out generated/ --output-name <name>_msg_catalog.h
git add messages/<name>.iq generated/<name>_msg_catalog.h
```

The `.iq` change and generated `.h` are committed together — one PR, one commit. Never split. Never edit `generated/*.h` directly.

---

## Gate 14 — Promotion Gate (dev→main)

| Step | Name | Action |
|------|------|--------|
| A | Mergeability check | `git log main..dev --oneline` — confirm dev is ahead of main, no conflicts. |
| B | contrib_sim | `bash tests/contrib_sim/run.sh` — must exit 0. Commit `last_run.md` to dev via feature branch PR before creating the main PR. |
| C | Create promotion PR | `gh pr create --base main --head dev --title "<title>" --body "<body>"` |

Rules:
- Before accepting Step B: verify `HEAD:` in `last_run.md` matches `git rev-parse dev`. If they differ — STOP. Re-run.
- **Agent role ends at Step C.** Agent does NOT merge. Agent does NOT tag. No exception.

**Human actions after agent creates promotion PR:**

```
Check if tag already exists:
  git tag -l "vX.Y.Z"
  If exists: git tag -d vX.Y.Z && git push origin :refs/tags/vX.Y.Z
Tag at correct commit:
  git checkout main && git pull origin main
  git tag -a vX.Y.Z -m "vX.Y.Z — <description>"
  git push origin vX.Y.Z
Verify:
  git rev-parse vX.Y.Z^{}
  git rev-parse main
  Both must return same SHA.
```

---

## Gate 15 — Release Verification (after human merge and tag)

```
GATE 15 — RELEASE VERIFICATION — vX.Y.Z
Read-only. No code changes. No commits. No PRs. Report only.
Step 1:
  git fetch origin
  git rev-parse origin/main
  git rev-parse vX.Y.Z^{}
  Both must return same SHA. If not — STOP. Report to PM.
Step 2:
  git log vX.Y.Z..origin/dev --oneline
  Report output. Flag any unexpected commits.
Step 3: Append to PM/release_log.md:
  ## vX.Y.Z — <date UTC>
  Tag SHA:       <sha>
  main HEAD SHA: <sha>
  Match:         ✅ / ❌
  dev post-release state: <git log output or "clean">
  Verified by:   Gate 15 agent pass
  Commit via branch: chore/release-log-vX.Y.Z → PR to dev.
Step 4: Report PR number and URL. Stop. Do not merge.
```

---

## Review Report Format

```
## F-<NNN>

| Field    | Value |
|----------|-------|
| File     | <path/to/file.c> |
| Lines    | <start>–<end> |
| Rule     | <Rule ID> — <Rule name> |
| Severity | Critical / High / Medium / Low |
| Impact   | <what breaks, when, on which target> |
| Found    | <exact code or pattern> |
| Expected | <correct code or pattern> |
| Fix type | Simple / Moderate / Complex |
| Blocker? | Yes — Phase 2 cannot proceed / No |
```

Severity: Critical = hardware failure / boot fault. High = architectural violation. Medium = style/naming. Low = docs.

End of each review report: (1) summary table by severity, (2) findings per rule ID, (3) Phase 2 blocker list.

---

## Change Log

| Date | Change |
|------|--------|
| 2026-03-26 | Gap analysis round 2 (transcript audit): 3 additional gaps fixed. Session startup checklist updated: origin/dev is the between-sessions canonical; sync direction dev→MP at session start. Gate 8 extended: never overwrite existing repo file with scaffold without reading existing first. Gate 9 extended: PR body quality check added. Gate 10 extended: all .md tables must be perfectly aligned. START/END prompt markers added to PM Operating Principles. |
| 2026-03-26 | Gap analysis round 1: 9 process gaps identified and fixed. Session startup checklist added. Gate 13 mandatory language extended with gh pr merge prohibition. Gate 14B HEAD validation step added. Gate 14 post-merge human tag guidance with tag-exists check. Protocol version header added to How to Use. Gate Violation Response procedure added. Gate 15 added. PM Thinking Rules made procedural with required output. |
| 2026-03-26 | Gate 0 added. PM naming principle added. Review report format added. File renamed from 13_gate_protocol.md to gate_protocol.md. |
| 2026-03-25 | Gate 14 added. PM Thinking Rules added. |
| 2026-03-24 | Gate 13 added. Gate 10 extended. |
