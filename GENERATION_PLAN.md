# Generation 1 — Correct 2D

Goal: the PS2Recomp Windows build of SLPM-65428 (RE Outbreak File #1, JP + English patch v2.1) draws its boot and menu screens as PCSX2 does.
Done-criteria: one repo command rebuilds the runner from PS2Recomp eb8688b + our numbered patches, runs a hidden scripted boot to character select, dumps frames at six fixed screens (ACCESS, CAPCOM, title "PRESS START", save-data screens, main menu, character select), compares each with a PCSX2 reference screenshot of the same screen; the three static screens (save-data, ACCESS, CAPCOM) are green (pass/fail) within the tolerance frozen in phase 1.2; the three movie-background screens (title, main menu, character select; their backgrounds are Sofdec movies, M5) are reported but advisory, and their game-drawn UI matches PCSX2 by side-by-side review (developer decision 2026-10-22, 1.2 T5); the PS2Recomp suites stay green; the constitution and the card describe the project as it is.

## The gate (built and frozen in 1.2)

- Command: `bash tools/ob.sh gate`: recompile if stale, hidden scripted boot (gate/boot.script), then compare each tagged capture with its reference. Exit 0 = every screen green.
- References: PCSX2 2.9.134 (ps2_emu/), Software renderer at native 1x, settings (deinterlace, aspect, filtering, screenshot size) fixed and recorded in a committed manifest with each image's sha256; images stay in C:\ob\ref (game-derived). Captured once, by or with the developer's "proceed".
- Alignment: our capture and PCSX2's screenshot are never the same vsync (the two emulators' timing differs: 6 vs 5 vsyncs for one bank load, R0-002), so each screen is captured as a window of frames once the script's guest-state predicate fires, and scored by its best frame in the window. This tests rendering, not timing; the window length is fixed in the manifest.
- Metrics per screen (R0-005): max per-channel delta over all pixels, and the share of pixels whose delta exceeds the per-pixel bound d. Green = share <= p. Both numbers printed for every screen, every run.
- Justifying d and p, before any fix is measured: (a) noise floor: PCSX2 against itself (two captures per screen) and our runtime against itself (T4 of 1.1 records it); (b) an analysis of differences that are not rendering faults, checked against PCSX2's GS source or the GS manual: 5-bit-to-8-bit colour expansion on 16-bit formats (one step is about 8 levels), dithering, field/CRTC merge. d is the largest such legitimate delta; p covers only measured edge/rounding effects. Both are written with their evidence in a committed file and frozen; a later change needs a developer-approved plan change.
- Verifying the verifier: the gate must pass reference-vs-second-reference and fail a deliberately damaged frame (a missing layer, a shifted glyph), before the baseline run.

## Phases

- 1.1 Groundwork | milestone: PROJECT_CONTEXT.md exists, the card has no TODO, and repo commands build the runner from eb8688b + patches 0001-0020 and run a hidden scripted boot that ends by itself at character select, leaving tagged frame dumps for the six gate screens (`bash tools/ob.sh recompile`, then `bash tools/ob.sh boot --script gate/boot.script`) | scope: constitution; card; C:\ob build/boot scripts into tools/; C:\ob\src traces dropped, frame dump as patch 0019; scripted input + hidden window as patch 0020 | depends: — | status: closed | phase-end: phase-ends/PhaseEnd_Phase1.1.md
- 1.2 The gate | milestone: the gate command compares the six dumped screens with PCSX2 references under a tolerance frozen and justified in a committed file, passes a PCSX2-vs-PCSX2 self-check, fails a deliberately damaged frame, and prints the baseline (expected red) for today's build (`bash tools/ob.sh gate`) | scope: reference capture in PCSX2 2.9.134 (fixed settings, developer's proceed); the gate tool; noise-floor measurement on both sides; tolerance analysis and freeze; baseline report | depends: 1.1 | status: closed | phase-end: phase-ends/PhaseEnd_Phase1.2.md
- 1.3 Boot logos | milestone: ACCESS and CAPCOM are green in the gate, no other screen's numbers get worse | scope: first, field presentation: the runner line-doubles one field (applyFieldPresentation, gs_cpu_backend.cpp:397-410) where PCSX2 shows 448 distinct lines (developer decision 2026-10-22, 1.2 T5); then diagnose and fix the colour fringing on ACCESS and anything wrong on CAPCOM in the GS/presenter path, as numbered patches with tests | depends: 1.2 | status: open | phase-end: phase-ends/PhaseEnd_Phase1.3.md
- 1.4 Title and save-data screens | milestone: the save-data screen is green in the gate, ACCESS and CAPCOM stay green; the title's game-drawn UI (PRESS START text, cut off at x=345 today) matches PCSX2 by side-by-side review (background = Sofdec movie, M5: out of scope) | scope: misshapen save-data glyphs (texture formats, CLUT, uploads, blending as the evidence says) | depends: 1.3 | status: open | phase-end: phase-ends/PhaseEnd_Phase1.4.md
- 1.5 Menus and character select | milestone: the game-drawn UI of the main menu and character select matches PCSX2 by side-by-side review (backgrounds = Sofdec movies, M5: out of scope), the static screens stay green | scope: corrupted character-select portraits and anything wrong on the main menu | depends: 1.4 | status: open | phase-end: phase-ends/PhaseEnd_Phase1.5.md
- 1.6 Validation | milestone: from a fresh clone of eb8688b with patches 0001-00NN applied by `git am`, the suites are green and the gate is green (static screens; movie screens advisory) on three consecutive runs, the tolerance file unchanged since its freeze commit (`bash tools/ob.sh gate`) | scope: clean rebuild; three gate runs; upstream/ps2recomp/README.md patch table; a reports/ entry | depends: 1.5 | status: open | phase-end: phase-ends/PhaseEnd_Phase1.6.md

## Ordering rationale

- Foundation before features: nothing can be measured while the dump and the input live as uncommitted hacks in C:\ob\src and the card has no build commands; 1.1 makes the boot reproducible from the repo (R0-002, R0-003).
- Observation before optimization: 1.2 builds and verifies the gate, and freezes its tolerance, before any fix is measured against it. A tolerance chosen after seeing fix results would be tuned to pass; freezing first is what makes green mean something (R0-005: no emulator project publishes a usable threshold, so it is derived from measurement).
- Fix phases follow boot order and rising complexity: logos (flat images, one fault seen) before title/save screens (textured 2D, CLUT fonts) before menus/character select (layered, portraits). Causes are unknown (R0-002); a root cause shared across screens is most cheaply found on the simplest screen, and a later phase whose screens are already green closes as a short confirmation.
- Grouping by screen, not by subsystem, because subsystem is what the phases discover; each fix phase's planner decides the diagnostic tools (e.g. replaying a PCSX2 GS dump through PS2Recomp's GS to separate GS faults from EE/VU1 data faults) at its own size.
- Validation is its own phase: 1.6 proves the result from a clean rebuild, not from the working tree that produced it.
- Later milestones (M4 overlays/3D, M5 sound/saves/movies, M6 single player, M7 network, the Linux/Android platforms) stay out; PROJECT_CONTEXT.md carries the generation map.

## Standing constraints

- Base: PS2Recomp pinned at eb8688b; every runtime/recompiler change is a numbered patch in upstream/ps2recomp/ made by `git format-patch` from C:\ob\up, carrying a test that fails before the change and passes after (upstream/ps2recomp/README.md:39-40); ps2x_tests, the IOP ctest and the GS cache ctest stay green after each patch (R0-003).
- Game data and anything derived from it (the ELF, recompiled C, dumped frames, PCSX2 screenshots) never enters git; it lives under C:\ob. The repo holds only our code, scripts, button/state scripts and hashes/manifests of references.
- The gate tolerance is frozen in 1.2 and changes only through a developer-approved plan change with written evidence; never to make a screen pass (rule P9).
- Rendering work is correctness in PS2Recomp's software GS and its presenter; no speed work, no SDL3/Vulkan presenter (design Decision 3 is reopened for a later generation; R0-004).
- PCSX2, any visible window and anything on the AYN Odin need the developer's "proceed" for that action: the expert returns `question`, never launches on its own. The Odin is out of scope this generation.
- Windows host, MSVC 19.44 / VS 2022, CMake 3.30; Python per the card. Clang/Linux re-runs follow the developer's decision at the 1.1 approval.

## Changes

<!-- - <date> claude|developer: <what> — <why> -->
- 2026-10-21 developer: phase 1.1 status -> closed
- 2026-10-22 developer: phase 1.2 status -> closed
