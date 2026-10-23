# Project tools (tools/)

Source: R0-003 (8), phase 1.1 T2/T4 (`docs/history/`). The everyday commands are in `build.md`.

## Live
- `bash tools/ob.sh tools|test|recompile|boot` — the Windows driver (card Build/Run/Test); MSVC steps via `tools/ob/vc.bat`.
- `PY -I tools/splat_functions.py <splat>/asm/SLPM_654.28/en/main <work>/main-functions.csv` — function map for the
  recompiler (0 errors over 5,358 functions with it; 8,261 with the fallback analyzer). Test: tools/test_splat_functions.py.
- `PY tools/ob/sync_changed.py` — copies generated sources/headers into `$OB_SRC` only when their content changed,
  pruning stale `.cpp` (used by `ob.sh recompile`): identical files keep their mtime, so ninja skips them.
- `PY tools/mwo3_to_elf.py` — wraps overlay `PROG/*.BIN` (MWo3) as ELFs for the recompiler (used by `ob.sh recompile`).
  Test: tools/test_mwo3_to_elf.py.
- `PY tools/q_scan.py` — read-only VU0 Q-register check. Test: tools/test_q_scan.py.
- `bash tools/recomp_cycle.sh` — Linux/xvfb form of the recompile + boot cycle (not run on Windows).
- `gate/boot.script` — scripted hidden boot to character select, six captures (grammar: `native/ob_diag/src/input_script.h`, was the patch 0020 header,
  docs/history/phase-1.1/tasks/T4.md).

## Legacy: ARMSX2 / PINE mod era (not part of the port) — moved to `legacy/` on 2026-10-22
- `legacy/` keeps the old layout (`legacy/tools`, `legacy/mod`, `legacy/patches`, `legacy/results`, its README), so
  the scripts still find each other. Nothing in the native build, the gate or the live plan uses it.
- `legacy/tools/f1_*.py`, `test_buttons.py`, `test_collide.py`, `test_freeaim.py` — File #1 free-aim mod tools;
  `mips.py` (+ `test_mips.py`) — hook assembler they import.
- `legacy/tools/phase2_check.py`, `push_patch.py` (adb push to the Odin), `pine_diag.py` — touch the device or a
  running emulator: each run needs a specific developer "proceed".
