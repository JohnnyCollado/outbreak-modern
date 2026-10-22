# Build, run, test

The everyday commands. Detail per topic is in the other `docs/ops/` files.

## Commands (Git Bash, from the repo root)

| What | Command | Notes |
|---|---|---|
| Build the tools | `bash tools/run.sh ob-tools -- bash tools/ob.sh tools` | Analyzer + recompiler into `C:\ob\t` |
| PCSX2 GS library + smoke test | `bash tools/run.sh pcsx2-gs -- bash tools/ob.sh pcsx2-gs` | Clones/builds deps if missing (`gs.md`) |
| Recompile + build the game | `bash tools/run.sh ob-recompile -- bash tools/ob.sh recompile` | Main ELF + 3 overlays, runner into `C:\ob\r`. ~16 s when generated code is unchanged; ~30 s after a runtime-internal change (patch 0028: game code sees only `ps2_runtime_api.h`); a full rebuild (~6 min) only when the recompiler output or the game-facing headers change |
| Debug build | `OB_DEBUG=1 bash tools/ob.sh recompile` | Full PDBs (`/Zi`, `/DEBUG:FULL`): much slower link; only for stepping through code |
| Test | `bash tools/run.sh ob-test -- bash tools/ob.sh test` | ps2x_tests, IOP ctest, gs_cache ctest |
| Gate | `PS2X_GS=<cpu\|pcsx2-sw\|pcsx2-vk> bash tools/ob.sh gate` | Hidden scripted boot + scoring (`gate.md`) |
| Hidden scripted boot | `bash tools/ob.sh boot --script gate/boot.script --tag T` | Ends by itself (`boot.md`) |
| Visible boot | `PS2X_GS=pcsx2-vk bash tools/ob.sh boot --tag visible` | Opens a window: needs the developer's "proceed" each time |

From PowerShell, set the variable first: `$env:PS2X_GS="pcsx2-vk"; bash tools/ob.sh boot --tag visible`
(make sure `bash` is Git Bash, not WSL).

`tools/run.sh` keeps long output in `.run/<phase>/logs/<name>.log` and prints the tail.

## Environment

- Windows, Git Bash and PowerShell. Python: `C:/Users/johnn/AppData/Local/Python/pythoncore-3.14-64/python.exe`
  (override with `OB_PY`).
- Pins (`pins.md`): MSVC 19.44 / VS 2022, CMake 3.30, PS2Recomp `eb8688b` + `upstream/ps2recomp/` patches,
  PCSX2 v2.9.134, game SLPM-65428 v1.04 + English patch v2.1.
- `OB_*` variables override the `C:\ob` paths (`workspace.md`).

## Paths

- Ground truth: PCSX2 2.9.134 in `ps2_emu/` (git-ignored) and the reference screenshots in `C:\ob\ref`.
- Workshop: `C:\ob` (`workspace.md`); the disc's files in `game_files/` (read only, git-ignored).
- Generated, never hand-edited: `C:\ob\work`, `C:\ob\ovl`, `src/runner/*.cpp`, `ps2_recompiled_*.h`,
  `upstream/ps2recomp/*.patch` (exported with `git format-patch` from `C:\ob\up`).
- Scratch: `.run/` (git-ignored). Nothing long-term lives there.

## Decisions still in force

- Base: PS2Recomp, improved through numbered patches, each with a test that fails without it (M0, 2026-10-21).
- PS2Recomp tracks upstream HEAD: check at each milestone and rebase our patches; pin only for a breaking bug.
- Graphics: PCSX2's GS, in-process, pinned v2.9.134; the CPU GS is a fallback (`gs.md`, 2026-10-22).
- Runner configured with `-DPS2X_ENABLE_AGRESSIVE_LOGS=OFF` (ON halves boot speed).
- Game data never enters git. Nothing is pushed without the developer's OK.
- Standing OK for work under `C:\ob` and hidden scripted boots; PCSX2 launches, visible windows and the Odin need
  a "proceed" each time.
