# outbreak-modern: architecture

What each part of this repository is for, how the parts fit together, and where the evidence
behind them lives. Appendix A indexes the reports; Appendix B lists every source used so far,
for citations.

## 1. What the project is

A native build of Resident Evil Outbreak File #1 (SLPM-65428, JP v1.04 + English fan patch
v2.1), Windows first, then Linux and Android, with modern controls. The game's code is
statically recompiled from the player's own disc at build time; nothing from the disc is stored
in git. [`PROJECT_CONTEXT.md`; `docs/native-port-design.md`]

## 2. How it runs

```
player's disc: ISO image or extracted folder (game_files/, ps2_emu/roms/, git-ignored; runner: patch 0043)
  ├─ SLPM_654.28 (main ELF) + prog/*.BIN overlays (DEMO, SUBMAIN, NETDNAS)
  │     │  ps2_analyzer + ps2_recomp (PS2Recomp + our patches)      tools/ob.sh recompile
  │     ▼
  │  ~5,700 generated C++ files ──► MSVC ──► ps2EntryRunner.exe        C:\ob\src, C:\ob\r
  │
  └─ netbio0x.dat archives (textures, fonts, sound) read at run time

ps2EntryRunner.exe at run time
  EE code ........ recompiled C++ (PS2 float rules emitted as calls)
  kernel/BIOS .... PS2Recomp HLE kernel and scheduler
  VU0 ............ inline (macro mode);  VU1 ... PS2Recomp interpreter
  VIF/GIF/DMA .... PS2Recomp interpreters ──► one GIF choke point
  GS (graphics) .. PS2X_GS=cpu        PS2Recomp CPU rasteriser (fallback, unfixed)
                   PS2X_GS=pcsx2-sw   PCSX2 2.9.134 software GS (used to verify)
                   PS2X_GS=pcsx2-vk   PCSX2 2.9.134 Vulkan GS (the display path)
  IOP ............ interpreter running the game's own IRX modules; runs in batches at its next event (patch 0042), one-compare checks per instruction (0049)
  SPU2 (sound) ... PCSX2 2.9.134 SPU2 in-process (native/pcsx2_spu2; PS2X_SPU2=pcsx2|off, patch 0056), out through
                   a raylib float audio stream; the IOP runs the game's own sound modules (reports/2026-10-24-m5-sound.md)
  IPU (movies) ... PCSX2 2.9.134 IPU in-process (native/pcsx2_ipu; PS2X_IPU=pcsx2|off, patch 0060) with DMA 3/4;
                   Sony's libmpeg/libipu run as game code (ob.sh recompile drops their stubs); 0059, 0061 needed too
  SIO2 (cards) ... PCSX2 2.9.134 SIO2 + memory cards in-process (native/pcsx2_sio2; PS2X_SIO2=pcsx2|off, patch 0071)
                   with DMA 11/12; PCSX2 .ps2 card files in outbreak.ini saves (0072); the game's own libmc2/libdbc
                   (0073 keeps sceDbcInit) and disc modules sio2man/mc2_s1 (reports/2026-10-26-memory-cards.md)
  window/input ... raylib window and pad (bindings: input.ini, patches 0038/0040); PCSX2 Vulkan draws into a child window;
                   title = frame counter
  disc ........... cdrom0: = the ISO 9660 image (ps2x/iop/iso9660.h: real LSNs, buffered reads) or the folder;
                   settings outbreak.ini (game, textures, mods; patch 0043; saves, 0072)
  Android ........ arm64 .so in an APK (NativeActivity, no Java); PCSX2 Vulkan on the ANativeWindow; VU1 interpreter
                   (docs/ops/android.md)
  diagnostics .... optional plugin ob_diag.dll (native/ob_diag) on the hook API runtime/ps2_diag_hooks.h
```
Sources: GS choice [`docs/ops/gs.md` Decisions; `research/2026-10-22-gpu-gs-options.md`];
hook points [`research/2026-10-22-gs-seam-in-ps2recomp.md`]; PS2 hardware blocks
[`research/2026-10-22-ps2-architecture.md`; Copetti, Appendix B].

## 3. Components

| Path | Purpose | Notes |
|---|---|---|
| `README.md` | Front page | |
| `PROJECT_CONTEXT.md` | Founding document: goals, stack, early decisions | Historical; not edited |
| `CREDITS.md` | Credits and licences of what we build on | |
| `docs/ARCHITECTURE.md` | This file | |
| `docs/native-port-design.md` | The design: recompile + decomp, HLE, platform layer | |
| `docs/milestones/` | Milestones M0-M7 (M0 = choosing the base) | Roadmap |
| `docs/android-port-map.md` | Every hardware/library touchpoint and its Android stand-in | |
| `docs/ops/` | How to build, boot, gate, pin; gotchas; the GS integration | Start at `docs/ops/build.md` |
| `docs/history/` | Past phase plans (1.1-1.3), their research notes (R-ids cited in `docs/ops/`) and task summaries | Read-only record |
| `CLAUDE.md` | Short working notes for Claude Code sessions in this repo | |
| `upstream/ps2recomp/` | **Our PS2Recomp changes**: numbered patches 0001-00NN, each with a test, plus a README table | The real source of runtime changes; applied as commits in `C:\ob\up` |
| `upstream/pcsx2/` | Our PCSX2 changes (none so far) | PCSX2 pinned at v2.9.134 (`docs/ops/pins.md`) |
| `native/pcsx2_gs/` | PCSX2's GS built as a static library, host stubs, D3D shims, smoke test, and the adapter that connects it to PS2Recomp's hook; the adapter runs PCSX2 on its own GS thread fed by a ring (`adapter/gs_ring.h`, PCSX2 MTGS's scheme; `PS2X_GS_THREAD=0` inline); on Android it also picks the Vulkan driver (`adapter/gpu_driver*`: system, or a player-supplied package loaded through libadrenotools, with fall-back; `ob_gpu_driver_tests`) | `docs/ops/gs.md`, `docs/ops/android.md` (GPU driver) |
| `native/pcsx2_vu/` | PCSX2's microVU (VU1 recompiler) built as a static library (wrapper TU, fakes, config defaults, link-only stubs), the runner's `PS2X_VU1=mvu` backend (`adapter/hook.cpp`, patch 0033; owns VU1 memory + code cache in one reservation and falls back to the interpreter, patch 0041), `ob_pcsx2_vu_compare`, the offline interpreter-vs-microVU diff on VU1 captures, and `ob_pcsx2_vu_tests` (placement under a crowded address space, fallback; run by `ob.sh test`). `a64/`: Android arm64 only, ARMSX2's ARM64 microVU (pinned 90126bd, compiled against ARMSX2's headers) as its own `libob_a64vu.so` with the same API (hidden symbols, exports only `ObPcsx2Vu::*` + the `ob_pcsx2_vu_api` table that `hook.cpp` dlopens; default `mvu` on Android, patch 0048) and `ob_vucompare_a64` (`compare/main.cpp` for the device, against x86 golden files) | `docs/ops/build.md` (VU1 backend, VU1 capture, vucompare), `docs/ops/android.md` (VU1 on arm64), `research/2026-10-23-microvu-feasibility.md`, `research/2026-10-24-armsx2-microvu-feasibility.md` |
| `native/vu1aot/` | `vu1aot_rt.h`: runtime of the VU1 ahead-of-time code (state record, interpreter-exact float helpers, PATH1 transfer); included by the generated programs, which stay under `C:\ob\vu1aot\gen` (game-derived) | `docs/ops/build.md` (VU1 ahead-of-time code), `reports/2026-10-24-vu1-aot-emitter.md` |
| `tools/vu1aot/` | VU1 ahead-of-time generator: `vu1aot.py` (front end: extracts and decodes the 27 microprograms from the ELF, `facts`), `emit.py` (C++ emitter, pipeline timing resolved at generation time); `ob.sh vu1aot` drives it | `research/2026-10-24-vu1-aot-and-shaders.md`, `reports/2026-10-24-vu1-aot-frontend.md`, `reports/2026-10-24-vu1-aot-emitter.md` |
| `native/ob_ui/` | **The settings menu** (`docs/plans/2026-10-26-settings-menu.md`): `core/` (std only, `ob_ui_tests` in `ob.sh test`: comment-keeping ini editor, the 3-step disc check with the build's ELF SHA-256/CRC, the menu trigger (F1/Esc; Windows Back + Start held 5 s with `comboProgress` for the gear ring; Android Back, no combo; no hold-back) and pad gate, `.ps2` card reading/backup/move, the touch controls' pad `touch_pad`: merged into the first pad in `filterPad`; `game_screen`: which touch layout the game's screen wants, from its RAM via PS2Recomp patch 0081), `ui/` (launcher and in-game menu, Dear ImGui on PCSX2's GS through `ob_gs_ui_present`; Windows Browse dialogs), `android/` (Storage Access Framework picker over JNI, zip import). Fed by `native/pcsx2_gs/adapter/ui_hook.cpp` (raylib input, window calls) over PS2Recomp's `runtime/ps2_ui_external.h` (patch 0078); its keys live under `[ui]` in `outbreak.ini` (0079) | Linked into `ps2x_external_gs`; launcher off for `ob.sh boot` and hidden runs (`PS2X_LAUNCHER=0`, `PS2X_HIDDEN=1`) |
| `native/windows/` | The Windows app icon: `outbreak.ico` and `outbreak-icon.png` (the runner's window icon, staged beside the exe by `ob.sh recompile`) | Made by `tools/ob/make_icons.py` |
| `docs/assets/` | The developer's icon art (square, rounded square, circle) | Source for `tools/ob/make_icons.py` (Android `native/android/res`, Windows `native/windows`) |
| `native/ob_diag/` | **The diagnostics plugin** `ob_diag.dll` over PS2Recomp's hook API (`runtime/ps2_diag_hooks.h`, patch 0036): scripted input (`PS2X_INPUT_SCRIPT`), frame dumps and captures (`PS2X_FRAME_DUMP_DIR`), frame-time CSV (`PS2X_FRAME_TIME`), guest PC sampler (`PS2X_PC_SAMPLE`), live mock controller on a named pipe (`PS2X_PAD_PIPE`; client `tools/ob/pad.py`), VU1 capture (`PS2X_VU1_CAPTURE`; `include/ob_vu1_capture.h` is the file format), and its tests (`ob_diag_tests`) | Built into `C:\ob\diag-build`; loaded only via `PS2X_DIAG` (ob.sh) or when placed beside the exe; a play build ships without it |
| `gate/` | The correctness judge: scripted boot, frozen tolerance, reference hashes, baselines, evidence | `docs/ops/gate.md` |
| `tools/ob.sh` (+ `tools/ob/`) | Windows driver: build tools, recompile, boot, test, gate, pcsx2-gs | `tools/ob/sync_changed.py` keeps rebuilds incremental |
| `native/outbreak.ini.example` | Commented example of the runner's `outbreak.ini` (game image/folder, textures root, mods root; patch 0043) | Copy beside the exe, or to the Android app's files dir |
| `native/android/` | `AndroidManifest.xml` of the APK (`com.outbreakmodern.app`) and `kotlin/` (the Compose launcher and in-game menu; `OutbreakActivity`, a NativeActivity subclass; the touch controls `TouchOverlay`/`TouchLogic`, design `docs/plans/2026-10-26-touch-controls.md`), `test/kotlin/` (JVM unit tests run by `android.sh package`), and the Gradle project that packages the APK (`build.gradle.kts`, wrapper) | Packaged by `tools/ob/android.sh` (`ob.sh android`) |
| `native/input.ini.example` | Commented example of the runner's `input.ini` (host key/gamepad bindings, patches 0038/0040) | Not staged; copy beside the exe to use |
| `tools/ob/android.sh` (+ `android_symcheck.py`; `apk_add_libs.py` no longer used) | Android arm64 build and APK: shaderc, NDK tree `C:\ob\android-r`, the native libraries staged for Gradle (`native/android`) which packs and signs, offline symbol check | `docs/ops/android.md` |
| `tools/hostprof/` | External sampling profiler `hostprof.exe` (suspend, read RIP, resume per thread; DbgHelp symbols; per-thread CPU time and top functions). Outside the engine: attaches to a running `ps2EntryRunner.exe` | Built by `ob.sh hostprof-build` into `C:\ob\hostprof-build`; names host functions in an `OB_PROFILE=1` build (`tools/ob/profile.cmake`, `docs/ops/build.md` "Host profiling") |
| `tools/gate/` | Gate scoring (`gate.py`) and reference handling (`refs.py`) | |
| `tools/mwo3_to_elf.py`, `tools/splat_functions.py` | Turn overlay `.BIN` files into ELFs and function lists for the recompiler | With tests |
| `tools/q_scan.py`, `tools/recomp_cycle.sh` | Analysis scan; Linux form of the recompile cycle | |
| `tools/run.sh` | Runs long commands with captured logs | |
| `research/` | Research reports (2026-10-22) with dated sources | Appendix B |
| `reports/` | Struggle reports: every issue and fix, as it happened | Appendix A |
| `results/` | Native-era analysis notes (analyzer, recompile, sound-bank wall) | |
| `legacy/` | The earlier ARMSX2 Modern Camera mod: its code, tools, patch and results | Not part of the native build |
| `.claude/settings.json` | Claude Code settings for this repo | |

Outside the repository, on purpose:
- `C:\ob\` is the workshop: `up` (PS2Recomp with our patches as commits), `src` (built copy),
  `t` (tools build), `r` (runner build), `work`/`ovl` (recompiler output), `pcsx2` and
  `pcsx2-deps` (pinned clone and deps), `ref` (reference screenshots), `frames-*`, `logs`.
  Game-derived, so never in git. [`docs/ops/workspace.md`]
- `game_files/` and `ps2_emu/` (the disc's files and the reference PCSX2) are git-ignored.

## 4. Engine vs diagnostics

The engine (PS2Recomp runtime plus `native/pcsx2_gs` and `native/pcsx2_vu`) holds only what plays the game and what
makes it faster or draw better (developer, 2026-10-23). Everything that observes or drives it for testing lives in
the optional plugin `native/ob_diag` (a DLL):
- The engine keeps thin hook points only: one `PS2DiagHooks` pointer, null = no plugin, one load and null check per
  site (same pattern as `PS2ExternalGS` / `PS2ExternalVu1`). Hook points never live in generated game code
  (`ob.sh depcheck`: 0 game units).
- A play build ships without the plugin and runs identically; the frame counter in the title is the one readout
  that stays in the engine (one compare per frame, one title set per second).
- New diagnostic tools go into the plugin over `runtime/ps2_diag_hooks.h`; if a tool needs something the API lacks,
  extend the API (bump `PS2_DIAG_API_VERSION`) rather than adding the tool to the engine.
- Later mod support is meant to reuse the same API (load path, versioned host table, hooks for vsync, pad, frames,
  guest memory reads). Not built: mods would need more (guest memory writes, function hooks, several plugins).

## 5. Rules that still apply

- Every PS2Recomp change is a numbered patch with a test that fails without it.
  [`upstream/ps2recomp/README.md`]
- The gate decides correctness; its tolerance is frozen. [`gate/TOLERANCE.md`]
- Game data never enters git.
- Nothing is pushed without the developer's OK.
- Every plan cites its sources (global rule 8).

## Appendix A: reports index

| Date | Report | What it covers |
|---|---|---|
| 2026-10-19 | `legacy/README.md` and `legacy/phase1-feasibility-report.md` | The ARMSX2 Modern Camera mod and its feasibility study |
| 2026-10-21 | `reports/2026-10-21-native-port-bring-up.md` | Milestone plans, choosing PS2Recomp, nine PS2Recomp fixes, first recompile and boot |
| 2026-10-21 | `reports/2026-10-21-windows-port-bank-wall.md` | Patches 0010-0018, the sound bank wall, overlays as guest modules; boot reaches the main menu |
| 2026-10-21 | `reports/2026-10-21-pa3-groundwork-and-boot-speed.md` | `tools/ob.sh`, patch 0019, the profiler deadlock, aggressive logs halving boot speed |
| 2026-10-22 | `reports/2026-10-22-gate.md` | The gate: PCSX2 reference captures, display-register probe, tolerance and baseline |
| 2026-10-22 | `reports/2026-10-22-pcsx2-gs.md` | PCSX2 GS in-process: the seam, crashes, black frames, Vulkan window, the glyph fault |
| 2026-10-21 | `results/*.md` | Analyzer NOPs, q-scan, main-ELF recompile, sound-bank wall notes |
| 2026-10-22 | `research/*.md` | See Appendix B |
| 2026-10-21/22 | `docs/history/phase-1.*/PhaseEnd.md`, `tasks/`, `research/` | Phase recaps, task summaries, research notes R0-001 to R1.3-010 |

## Appendix B: sources

### Web (all fetched 2026-10-22 unless noted; GitHub first, per the web-safety rule)

PS2 hardware and emulation
- Rodrigo Copetti, *PlayStation 2 Architecture*: https://www.copetti.org/writings/consoles/playstation-2/ (changelog to 2026-09-06)
- Hans-Kristian Arntzen, *PlayStation 2 GS emulation: the final frontier of Vulkan compute emulation* (2024-07-03): https://themaister.net/blog/2024/07/03/playstation-2-gs-emulation-the-final-frontier-of-vulkan-compute-emulation
- PCSX2 GS source: https://github.com/PCSX2/pcsx2/tree/master/pcsx2/GS (+ `/Renderers`, `/Renderers/HW`)
- PCSX2 GameIndex docs: https://github.com/PCSX2/pcsx2/blob/master/pcsx2/Docs/GameIndex.md
- PCSX2 issue #3024 and PR #4726 (search results only): https://github.com/PCSX2/pcsx2/issues/3024, https://github.com/PCSX2/pcsx2/pull/4726
- ARMSX2: https://github.com/ARMSX2/ARMSX2 (and PR #741), https://github.com/Trixarian/ARMSX2 (its ARM64 microVU, `pcsx2/arm64/microVU-arm64*`, `AsmHelpers`, `vuaot/`, is built from a sparse clone at 90126bd into `libob_a64vu.so`, 2026-10-24)
- Play!: https://github.com/jpd002/Play- (licence: https://github.com/jpd002/Play-/blob/master/License.txt)
- DobieStation: https://github.com/PSI-Rockin/DobieStation
- paraLLEl-GS: https://github.com/Arntzen-Software/parallel-gs
- libadrenotools (BSD-2-Clause; custom Vulkan driver loading on Adreno, fetched 2026-10-24): https://github.com/bylaws/libadrenotools (README, `include/adrenotools/driver.h`, `tools/ADPKG.md`), https://github.com/bylaws/liblinkernsbypass; ARMSX2's use of it (reference only): https://raw.githubusercontent.com/ARMSX2/ARMSX2/HEAD/pcsx2/GS/Renderers/Vulkan/VKLoader.cpp

Recompilation projects
- PS2Recomp: https://github.com/ran-j/PS2Recomp
- reo (RE Outbreak, US): https://github.com/sp00nznet/reo; fork https://github.com/Ninjacornix/reo
- RaccoonRecomp Outbreak: https://github.com/RaccoonRecomp/ResidentEvilOutbreakRecompiled
- gt4-staticrecomp PCSX2-GS process split: https://github.com/moesuito/gt4-staticrecomp/pull/24 (also #26, #28, #34)
- Def Jam Vendetta recomp (paraLLEl-GS): https://github.com/jcsix694/def-jam-vendetta-recomp
- ps2-web-recomp: https://github.com/BlizzHacker/ps2-web-recomp
- Zelda64Recomp: https://github.com/Zelda64Recomp/Zelda64Recomp
- UnleashedRecomp: https://github.com/hedge-dev/UnleashedRecomp
- recompedia PS2 list (third party, opened before the safety rule): https://recompedia.com/platform/ps2
- Time Extension news, 2026-01-27 (opened before the safety rule): https://www.timeextension.com/news/2026/01/native-pc-ports-of-ps2-games-could-be-on-the-way-thanks-to-new-recompilation-experiment

Outbreak-specific
- Outbreak-decomp (CC0; names, structures, function map): https://github.com/GalaxyNostalgia/Outbreak-decomp
- Outbreak research documentation: https://outbreak-research.github.io/Documentation/
- PAL File #2 private server: https://github.com/erizodev/pal-outbreak-2-server
- Outbreak·Web (given by the developer; read only): https://outbreakweb.com/file1/

### Local sources (on this machine)

- PCSX2 2.9.134 source clone, pinned `a81f1aa`: `C:\ob\pcsx2` (e.g. `pcsx2/GS/GSState.cpp`,
  `pcsx2/Vif_Codes.cpp`, `pcsx2/Vif_Transfer.cpp`)
- PCSX2 2.9.134 reference build and its game database: `ps2_emu/` (`resources/GameIndex.yaml`)
- PS2Recomp with our patches: `C:\ob\up` (history = `upstream/ps2recomp/`)
- The disc's files: `game_files/` (font archive: `netbio00.dat`, AFS at 0x800, entries 554/555)
- Reference screenshots: `C:\ob\ref\*-a.png`, `*-b.png`
- Run logs and frames: `C:\ob\logs\`, `C:\ob\frames-*`, `.run/1.3/`
