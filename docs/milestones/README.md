# Milestone plans for the native Windows + Android + Linux port

One plan per milestone of `docs/native-port-design.md`, plus a milestone 0 (a decision this
research showed is missing). Each plan lists the goal, the check that says it is done, the work,
what is needed to do it, the limitations, and **what is not known**.

Linux was added as a third platform on 2026-10-21 (`docs/native-port-design.md`, "Linux"). Where a
plan says "both platforms", read "Windows, Linux and Android".

Status: plans only. Nothing has been built or run. **No time estimates**: nothing has been
measured to base one on.

Labels, as in `docs/phase1-feasibility-report.md`:

- **Confirmed**: seen directly in source or a document, with the reference.
- **Unverified**: a source claims it, or it follows from something confirmed, but nobody here has
  tested it.
- **Unknown**: not known. Each says what would answer it.

Sources read for these plans (2026-10-20):

| Source | Version |
|---|---|
| [GalaxyNostalgia/Outbreak-decomp](https://github.com/GalaxyNostalgia/Outbreak-decomp) | `eb6af38` (2026-10-19) |
| [sp00nznet/reo](https://github.com/sp00nznet/reo) (an Outbreak recomp, see below) | `de19db7` (2026-10-19) |
| [ran-j/PS2Recomp](https://github.com/ran-j/PS2Recomp) | `eb8688b` (2026-10-14) |
| PCSX2 `pcsx2/GS/Renderers/HW/GSTextureReplacements.cpp`, `GSTextureCache.cpp` | `master` as fetched 2026-10-20 (commit not recorded) |
| GalaxyNostalgia/Outbreak-PC | **could not be read** (git asks for credentials: private, renamed or gone; unknown which) |

## Read this first: findings that change the design

### 1. A recompilation of Outbreak already exists (REO), on an existing PS2 recompiler

- **REO** (`sp00nznet/reo`, GPL-3.0) statically recompiles **the US version (SLUS-20765)** to
  native Windows x86-64, using **PS2Recomp**. Its README (2026-10-19) says a scripted boot reaches
  J's Bar, drawn by the game's own code, **about 35x slower than real time**. It has no sound
  output, a memory card image that doesn't mount yet, and green blocks in movies. Online play is
  out of scope. Confirmed (its README); not run by us.
- REO's own docs disagree with each other. The README says all nine overlays are recompiled.
  `docs/architecture.md` ("facts as of 2026-10-17") says two are. Which is current is **unknown**.
- REO doesn't say what makes it 35x slow. Its in-game 3D runs through an emulated VU1
  (`ps2_vu1_core.cpp` in PS2Recomp) and GS. That this is the cause is **unverified**.
- **PS2Recomp** (`ran-j/PS2Recomp`, GPL-3.0) is a general PS2 recompiler and runtime. It handles
  MMI and VU0 macro mode, overlays, function stubs by address, and guest memory. Its float macros
  follow PS2 rules (no Inf/NaN/denormals, saturation, x/0), with a comment saying they were
  "checked against the hardware results in ps2autotests". It has an Android runner (`android/`)
  and builds for ARM with sse2neon. Confirmed (source). Whether it runs on the Odin 3, and how
  fast, is **unknown**. Its README: "Performance is very bad for VU and GS".
- Your game is **SLPM-65428**, the Japanese File #1 (`legacy/patches/SLPM-65428_32088394.pnach`). REO's
  configuration and overrides are made for the US ELF. They name US addresses and won't apply as
  they are. How much of REO carries over is **unknown**.

Design Decision 1 compared "wait for the decomp / embed an interpreter / static recompilation".
It never compared **writing our own recompiler** with **using PS2Recomp** or **building on REO**.
That is milestone 0.

### 2. The design's beta plan breaks the decomp's distribution rule

`Outbreak-decomp/docs/LEGAL.md` rule 1: never distribute "prebuilt binaries of the game or the PC
build that contain game code". A recompiled build **is** game code. So the design's line "Windows
zip and an Android APK from the same commit, by CI" can't stand as written. REO's answer is that
each user recompiles and builds on their own machine (Visual Studio 2022, 20-40 minutes for the
last step, ~15 GB free, ~2 GB of RAM per compile job; its README). How to do that for Android is
**unknown**. The options are in `m6-single-player.md`. Not legal advice; none of these plans is.

### 3. Corrections to numbers and claims in the existing docs

| Claim (where) | What was found | Label |
|---|---|---|
| "~1,660 of ~14,500 functions in C" (design, port map) | The decomp README says 238 byte-identical + 77 `NON_MATCHING` for File 1. A regex count of function bodies in its `src/` gives ~1,664. | **Unknown** which is right. The README may be stale, or the regex may count helpers/duplicates. Answer: build the decomp and read `build/<ELF>/progress.json`. |
| Texture keys "computed from the TIM2 data, once" (design, Decision 6) | For full-size textures, PCSX2 hashes the blocks **as laid out in GS memory**, in block order. Small or partial-bit formats are expanded first. The CLUT hash is XXH3 over 16 or 256 entries (`GSTextureCache.cpp` `HashTextureLevel`, `PaletteKeyHash`). | Confirmed. Matching keys needs PCSX2's GS memory layout, for every format the game uses. More work than the design said. |
| "run the 27 VU1 microprograms recompiled to C first" (design, Decision 4) | The decomp's ROADMAP says "port each microprogram to CPU/GPU code; never emulate VU1". | Confirmed that the paths differ. Recompiling is not emulating at run time, but it isn't the decomp's path either. Agree it with them or accept the difference. |
| Code is fixed at build time (implicit in static recompilation) | `patch.c` `func_001D9810` writes `{overlay, address, bytes}` records over an overlay "code or data" right after it loads. | Confirmed that the mechanism exists. **Unknown** whether any record on the disc hits code, and whether a patch set is loaded in an offline boot (`set->loaded`). |
| Online uses only code from the disc | Obsrv sends File 1 clients a 0x7AA0-byte **code** patch at run time (decomp NETWORK.md, from BioServer 1's source). | Confirmed in the server source. A static recompiler can't translate code it has never seen. See M7. |
| Odin 3 is 64-bit only (port map: "as far as I know") | Not checked. | **Unverified.** Answer: Qualcomm's spec, or `getprop ro.product.cpu.abilist` on the device (needs your "proceed"). |
| Hardware tier table (design) | Estimates; the design says so. | Unverified until measured. |
| This repository's licence | No `LICENSE` file. | **Unknown.** Using PS2Recomp or REO, or reimplementing PCSX2's hashing from its source, brings GPL-3.0. Decide before M1. |

## The milestones

| # | Plan | Ends with | Blocked by |
|---|---|---|---|
| 0 | [m0-choose-the-base.md](m0-choose-the-base.md) | a written decision: own recompiler, PS2Recomp, or REO | reading REO and PS2Recomp closely; one test build |
| 1 | [m1-skeleton.md](m1-skeleton.md) | a window on Windows and the Odin; guest memory; ISO/AFS reader | M0; a Windows machine; your "proceed" for the Odin |
| 2 | [m2-recompiler-main-elf.md](m2-recompiler-main-elf.md) | the main ELF boots to its first `sceGsSyncV`; instruction tests pass | M1; your disc image on the build machine |
| 3 | [m3-2d.md](m3-2d.md) | boot image, logos, title screen, pad input | M2 |
| 4 | [m4-overlays-3d.md](m4-overlays-3d.md) | the overlays loaded; a room drawn | M3; splitting the VU microcode out of the ELF |
| 5 | [m5-sound-saves-movies.md](m5-sound-saves-movies.md) | sound effects, music; saves; movies | M2 (sound can start before M4) |
| 6 | [m6-single-player.md](m6-single-player.md) | File #1 offline from start to end, Modern Camera built in; beta | M4, M5; a distribution answer that keeps game code out of what's shared |
| 7 | [m7-network.md](m7-network.md) | a mixed PS2/PCSX2 + native room on a local server, no desync | M6; the v1.01 patch question; the Obsrv admins for anything live |

## Unknowns that block more than one milestone

1. **Which base** (M0). Every later plan has a "depends on M0" section.
2. **Can the Odin build or run the result?** If the recompiled C has to be built from each user's
   disc, an Android user needs a way to build it. M6 has the options; none is tested.
3. **How fast is recompiled code on the Odin?** Nothing measured. REO on a PC is 35x too slow in
   3D, for reasons it doesn't state.
4. **Does online play need exact PS2 maths?** It depends on whether peers simulate each other
   (lockstep) or send state. The in-game packets are undocumented (decomp NETWORK.md). The answer
   decides how strict M2's maths layer has to be. See M7.
5. **What the Outbreak-PC repository has.** If it can be read, its platform layer could replace
   parts of M1, M3 and M5 for Windows.

## What I need from you

- Whether **you have a Windows PC** to build and test on. Unknown. Every milestone ends "on both
  platforms".
- Your **SLPM-65428 disc image** available on whichever machine builds the game. The decomp and
  any recompiler read it locally; it never goes into git.
- A "**proceed**" each time something should be installed or run on the Odin (your rule 6).
- Decisions: milestone 0, this repository's licence, and the separate-repository question from
  the design.
