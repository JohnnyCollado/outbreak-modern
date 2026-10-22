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
player's disc (game_files/, git-ignored)
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
  IOP ............ interpreter running the game's own IRX modules
  SPU2 (sound) ... not producing sound yet
  window/input ... raylib window and pad; PCSX2 Vulkan draws into a child window
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
| `native/pcsx2_gs/` | PCSX2's GS built as a static library, host stubs, D3D shims, smoke test, and the adapter that connects it to PS2Recomp's hook | `docs/ops/gs.md` |
| `gate/` | The correctness judge: scripted boot, frozen tolerance, reference hashes, baselines, evidence | `docs/ops/gate.md` |
| `tools/ob.sh` (+ `tools/ob/`) | Windows driver: build tools, recompile, boot, test, gate, pcsx2-gs | `tools/ob/sync_changed.py` keeps rebuilds incremental |
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

## 4. Rules that still apply

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
- ARMSX2: https://github.com/ARMSX2/ARMSX2 (and PR #741), https://github.com/Trixarian/ARMSX2
- Play!: https://github.com/jpd002/Play- (licence: https://github.com/jpd002/Play-/blob/master/License.txt)
- DobieStation: https://github.com/PSI-Rockin/DobieStation
- paraLLEl-GS: https://github.com/Arntzen-Software/parallel-gs

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
