# Milestone 2: recompiler, main ELF

## Goal

From the design: "boots `main()` to the first `sceGsSyncV`; EE instruction tests pass (FPU rules
included)." Only the main ELF (`SLPM_654.28`, 5,357 functions per the decomp's LAYOUT.md), not
the overlays.

## Done when

- The recompiler reads the user's `SLPM_654.28` (sha1 checked against the decomp's
  `config/SLPM_654.28/build.sha1`) and writes C (or C++) that builds for x86-64 and arm64.
- The program runs from the entry point (0x100008) through crt0, `main()` and the boot path, and
  reaches the first `sceGsSyncV` call on both platforms, with the system calls on the way answered
  by HLE.
- An instruction test suite passes on both platforms with the same results (see Work 4).

## Depends on M0

With PS2Recomp, steps 1-3 exist and M2 becomes "make it handle the JP ELF and pass our tests".
With our own recompiler, all of it is new.

## Work

1. **Find every function.** The decomp's splat configs and `symbol_addrs.txt` (225 named symbols
   in the main file) give boundaries. Splat's counts "will change as boundaries are refined"
   (LAYOUT.md). The recompiler also needs every **indirect target**: function pointers in structs
   (`o->draw(o)` in `game/draw.c`), jump tables, and thread entry points. Method: static scan,
   plus a run-time "unknown target" trap that logs the address and stops.
2. **Translate R5900 to C**: 128-bit GPRs, MMI, `lq`/`sq`, branch delay slots, branch-likely,
   the FPU with its quirks (the `sqrt.s` operand field from the feasibility report), VU0 macro
   mode (COP2). `mod/build.py` already holds some EE rules learned from the camera patch.
3. **Data region.** The ELF's 0x1d9b00-0x23ba00 holds VU microcode, an EE kernel-mode block and
   .data/.rodata/.sdata, not split yet (decomp LAYOUT.md, ROADMAP Phase 1). The recompiler must
   not translate microcode or data as EE code. The kernel-mode block (`mfc0`/`mtc0`, 0x8007xxxx)
   is something the game installs into the kernel. It gets HLE, not translation, once its job is
   known.
4. **Instruction tests.** One test per instruction, with edge values (overflow, NaN-like bit
   patterns, denormals, divide by zero, rounding), compared against a reference. Which reference
   is open: the unicorn tests we already have (but unicorn emulates plain MIPS, not the PS2's
   FPU), PCSX2's interpreter, or the hardware results PS2Recomp cites (ps2autotests).
5. **HLE for the boot path.** Kernel calls (`CreateThread`, semaphores, `AddIntcHandler`,
   `FlushCache` and others; decomp `kernel.h`), the SIF/IOP calls (`sceSifInitRpc`, module loads:
   answered "loaded" without loading), the CDVD and file calls, `sceGsResetGraph`. The port map
   lists them by function address.
6. **Threads.** The game uses EE kernel threads (Create/Start/Sleep/WakeupThread). EE threads run one at a time
   on one CPU, by priority, with no time slicing (the usual description of the EE kernel; not
   checked against a source here). Host threads run truly in parallel. The runtime
   must run one guest thread at a time, in PS2 priority order. Otherwise timing changes, and with
   it the game's results.

## What's needed

- The disc image on the build machine.
- A reference for float behaviour (Work 4). The PS2 itself would be best; whether you have one
  you can run test programs on is **unknown**.
- A C compiler for both targets (MSVC or clang for Windows, NDK clang for Android).

## Limitations

- **Bugs in the recompiler look like game bugs.** The instruction tests catch some. The rest show
  up as "the game does something odd" and need tracing back.
- **Generated code size and build time are unmeasured.** REO reports a 20-40 minute build and
  ~2 GB of RAM per compile job on Windows for its US build. A comparable cost for ours is
  unverified.
- **Mixing decompiled C in** (the design's "function by function") only works where the decomp's
  C follows the guest-address model. Today it doesn't: the decomp's PC build "gives every symbol
  a native address" and keeps 4-byte native pointers (decomp PCPORT.md), which a 64-bit build
  can't. Until that's agreed with the decomp project (design question 3), M2 uses recompiled code
  only.

## Unknowns

- **How exact the maths must be.** That depends on M7's question (do peers simulate each other?).
  Single player only needs "plays the same". Online cross-play may need bit-exact results. Until
  it's known, aim for bit-exact and measure the cost.
- **What the kernel-mode block does.** Not decompiled. Answer: disassemble it (splat output from
  the user's disc).
- **Whether the main ELF changes its own code at run time** beyond overlay loading and `patch.c`.
  No evidence either way. Answer: a write-watch on the code range during a boot.
- **How many indirect-call targets are missing from a static scan.** Unknown until the trap from
  Work 1 runs.
- **How fast the result runs on the Odin.** Nothing measured. M2 is the first point where it can
  be: time the boot path.
