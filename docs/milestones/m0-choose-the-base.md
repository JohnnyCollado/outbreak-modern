# Milestone 0: choose what to build on

Not in the design's list. It's added because the design never compared its own recompiler with
the two existing projects found on 2026-10-20 (see `README.md`, finding 1). Everything from M1
on depends on this choice.

## Goal

A written decision, with the evidence behind it, between:

| | A. Our own recompiler (the design as written) | B. Build on PS2Recomp | C. Build on REO (which uses PS2Recomp) |
|---|---|---|---|
| What exists | nothing | recompiler + runtime + IOP interpreter + Android runner (Confirmed, source) | B, plus Outbreak-specific config, overrides and boot fixes, **for SLUS-20765** (Confirmed, README) |
| Game version | any | any | US. Your game, and Obsrv, are JP (SLPM-65428) |
| Licence | ours to pick | GPL-3.0 | GPL-3.0 |
| Speed | unknown | "very bad for VU and GS" (its README) | 35x slower than real time in 3D on a PC (its README) |
| Android | to write | a runner exists; never tried on the Odin | Windows only today |
| PS2-exact floats | to write | done, "checked against ps2autotests" (source comment; tests not run by us) | same as B |
| Fits the design's HLE plan (no IOP emulation, VU1 not emulated) | yes | partly: it can run the original IRX modules on an IOP interpreter (README) | REO chose the IOP interpreter (architecture.md) |
| Upstream to work with | none | ran-j; takes PRs (REO's ROADMAP: "toolkit changes go to PS2Recomp as PRs") | sp00nznet; contributing rules in its CONTRIBUTING.md (not read) |

A mixed answer is possible too, and often what happens: PS2Recomp's recompiler and float layer,
with our own HLE, renderer and Android layer.

## Done when

- `docs/milestones/m0-decision.md` exists, saying which option, why, and what was measured.
- The measurements below are in `results/`.

## Work

1. **Read REO in full**: `docs/boot-gates.md`, `docs/architecture.md`, `recomp/config/outbreak.toml`,
   the overrides, CONTRIBUTING.md and REPO_RULES. What is US-specific and what is generic?
2. **Read PS2Recomp's recompiler**. How does it handle what this game needs?
   - MWo3 overlays at shared addresses (DEMO/GAME/NETWK at 0x540000; SINGLE/NETAQ at 0x820000;
     NETDNAS/NETHTTP at 0x6C0000; decomp LAYOUT.md). REO's README says overlays are "dispatched
     by which one is resident". Unverified for the JP layout.
   - Indirect calls: the game calls through function pointers stored in its structs
     (`o->draw(o)`, decomp `game/draw.c`). How does it find every target?
   - The EE kernel-mode block (`mfc0`/`mtc0`, 0x8007xxxx, ending at 0x1ed8a8; decomp LAYOUT.md).
   - Code patched after load (`patch.c`).
3. **Test build of PS2Recomp on the JP ELF**, on a Linux or Windows machine: does it recompile
   `SLPM_654.28` and compile the output? Record the time, the output size and the errors.
4. **Test build of PS2Recomp's Android runner** with the NDK (no game code yet). Does it build
   for arm64? Running it on the Odin needs your "proceed".
5. Ask the **decomp project** whether its C can follow the guest-address rule (design question 3),
   and whether they know what Outbreak-PC is now.
6. Ask **REO's maintainer** whether JP support and Android are welcome, if C is in the running.

## What's needed

- Your SLPM-65428 image on the build machine.
- A build machine. This cloud container can run Linux builds. Whether it has enough disk is
  **unknown** (REO wants ~15 GB). Windows builds need a Windows PC (do you have one? **unknown**)
  or cross-compiling (untested).
- The Android NDK in the container. Installing it needs network access to Google's servers,
  which this environment's network policy may or may not allow: **unknown** until tried.

## Limitations

- B and C bind the project to GPL-3.0. Fine for a fan project, but it decides the licence for
  everything linked with it, the Modern Camera code included.
- B and C mean following someone else's architecture and pace. If upstream stops, we maintain a
  fork.
- A means re-doing work that exists: a decoder for the R5900 instruction set, MMI, VU0 macro mode,
  the float rules. How much work that is has **not been estimated**.

## Unknowns

- PS2Recomp's speed on ARM, and how much sse2neon costs. Answered by step 4 plus a run.
- Whether REO's 35x slowdown is VU1/GS emulation, the IOP interpreter, or something else. REO
  doesn't say. Answered by profiling a REO build (needs the US disc, which you may not have:
  **unknown**) or asking its maintainer.
- Whether PS2Recomp can mix in decompiled C function by function (the design's core idea). Not
  read yet.
- Whether Outbreak-PC still exists and what's in it.
