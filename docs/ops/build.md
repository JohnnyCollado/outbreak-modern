# Build, run, test

The everyday commands. Detail per topic is in the other `docs/ops/` files.

## Commands (Git Bash, from the repo root)

| What | Command | Notes |
|---|---|---|
| Build the tools | `bash tools/run.sh ob-tools -- bash tools/ob.sh tools` | Analyzer + recompiler into `C:\ob\t` |
| PCSX2 GS library + smoke test | `bash tools/run.sh pcsx2-gs -- bash tools/ob.sh pcsx2-gs` | Clones/builds deps if missing (`gs.md`) |
| Recompile + build the game | `bash tools/run.sh ob-recompile -- bash tools/ob.sh recompile` | Main ELF + 3 overlays, runner into `C:\ob\r`, then the ob_diag plugin into `C:\ob\diag-build` (~3 s). ~16 s when generated code is unchanged; ~30 s after a runtime-internal change (patch 0028: game code sees only `ps2_runtime_api.h`); a full rebuild (~6 min) only when the recompiler output or the game-facing headers change |
| VU1 interpreter vs microVU | `bash tools/ob.sh vucompare [DIR\|--synthetic] [--repeat N] [--verbose] [--no-flag-hack]` | Builds `native/pcsx2_vu` + `ob_pcsx2_vu_compare.exe` into `C:\ob\pcsx2-vu-build` (first build ~55 s incl. its own copy of `ob_pcsx2_gs`; then 5-8 s), runs it on `C:\ob\vu1-captures`. Needs `ob.sh recompile` first (links the runner's `ps2_runtime.lib`). See "VU1 capture" below |
| Dependency check | `bash tools/ob.sh depcheck` | ~12 s. Touches `gs_frontend.h` (mtime restored after), dry-runs the runner build (`cmake --build C:/ob/r --target ps2EntryRunner -- -n -f C:/ob/r/build.ninja` via `tools/ob/vc.bat`), counts the steps. Green = `depcheck: ok: gs_frontend.h rebuilds N runtime step(s), 0 generated game units`, exit 0; else exit 1 and the game units (`Unity\`) are listed. The `-f` with the absolute manifest path is needed: CMake's CONFIGURE_DEPENDS glob check is always dirty, and a plain `-n` stops after "Re-running CMake" with no steps. Run after any runtime change (patch 0028 keeps game code off runtime headers). Log `C:\ob\logs\ob-depcheck.log` |
| Debug build | `OB_DEBUG=1 bash tools/ob.sh recompile` | Full PDBs (`/Zi`, `/DEBUG:FULL`): much slower link; only for stepping through code |
| Profile build | `OB_PROFILE=1 bash tools/ob.sh recompile` | Debug info for the host libraries only, game units untouched; copy in `C:\ob\r-profile`. See "Host profiling" below |
| Host profiler | `bash tools/ob.sh hostprof-build` | `C:\ob\hostprof-build\hostprof.exe` (~5 s). See "Host profiling" below |
| Test | `bash tools/run.sh ob-test -- bash tools/ob.sh test` | ps2x_tests, IOP ctest, gs_cache ctest, `ob_diag` (the plugin's own tests; builds `native/ob_diag` into `C:\ob\diag-build`, ~3 s incremental), `pcsx2_vu` (`ob_pcsx2_vu_tests`: microVU placement under a crowded address space and the fallback; built in `C:\ob\pcsx2-vu-build`, links the runner's `ps2_runtime.lib`, so rebuild the runner after a runtime change) |
| Gate | `PS2X_GS=<cpu\|pcsx2-sw\|pcsx2-vk> [PS2X_VU1=<interp\|mvu>] bash tools/ob.sh gate` | Hidden scripted boot + scoring (`gate.md`). The gated screens end at character select, before any VU1 run: `PS2X_VU1` does not change them |
| Hidden scripted boot | `bash tools/ob.sh boot --script gate/boot.script --tag T` | Ends by itself (`boot.md`); loads the ob_diag plugin (`PS2X_DIAG`) |
| Engine-only boot | `bash tools/ob.sh boot --no-diag --hidden --secs N --tag T` | No plugin, as a play build; speed from the `[fps]` log lines |
| Live pad boot | `bash tools/ob.sh boot --pad ob_pad --secs 900 --tag T` + `python tools/ob/pad.py <cmd>` | Hidden; driven over a named pipe, one command at a time (`boot.md` "Live pad") |
| Visible boot | `PS2X_GS=pcsx2-vk bash tools/ob.sh boot --tag visible` | Opens a window: needs the developer's "proceed" each time |
| Boot or gate from the disc image | `OB_GAME="$PWD/ps2_emu/roms/Biohazard - Outbreak (English v2.1).iso" PS2X_GS=pcsx2-sw bash tools/ob.sh gate` | `OB_GAME` = what boot/gate run: the ISO, an extracted folder, or the boot ELF (default `game_files/SLPM_654.28`, as before). Recompile still reads `game_files/` (`OB_RUN`). Patch 0043 |
| Android APK | `bash tools/ob.sh android` (or one step: `shaderc`, `configure`, `build`, `package`, `verify`) | arm64 `.so` + `C:\ob\android-apk\outbreak-modern.apk`; never touches a device. `docs/ops/android.md` |

From PowerShell, set the variable first: `$env:PS2X_GS="pcsx2-vk"; bash tools/ob.sh boot --tag visible`
(make sure `bash` is Git Bash, not WSL).

`tools/run.sh` keeps long output in `.run/<phase>/logs/<name>.log` and prints the tail.

## Environment

- Windows, Git Bash and PowerShell. Python: `C:/Users/johnn/AppData/Local/Python/pythoncore-3.14-64/python.exe`
  (override with `OB_PY`).
- Pins (`pins.md`): MSVC 19.44 / VS 2022, CMake 3.30, PS2Recomp `eb8688b` + `upstream/ps2recomp/` patches,
  PCSX2 v2.9.134, game SLPM-65428 v1.04 + English patch v2.1.
- Frame counter (patch 0035, in the engine): the window title reads `<title> | 27.3 fps` (guest vsyncs per wall second,
  updated once per second); with a hidden window the same rate is logged as `[fps] tick N | 27.3 fps` each second.
- Diagnostics are a plugin, not the engine (patches 0036/0037, `docs/ARCHITECTURE.md` "Engine vs diagnostics"): the
  variables below and `PS2X_INPUT_SCRIPT` / `PS2X_FRAME_DUMP_DIR` work only when `native/ob_diag`'s `ob_diag.dll` is
  loaded: `PS2X_DIAG=<path to the dll>` (what `ob.sh boot` sets), or the dll placed beside the exe. Without it they
  do nothing. Hook API: `runtime/ps2_diag_hooks.h` in PS2Recomp.
- `PS2X_PC_SAMPLE=<path.csv>` (plugin; was patch 0030): guest PC sampling profiler (~550 Hz measured: 1 ms sleeps); CSV `tick,tid,pc,count,vu1,iop,gs,gsvsync`
  per 64-vsync bucket (pc 0 = outside guest code). `PS2X_FRAME_TIME=<path.csv>` (plugin; was 0027/0029): per-vsync `tick,wall_us,busy_us`.
  Summarize: `python -I tools/ob/pc_profile.py <csv> --from T --to T` (maps pcs to functions).
- `PS2X_VU1_CAPTURE=<dir>[,N[,K]]` (plugin; was patch 0031): VU1 program capture, see below.
- `PS2X_INPUT_INI=<path>` (engine, patches 0038/0040): host input bindings; default `<exe dir>/input.ini` when present,
  else the built-in mapping: PCSX2's keyboard and XInput defaults, both live (`native/input.ini.example`; controls
  table in `boot.md` "Input bindings"). `PS2X_PAD_PIPE=<name>` / `PS2X_PAD_SHOTS=<dir>` (plugin): the live pad.
- `PS2X_IOP_BATCH=0` (engine, patch 0042): run the IOP at every EE checkpoint (32 EE cycles) as before 0042, for
  A/B speed runs; stderr `[iop] batching off ...`. Default (unset or anything else): PCSX2's EEsCycle scheme, the
  EE banks cycles and the IOP runs when they reach its next event (thread wake, timer, DMA completion, callback),
  3072 EE cycles (PCSX2 `eeWaitCycles`), or an EE call into the IOP (RPC, SIF DMA/transfer, IOP memory, modules),
  which syncs it first. A performance switch, not a diagnostic.
- `PS2X_IOP_FASTLOOP=0` (engine, patch 0049): the IOP interpreter's full checks after every instruction (take a
  DMA start, walk the pending DMAs and callbacks, ask the import registry), as before 0049, for A/B speed runs;
  stderr `[iop] fast loop off ...`. Default: each check is one inline compare (a DMA start flag, the earliest DMA
  completion cycle, the `jr ra` word an import stub starts with), and the full work runs only when it can do
  something. Same instructions and cycles either way (`ps2_iop_batch_tests`). A performance switch, not a diagnostic.
- `PS2X_GS_THREAD=0` (adapter, `native/pcsx2_gs`): run every PCSX2 GS call inline on the game thread, as before the
  GS thread, for A/B speed runs; stderr `[pcsx2-gs] gs thread off`. Default (unset or anything else): PCSX2's MTGS
  scheme, a GS thread fed by an 8 MiB ring; the game thread stalls at a vsync while more than 2 are queued (PCSX2
  `VsyncQueueSize`); readback and snapshot wait for the ring to drain (`docs/ops/gs.md` "GS thread"). Every 1800
  vsyncs stderr has a `[pcsx2-gs] gs thread:` stats line (sync waits, queue-ahead stalls, ring full). Only with
  `PS2X_GS=pcsx2-sw|pcsx2-vk`. A performance switch, not a diagnostic.
- Game data (patch 0043): the runner takes an ISO 9660 disc image (read in place through one buffered handle; the
  boot ELF is SYSTEM.CNF's `BOOT2` inside it; `sceCdSearchFile` answers with the image's real LSNs), an extracted
  folder (boot ELF from its `SYSTEM.CNF`), or the boot ELF itself (the original form). Memory cards (`mc0`) and
  `host:` sit beside the image, or in the folder. stdout `[game] <image|folder|elf> <path> boot=... serial=...`.
- `outbreak.ini` (engine, patch 0043): start-up settings beside the exe, or `PS2X_SETTINGS=<path>`; read once;
  keys `game` (image, folder or ELF; the command-line argument wins), `textures` (PCSX2 layout
  `<root>/<serial>/replacements`, recorded and logged only), `mods` (reserved). A bad line is logged once as
  `[settings] line N: ...`. Example and format: `native/outbreak.ini.example`. `ob.sh` stages none and passes the game
  on the command line.
- `PS2X_FPS_LOG=1` (engine, patch 0044): `[fps]` lines with a visible window too (always on Android: logcat).
- `OB_*` variables override the `C:\ob` paths (`workspace.md`).

## Host profiling: `OB_PROFILE=1` build and `tools/hostprof`

Where the host CPU time goes (GS, microVU, runtime, generated game code), under real play: visible window, no ob_diag.

- Build: `OB_PROFILE=1 bash tools/ob.sh recompile`. `tools/ob/profile.cmake` is injected through
  `CMAKE_PROJECT_INCLUDE` (no PS2Recomp edit) and, at the end of the configure, adds `/Z7` (C/C++ only, not ml64) to
  `ps2_runtime`, `ps2_iop`, `ob_pcsx2_gs`, `ob_pcsx2_gs_adapter`, `ps2x_external_gs`, `ob_pcsx2_vu`,
  `ps2x_external_vu1`, and links `ps2EntryRunner` with `/DEBUG:FULL /OPT:REF /OPT:ICF` (`/DEBUG` alone turns
  REF/ICF off). Optimisation flags unchanged. Game units (`Unity\`, the PCH) keep their command lines: toggling the
  mode in either direction rebuilds 0 of them (count `Unity` in the `[n/m]` lines of `C:\ob\logs\ob-runner-build.log`).
  raylib is left out (its build tree `C:\ob\t\_deps` is shared with the tools tree). The exe, its PDB (~240 MB),
  the DLLs and shaders are copied to `C:\ob\r-profile`, so the profile exe survives the next normal build.
- Cost (2026-10-23): either toggle recompiles the host libraries, 0 game units: normal -> profile 84 s runner build
  (link 3.4 s), profile -> normal 81 s (link 1.7 s); an unchanged re-run 3 s (`reports/2026-10-23-gameplay.md`
  entry 25). The normal build after a round trip has the same `.text` hash as before. The profile `.text` differs by +384 bytes of 65 MB (linker folding
  under `/DEBUG`); the `/Z7` objects linked without `/DEBUG` give the normal `.text` size.
- Profiler: `bash tools/ob.sh hostprof-build`, then attach to a running runner:
  `C:\ob\hostprof-build\hostprof.exe --name ps2EntryRunner.exe --secs 30 --out C:\ob\logs\hostprof-jsbar.txt`
  (or `--pid N`; `--hz 1000` default, effective ~750-1000 Hz; `--min-cycles 50000`). Per tick, for each thread whose
  cycle counter moved by `--min-cycles` since the last tick: `SuspendThread`, `GetThreadContext` (RIP, RSP),
  `ResumeThread`, nothing allocated or locked in between (`gotchas.md`); idle threads are not suspended. Then DbgHelp
  symbols (PDB beside the exe) and the report: per-thread CPU time (`GetThreadTimes` over the window), on-CPU samples,
  top 30 functions (exclusive) and modules per busy thread. Generated game code has public names only: a sample is
  named after the public that starts its function (`.pdata`), otherwise `game code (generated)`; code with no module
  is JIT (microVU's cache). Exclusive only: no call stacks.
- Developer run: `play-profile.bat` (git-ignored, beside `play.bat`: the same line on `C:\ob\r-profile`), walk into
  J's Bar, run the hostprof command above from a second shell.

## VU1 capture (ob_diag plugin, was patch 0031) and `vucompare`

- Capture: `PS2X_GS=pcsx2-vk PS2X_VU1_CAPTURE='C:\ob\vu1-captures,400,4' bash tools/ob.sh boot --secs 420 --script gate/play.script --tag vucap`.
  Every VU1 run started by VIF1 MSCAL/MSCALF/MSCNT is offered to the recorder; a file is written when fewer than N
  (default 200) exist, its key (kind, start pc, hash of the 16 KB micro memory) has fewer than K (default 4), and no
  run with the same inputs was written. Captures are game-derived: keep them under `C:\ob`, never in git.
- File `vu1-<index6>-<pc4>.cap`, little-endian, layout in `native/ob_diag/include/ob_vu1_capture.h` (also read by `vucompare`):
  `FileHeader` (104 B: magic `OBVU1CAP`, version 1, header size, kind 0 = MSCAL / 1 = MSCNT, flags bit 0 = cycle
  budget hit, run index, start pc in bytes (MSCNT: state pc on entry), TOP, ITOP, EE-side FBRST, VPU_STAT before and
  after, cycle budget, interpreter cycles of the run, code generation, code hash, input hash, code/data/state sizes,
  packet count) | micro memory (16 KB) | data memory before (16 KB) | `StateRecord` before (656 B: VF as raw bits,
  VI, ACC, Q/P/I/R, pc, MAC, CLIP, STATUS, cycles, E/halt/D/T/branch bits, TOP/ITOP, branch target/delay) | data
  after | `StateRecord` after | per PATH1 (XGKICK) packet: u32 size + bytes.
- `summary.txt` in the same directory: `runs`, `files`, `bytes`, `keys`, `budget_hits`, then one line per key
  `kind pc code_hash first_gen runs cycles files` for every run seen (written or not). `vucompare` weights its
  timings by these cycles.
- `vucompare` per capture: (0) replays it through PS2Recomp's `VU1Interpreter` (the runner's own `ps2_runtime.lib`)
  and checks the replay reproduces the recorded outputs (column `replay`), (a) times the interpreter, (b) runs and
  times microVU (`native/pcsx2_vu`), and compares microVU against the interpreter bit for bit: data memory, VF, VI
  (16 bits), ACC, Q, P, I, R, STATUS, MAC, CLIP, PATH1 packets. Prints the first differing field per capture,
  per-field counts, known patterns (STATUS/MAC left at their input value by microVU's flag elimination; P equal to
  the final Q), and ns per interpreter cycle (median of `--repeat` warm runs; only the run call is timed).
  `--synthetic` runs a few built-in programs instead. Exit 0 = all bit-exact, 1 = some differ, 2 = usage.
- Golden files (for the arm64 build, `docs/ops/android.md` "VU1 on arm64"): `--write-golden DIR` writes microVU's
  outputs per capture as `DIR/<capture>.gold` (same fields + ended flag and cycle count + the capture's code/input
  hashes); `--golden DIR` compares this build's microVU with them bit for bit and prints
  `golden (...): mvu == x86 golden bit for bit N/M`. x86 goldens: `C:\ob\vu1-captures\golden` (120, 2.5 MB) and
  `golden-synthetic` (4); x86 microVU reads its own back 120/120. The same `compare/main.cpp` builds for arm64 as
  `ob_vucompare_a64` (`tools/ob/vucompare_a64.sh`).
- Baseline (2026-10-24, patch 0045): replay reproduces 100/120 (the 20 captures recorded before patch 0032 show
  `DIFF`, as before), interpreter 110 ns per VU1 cycle (182 with 0044); `C:\ob\logs\vucompare-0045d.log`. A change
  to the interpreter must keep the same replay-exact set.
- `native/pcsx2_vu` caveats handled in code: in the runner, VU1 data, VU1 code and the 64 MB code cache are one
  reservation made at startup (`InitOwned`, patch 0041; worst cache-to-data distance 64 MB); the compare tool uses
  `InitOwned` too (since 2026-10-24: the arm64 build can only run on backend-owned VU1 data). The disp32 reach (cache to data, globals to the rbx text pointer)
  is checked in every build (failure = error, no run); MXCSR is set to `EmuConfig.Cpu.VU1FPCR` around each run; microVU's program cache is cleared when
  the code generation changes; PCSX2 config defaults are the verbatim `Pcsx2Config.cpp` constructor bodies
  (`src/config_defaults.cpp`, re-copy on a PCSX2 bump). No PCSX2 source edit.

## VU1 backend in the runner: `PS2X_VU1=interp|mvu` (patch 0033)

- Selector read once at startup (`ps2_runtime.cpp`, next to `PS2X_GS`): `PS2X_VU1`, else `outbreak.ini`
  `vu1 = mvu|interp`, else the platform default (`ps2platform::defaultVu1Mode`: `interp` on the desktop, `mvu` on
  Android when the build has the external VU1; patch 0048; stdout `[vu1] <mode> from the settings|default` when the
  variable did not decide). `interp` = PS2Recomp's VU1 interpreter; `mvu` = PCSX2's microVU (`native/pcsx2_vu`;
  Android: ARMSX2's ARM64 microVU in `libob_a64vu.so`, dlopened by `adapter/hook.cpp`, so a load failure is a
  fall-back like a start failure); anything else = `[vu1] unknown PS2X_VU1=...`, exit 2 (a bad `vu1` value is a
  `[settings]` bad line, ignored).
  mvu starts right there, before PS2Memory and the window (patch 0041): stderr `[vu1] mvu active: <cache/data
  addresses, reach>`; if it cannot start (or the build has no microVU), stderr `[vu1] mvu unavailable (<reason>);
  falling back to interp` once and the interpreter runs. stdout `[vu1] backend=<mode actually used>`.
  PS2Memory takes its VU1 data/code from the backend (`PS2ExternalVu1::vu1Memory`), so the cache is always in reach
  (`reports/2026-10-23-gameplay.md` entry 24: a visible-window run found no free 64 MB within 1 GB of a heap block).
  The gate passes it through: `PS2X_GS=pcsx2-sw PS2X_VU1=mvu bash tools/ob.sh gate`.
- Seam (`runtime/ps2_vu1_external.h`): `PS2ExternalVu1::mscal/mscnt` replace the interpreter at the VIF1 MSCAL/MSCNT
  callbacks; `setState/readState` translate state (reset; `PS2X_VU1_CAPTURE`; `PS2Runtime::vu1State()`). In mvu
  mode PCSX2's `vuRegs[1]` is the only VU1 state. XGKICK packets go to `PS2Memory::submitGifPacket(Path1)`.
- Two targets, as the GS: `native/pcsx2_vu/adapter/hook.cpp` (PS2Recomp headers and flags) implements the seam over
  the plain-types `include/ob_pcsx2_vu.h`; `ob_pcsx2_vu` (PCSX2 headers and flags, links `ob_pcsx2_gs` PRIVATE) holds
  microVU. CMake `-DPS2X_EXTERNAL_VU1_DIR=native/pcsx2_vu` (set by `cmd_recompile`); nothing extra is staged. The
  compare tool is configured only in the standalone `ob.sh vucompare` build. `gate.py stamp` hashes `native/pcsx2_vu/`.
- Differences from the interpreter (by design): D bit ignored (microVU compiles it out); per-run budget 3,000,000
  cycles (PCSX2's `vu1RunCycles`) instead of 65,536, a program that does not reach its E bit is logged (first 8);
  STATUS/MAC only computed where read (flag elimination, `reports/2026-10-23-gameplay.md` entry 6).

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
- Game data model (developer, 2026-10-24): the runner accepts a copy of the game (disc image or extracted folder),
  optionally a root folder for the 4K textures and a root folder for mods (`outbreak.ini`, patch 0043).
- Standing OK for work under `C:\ob` and hidden scripted boots; PCSX2 launches, visible windows and the Odin need
  a "proceed" each time.
