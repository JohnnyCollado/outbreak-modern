# First recompile and boot of SLPM_654.28 with PS2Recomp

2026-10-21. Input: your SLPM-65428 v1.04 + English patch v2.1 (sha1-verified, see
`q-scan-20261021.md`). PS2Recomp `eb8688b` plus our patches 0001-0005. Everything generated
from the game stayed in the session's scratch folder; none of it is in git.

## 1. Recompile

| | Run 1: PS2Recomp's fallback analyzer | Run 2: the decomp's function list |
|---|---|---|
| How functions were found | JAL scanner (`ps2_analyzer`) | splat on the decomp's configs → `tools/splat_functions.py` → `ghidra_output` |
| Functions | 3,509 | **5,358** (0x100000-0x1D9ADC) |
| Recompiler errors | 8,261, **all** in the VU microcode/data region (strings and data decoded as code; 37 fake functions, the largest 7.4 MB) | **0** |
| Warnings | 448 | 570, all "unresolved JR/JALR" (calls through function pointers) |
| Generated C++ | 3,525 files, 87 MB | 5,362 files, 60 MB |
| Decomp-named functions found | 178 of 211 (missed e.g. `ExitThread`, `iTerminateThread`: merged into neighbours) | all |

Both runs took about a second. The analyzer's 217 SDK stubs (matched from its embedded Sony
symbol database) and 484 entry points were kept in run 2. All of them lie in the code range.

**The 570 JR/JALR warnings** are the functions that call through pointers. PS2Recomp handles
them by making every instruction a possible re-entry point ("promoted fallback entries"). That
works but blocks optimisation: speed issue #3 in `docs/milestones/m0-decision.md`.

## 2. Compile and link (Linux, GCC 13, 4 cores)

- `ps2EntryRunner` with all 5,358 functions: **250 s**, unity build (32 functions per batch),
  `-O2`, no LTO. 23.5 MB executable. No errors.
- An earlier per-file estimate (200 random files, 100 s → ~36 min for everything) was far too
  high. Per file, most of the time goes to parsing the runtime headers, which unity batching does
  once per 32 functions.
- The build used PS2Recomp's own CMake (`src/runner/`). It ran in a scratch clone, because
  `src/runner/register_functions.cpp` is tracked upstream and now holds game-derived data.
  Never commit in that clone.

## 3. Boot test (90 s, Xvfb virtual display, no audio device)

Booted from a folder with the 10 files only: `SLPM_654.28`, `SYSTEM.CNF`, `PROG/*.BIN`.

- No crash in 90 s. The runtime presented 127 frames. The game set the display registers
  (`DISPFB1=0x1400`, `DISPLAY1` set) and made 18 GIF DMA transfers. One guest thread was active.
- Then it **loops at 0x1aa8cc-0x1aa8e0** in `func_001AA830` (decomp `sys.c`: IOP reboot and
  module load). Decoded, the loop is `sceOpen("cdrom0:\IOP\MODULES\DRIVE.DAT;1")` with `bltz` back
  while it fails. The C in the decomp (`while ((fd = sceOpen(D_00231A70, 1)) < 0) {}`) says the
  same.
- Just before it, `sceSifRebootIop("cdrom0:\IOP\MODULES\DNAS280.IMG;1")` returned: PS2Recomp's
  runtime accepted the IOP reboot without the file.

**So the boot is waiting for a disc file, not broken.** The next run needs the disc's
`IOP\MODULES\` folder. `DRIVE.DAT` holds the IOP modules the game starts (decomp port map: Sony's
CDVD, pad, memory card, network modules and Tamsoft's sound driver); the port map also names
`USBD.IRX` and `USBKB.IRX`.

**Not shown yet:** whether the boot reached a `sceGsSyncV` (the M2 check). The log has no
function-level trace, and the display setup seen above could come from the runtime's own
defaults. That has to be confirmed with a trace (`PS2X_TRACE_*` switches, as REO does) on the
next run.

## 4. What this means for the plan

- **Use the decomp's function list, not PS2Recomp's fallback analyzer.** It fixes the data-region
  problem and the missed functions in one go, and the same tool works for the overlays (their
  splat output is already made: `asm/SLPM_654.28/{en/game,en/demo,en/netwk,submain,single,...}`).
- Overlays are still the next big blocker (PS2Recomp has no overlay support,
  `m0-decision.md`), but the main program now recompiles cleanly and runs until it needs disc
  files.
