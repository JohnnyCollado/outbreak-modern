# How we work — outbreak-modern

<!-- The card: standing facts for every agent. Never appended to; the cap is
     card.max_chars in .claude/pa.json (default 7000), enforced by the archive.
     Each role prints its slice with `tools/card.py slice <role>`.
     Written at install (interview) and edited in the same task as whatever changed it (H7). -->

## Project <!-- roles: claude expert coder planner review auditor curator -->
outbreak-modern — Recompilation of Resident Evil Outbreak with Modern Controls. Constitution `PROJECT_CONTEXT.md`; roadmap `GENERATION_PLAN.md`;
rules `rules/INDEX.md`; techniques `cookbook/INDEX.md`; ops detail `docs/ops/INDEX.md`.

## Developer <!-- roles: claude planner review auditor curator -->
Who: JohnnyC96. Experience: intermediate. Domain: outbreak-modern.
Preferences: recommendations, not questions. Plain-English recaps at phase end. A notification
whenever anything waits on them. Autonomy: full inside an approved plan; stop only at the two gates. Notification channel: toast.
The developer pushes; agents never do. They ratify rules at the next plan approval.

## Rhythm <!-- roles: planner review auditor curator -->
autonomous  <!-- autonomous: the loop (the mod) runs the phase end to end, stopping
                  only at the two gates. review: tasks marked `review: yes` stop the queue with REVIEW.md. -->
Gates: plan approval (in the plan-mode view) and the milestone (confirmed by the
developer at the close question).

## Tools <!-- roles: expert coder planner review auditor curator -->
PY = C:/Users/johnn/AppData/Local/Python/pythoncore-3.14-64/python.exe

| Name | Command | Purpose |
|---|---|---|
| launch | `PY tools/launch.py --state-json` | the state detector: the mod's read-only report; never typed by the developer |
| plan_edit | `PY tools/plan_edit.py` | the only writer of `PHASE_PLAN.md` (status, Changes, add/reopen) |
| status | `PY tools/status.py` | `.run/status.json` for the statusline; INBOX consume; waiting flags |
| task_log | `PY tools/task_log.py` | lint and finish a task summary |
| research_add | `PY tools/research_add.py` | allocate a report id, write the index line |
| rules_add | `PY tools/rules_add.py` | add, supersede, promote, retire a rule |
| skill_add | `PY tools/skill_add.py` | turn a `workflow:` gotcha into `.claude/skills/<name>/SKILL.md` |
| cookbook_add | `bash tools/cookbook_add.sh` | add a cookbook entry and its index line |
| phaseend_index | `PY tools/phaseend_index.py` | assemble and archive a PhaseEnd |
| genend_index | `PY tools/genend_index.py` | assemble a GenerationEnd |
| commit_task | `bash tools/commit_task.sh` | the only commit path; explicit paths, no trailers, never pushes |
| run | `bash tools/run.sh` | any command that may print >40 lines |
| curate | `PY tools/curate.py` | `memory --route\|--reindex`, `rules --reseed\|--demote`, `cookbook --demote`, `claude-md\|card --refresh`, `how-we-work --retire`; `--dry-run` first |
| ob | `bash tools/ob.sh` | Windows driver (see Build) |
| splat_functions | `PY -I tools/splat_functions.py` | recompiler function map |
| mwo3_to_elf | `PY tools/mwo3_to_elf.py` | overlay BIN -> ELF |
| q_scan | `PY tools/q_scan.py` | VU0 Q check (read only) |
| recomp_cycle | `bash tools/recomp_cycle.sh` | Linux form of the cycle |

ARMSX2-mod legacy (f1_*, phase2_check, push_patch, pine_diag): device actions, a "proceed" each.

## Skills <!-- roles: expert planner -->
<!-- one line per captured workflow; the SKILL.md is the canonical text -->
- (none yet)

## Paths <!-- roles: claude expert coder planner review auditor curator -->
- Oracles (the ground truth X3 names): PCSX2 2.9.134 in `ps2_emu/` (git-ignored); phase 1.2 reference screenshots
- Data: C:\ob (layout `docs/ops/workspace.md`); game in `game_files/` (read only)
- Generated (never hand-edited, H1): C:\ob\work, ovl; src/runner/*.cpp, ps2_recompiled_*.h; `upstream/ps2recomp/*.patch`
- Hand-edited: `tools/`, `gate/`, docs; PS2Recomp via commits in C:\ob\up
- Scratch: all of a phase's work in `.run/` goes under `.run/<phase>/` (gitignored; never the system temp), nowhere
  else in `.run/`. It is a temp folder only, never a home for long-term content: what the project needs lives in its
  proper place in the repo. The phase's cleanup task checks it and
  the close deletes it; a tool deletes its own temp folders when it finishes.

## Build / run / test <!-- roles: claude expert coder planner review auditor curator -->
- Build: `bash tools/run.sh ob-tools -- bash tools/ob.sh tools` (-> C:\ob\t); `bash tools/run.sh ob-recompile -- bash tools/ob.sh recompile` (main ELF + 3 overlays, runner -> C:\ob\r). Long: background shell.
- Run: `bash tools/ob.sh boot --script gate/boot.script --tag T` (patch 0020: hidden, scripted pad, captures to C:\ob\frames-<T>, log C:\ob\logs\boot-<T>.log, ends by itself; `docs/ops/boot.md`). Plain `boot [--secs N]`: VISIBLE window, every frame dumped: a developer proceed each.
- Test / the gate (until 1.2): `bash tools/run.sh ob-test -- bash tools/ob.sh test`; green = all three suites pass (ps2x_tests 471/471, IOP 4/4, gs_cache 45/45).
- Modes: OB_* env vars override the C:\ob paths (`docs/ops/workspace.md`); MSVC steps via tools/ob/vc.bat.
Long gates go through `tools/run.sh` with a raised timeout, never a poll loop.

## Conventions & house style <!-- roles: claude expert coder planner review auditor curator -->
- Struggle reports in `reports/` (`TEMPLATE.md`) as issues are solved.
<!-- project-specific only; the general house style is in the project-architect skill -->

## Environment <!-- roles: claude expert coder planner review auditor curator -->
- OS / shells: Windows / bash, powershell
- Python: C:/Users/johnn/AppData/Local/Python/pythoncore-3.14-64/python.exe (3.14.5)
- Pins (`docs/ops/pins.md`): MSVC 19.44/VS2022, CMake 3.30, MSYS2 GCC 16.1, PS2Recomp eb8688b+0001-0020, SLPM-65428 v1.04+EN 2.1
- Harness gotchas (`docs/ops/gotchas.md`): (std::max)/NOMINMAX; no alloc between Suspend/ResumeThread; ps2x_tests cwd
  C:\ob\src\ps2xTest; vc.bat path no spaces; C:\ob for path length; vendored PS2Recomp-main stale: never build;
  native frame 512x448; PCSX2 debugger quirks.

## Decisions <!-- roles: claude expert coder planner review auditor curator -->
- M0 = PS2Recomp option B; game data never in git; runner `-DPS2X_ENABLE_AGRESSIVE_LOGS=OFF` (ON halves boot speed).
- This generation: standing proceed for C:\ob work + hidden scripted boots (PCSX2, visible windows, Odin: per action);
  Clang/Linux re-run deferred to the Linux/Android generation.

## Docs map <!-- roles: expert coder planner review auditor curator -->
- `docs/ops/INDEX.md` — workspace, boot, pins, gotchas, tools
- `cookbook/INDEX.md` — techniques, grep by tag
- `rules/INDEX.md` — full rule texts
- `phase-ends/TASK_INDEX.md`, `phase-ends/RESEARCH_INDEX.md` — what was done and what was learned
- `docs/research-archive/` — migrated legacy reports
- `docs/retired/` — everything moved out of the load order
