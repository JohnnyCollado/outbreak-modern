# PhaseEnd — Phase 1.2: The gate (implements Gen 1.2)
Approved: 2026-10-22 | Closed: 2026-10-22 | Planner: pa-planner | Tasks: 8 done, 0 superseded

## Milestone
Milestone: `bash tools/ob.sh gate` compares the six dumped screens with PCSX2 2.9.134 references under a tolerance frozen and justified in a committed file, its self-test passes PCSX2-vs-PCSX2 and fails deliberately damaged frames, and it prints both numbers per screen for today's build (expected red), recorded in a committed baseline.
Confirmed by the developer at the close.

## Tasks
- T1 — reference capture kit: display-mode probe, settings checklist, intake tool | Done: `tools/gate/refs.py` (check-settings, begin, intake [--dry-run]) and the PCSX2 capture checklist exist; the developer's ini fails check-settings on 4 keys. Summary: We now have a written recipe for taking PCSX2 reference screenshots the same way every time, and a tool that checks PCSX2's settings against that recipe, then files the screenshots under the right screen names. We could not read the game's display registers from the boot script, so the screenshot size (512x448) is a prediction; the first real shot will confirm it before the rest are taken. | commit: b51169c | tasks/T1.md | logs/T1.md
- T2 — gate tool: compare core, self-test harness, `ob.sh gate` with staleness stamp | Done: `tools/gate/gate.py` (compare, selftest, stamp, synth) and `bash tools/ob.sh gate [--selftest] [--d N --p F]` exist; on today's tree the gate recompiles only on a stale stamp, boots hidden, and exits 2 "no references". Summary: The gate is now one command. It rebuilds the game only when something the build reads has changed, boots it with no window, and compares each of the six screens against PCSX2 reference pictures, printing two numbers per screen and a pass or fail. Before it trusts itself, it checks that it can tell a good copy from two deliberately damaged ones. The reference pictures do not exist yet, so today it stops with "no references", as planned. | commit: 82a99cb | tasks/T2.md | logs/T2.md
- T3 — our-side noise floor and capture window | Done: our-side noise numbers and per-screen change periods are in docs/ops/gate.md; gate/boot.script captures savedata 6, access 6, capcom 6, title 64, mainmenu 32, charselect 6; a gate boot with it exits 0 (t3w, 50 s). Summary: We booted the same build twice and compared the pictures moment by moment: they match exactly except a tiny patch of garbage pixels on character select. A long recording showed which screens move: the PRESS START text pulses about every 1.4 s, and several screens alternate between two images one line apart. Each screen now records enough frames to include every image the developer's PCSX2 shot could show, while staying inside the screen's steady stretch. | commit: - | tasks/T3.md | logs/T3.md
- T4 — developer captures the PCSX2 references | Done: check-settings `settings OK`; intake wrote C:\ob\ref (12 images, 512x448) and gate/ref_manifest.json (committed); developer confirmed the contact sheet mapping 2026-10-22. Summary: The developer took two boots of six PCSX2 2.9.134 screenshots (savedata, access, capcom, title, mainmenu, charselect) with the Software renderer at native size, shots 03:00-03:04. Every image was checked against its label before the intake. The save-data shots show the same "no data on MEMORY CARD slot 1 / Start the game anyway? YES/NO" prompt our build shows. | commit: - | tasks/T4.md | logs/T4.md
- T5 — tolerance analysis: noise floors both sides and legitimate GS differences -> proposed d and p | Done: gate/TOLERANCE.md (draft): d=8, p=0.0001; savedata/access/capcom gated, title/mainmenu/charselect advisory; gate.py reports advisory screens outside the exit code; end-to-end gate: exit 1, 0/3 gated green. Summary: We measured how much PCSX2 disagrees with itself and which differences between PCSX2 and our runner are expected, and set the tolerance from that: 8 levels per channel, at most 0.01% of pixels. It is proven on the save-data, ACCESS and CAPCOM screens. The title, main menu and character select backgrounds are movies, which the runner cannot play yet, so those three screens are reported but do not decide pass or fail in this generation. Today's build fails all three gated screens, partly because the runner shows only every other line. | commit: - | tasks/T5.md | logs/T5.md
- T6 — verifier self-tests on real references, then freeze the tolerance | Done: self-test on the real refs passes 9/9 gated (exit 0) with d 8, p 0.0001, advisory title/mainmenu/charselect; draft gate/tolerance.json written and gate/TOLERANCE.md finalized, both uncommitted until the developer says freeze. Summary: We ran the gate's self-check on the real PCSX2 pictures. On the three screens that decide pass or fail, PCSX2 matches itself exactly and every deliberately damaged copy is caught, with the smallest damage at twice the limit. The three movie screens are printed but not counted, as decided. Two loopholes were closed first: the gate no longer accepts a tolerance typed on the command line, and the self-check now refuses reference pictures whose hashes do not match the manifest. The tolerance is ready to freeze on your word. | commit: 3f75886 | tasks/T6.md | logs/T6.md
- T7 — baseline run on today's build (expected red) | Done: gate/baseline-1.2.md records today's build scored by the frozen gate (exit 1, 0/3 gated green) with per-screen visible faults; two runs agree. Summary: We ran the finished gate on today's build, twice, with the frozen tolerance and the window hidden. It fails as expected: none of the three screens that count pass, and the three movie screens are far off too. Both runs gave exactly the same scores. For each screen we wrote down what looks wrong next to the PCSX2 picture, so the next phase knows where to start. | commit: - | tasks/T7.md | logs/T7.md
- T8 — phase cleanup | Done: what the project still needs from .run/1.2/ is in gate/evidence/1.2/; the rest is scratch for the phase close to delete. Summary: The frozen tolerance's evidence chain (gate/TOLERANCE.md, docs/ops/gate.md, the 1.2 summaries) cites scripts and logs under .run/1.2/, which the phase close deletes. The cited ones were copied to gate/evidence/1.2/ under the same names, and docs/ops/gate.md maps the old paths there; TOLERANCE.md itself was left untouched because it is frozen. Everything else in .run/1.2/ is scratch: synthetic test frames, one-off sheet and edit scripts, superseded logs. | commit: - | tasks/T8.md | logs/T8.md

## Decisions that still bind
- T1 — reference settings = Renderer 13, ScreenshotSize 2, ScreenshotFormat 0, OrganizeScreenshotsByGame false, deinterlace_mode 1, linear_present_mode 0, Crop* 0, fxaa/ShadeBoost false, TVShader 0, CASMode 0, pcrtc_offsets/overscan false, upscale_multiplier 1, [Folders] Snapshots snaps; recorded only: pcrtc_antiblur, dithering_ps2, AspectRatio (refs.py EXPECTED/RECORDED is the source).
- T2 — exit codes of `ob.sh gate`: 0 all green, 1 any red, 2 refusal (no refs, sha256/size/count mismatch, no tolerance, missing tick), 3 boot failed, 4 recompile failed, 5 selftest red.
- T6 — after the freeze, d/p/metric/advisory change only by a developer-approved plan change (P9); the gate reads
- T8 — gate/TOLERANCE.md is not edited after the freeze (20233c3); path changes go in docs/ops/gate.md.
-

## Rules proposed
- (none)

## Cookbook entries added
- (none)

## Research
R1.2-001 | phase 1.2 gate plan, Interfaces section (read-only research) | ob.sh gate interfaces: dispatch, boot, script grammar, frame dump, GS presentation | ob.sh, gate, boot, PS2X_INPUT_SCRIPT, frame-dump, GS, PSMCT16, interlace, DISPLAY | retriever-code | 2026-10-21 | 61 lines
R1.2-002 | what phase 1.1 leaves for building the screenshot gate (timing, frame capture, noise, open items, R0-005 conclusions, reports) | Phase 1.1 handoff for the 1.2 screenshot gate | phase-1.1, gate, scripted-boot, frame-dump, compare, tolerance, pcsx2, reports | retriever-digest | 2026-10-21 | 58 lines
R1.2-003 | phase 1.2 reference screenshots (PCSX2 vs native 512x448 PNG frames) | PCSX2 2.x screenshot geometry and settings for 512x448 1:1 reference captures | pcsx2, screenshot, gs, geometry, reference-capture | retriever-web | 2026-10-21 | 53 lines
R1.2-004 | phase 1.2 tolerance freeze, native vs PCSX2 2.9.134 SW renderer, 512x448 | PS2 GS legitimate per-pixel deltas for frame tolerance (phase 1.2) | ps2, gs, pcsx2, tolerance, dithering, crtc, research | retriever-web | 2026-10-21 | 46 lines
R1.2-005 | Phase 1.2 plan, PCSX2 deinterlace choice and tolerance analysis for comparing 512x448 frames to PCSX2 screenshots | RE Outbreak (SLPM_654.28) GS display init: what the decomp shows | ps2, gs, display, interlace, pmode, dither, outbreak, decomp | retriever-code | 2026-10-21 | 49 lines
R1.2-006 | R1.2 PCSX2 reference-screenshot settings checklist | PCSX2 2.9.134 settings, output size, deinterlace, bilinear (for phase 1.2 checklist) | pcsx2, settings, deinterlace, screenshot, gs, phase-1.2 | retriever-web | 2026-10-21 | 62 lines
R1.2-007 | pixel-diff gate tolerance (d per-channel, p pixel share) for our software GS vs PCSX2 SW screenshots | PCSX2 v2.9.134 SW renderer: 16-bit expansion, dithering, antiblur, screenshot path | pcsx2, gs, software-renderer, expansion, dithering, antiblur, screenshot | retriever-web | 2026-10-22 | 64 lines
R1.2-008 | loop periods and timeouts for title/menu/select backgrounds, for sizing best-frame windows | Background animation timers: title, main menu, select (RE Outbreak SLPM-65428) | outbreak, title, title.c, menu, select, attract, pulse, timers, decomp | retriever-code | 2026-10-22 | 37 lines

## Audit
- seed: median 19k, max 20k, n=6 (phase 1.2)
- previous phase 1.1: median 19k, growth -0.4%
- CLAUDE.md: 902 bytes (unchanged)
- .claude/skills/pa-planning/SKILL.md: 7647 bytes (unchanged)
- .claude/skills/project-architect/SKILL.md: 13339 bytes (unchanged)
- .claude-state/memory/MEMORY.md: 431 bytes (unchanged)
- HOW_WE_WORK.md: 7004 bytes
- cookbook/INDEX.md: 278 bytes
- rules/INDEX.md: 2963 bytes
### Carry audit — phase 1.2 (2026-10-22T05:05:01Z → open UTC, 1 sessions, 363 requests)

| file (read) | chars | n |
|---|---|---|
| title.c | 36.6k | 14 |
| gate.py | 31.7k | 3 |
| ob.sh | 20.7k | 3 |
| native-port-design.md | 16.8k | 1 |
| refs.py | 13.4k | 2 |
| TOLERANCE.md | 13.0k | 2 |
| CLAUDE.pre-pa3.md | 11.3k | 1 |
| ps2_runtime.cpp | 11.0k | 6 |
| README.md | 9.4k | 1 |
| gs_cpu_backend.cpp | 8.3k | 2 |
| file (write) | chars | n |
|---|---|---|
| gate.py | 25.6k | 12 |
| 2026-10-22-gate.md | 9.4k | 7 |
| refs.py | 8.1k | 4 |
| gate.md | 7.4k | 8 |
| retriever-web-pcsx2-sw-output-dither-antiblur.md | 6.7k | 1 |
| TOLERANCE.md | 6.1k | 1 |
| reference-capture.md | 4.8k | 1 |
| T1.md | 4.0k | 1 |
| result kind | chars | n |
|---|---|---|
| Read | 329.7k | 131 |
| bash other | 204.8k | 137 |
| tools/plan_edit.py | 107.7k | 27 |
| Grep | 100.8k | 39 |
| tools/ob.sh | 41.4k | 17 |
| other | 37.0k | 110 |
| run.sh | 33.8k | 31 |
| Agent | 21.5k | 33 |
| Glob | 11.9k | 7 |
| tools/task_log.py | 10.2k | 12 |
- spilled: 1 results, 31.1k chars on disk, 0 read whole
- whole reads over 20.0k: 0
- seed floor: retriever-code 5,829 (n 7, prev 5,829) · retriever-digest 5,834 (n 4, prev 5,834) · retriever-web 4,586 (n 5, prev 4,586)
- outline credit: 0 outlines · 0 followed by a ranged read · 0 chars credited
- warm pings: 0 pings over 0 runs, 0 warmed waits over the TTL, rewrites across warmed waits 0, waits past the cap 0, cost: pings $0.0000 + relay $0.0000 + re-arm $0.0000 = $0.0000 vs rewrites replaced $0.0000, cheaper than one rewrite: n/a, undelivered 0
- toasts by cause (waiting): question 0, review 0, replan 0, permission 0, input 0, crash 0, idle 0, stop 0, subagent-stop 0, model 0, other 0
- router turns by cause: loop 31, relay 0, re-arm 0, other 25, relay cost $0.0000
- retriever re-asks: 1 of 2 retriever briefs repeat a lookup of the same run
- router: main 92285478 · requests 123 · ctx at end 379.0k · growth aeb7ae01 +2.7k, ad3d9330 +33.2k, acd04d44 +15.7k, a04be586 +26.7k, ab53c08e +1.8k, a37fed04 +4.1k, a3f345ad +8.2k, a280d18f +1.9k, ad33fdf0 +33.7k, a9e475b0 +1.7k, a1c5cfcd +1.8k · top: bash other 23.4k/40, Read 20.0k/23, tools/plan_edit.py 12.7k/21, Agent 11.9k/11, other 7.2k/74
- noise: 19 lines 1.2k chars — usage: : 16 lines/978, No such file or directory: 3 lines/185
- price: read $0.20/Mtok · 1h write $7.90/Mtok · output $19.74/Mtok (phase model mix)
- median requests after a read: 0
- carry/request: 2545 chars, 363 requests (prev 8715 chars, 484 requests, growth -70.8%)
- flag: 5 whole-plan reads by main
- flag: 8 tool-source reads by pa-coder, pa-expert
  D:/NEXTJJEN/repos/outbreak-modern/tools/gate/gate.py  11.3k chars  pa-expert
  D:/NEXTJJEN/repos/outbreak-modern/tools/gate/gate.py  5.1k chars  pa-coder
  D:/NEXTJJEN/repos/outbreak-modern/tools/ob.sh  1.6k chars  pa-coder
  D:/NEXTJJEN/repos/outbreak-modern/tools/gate/gate.py  15.3k chars  pa-coder
  D:/NEXTJJEN/repos/outbreak-modern/tools/ob.sh  9.6k chars  pa-coder
  D:/NEXTJJEN/repos/outbreak-modern/tools/gate/refs.py  6.7k chars  pa-coder
  D:/NEXTJJEN/repos/outbreak-modern/tools/ob.sh  9.6k chars  pa-coder
  D:/NEXTJJEN/repos/outbreak-modern/tools/gate/refs.py  6.7k chars  pa-coder
- flag: router end context 379.0k over audit.router_ctx_flag

## Agent runs
- - | pa-expert | expert | claude-opus-5-5 | medium | ctx 71k | $1.130 | saved - | completed | parent 92285478
- - | pa-coder | coder | claude-opus-5-5 | medium | ctx 34k | $0.355 | saved - | completed | parent a37fed04
- - | pa-expert | expert | claude-opus-5-5 | medium | ctx 66k | $0.869 | saved - | completed | parent 92285478
- T2 | pa-coder | coder | claude-opus-5-5 | medium | ctx 69k | $1.352 | saved - | completed | parent a3f345ad
- - | pa-expert | expert | claude-opus-5-5 | medium | ctx 88k | $1.382 | saved - | completed | parent 92285478
- - | pa-expert | expert | claude-opus-5-5 | medium | ctx 150k | $3.160 | saved - | completed | parent 92285478
- - | retriever-web | retriever | claude-haiku-5-5 | medium | ctx 38k | $0.013 | saved - | completed | parent ad33fdf0
- - | retriever-code | retriever | claude-haiku-5-5 | medium | ctx 59k | $0.020 | saved - | completed | parent ad33fdf0
- - | pa-coder | coder | claude-opus-5-5 | medium | ctx 48k | $0.546 | saved - | completed | parent ad33fdf0
- - | pa-expert | expert | claude-opus-5-5 | medium | ctx 70k | $0.902 | saved - | completed | parent 92285478
- - | pa-expert | expert | claude-opus-5-5 | medium | ctx 57k | $0.696 | saved - | completed | parent 92285478

## Discussions
- (none)

## Deferred
- from T8: phase 1.2 closes; phase 1.3 starts from gate/baseline-1.2.md and the line-doubling fix. (unconsumed)

## Changes
- 2026-10-22 developer: Plan approved 2026-10-22 ("tackle T1-5"); developer takes the T4 PCSX2 screenshots after T3; T5 needs T4's references, so it runs after T4.
- 2026-10-22 router: T1 next -> done
- 2026-10-22 router: T2 next -> done
- 2026-10-22 router: T3 next -> done
- 2026-10-22 router: T4 next -> done
- 2026-10-22 developer: T5 question answered 2026-10-22: (Q1) title, mainmenu, charselect split into strict UI regions (d, p as static screens) + a coarse background check (statistic + tolerance from PCSX2 a-vs-b), no plain mask; regions + metric added in T5 (tool change in tools/gate/gate.py via coder); (Q2) runner line-doubling = runner fault, moved to phase 1.3 scope (GENERATION_PLAN.md); refs not retaken with Bob.
- 2026-10-22 developer: T5 second question answered 2026-10-22: animated screens (title, mainmenu, charselect) use the full-frame check with the same d, p, scored on the closest frame of a capture window long enough to cover each animation loop; marked UNVERIFIED in TOLERANCE.md until the backgrounds render; re-checked first in phase 1.4 (GENERATION_PLAN.md); fallback there = background-only check. Region split dropped.
- 2026-10-22 developer: T5 third question answered 2026-10-22 (option A): title/mainmenu/charselect backgrounds are Sofdec movies (M5); those three screens stay UNVERIFIED and advisory in the gate (reported, not counted toward exit code) until movie playback exists; generation 1 gates savedata/access/capcom pass/fail; UI fixes on the movie screens judged by side-by-side review vs the PCSX2 refs. GENERATION_PLAN.md done-criteria, 1.4, 1.5, 1.6 reworded; the 1.4 best-frame re-check is dropped (moves to the movie generation).
- 2026-10-22 router: T5 next -> done
- 2026-10-22 developer: T6 review: developer approved the freeze verbatim "Freeze the tolerance as drafted: d=8, p=0.0001, advisory title/mainmenu/charselect; hash in a follow-up commit." Freeze commit 20233c3.
- 2026-10-22 router: T6 next -> done
- 2026-10-22 router: T7 next -> done
- 2026-10-22 router: T8 next -> done

## Plain-English Recap
Phase 1.2 built the gate: the measuring stick for whether our build draws a screen the way PCSX2 does. `bash tools/ob.sh gate` rebuilds the runner if anything changed, boots the game hidden to character select, captures six screens and compares each with a PCSX2 screenshot of the same screen, pixel by pixel. A run takes about 55 seconds.

The references are twelve PCSX2 2.9.134 screenshots (two boots, six screens each), taken by the developer with the Software renderer at the game's native 512x448. The images stay in C:\ob\ref (they are the game's artwork); only their settings and checksums are committed (gate/ref_manifest.json).

The tolerance is frozen (commit 20233c3, gate/tolerance.json and gate/TOLERANCE.md). "d" is how far apart one pixel's colour may be and still count as matching: 8, from the two emulators rounding 16-bit colours differently plus one blending step. "p" is the share of pixels allowed to exceed d: 0.0001 (about 23 pixels), above both sides' measured noise (zero on the static screens) and below the smallest real fault measured (48 missing pixels). The self-test proves the gate passes PCSX2 against itself and fails deliberately damaged copies.

Three discoveries changed the plan, each decided by the developer. Our build shows only half of each frame's lines, doubling one field; that is our fault and opens phase 1.3. The title, main menu and character select play Sofdec movies (CRI's video format) behind their menus, which our build cannot play yet (milestone M5); those three screens are "advisory": printed, not counted, and their menus are judged side by side with the PCSX2 shots. The generation's done-criteria and phases 1.4 to 1.6 were reworded to match.

Today's baseline is red, as expected: save-data 4.4% of pixels off, ACCESS 24.9%, CAPCOM 8.2% (gate/baseline-1.2.md). The struggles, fixes and mistakes are in reports/2026-10-22-gate.md.
