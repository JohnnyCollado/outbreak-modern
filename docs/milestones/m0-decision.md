# Milestone 0: decision and findings

**Decision (2026-10-21, you): option B.** Build on PS2Recomp and improve it along the way. Our
own graphics, sound and Android layers go on top where PS2Recomp's are missing or too slow. See
`m0-choose-the-base.md` for the options.

What was checked: PS2Recomp `eb8688b` (2026-10-14), read in full for the recompiler and runtime,
and built here. REO `de19db7` was read as a reference. Paths below are in the PS2Recomp repo
unless they start with `reo/`. Labels are as in `README.md`.

## What was run

| Check | Result |
|---|---|
| Linux x86-64 build (GCC 13, Ninja, Release) | **Fails as shipped.** The runtime uses SSE4.1 intrinsics (`_mm_extract_epi32/64`), but only MSVC gets an arch flag (`/arch:AVX2`, `ps2xRuntime/cmake/ReleaseMode.cmake:20`). Builds with `-msse4.2` added on the command line. Confirmed. |
| PS2Recomp's test suite (Linux, with the flag above) | **445 / 445 pass.** Confirmed. |
| Android arm64 build of the runtime (NDK r27c, API 29) | **Builds** `libps2EntryRunner.so`, with 7 warnings and no errors. Not packaged as an APK yet, not run on a device. Confirmed. |
| `long double` on Android arm64 (NDK clang) | 16 bytes, 113-bit mantissa: IEEE quad, done in software. Confirmed. This matters for VU1 (below). |
| The JP ELF through the recompiler | **Not done.** Needs your disc image. |

## What PS2Recomp gives us (Confirmed in source)

- R5900 to C++ with 128-bit GPRs, MMI, delay slots, branch-likely, and almost all VU0 macro ops.
- PS2 float rules for the EE FPU (no Inf/NaN/denormals, saturation, x/0), with 177 rows checked
  against real-hardware results from ps2autotests (`ps2xTest/src/ps2_float_hw_vectors.inc`).
- A guest-thread scheduler: 128 priority queues, one guest thread at a time, on one host thread
  (`Kernel/EeScheduler.cpp`).
- An IOP interpreter that runs the game's real IRX modules, plus HLE for some services.
- Function overrides by address (`stubs = ["handler@0xaddr"]`, game overrides), so hand-written
  or decompiled C can replace a function. Caveat: `j` tail calls bypass overrides
  (`ps2xRecomp/src/lib/control_flow_emitter.cpp:210-225`).
- An Android NativeActivity shell and ARM builds through sse2neon.

## What blocks Outbreak (Confirmed in source)

1. **No overlay support.** One ELF, one flat function table. `registerFunction` refuses any
   address outside the main ELF's table (`ps2xRuntime/src/lib/ps2_runtime.cpp:1091-1109`).
   REO's docs describe an "overflow map", and its config uses keys (`thread_entries`,
   `function_starts`) this commit doesn't read. REO was built on a different fork, which isn't
   in our copy. Outbreak can't run past the main ELF until this is solved.
2. **Code patched at run time isn't handled.** `isCodeModified` exists but nothing calls it.
   The analyzer even replaces some `sw`-then-`j` pairs with zeros as "self-modifying code"
   (`ps2xAnalyzer/src/elf_analyzer.cpp:540-581`). REO hit exactly this. Our `patch.c` records
   need applying to the overlay images before recompiling.
3. **Function splitting.** A hint inside an existing function only adds a label
   (`ps2xRecomp/src/lib/ps2_recompiler.cpp:1916`). REO records host stack overflows from 36-41 KB
   host functions.

## Why it's slow (where the 35x probably goes)

All of this runs on **one host thread**: EE code, the IOP interpreter, VIF, VU1 and the GS.

| Cost | Where | Label |
|---|---|---|
| The GS is a **software rasteriser**: a scalar float loop per pixel. The GPU is used only to show the finished picture (raylib). | `gs/gs_cpu_backend.cpp:1278-1394` | Confirmed that it exists; that it's the main cost is Unverified |
| The VU1 interpreter models the pipeline cycle by cycle and recomputes every FMAC lane in `long double`. **On the Odin that's software quad precision.** | `vu/ps2_vu1_core.cpp:197-489`, `942-956` | Confirmed; the cost has not been measured |
| A function with an unresolved `jalr` (every call through a struct function pointer) turns every instruction into a label, which blocks optimisation | `control_flow_analyzer.cpp:192-425` | Confirmed; the cost has not been measured |
| Every `jal` goes through the dispatcher: a checkpoint check, two table lookups, a history-ring write | `control_flow_emitter.cpp:210-237`, `ps2_runtime.cpp:1139-1397` | Confirmed |
| Every scratchpad access takes the slow out-of-line path, with a `try/catch` and a mutex | `include/runtime/ps2_address.h:43-59`, `ps2_memory.cpp:738-1000` | Confirmed |
| Context switches throw a C++ exception | `ee_scheduler.h:19-22` | Confirmed |

REO publishes no profile. A real profile run is the only way to rank these. It needs a game
running, so it waits for the JP ELF to boot (or the US disc for REO, which you may not have:
Unknown).

## Correctness gaps that matter for cross-play (Confirmed in source)

- `CVT.W.S` rounds (`nearbyintf`) and doesn't saturate (`ps2_runtime_macros.h:856`). What the EE
  does (believed: truncate and saturate) is still **to be checked** against ps2autotests.
- The EE FPU uses the host's round-to-nearest. Only the VU1 core sets round-toward-zero.
- MADD/MSUB round twice. MAX/MIN and C.cond don't follow PS2 rules for exponent-255 values.
- BLEZ/BGTZ/BLTZ/BGEZ compare 32 bits; the R5900 compares 64.
- VU0: VRSQRT ignores fs (`vu_translation_helpers.cpp:88-92`). VDIV by zero gives 0. VLQI/VSQI/
  VILWR/VISWR address EE RAM instead of VU0 memory. The MAC/status flags are never updated.
- Timing isn't instruction-accurate: fixed weights (32 per loop edge, 8 per call), vblank at
  60.00 Hz rather than 59.94, alarms on host time. For lockstep online play this would matter.
  Whether Outbreak is lockstep is still Unknown (M7).
- MSVC Release builds use `/fp:fast`, which allows the compiler to undo the float emulation.
  Whether it actually does is Unverified.

## Missing pieces we'd have to write anyway

- **Sound:** no SPU2. The output is heuristic (VAG decode from RPC buffers through raylib), and
  there's no ADX decoder.
- **Movies:** FFmpeg HLE on desktop. It's off on Android, which gives stub frames.
- **Android:** `ps2_android_runtime.cpp` is a 3-line TODO. There's no file picker, no touch
  controls and no pause/resume handling. The ELF path is fixed at build time, game files are
  pushed with adb, logs are verbose by default, and builds are signed with the debug key.
- **Disc access:** a folder of extracted files, not an ISO image.

## Proposed improvement order

Each change is small, tested, and offered upstream as one pull request (REO works the same
way). Nothing is sent to GitHub without your OK.

| # | Change | Why first |
|---|---|---|
| 1 | SSE4.1 flags for GCC/Clang on x86 | Tiny; unblocks Linux builds, including CI |
| 2 | 64-bit compares for BLEZ/BGTZ/BLTZ/BGEZ | Small, clear correctness bug, easy to test |
| 3 | VU0 fixes: VRSQRT, VDIV/VSQRT edge cases, VLQI/VSQI addressing | Small, testable |
| 4 | CVT.W.S and EE rounding, checked against ps2autotests | Needs the hardware results first |
| 5 | **Overlay support** (an overflow table plus resident-overlay dispatch) | The real blocker for Outbreak; larger, so discuss the design with ran-j first |
| 6 | VU1 fast path without `long double` | The biggest Android-specific cost |
| 7 | Android: a game picker (SAF), pause/resume, logs off | Needed for a usable APK |
| 8 | GPU GS renderer (Vulkan or GLES) | Largest; after profiling confirms it's the bottleneck |

## Still unknown

- How fast anything runs on the Odin. Needs a test APK and your "proceed".
- Whether the JP ELF recompiles cleanly. Needs your disc image here.
- Which fork REO used, and whether its overlay work can be reused. Ask its maintainer.
- Whether ran-j wants these changes upstream. Ask before sending large ones.
