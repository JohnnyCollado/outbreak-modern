# Workspace: C:\ob, game data, generated paths

Source: R0-003 (from docs/retired/CLAUDE.pre-pa3.md:83-94), phase 1.1 T2-T4 summaries; checked 2026-10-21 (`ls C:\ob`).

## Why outside the repo
- Game-derived data (recompiled C++, overlays, frames, decomp) never enters git.
- MSVC 260-char path limit: keep build trees at short roots (C:\ob\t, C:\ob\r).

## C:\ob layout (env overrides in parentheses, read by tools/ob.sh)
- `src` (OB_SRC) — PS2Recomp working copy the builds use; == `up` except generated files (T4).
- `up` — upstream eb8688b + patches 0001-0020 as commits; `git format-patch` source for `upstream/ps2recomp/`. Local, never pushed.
- `t` (OB_T) — MSVC tools + tests build (analyzer, recompiler, ps2x_tests, gs_cache; `_deps` FetchContent cache).
- `r` (OB_R) — runner build (`ps2EntryRunner`).
- `g` — MSYS2 GCC build for gdb probes (`probe*.py`).
- `work` (OB_WORK) — analyzer/recompiler I/O for the main ELF (`main-functions.csv`, `decomp.toml`, output).
- `ovl` (OB_OVL) — overlay configs and I/O (demo, submain, netdnas).
- `splat` (OB_SPLAT = C:\ob\splat\SLPM_654.28) — splat asm for the function map and overlays.
- `decomp-src` — Outbreak-decomp eb6af38, flat .c files, no git.
- `logs` (OB_LOGS) — `boot-<T>.log`, build/test logs. `frames-<T>` — boot captures. `stash` — preserved dropped hunks.
- `ref`, `trace`, `ut`, `upiop`, `ioptests` — older probe/test material; purpose not recorded.
- Old scripts (build-*.bat, tests.bat, recomp-cycle.sh, ovl-cycle.sh, boot-frames*.ps1, ...) are replaced by
  `tools/ob.sh` and left in place, unused.

## Game folder (OB_RUN = repo `game_files/`, git-ignored, read only)
Upper-case names: `SLPM_654.28`, `SYSTEM.CNF`, `PROG/*.BIN`, `IOP/MODULES/` (33 files), `NETBIO*.DAT`.
Game SLPM-65428 v1.04 + English patch v2.1. Main history (bb0c282, e012448) holds game files: never merge them.

## Generated (never hand-edited)
- C:\ob\work and C:\ob\ovl outputs; in the PS2Recomp tree: `ps2xRuntime/src/runner/*.cpp`,
  `register_functions.cpp`, `ps2_recompiled_functions.h`, `ps2_recompiled_stubs.h` (copied in by `ob.sh recompile`).
- `upstream/ps2recomp/00NN-*.patch` — produced by `git format-patch` from C:\ob\up, never edited as text.
- Boot logs and frames (raw outputs; analysis writes new files).

## Hand-edited
- Repo: `tools/` (ob.sh, ob/vc.bat, python tools), `gate/*.script`, `upstream/ps2recomp/README.md`, docs, reports.
- PS2Recomp changes: edit and commit in C:\ob\up (one patch per change, with a ps2x_tests test), sync C:\ob\src,
  then format-patch into `upstream/ps2recomp/` with a README row.

## Vendored PS2Recomp-main/PS2Recomp-main (repo)
Stale: a copy at about eb8688b + 0001-0018 (2026-10-21 `diff -rq` vs C:\ob\up: lacks 0019 frame dump and 0020 input
script; 7 files differ by CR only, 6 really). Its nested `.gitignore` hides out/, src/runner/ and the generated headers;
ps2EntryRunner does not link there (register_functions.cpp missing). Never build from it; C:\ob\up is the truth.
