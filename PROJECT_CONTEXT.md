# outbreak-modern — Project Context & Roadmap

> **Version:** 1.0.0
> **Generated:** 2026-10-21
> **Generation:** 1 | **Tech Stack:** PS2Recomp (C++ static recompiler + runtime, pinned `eb8688b` + numbered patches) · MSVC / CMake on Windows · Python tools · raylib presenter (today; see Open items)

---

## For Humans — Quick Guide

This is your project's **permanent constitution** — the vision, decisions, architecture, and the
generation map. Written once, **never edited**. State, rules and the live plan evolve elsewhere.

**The document system (where everything lives):**

| File | What it is | Who edits it |
|---|---|---|
| **This file** | permanent vision, decisions, architecture, generation map | nobody, after generation |
| `CLAUDE.md` | ≤300 tokens: roles, pointers, the always-rules | the installer |
| `project-architect` skill | the standing rules every agent loads | the installer |
| `HOW_WE_WORK.md` | developer profile, tools, paths, build/test, standing decisions | any task that changes one (H7) |
| `GENERATION_PLAN.md` | the live roadmap: phases, milestones, open/closed | Claude's plan, installed by the mod; `plan_edit.py gen-close` |
| `phase-ends/current/PHASE_PLAN.md` | the approved plan of the open phase | `tools/plan_edit.py` only |
| `phase-ends/` | PhaseEnds, GenerationEnds, task/research indexes | scripts at each close |
| `rules/` `cookbook/` `docs/ops/` | full rule texts, techniques, ops detail — one file per entry, found by grep on the INDEX | the task that earns the entry |

**To work on this project:** open `claude` in the repo and talk with Claude (PA 3.2). Governance methodology:
`docs/project-architect.md`.

---

## Rules & Protocols

Not in this file. A permanent-static document would freeze them.

- Standing rules for every agent: the **`project-architect`** skill (`.claude/skills/project-architect/SKILL.md`).
- Full rule texts, one file per rule: **`rules/INDEX.md`** → `rules/<id>.md`.
- Pointers and the always-rules: **`CLAUDE.md`**.

**Precedence:** where this file's wording ages out of step, the project-architect skill and `rules/` govern.

---

## Quick Reference Card

- **Project:** outbreak-modern — a native (statically recompiled) build of Resident Evil Outbreak File #1 (SLPM-65428, JP v1.04 + English fan patch v2.1) for Windows first, then Linux and Android, with modern controls; grown out of the Modern Camera patch for ARMSX2 that this repo also holds.
- **Goal:** one codebase that builds native Windows, Linux and Android (AYN Odin 3 first) games of File #1 from the player's own disc, playable single player with the Modern Camera built in, and later able to play online on Obsrv with PS2/PCSX2 players (cross-play).
- **Definition of done (the milestone gate):** each generation's gate compares what our build does with PCSX2 doing the same thing on the same disc, machine-checked, under a tolerance frozen before any fix is measured. Generation 1's gate: six boot/menu screens dumped from a scripted boot match PCSX2 2.9.134 reference screenshots (`bash tools/ob.sh gate`, built in phase 1.2).
- **Stack:** PS2Recomp `eb8688b` + `upstream/ps2recomp/0001-*.patch`… (analyzer, recompiler, ps2xRuntime with software GS, IOP interpreter, raylib window) · MSVC 19.44 / VS 2022, CMake 3.30 · MSYS2 GCC + gdb for debugging · Python (`tools/`) · Outbreak-decomp `eb6af38` as the function map and the source of names · PCSX2 2.9.134 as the reference.
- **Environment:** see `HOW_WE_WORK.md` and `docs/ops/INDEX.md`.
- **Current generation:** 1 — Correct 2D: the PS2Recomp Windows build draws its boot and menu screens as PCSX2 does.

---

## Project Overview

The repo began as **Outbreak Modern Camera on ARMSX2** (`README.md`): an over-the-shoulder camera,
free aim and modern controls for File #1, shipped as a `.pnach` patch (`mod/`, built by
`python mod/build.py`) that ARMSX2 on Android loads by itself. That patch holds only our code and
game addresses (from the CC0 Outbreak decomp), never game code.

The project now aims past the emulator: a **native port** (`docs/native-port-design.md`). The
game's MIPS code is statically recompiled to C++ at build time from the user's own disc, linked
against a platform layer, and run natively. Decompiled C may replace recompiled functions one by
one as the decomp grows. Milestone 0 (`docs/milestones/m0-decision.md`, 2026-10-21, the
developer's choice, option B) settled the base: **build on PS2Recomp and improve it**, with our
own graphics, sound and Android layers on top where PS2Recomp's are missing or too slow. Every
change to PS2Recomp is a numbered patch in `upstream/ps2recomp/` carrying a test that fails
before and passes after.

Where it stands (2026-10-21, `reports/2026-10-21-native-port-bring-up.md`,
`reports/2026-10-21-windows-port-bank-wall.md`): with patches 0001-0018 the Windows build boots
the game through its own setup, loads its archives and first sound bank, runs the DEMO and
SUBMAIN overlays as recompiled guest modules, and reaches ACCESS, CAPCOM, the title, the
save-data screens, the main menu, Scenario Select and character select — driven blind by a
temporary auto-press. **Rendering is wrong on every screen** (missing title background,
misshapen glyphs, colour fringing on ACCESS, corrupted character-select portraits); causes
unknown. That is generation 1's work.

### Project Assumptions

- The player supplies their own disc (SLPM-65428 v1.04, English patch v2.1, sha1-checked against
  the decomp). Recompiled code is built from it on the player's machine.
- PS2Recomp's model (R5900 → C++ with 128-bit GPRs, MMI, VU0 macro mode, PS2 float rules, one
  guest thread at a time, IOP interpreter running the real IRX modules) is a sound base; its
  correctness bugs are fixed upstream-style as patches (m0-decision.md).
- PCSX2 is the behavioural reference ("black box to compare against", as REO does). Real PS2
  hardware is not assumed available.
- The Outbreak decomp (`eb6af38`) is trustworthy for names, function boundaries and layout; its
  splat function list took recompiler errors from 8,261 to 0 over 5,358 functions
  (native-port-bring-up struggle 7).
- File #1 only. File #2 is out of scope (m6).
- Not legal advice; the legal constraints below follow the decomp's `docs/LEGAL.md`.

---

## Lessons Learned / Known Risks

- **Bugs in the recompiler look like game bugs.** The method that worked at every wall: find the
  exact loop the game is in (pc → decomp function → decomp C), prove the cause in a debugger (gdb
  on a GCC build; PCSX2's debugger for hardware behaviour), then fix with a test that fails first
  (both 2026-10-21 reports).
- **The analyzer silently rewrites code.** PS2Recomp's "self-modifying code" heuristic replaced
  37 of the game's own stores with NOPs (patch 0012 now reports, never rewrites).
- **Runtime timing is not hardware timing.** SPU2 streaming DMA completed 200x too fast and
  starved the sound bank upload (patch 0015); vblank runs at 60.00 Hz, not 59.94; instruction
  timing uses fixed weights (m0-decision.md). PCSX2 and our build are never on the same vsync for
  the same event (6 vs 5 vsyncs for one bank load), so comparisons align by game state, not time.
- **Case-sensitive file lookup** is a Linux/Android bug class (patch 0006).
- **MSVC path limit:** builds need short paths; the working tree, game data and build dirs live
  in `C:\ob` outside the repo (bank-wall struggle 1).
- **Uncommitted hacks:** the frame dump, auto-press and traces live only in the `C:\ob\src`
  working tree (bank-wall "Still open"); phase 1.1 moves them into patches or drops them.
- **Performance is unmeasured.** REO (the US-version recomp on PS2Recomp) is ~35x slower than
  real time in 3D. Suspects in PS2Recomp (m0-decision.md, all unprofiled): the scalar software GS
  rasteriser, VU1 interpreter in `long double` (software quad on Android arm64), unresolved `jalr`
  labels, the `jal` dispatcher, slow scratchpad path, exceptions for context switches; all on one
  host thread. Nothing has been timed on the Odin.
- **Cross-play correctness gaps** in PS2Recomp listed in m0-decision.md: patches 0002-0005 fixed
  the 64-bit branch compares, CVT.W.S, VU0 divide/sqrt and VU0 memory ops; still open are host
  round-to-nearest in the EE FPU, double rounding in MADD/MSUB, MAX/MIN and C.cond on
  exponent-255 values, the MAC/status flags, and MSVC `/fp:fast` possibly undoing the float
  emulation. The VU0 divide-by-zero value (±0x7FFFFFFF vs PCSX2's ±0x7F7FFFFF) is unknown.
- **Untested platforms:** patches 0010-0018 ran on MSVC only; Clang last ran at 0006 (450/450),
  Linux GCC/Clang 445/445 at 0001; the Android runtime `.so` builds (NDK r27c) but no APK has run.
- **Overlays not built yet:** GAME, NETWK, SINGLE, NETHTTP, NETAQ; NETDNAS only its first 30
  functions (the decomp's splat output for it is one data block).
- **Run-time code patches:** `patch.c` `func_001D9810` can patch overlay code after load, and
  Obsrv sends File 1 clients a 0x7AA0-byte code patch; a static recompiler cannot translate code
  it has never seen (milestones README, corrections; m7).
- **Dead ends already tried:** writing our own recompiler (superseded by M0); PS2Recomp's
  fallback analyzer (8,261 errors); PCSX2 memory breakpoints on hardware registers (never fire).

---

## Project Philosophy

**North Star (one sentence):** Resident Evil Outbreak File #1 running natively on PC and the Odin from the player's own disc, behaving exactly as on a PS2 — so faithfully that it can one day share an online room with PS2 players — with modern controls on top.

- **Faithful first, better second.** Correctness against PCSX2 comes before speed or features;
  enhancements are presentation only and never touch the 30 Hz logic, game memory or what is
  sent online (design Decision 6).
- **Measure against the reference.** Every milestone is a machine check against PCSX2 (frames,
  sound, packets), with a tolerance justified and frozen before fixes are measured.
- **One wall at a time, proved.** Find the exact cause, prove it, fix it with a test that fails
  first, one numbered patch per change.
- **Only our code in the repo.** Game code and anything derived from the disc stay out.
- **Build on what exists.** Use PS2Recomp, the decomp and PCSX2's knowledge rather than rewrite;
  offer fixes upstream (only with the developer's OK).

---

## Key Decisions

| Decision | Choice | Rejected alternative | Why |
|---|---|---|---|
| D1 How the game's code runs | Static recompilation of every MIPS function to C at build time, decompiled C replacing functions one by one | wait for the decomp (years); embed an interpreter/JIT (an emulator again, a JIT per CPU) | native speed on x86 and ARM, same generated code everywhere, EE-exact maths emitted as calls (design Decision 1) |
| D2 Guest memory | The game's 32 MiB + scratchpad in one reserved block; every stored pointer is a 32-bit PS2 address (`base + addr`) | native 64-bit pointers; a 32-bit build | data layout identical on PS2/Windows/Android byte for byte (packets, saves); Modern Camera's `MEM(type, addr)` compiles unchanged (design Decision 2) |
| D3 Platform layer | SDL3 (window, input, audio, Android activity), Vulkan on all platforms, mbedTLS for the legacy DNAS ciphers, CMake everywhere | per-platform APIs; a D3D/GL split | one API and one renderer for Windows, Linux, Android (design Decision 3). **OPEN: today's base presents through raylib; see Open items.** |
| D4 System libraries | Replace Sony libraries, kernel and services at the call (HLE), shared by all platforms; GS interpreter first, VU1 programs recompiled to C, then shaders; native sound driver; IOP never emulated | emulating the hardware | one shared library written once (design Decision 4). PS2Recomp in fact runs the real IRX modules on an IOP interpreter and interprets VU1 (m0-decision.md); see Open items. |
| D5 The game's clock | Logic at its original 30 Hz tick everywhere, driven by display timing; rendering may interpolate on top | variable-rate logic | online play and cross-play need it (design Decision 5) |
| D6 Visuals and performance | Everything here presentation only: PCSX2-format texture packs keyed as PCSX2 keys them, BC7/ASTC conversion offline, per-room streaming, interpolation, resolution scale, widescreen; each switchable, off online if in doubt | changes to logic or memory for looks | keeps cross-play safe (design Decision 6) |
| M0 The base | **Option B: build on PS2Recomp (`eb8688b`) and improve it**, numbered patches with tests, offered upstream with the developer's OK (developer, 2026-10-21) | A: write our own recompiler; C: build on REO (US-version, different fork) | PS2Recomp already has R5900→C++, MMI, VU0 macro, PS2 float rules checked against ps2autotests, a scheduler, an IOP, an Android runner (m0-decision.md, m0-choose-the-base.md) |
| G1 First generation | Correct 2D in the Windows build, against PCSX2 screenshots | speed work; an SDL3/Vulkan presenter; Linux/Android first | rendering is the current wall; correctness before speed; Windows is where the boot runs (GENERATION_PLAN.md) |

---

## Core Logic / Strategy

Build pipeline (per player, per disc): the user's disc files are extracted to a folder; the
decomp's splat output gives every function boundary (`tools/splat_functions.py` →
`main-functions.csv`); `ps2_analyzer` + `ps2_recomp` turn the main ELF into C++ (0 errors over
5,358 functions); each `MWo3` overlay is wrapped as an ELF (`tools/mwo3_to_elf.py`) and
recompiled as a **guest module** whose function table is selected by the overlay header resident
in RAM (patches 0017-0018), so overlays sharing an address each keep their own code; the
generated sources build with the runtime into `ps2EntryRunner`.

Run time: the runtime maps guest memory, runs one guest thread at a time in PS2 priority order,
answers kernel and library calls by HLE, runs the game's IOP modules on its IOP interpreter
(sound driver, CD, pad via libpad2/libdbc served natively, patch 0016), and draws through a
software GS: GIF → `GS::processGIFPacket` → `GSCpuBackend` draw → `PresentFromLocalMemory` (CRTC
merge, field parity) → latched RGBA frame → raylib texture, letterboxed.

Verification strategy: a scripted, hidden boot reaches fixed game states; captures are compared
with PCSX2 captures of the same states; a frozen tolerance decides green.

### Component Inventory

- `upstream/ps2recomp/` — our numbered patches against PS2Recomp `eb8688b` (0001-0018 today) and
  its README (patch table, test rule).
- `PS2Recomp-main/PS2Recomp-main/` — a vendored copy of PS2Recomp with patches applied (at least
  through 0016; equality with `eb8688b` + 0001-0018 unverified).
- `C:\ob` (outside the repo) — PS2Recomp working copy, upstream-with-patches commits, build
  dirs, game files, overlay configs, logs, frame dumps, decomp checkout.
- `tools/` — Python and shell tools: the recompile cycle, `mwo3_to_elf.py`, `splat_functions.py`,
  and the ARMSX2/PINE tools for the camera patch.
- `mod/` — the Modern Camera patch (C + EE assembly, clang/ld.lld → `.pnach`), with emulator
  tests; to be built into the native game at M6.
- `docs/` — the design (`native-port-design.md`), milestone plans (`milestones/`), the Android
  port map, the phase-1 feasibility report; `reports/` — dated session reports.

---

## Safety / Guardrails / Error Handling

- **Never commit game data** or anything derived from it: the ELF and overlays, IRX modules, disc
  archives, recompiled C, dumped frames, PCSX2 screenshots. They live under `C:\ob`; the repo
  holds our code, scripts, input scripts and hashes/manifests of references. `.gitignore` lists
  `game_files/`.
- **Never distribute binaries that contain game code** (decomp `docs/LEGAL.md` rule 1). A
  recompiled build is game code, so players build it themselves from their own disc; the design's
  "CI builds a Windows zip and an APK" cannot stand as written (milestones README finding 2; the
  Android route is open, m6).
- **Nothing live on Obsrv** without the Obsrv admins' agreement (LEGAL.md rule 5); local servers
  first.
- **The developer's specific "proceed"** for PCSX2 or any visible window, anything on the AYN
  Odin, installs, and anything that leaves the machine (push, PRs, upstream offers).
- **Every PS2Recomp change** is a numbered patch with a test that fails before and passes after;
  the suites stay green after each patch.
- **Gate tolerances** are frozen with written evidence before fixes are measured and change only
  through a developer-approved plan change, never to make a screen pass.
- Missing guest functions surface as `[guest-branch:missing-target]` with the address, never a
  silent skip.

---

## Architecture

### Project Structure

```
outbreak-modern/
  mod/                    Modern Camera patch for ARMSX2 (C + asm -> .pnach), tests
  patches/                the built .pnach
  tools/                  ARMSX2/PINE tools; recomp cycle; mwo3_to_elf.py; splat_functions.py; PA tools
  upstream/ps2recomp/     numbered patches against PS2Recomp eb8688b + README
  PS2Recomp-main/         vendored PS2Recomp (analyzer, recompiler, ps2xRuntime, IOP, tests)
  docs/                   native-port design, milestones/, port map, feasibility report, ops/
  reports/                dated session reports
  results/                tool outputs and probe records
  ps2_emu/                portable PCSX2 2.9.134 (the reference)
  game_files/             local game files (ignored; never committed)
  phase-ends/ rules/ cookbook/ templates/   Project Architect
C:\ob\                    out-of-repo workspace: src, up, t, r, g, work, logs, frames, decomp-src, ovl, ref
```

The design's proposed separate repository (`outbreak-native/` with `recomp/ runtime/ hle/ gfx/
platform/ net/ game/ apps/ tests/`) has not been created; the work lives here (see Open items).

### Services / Dependency Wiring

User disc folder → analyzer/recompiler (with the decomp function list) → generated C++ +
module tables → `ps2EntryRunner` (ps2xRuntime: guest memory, EE scheduler, HLE kernel/libraries,
IOP interpreter + IRX modules, VIF/VU1 interpreter, software GS, raylib presenter and gamepad).
PCSX2 runs beside it only as the reference. Later: SDL3/Vulkan or other presenter, native sound,
FFmpeg movies, sockets + TLS for the network.

### Config / Settings

- Recompiler TOML per ELF/overlay (`analyzed.toml` → `decomp.toml` with `ghidra_output` set to
  the function CSV; overlay `module` configs under `C:\ob\ovl`).
- Runtime environment variables (e.g. the frame dump directory and scripted input, as they become
  patches in phase 1.1).
- Pins and paths: `HOW_WE_WORK.md`.

---

## Testing & Validation Strategy

- **Patch-level:** each PS2Recomp patch adds a test that fails first. Suites: `ps2x_tests`
  (461/461 after 0018, MSVC), the IOP ctest (4/4), the GS cache ctest (45/45, last run before
  0010) — all stay green.
- **Instruction-level:** PS2 float rules checked against ps2autotests hardware vectors (177 rows
  in PS2Recomp); unicorn tests for the camera patch's EE code (`mod/test_mod.py`).
- **Milestone-level:** behaviour compared with PCSX2 at fixed game states reached by scripted
  input: frames for rendering (generation 1), waveforms for sound, memory-card files for saves,
  packets and per-tick state for the network (m3, m5, m7).
- **Validation phases:** each generation ends with a clean rebuild from `eb8688b` + patches by
  `git am`, suites green, and the gate green on repeated runs.

**Pass criteria:** the generation's gate exits 0 from a clean rebuild on repeated runs with its
frozen tolerance unchanged, and every suite is green. Generation 1: all six screens (ACCESS,
CAPCOM, title "PRESS START", save-data screens, main menu, character select) within the
tolerance frozen in phase 1.2.

---

## Generation Map

*The permanent sketch: what each generation is for, and the milestone-shaped phases it is expected
to need. Milestones only — no task checklists. The live phase list (names, scope, dependencies,
open/closed, PhaseEnd links) is `GENERATION_PLAN.md`, and the tasks of the open phase are
`phase-ends/current/PHASE_PLAN.md`. Where the two disagree, the live plan governs.*

Milestone status before generation 1 (sources: `reports/2026-10-21-native-port-bring-up.md` =
NPB, `reports/2026-10-21-windows-port-bank-wall.md` = BW):

- **M0 Choose the base** — done 2026-10-21: option B, PS2Recomp (NPB struggle 2; m0-decision.md).
- **M1 Skeleton** (SDL3 + Vulkan window, guest memory, ISO/AFS reader) — not done as written.
  The base supplies a raylib window, guest memory and a folder-based disc reader (not ISO); the
  runtime builds for Linux (445/445, NPB struggle 3) and Android arm64 (`.so` only, no APK, no
  device run; m0-decision.md). The SDL3/Vulkan part is open (Open items).
- **M2 Recompiler, main ELF** — passes on Linux: gdb backtrace `_start` → `main` → boot graphics
  → `sceGsSyncV` (NPB struggle 11); main ELF recompiles with 0 errors (NPB struggle 7); rebuilt on
  Windows (BW). Not run on Android.
- **M3 2D** — in progress, generation 1: the boot reaches ACCESS, CAPCOM, title, save-data
  screens, main menu, Scenario Select and character select with libpad2 input (patch 0016) under
  a blind auto-press; rendering wrong on every screen; no real-controller play; no golden-frame
  test yet; DEMO is loaded as a guest module, which answers m3's "load DEMO early" question in
  practice (BW Outcome, struggle 8, Still open).
- **M4 Overlays + 3D** — started: overlay mechanism (patches 0017-0018) with DEMO, SUBMAIN and
  part of NETDNAS; GAME and the rest not built; VU1 programs not split or recompiled; no room
  drawn (BW struggle 8).
- **M5 Sound, saves, movies** — started: Tamsoft sound driver connects (patch 0008, NPB struggle
  14) and the first sound bank arrives (patch 0015, BW struggle 5); no audible output (no SPU2);
  memory-card prompt answered, saves not tested; movies not started (BW).
- **M6 Single player end to end + beta** — not started; the distribution route is undecided.
- **M7 Network and cross-play** — not started.

### Generation 1 — Correct 2D
Scope: the PS2Recomp Windows build of SLPM-65428 draws its boot and menu screens as PCSX2 does; correctness in the software GS and its presenter only; no speed work, no new presenter; Windows only.
Done when: one repo command rebuilds the runner from `eb8688b` + our patches, runs a hidden scripted boot to character select, dumps the six gate screens, and each is within the tolerance frozen in 1.2 against a PCSX2 2.9.134 reference; the suites stay green; this file and the card describe the project as it is.

- **Phase 1.1 Groundwork** — milestone: this constitution and a complete card; repo commands rebuild the runner from `eb8688b` + patches and run a hidden scripted boot that ends at character select, leaving tagged frame dumps of the six screens.
- **Phase 1.2 The gate** — milestone: the gate compares the six screens with PCSX2 references under a justified, frozen tolerance, passes PCSX2-vs-PCSX2, fails a damaged frame, and prints today's (red) baseline.
- **Phase 1.3 Boot logos** — milestone: ACCESS and CAPCOM green, no other screen worse.
- **Phase 1.4 Title and save-data screens** — milestone: those screens green, earlier ones stay green.
- **Phase 1.5 Menus and character select** — milestone: main menu and character select green, all earlier stay green.
- **Phase 1.6 Validation** — milestone: from a fresh clone + `git am`, suites green and the gate green on three consecutive runs, tolerance unchanged since its freeze.

Each phase produces something runnable and testable and ends at an observable, machine-checkable
milestone (the gate P9/M1 hold it to). Validation is a phase of its own; enhancement layers are
toggleable and added one at a time.

---

## Enhancement Backlog
*(Toggleable feature-flagged modules to add one at a time, measured before the next — not scheduled into a generation yet.)*

All presentation only (design Decision 6), off by default and off online unless shown to be visual only:

- PCSX2-format texture packs (keys computed from GS memory at draw time, as PCSX2 does), then
  BC7/ASTC offline conversion, per-room streaming, LRU budget, bindless textures.
- Interpolated rendering between 30 Hz ticks (60/120 Hz display).
- Internal resolution scale 1x-4x, widescreen in the projection, MSAA/FXAA.
- Performance work once profiled: GPU GS renderer, VU1 fast path without `long double`, VU1
  programs as shaders, draw batching, SSE/NEON helpers.
- In-game tester report (device, GPU/driver, settings, frame times, memory, last log seconds).

---

## Future Generations
*(Evolutionary leaps, not version bumps — sketched, not phase-detailed.)*

Order after generation 1 is the developer's call at each generation start; the milestone order
of the design is the default:

- **Overlays and 3D (M4):** every overlay as a guest module, VU microcode split out, the 27 VU1
  programs tested against PCSX2, the first offline room matching PCSX2 golden frames.
- **Sound, saves, movies (M5):** audible sound effects and ADX music matching PCSX2 by waveform,
  saves through the memory-card code to files, Sofdec movies.
- **Single player (M6):** a File #1 offline scenario start to end at full speed on a reference PC
  and the Odin, Modern Camera built in; a distribution route that ships no game code; the beta.
- **Platforms:** Linux (re-run suites with GCC/Clang, then the gates) and Android on the Odin
  (APK, file picker, pause/resume, on-device speed); where they enter the sequence is open.
- **Network and cross-play (M7):** a local BioServer lobby and room, packets byte-identical to
  PCSX2's for the same inputs, a mixed PS2/PCSX2 + native room with no desync; live on Obsrv only
  with the admins' agreement.

---

## What Success Looks Like

A player with their own File #1 disc builds the game on their PC (and later for their Odin),
starts it without an emulator, and plays an offline scenario start to end at full speed with the
Modern Camera, looking and sounding as it does in PCSX2 — every milestone of the way proved by a
machine check against PCSX2. Ultimately, the same build joins a mixed online room with PS2 and
PCSX2 players and never desyncs.

---

## Feature & Architecture Inventory

- Modern Camera patch for ARMSX2 (shipped): over-the-shoulder camera, free aim, quick turn,
  camera-relative movement, wall collision, Debug aim trim; PINE tools and tests.
- PS2Recomp patches 0001-0018: SSE4.1 for GCC/Clang; 64-bit branch compares; CVT.W.S/TRUNC.W.S;
  VU0 VDIV/VSQRT/VRSQRT and memory ops; case-insensitive VFS; IOP LOADFILE from IOP RAM; IOP
  message boxes; `sceCdLayerSearchFile`; thread priority 0; `ChangeThreadPriority` return;
  analyzer reports instead of NOPing; `sceDmaRecvN`; heap and runtime pools below 0x100000; SIF
  `sceSifGetOtherData` + SPU2 block timing; libpad2/libdbc; guest modules (overlays) in runtime
  and recompiler.
- Our tools: `mwo3_to_elf.py`, `splat_functions.py`, the recompile cycle.
- Planned in generation 1: frame dump (patch), scripted input and hidden window (patch), the
  frame-comparison gate.

---

## Data Sources / External Dependencies

- **The game:** SLPM-65428 v1.04 + English patch v2.1, the player's own; all binaries
  sha1-verified against the decomp. Never in git.
- **GalaxyNostalgia/Outbreak-decomp** `eb6af38` (CC0): function map, names, layout, LEGAL.md,
  NETWORK.md.
- **ran-j/PS2Recomp** `eb8688b` (GPL-3.0): the base.
- **sp00nznet/reo** `de19db7` (GPL-3.0): read-only reference (US version).
- **PCSX2** 2.9.134 portable (`ps2_emu/`): the behavioural and visual reference; its source
  (GPL-3.0) for GS layouts, texture hashing, SPU2.
- **ps2sdk** `329e2bb`, **ps2autotests**: reference only.
- **Obsrv / BioServer:** the online server (later; admins' agreement for anything live).
- GalaxyNostalgia/Outbreak-PC: could not be read (private, renamed or gone).

---

## Libraries / Dependencies

- PS2Recomp and what it brings: raylib (window, input, texture upload; stb_image_write via
  `ExportImage`), sse2neon on ARM, FFmpeg HLE for movies on desktop.
- Toolchains: MSVC 19.44 / VS 2022, CMake 3.30, MSYS2 GCC 16.1 + gdb 17.2 (Windows); GCC 13.3,
  Clang 18.1 (Linux); Android NDK r27c; clang + ld.lld for the camera patch; splat 0.50.0,
  rabbitizer 1.16.2 for the decomp.
- Python (standard library for tools; unicorn for camera-patch tests).
- Design-planned, not yet in use: SDL3, Vulkan, mbedTLS, FFmpeg (LGPL/GPL by build), vgmstream's
  ADX decoder.

---

## Parking Lot
*(Dream features, someday/maybe ideas, out-of-scope thoughts — captured so they're not lost, explicitly not committed.)*

- File #2.
- Steam Deck / SteamOS and Flatpak packaging; Linux arm64.
- GLES/OpenGL fallback renderer for older Android; 1K/2K texture tiers.
- Touch controls on Android.
- PCSX2-compatible memory-card images to move saves between ARMSX2 and the native port.
- Building on the device (recompiler + compiler inside the Android app).
- Mixing decompiled C into recompiled code (needs the decomp's guest-address rule, design question 3).

---

## Notes for Future Phases

### Open items (recorded, not resolved)

1. **Design Decision 3 vs the base's presenter.** D3 chose SDL3 + Vulkan on every platform;
   PS2Recomp, the M0 base, presents through raylib (OpenGL) and reads the gamepad through raylib.
   Generation 1 keeps raylib and builds no new presenter; D3 is reopened for a later generation
   (GENERATION_PLAN.md standing constraints; m1 "Depends on M0"). Undecided: keep raylib, move to
   SDL3 + Vulkan, or another split.
2. **`docs/milestones/m0-decision.md` is stale in places.** Line 19 says the JP ELF has not been
   through the recompiler, and lines 36-40 say PS2Recomp has no overlay support and Outbreak
   cannot run past the main ELF; both stopped being true on 2026-10-21 (NPB: main ELF recompiled
   and booting; patches 0017-0018 run DEMO and SUBMAIN). Its correctness-gap list is partly fixed
   by patches 0002-0005. Whether to annotate the file or leave it as a dated record is undecided.
3. **No LICENSE file.** Using PS2Recomp (GPL-3.0), and reimplementing PCSX2's GS layouts, hashing
   or SPU2 from its source, brings GPL-3.0 obligations. The milestones README asked for a
   decision "before M1"; none has been made.
4. **Design Decision 4 vs the base:** D4 says "IOP never emulated" and "VU1 recompiled, never
   emulated"; PS2Recomp runs the game's real IRX modules on an IOP interpreter and interprets VU1.
   Undecided which way later generations go.
5. **Separate repository** (`outbreak-native`, design question 1): not created; all work is in
   this repo today. Undecided.
6. **Game files on `main`:** the NPB report says `game_files/` (program code and IOP modules) is
   committed on `main`, breaking the never-commit-game-data rule; cleanup postponed by the
   developer. On `claude/windows-boot-adx` it is ignored and untracked.
7. **Distribution route** for players without a build machine, especially Android (m6). Undecided.
8. **Upstreaming** patches to ran-j/PS2Recomp: none offered; waits for the developer's OK.

### Pointers

- Live roadmap: `GENERATION_PLAN.md`. Facts, pins, commands: `HOW_WE_WORK.md`.
- Milestone plans with their unknowns: `docs/milestones/m0..m7`; the design:
  `docs/native-port-design.md`; what a port must implement: `docs/android-port-map.md`.
- Session history: `reports/2026-10-21-native-port-bring-up.md`,
  `reports/2026-10-21-windows-port-bank-wall.md`.
