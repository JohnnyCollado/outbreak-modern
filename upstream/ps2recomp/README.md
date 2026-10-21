# Changes for PS2Recomp

Fixes made while building on [PS2Recomp](https://github.com/ran-j/PS2Recomp) (GPL-3.0). They are
already applied to the copy in `PS2Recomp-main/PS2Recomp-main/`. The patches here are the same
changes split one per fix, so each can go upstream as its own pull request, **only with your
OK**. Apply them in order with `git am` on PS2Recomp `eb8688b`. Each patch builds on its own and
adds its own test.

| Patch | What | Reference for the right behaviour |
|---|---|---|
| 0001 | Linux (any GCC/Clang x86-64) builds failed: SSE4.1 intrinsics without the target enabled. Adds `-msse4.1` next to the existing ARM flags. | build error |
| 0002 | BLEZ/BGTZ/BLTZ/BGEZ (and -L/-AL) compared 32 bits; the R5900 compares the 64-bit register. | PCSX2 `Interpreter.cpp` (`GPR.r[rs].SD[0]`) |
| 0003 | CVT.W.S rounded instead of truncating; CVT.W.S/TRUNC.W.S didn't saturate. The out-of-range cast was undefined and **gave different results on x86 and ARM**. | PCSX2 `FPU.cpp` `CVT_W` |
| 0004 | VU0 VRSQRT ignored fs. VDIV by zero gave 0. VSQRT clamped negatives to 0. Now uses the same PS2 float helpers as the EE FPU. | the repo's own ps2autotests-checked helpers |
| 0005 | VU0 LQI/SQI/LQD/SQD/ILWR/ISWR used EE RAM instead of VU0 data memory. SQI/SQD had fs/it swapped. VI0/VF0 could be written. VILWR/VISWR addressed `vi << 2` and ignored the field. A 5-bit VI index could write past `ctx->vi[16]`. | PCSX2 `VUops.cpp` `_vuLQI`, `_vuSQI`, `_vuLQD`, `_vuSQD`, `_vuILWR`, `_vuISWR` |
| 0006 | `fioOpen` and the rest of the VFS joined PS2 paths with exact case. On Linux/Android, `cdrom0:\IOP\MODULES\DRIVE.DAT;1` missed a disc copy named `iop/modules/drive.dat`, and Outbreak's boot retried forever. Now an exact-case miss falls back to matching each component ignoring case; Windows is unchanged. Found by booting Outbreak. | PS2 paths are case-insensitive (ISO 9660) |

## How they were tested (2026-10-21, Ubuntu 24.04)

- `ps2x_tests`: **450 / 450 pass** with all six patches, with GCC 13.3 and with Clang 18.1,
  Release builds (445 before 0002-0005, which add 4 tests; 0006 adds 1). 0006's test fails
  without the fix (449/450, GCC).
- The 4 new tests were run against the **old** code: exactly those 4 fail and the other 445 pass.
  So each test catches the bug it is for.
- Patches 0002-0005 were checked one at a time: each commit builds and passes (446, 447, 448,
  449).
- GS cache tests, built standalone as their CMake file intends (`cmake -S ps2xTest/gs_cache`):
  **45 / 45 pass** with GCC and Clang. (A note on an earlier claim: on the first day these were
  started without a test name. They only printed a usage message, so "they pass" wasn't backed by
  anything until this run.)
- Android arm64 (NDK r27c, API 29): `ps2_runtime` and `ps2_iop` build with the changes.

## Known gaps, not fixed here

- **VU DIV-by-zero value.** PCSX2's VU interpreter returns ±0x7F7FFFFF for VDIV/VRSQRT by zero;
  these patches give ±0x7FFFFFFF, the EE FPU's value. Which one VU0 hardware gives is **unknown**.
  ps2autotests (or a real PS2) would answer it.
- The VU0 MAC/status flags are still not updated by macro-mode ops.
- The EE FPU still rounds to nearest; the hardware rounds toward zero (`m0-decision.md`).
- `ps2EntryRunner` doesn't link in **our** copy (`PS2Recomp-main/`), because
  `ps2xRuntime/src/runner/register_functions.cpp` is missing. Upstream tracks that file (a
  placeholder function table), but its own `.gitignore` lists `src/runner/`, so it was left out
  when PS2Recomp was added to this repo. This is **not an upstream bug**. An earlier version of
  this note said it was; that was wrong. The fix belongs in how we vendor it (e.g. a git
  submodule), not in PS2Recomp.
- The GS cache tests aren't registered with CTest in the top-level build (only `include(CTest)`
  when the folder is built standalone).
