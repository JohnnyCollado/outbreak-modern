# PhaseEnd — Phase 1.1: Groundwork (implements Gen 1.1)
Approved: 2026-10-21 | Closed: 2026-10-21 | Planner: pa-planner | Tasks: 6 done, 0 superseded

## Milestone
Milestone: PROJECT_CONTEXT.md exists, HOW_WE_WORK.md has no TODO, and, with no hand steps, `bash tools/ob.sh recompile` builds the runner from eb8688b + patches 0001-0020 and `bash tools/ob.sh boot --script gate/boot.script` runs a hidden scripted boot that ends by itself at character select and leaves tagged frame dumps for ACCESS, CAPCOM, title, save-data screens, main menu and character select.
Confirmed by the developer at the close.

## Tasks
- T1 — write PROJECT_CONTEXT.md | Done: PROJECT_CONTEXT.md exists, built on the skeleton with every placeholder filled; three named conflicts (D3 vs raylib, stale m0-decision.md lines, no LICENSE) and five more recorded open, not resolved. Summary: The project's constitution is written: what the native port is for, the six design decisions plus the choice to build on PS2Recomp, the legal and game-data rules, where each milestone stood on 2026-10-21, and generation 1 (correct 2D on Windows) with its six phases. Conflicts between the old design and today's base are listed as open questions for the developer, not settled. | commit: - | tasks/T1.md | logs/T1.md
- T2 — Windows build and boot scripts into the repo | Done: tools/ob.sh (tools, test, recompile, boot) replaces the C:\ob scripts; tools, test and recompile are green from a fresh shell. The one boot was closed at ~142 s on the ACCESS screen, before the main menu. Summary: One script in the repo now builds the tools, runs the three test suites, recompiles the game plus three overlays and boots the runner, with every path overridable and nothing game-derived inside the repo. The builds and tests pass with the expected counts and the recompile has no errors. The single approved boot ended early because the window received a close request at about 142 seconds, while the ACCESS logo screen was showing, so the main-menu clause is not yet met. | commit: 942fc04 | tasks/T2.md | logs/T2.md
- T3 — clean C:\ob\src; frame dump as patch 0019 | Done: C:\ob\src = C:\ob\up (eb8688b + 0001-0019) + the auto-press hunk only; frame dump is patch 0019 with a ps2x_tests test. Summary: The working copy used to build the game carried a pile of temporary debugging edits. They are now sorted: the frame dump became a proper patch that saves exactly what the window shows, at its native size, one picture per displayed frame; the debugging prints were removed (and saved); the automatic button presser stays until the next task replaces it. All three test suites pass and a one-minute boot wrote correctly sized pictures. | commit: 7f58abb | tasks/T3.md | logs/T3.md
- T4 — scripted input and hidden window, patch 0020 | Done: patch 0020 replaces the auto-press: a text script drives the pad on guest vsyncs, the window is hidden, dumps happen only in capture steps; gate/boot.script boots hidden to character select and captures the six gate screens. Summary: The game can now be booted with no window on screen, driven by a small script that presses buttons and waits for the game's own state rather than for time, and that saves pictures only of the screens we want. Two runs of the same build took every picture at the same moment, and 17 of the 18 pictures were byte for byte the same; the odd one was taken one frame later. This is the noise floor the next phase's comparison has to tolerate. The old blind button masher is gone. | commit: 2058e9a | tasks/T4.md | logs/T4.md
- T5 — complete the card | Done: HOW_WE_WORK.md has no placeholders: project tools, paths, build/run/gate, pins, gotchas, decisions filled; detail in docs/ops/ (6 files); card 6988/7000, `card.py check` healthy. Summary: The project's standing fact sheet is now complete: it names the project's own tools, where data and generated files live, how to build, boot and test, the toolchain versions, the traps that have bitten us, and the decisions that stand for this generation. Because the sheet has a hard size limit, the longer detail went into a small set of operations notes it points to. The repository's bundled copy of PS2Recomp was checked against the real working copy and found out of date, so the sheet now says never to build from it. | commit: - | tasks/T5.md | logs/T5.md
- T6 — phase cleanup | Done: what the project still needs from .run/1.1/ is in phase-ends/current/logs/; the rest is scratch for the phase close to delete. Summary: The phase's scratch folder was checked file by file. Two small scripts from T4 are worth keeping: the discovery boot script that mapped each gate screen to a game-memory value, and the frame comparison used for the 17-of-18 byte-identical result. Both are now beside T4's log. Everything else is scratch: run logs (their results are in the task logs), a T3 diff already saved as logs/T3.src-vs-up.diff, contact sheets of game frames (game-derived, never committed), a backup and a debugging script. | commit: - | tasks/T6.md | logs/T6.md

## Decisions that still bind
- T2 — splat asm for the overlays lives at C:\ob\splat\SLPM_654.28 (copied from an old session scratchpad); `OB_SPLAT` points there.
- T4 — scripted boots step on the guest vsync (EeScheduler VBlankStart), never host frames; with a script the pad is script-only (no host input) — the determinism the gate relies on.
-

## Rules proposed
- (none)

## Cookbook entries added
- (none)

## Research
R0-001 | map GS render and presentation path for 2D correctness plan (Interfaces section) | PS2Recomp ps2xRuntime GS/GIF/VU1/presenter map | ps2recomp, gs, gif, vu1, presenter, interfaces | retriever-code | 2026-10-21 | 66 lines
R0-002 | extract rendering symptoms, frame dump, auto-press, test suites, builds, timings, open items from two 2026-10-21 reports for Generation 1 planning | Bank-wall + native bring-up: inputs for rendering Gen 1 | rendering, gen1, ps2recomp, windows, test-suites, build, auto-press, frame-dump | retriever-digest | 2026-10-21 | 65 lines
R0-003 | phase-1 HOW_WE_WORK.md card fill + plan Context section | outbreak-modern card: operational facts (Build/Run/Test/Paths/Pins/Gotchas) | outbreak-modern, ps2recomp, windows, build, test, paths, pins, gotchas | retriever-digest | 2026-10-21 | 79 lines
R0-004 | first GENERATION_PLAN.md (gen 1 = correct 2D in PS2Recomp Windows build) + phase-1 PROJECT_CONTEXT.md | Project constitution and generation-roadmap material (outbreak-modern) | outbreak, ps2recomp, constitution, roadmap, milestones, legal, status | retriever-digest | 2026-10-21 | 68 lines
R0-005 | milestone gate gen-1 (6 scripted boot frames vs PCSX2 2.9.134 portable reference, SLPM-65428) | PCSX2 screenshot comparison and justified tolerance for the gen-1 milestone gate | pcsx2, gs, gsrunner, screenshot, regression, tolerance | retriever-web | 2026-10-21 | 49 lines
R1.1-006 | Find guest globals that identify each boot screen (ACCESS, CAPCOM, title, memcard/warning, main menu, Scenario Select, char select, Difficulty) in Outbreak-decomp eb6af38 for T4 gate/boot.script. | Boot-screen guest state addresses for RE Outbreak SLPM_654.28 (decomp lookup) | outbreak, boot, decomp, SLPM_654.28, not-found | retriever-code | 2026-10-21 | 33 lines
R1.1-007 | Find guest EE globals that identify each boot screen for gate/boot.script polling (T4). | RE Outbreak boot screen state globals (decomp-src) | re-outbreak, boot, title, state-machine, decomp-src, polling | retriever-code | 2026-10-21 | 48 lines
R1.1-008 | R1.1-007 follow-up. Exact addresses/offsets/values for a host poll script (T4 gate/boot.script). | EE RAM addresses for SLPM_654.28 (en): title, boot task, memory card, overlays | re, ps2, outbreak, ee-ram, title, boottask, memcard, overlay | retriever-code | 2026-10-21 | 60 lines

## Audit
- seed: median 19k, max 19k, n=5 (phase 1.1)
- CLAUDE.md: 902 bytes
- .claude/skills/pa-planning/SKILL.md: 7647 bytes
- .claude/skills/project-architect/SKILL.md: 13339 bytes
- .claude-state/memory/MEMORY.md: 431 bytes
- HOW_WE_WORK.md: 7004 bytes
- cookbook/INDEX.md: 278 bytes
- rules/INDEX.md: 2963 bytes
### Carry audit — phase 1.1 (2026-10-21T22:08:22Z → open UTC, 1 sessions, 474 requests)

| file (read) | chars | n |
|---|---|---|
| title.c | 23.9k | 10 |
| ps2_runtime.cpp | 17.6k | 11 |
| boottask.c | 17.4k | 5 |
| native-port-design.md | 16.8k | 1 |
| R0-003.md | 16.4k | 2 |
| src-vs-up.diff | 13.6k | 1 |
| text.s | 11.6k | 4 |
| CLAUDE.pre-pa3.md | 11.3k | 1 |
| m2-recompiler-main-elf.md | 10.5k | 2 |
| m7-network.md | 9.7k | 2 |
| file (write) | chars | n |
|---|---|---|
| PROJECT_CONTEXT.md | 31.7k | 2 |
| ps2_input_script.cpp | 15.5k | 4 |
| ps2_input_script_tests.cpp | 13.0k | 4 |
| 2026-10-21-pa3-groundwork-and-boot-speed.md | 12.8k | 5 |
| TASK_PROGRESS.md | 11.5k | 4 |
| ob.sh | 10.8k | 3 |
| HOW_WE_WORK.md | 7.7k | 9 |
| t5card.py | 6.3k | 2 |
| result kind | chars | n |
|---|---|---|
| bash other | 335.2k | 177 |
| Read | 304.3k | 130 |
| tools/plan_edit.py | 79.0k | 23 |
| Grep | 71.2k | 48 |
| tools/ob.sh | 27.8k | 34 |
| Glob | 18.4k | 15 |
| Agent | 16.4k | 23 |
| tools/task_log.py | 15.0k | 10 |
| tools/research_add.py | 11.5k | 3 |
| other | 9.9k | 86 |
- spilled: 1 results, 33.6k chars on disk, 0 read whole
- whole reads over 20.0k: 0
- seed floor: retriever-code 5,851 (n 4, prev 5,851) · retriever-digest 5,783 (n 3, prev 5,783) · retriever-web 4,338 (n 1, prev 4,338)
- outline credit: 0 outlines · 0 followed by a ranged read · 0 chars credited
- warm pings: 0 pings over 0 runs, 0 warmed waits over the TTL, rewrites across warmed waits 0, waits past the cap 0, cost: pings $0.0000 + relay $0.0000 + re-arm $0.0000 = $0.0000 vs rewrites replaced $0.0000, cheaper than one rewrite: n/a, undelivered 0
- toasts by cause (waiting): question 0, review 0, replan 0, permission 0, input 0, crash 0, idle 0, stop 0, subagent-stop 0, model 0, other 0
- router turns by cause: loop 31, relay 0, re-arm 0, other 11, relay cost $0.0000
- retriever re-asks: 2 of 3 retriever briefs repeat a lookup of the same run
- router: main 92285478 · requests 129 · ctx at end 246.5k · growth aeb7ae01 +2.7k, ad3d9330 +33.2k, acd04d44 +15.7k, a04be586 +26.7k, ab53c08e +1.8k · top: bash other 25.5k/26, Read 17.2k/20, other 8.5k/65, Agent 7.6k/7, tools/plan_edit.py 7.5k/14
- noise: 33 lines 2.6k chars — warning: in the working copy of: 9 lines/1.2k, No such file or directory: 12 lines/763, usage: : 12 lines/665
- price: read $0.20/Mtok · 1h write $7.87/Mtok · output $19.67/Mtok (phase model mix)
- median requests after a read: 0
- carry/request: 1964 chars, 474 requests (prev 3624 chars, 474 requests, growth -45.8%)
- flag: 3 whole-plan reads by main, pa-expert
- flag: 1 tool-source reads by pa-expert
  %USERPROFILE%/.claude/projects/D--NEXTJJEN-repos-outbreak-modern/memory/pcsx2-debugging-setup.md  2.0k chars  pa-expert

## Agent runs
- - | pa-expert | expert | claude-opus-5-5 | medium | ctx 98k | $1.384 | saved - | completed | parent 92285478
- - | pa-expert | expert | claude-opus-5-5 | medium | ctx 75k | $1.275 | saved - | completed | parent 92285478
- T2 | pa-coder | coder | claude-opus-5-5 | medium | ctx 53k | $0.950 | saved - | completed | parent ad3d9330
- - | pa-expert | expert | claude-opus-5-5 | medium | ctx 71k | $1.394 | saved - | completed | parent 92285478
- T3 | pa-coder | coder | claude-opus-5-5 | medium | ctx 57k | $1.445 | saved - | completed | parent acd04d44
- - | pa-expert | expert | claude-opus-5-5 | medium | ctx 155k | $3.732 | saved - | completed | parent 92285478
- - | retriever-code | retriever | claude-haiku-5-5 | medium | ctx 24k | $0.007 | saved - | completed | parent a04be586
- - | retriever-code | retriever | claude-haiku-5-5 | medium | ctx 37k | $0.012 | saved - | completed | parent a04be586
- - | retriever-code | retriever | claude-haiku-5-5 | medium | ctx 48k | $0.018 | saved - | completed | parent a04be586
- T4 | pa-coder | coder | claude-opus-5-5 | medium | ctx 83k | $1.888 | saved - | completed | parent a04be586
- T4 | pa-coder | coder | claude-opus-5-5 | medium | ctx 45k | $0.960 | saved - | completed | parent a04be586
- - | pa-expert | expert | claude-opus-5-5 | medium | ctx 88k | $1.388 | saved - | completed | parent 92285478

## Discussions
- (none)

## Deferred
- from T6: phase 1.1 closes; phase 1.2 builds the gate from gate/boot.script and logs/T4.compare.py. (unconsumed)

## Changes
- 2026-10-21 developer: Developer decides answered 2026-10-21: standing proceed for C:\ob work (read/edit/build/test/recompile) + hidden scripted runner boots that end by themselves, this generation; PCSX2, visible windows, Odin stay per action. Clang/Linux re-run deferred to the Linux/Android generation.
- 2026-10-21 router: T1 next -> done
- 2026-10-21 developer: T1 review: PROJECT_CONTEXT.md stands as written, all eight open items kept (2026-10-21)
- 2026-10-21 claude: T2 boot t2c (PS2X_PROFILE=30, 270 s, developer-approved): profiler hang fixed (sample stored after ResumeThread, TempProfiler.cpp:94-103, uncommitted C:\ob\src hunk). GameThread 64-97% in NtWriteFile across 8 windows; 11951x2 std::cerr lines from sceSifSetDma traces (SIF.cpp:651,665, temp hunk). ~24 vsync ticks/s mid-boot vs 60. Logs C:\ob\logs\boot-t2c.{log,err}. Main-menu clause still unmet.
- 2026-10-21 claude: T2 boot t2d (developer-approved): runner reconfigured -DPS2X_ENABLE_AGRESSIVE_LOGS=OFF (upstream default ON; now in ob.sh runner configure). Same build otherwise. Reached char select at +1:39, Difficulty screen at +2:11 (t2c with logs: ~+4:13); steady ~60 vsync ticks/s; GameThread 92% waiting after Difficulty (black, scenario load = M4 scope). Main-menu clause met; T2 done.
- 2026-10-21 router: T2 next -> done
- 2026-10-21 router: T3 next -> done
- 2026-10-21 router: T4 next -> done
- 2026-10-21 router: T5 next -> done
- 2026-10-21 router: T6 next -> done

## Plain-English Recap
Phase 1.1 laid the groundwork for measuring the game's broken graphics. The project now has its founding document (PROJECT_CONTEXT.md, approved by the developer) and a complete fact sheet (HOW_WE_WORK.md, the "card", with overflow in docs/ops/). The Windows build, test, recompile and boot steps, which lived as loose scripts in C:\ob, are one repo script, tools/ob.sh.

Two patches were added to PS2Recomp, the recompiler and runtime the port is built on. Patch 0019 saves the frames the game shows as PNG images at the PS2's native 512x448. Patch 0020 lets a text script press the controller buttons and wait for the game to reach a given state, and runs the game with its window hidden. With them, gate/boot.script boots from power-on to character select by itself and saves three frames of each of six screens: the save-data prompt, ACCESS, CAPCOM, the title, the main menu and character select. Across two runs, 17 of the 18 frames were byte-for-byte identical; the odd one was caught one frame later.

The developer found the build very slow and asked whether PS2Recomp was the wrong base. Profiling showed the game spent most of its time writing log lines, because PS2Recomp turns its "aggressive logs" on by default. Building with them off (now in ob.sh) roughly halved the time to every screen. The full story, with mistakes, is in reports/2026-10-21-pa3-groundwork-and-boot-speed.md.

Still open: every screen still draws wrong (the purpose of phases 1.2-1.5); after the Difficulty screen the game goes black (scenario loading, milestone M4); the boot logos run slower than real time (cause unknown); the game's own code sits in main's pushed git history (commit bb0c282, the developer's decision).
