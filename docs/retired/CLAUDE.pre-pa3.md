# Working on this repository (notes for Claude sessions)

Last updated 2026-10-21, end of the bring-up session. Read `reports/2026-10-21-native-port-bring-up.md`
for the full story; this file is the hand-off.

## How Johnny wants to work

1. Talk first; brainstorm before implementing. Challenge assumptions; honest feedback over agreement.
2. Show before building: a plan or example, then wait for approval. Minor fixes can just be done
   with a short explanation; anything significant or unfamiliar: ask "auto-fix or walkthrough?".
3. **Never act on his devices (the Odin 3, phone, PC) without an explicit "proceed" for that action.**
   Everything so far has run only in the cloud container.
4. Explain after: show it working, then what changed and why.
5. Don't assume. Say what's **Confirmed**, **Unverified** or **Unknown**, as in `docs/`.
6. At the end of a piece of work, write a report in `reports/` (format: `reports/TEMPLATE.md`),
   including mistakes and how they were corrected.

## Rules of this repo

- **No game data in git, ever**: no disc files, IOP modules, archives, disassembly, recompiler
  output, generated `register_functions.cpp`/`ps2_recompiled_*.h`. Hashes, addresses, names and
  sizes are fine. (`game_files/` was committed to `main` on 2026-10-21; a later commit `de60c78`
  removed it from the tree and git-ignored the folder, but commits `bb0c282` and `e012448` still
  hold the files in `main`'s history. Full removal needs a history rewrite of `main`, which only
  Johnny can OK. Don't merge those commits into working branches.)
- PS2Recomp is GPL-3.0 and vendored at `PS2Recomp-main/PS2Recomp-main/` (upstream `eb8688b`
  + our patches 0001-0009 applied). Every change to it also goes into `upstream/ps2recomp/` as
  a numbered patch with its own test, and its README row. **Nothing is sent to ran-j
  (PS2Recomp's author) without Johnny's OK**; none has been yet.
- Commits: clear messages, one topic each; push to the working branch after each.

## Where things are

| | |
|---|---|
| GitHub | `JohnnyCollado/outbreak-modern` (renamed from `outbreak-modern-camera-mod-android`; the local checkout keeps the old folder name) |
| Working branch | `claude/sharp-planck-x8k410` (all the port work; `main` has none of it yet) |
| Plans | `docs/native-port-design.md`, `docs/milestones/` (M0-M7, `m0-decision.md` = we build on PS2Recomp) |
| Measurements | `results/recomp-main-20261021.md` (boot progress), `results/q-scan-20261021.md` |
| Reports | `reports/` |
| PS2Recomp patches | `upstream/ps2recomp/0001-0018` + README (what, why, how tested) |
| Reference emulator | `ps2_emu/` (git-ignored): PCSX2 2.9.134 portable with Johnny's BIOS and ISO; its debugger is the ground truth for kernel/IOP questions (`results/sound-bank-wall-20261021.md`) |
| Tools | `tools/q_scan.py` (+test), `tools/splat_functions.py` (+test), `tools/mwo3_to_elf.py` (+test), `tools/recomp_cycle.sh` |
| Game files Johnny provided | In `main`'s **history only**, commit `e012448` (`git archive e012448 game_files \| tar -x -C <scratch>`): `SLPM_654.28`, `SYSTEM.CNF`, `prog/*.bin` (8 overlays), `iop/modules/` (33 files). If that history gets rewritten, Johnny has to provide them again (Dropbox/Drive). SLPM-65428 v1.04 **with the English patch v2.1**: 4 binaries match the decomp's `english.sha1`, 5 match `build.sha1`. |

## State of the port (what works, what's next)

- Main ELF recompiles with the decomp's function list: 5,358 functions, 0 errors, ~4 min build.
- Boots under Xvfb through: graphics init (M2 check passes: reaches `sceGsSyncV`), all 11
  `DRIVE.DAT` IOP modules running as real code on PS2Recomp's IOP interpreter (option C: HLE
  for Sony modules is now an optimisation to measure, not a blocker), HDD check, USB modules,
  **sound driver connected**, CRI DVD file system set up.
- Since 2026-10-21 (second session, on Johnny's Windows PC; `reports/2026-10-21-windows-port-bank-wall.md`):
  the archives and every root file of the disc are local (`game_files/`, git-ignored), and with
  patches 0010-0016 the game loads its archives, fonts and first sound bank, draws its save-data
  screens, reads the controller (libpad2) and answers the memory-card prompt (Left, then
  **Circle**: Japanese confirm layout; game bits Up/Down/Left/Right = 1/2/4/8, Cross 0x10,
  Circle 0x20).
- **Overlays work** (patches 0017-0018 + `tools/mwo3_to_elf.py`): each `PROG/*.BIN` is wrapped as
  an ELF and recompiled with `module = "<name>"`; the runtime picks the module whose MWo3 header
  is in RAM. Built so far: DEMO, SUBMAIN (both needed for the menus) and NETDNAS (partial, see
  below). Cycle on Windows: `C:\ob\recomp-cycle.sh` (main ELF) then `C:\ob\ovl-cycle.sh demo
  submain netdnas` (config template `C:\ob\ovl\demo.toml`), then a `boot-frames-N.ps1`.
- **Reached (2026-10-21):** ACCESS/CAPCOM screens → title → **main menu** → Scenario Select →
  character select, with no missing function in 300 s, driven blind by the temporary auto-press
  (Left, Circle, Start on a cycle; `PS2X_AUTO_PRESS`).
- **Next wall: rendering.** Every screen draws wrongly (title background missing, glyphs
  misshapen, ACCESS screen fringed, character-select images corrupted). Causes unknown. Compare
  against PCSX2 screenshots of the same screens (`ps2_emu/`) and find the GS feature behind each.
- **Then:** GAME (4,414 functions) and the rest of the overlays as modules for gameplay. NETDNAS
  needs `splat_functions.py` to read the decomp's bare `nonmatching <name>` + `alabel` markers
  (the network library was never disassembled; 1,305 starts, no sizes) if offline play ever
  calls past its first 30 functions. Overlay headers: SUBMAIN id 1 @0x340000; DEMO 2, GAME 3,
  NETWK 4 @0x540000; NETDNAS 5, NETHTTP 6 @0x6C0000; SINGLE 8, NETAQ 9 @0x820000.
- Still unknown: performance on the Odin (nothing measured), this repo's licence, Clang on
  patches 0007-0018, VU0 ÷0 exact value, whether `patch.c` records ever hit overlay code (the
  module code is the disc's; a patch to overlay code after load would not be seen).

## Rebuilding the working state

### On Johnny's Windows PC (what exists since 2026-10-21; nothing is wiped between sessions)

Everything lives in `C:\ob` (Johnny's OK; MSVC hits the 260-character path limit in the
scratchpad): `src` = working copy of the vendored PS2Recomp (**holds uncommitted temporary
traces**: IOP RPC/thread/import traces, `[pad2:*]`, frame dump `PS2X_FRAME_DUMP_DIR`, auto-press
`PS2X_AUTO_PRESS`; the clone `C:\ob\up` and the repo's vendored copy are clean), `up` = clone of
upstream `eb8688b` + patches (where each patch is committed and `git format-patch`'d from), `t` =
MSVC tools+tests build (`build-tools.bat`, `tests.bat` → `ps2x_tests`), `r` = runner build
(`build-runner.bat`), `g` = MSYS2 GCC build for gdb Python probes (`probe*.py`), `work` =
analyzer/recompiler input and output, `logs`, `frames`, `decomp-src` = decomp C files fetched
from GitHub. Cycle: `recomp-cycle.sh` (analyzer → recompiler → copy into `src/runner`), then
`boot-frames-N.ps1` (runner build + 200 s boot with frame dumps; no window access needed: read
`C:\ob\frames\frame-*.png`). The decomp clone and its splat output are in the session
scratchpad (`decomp/`, `WORK/`); re-clone if gone.

### In a Linux container (the original recipe; the container is wiped between sessions)

Keep all of this **outside the repo** (the session's scratchpad dir). Steps, in order:

1. `apt-get install -y libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libgl1-mesa-dev libwayland-dev libxkbcommon-dev xvfb gdb`
2. Game files: `git archive e012448 game_files | tar -x -C <scratch>` (see the table above); then make the run
   folder with **upper-case** names: `RUN/SLPM_654.28`, `RUN/SYSTEM.CNF`, `RUN/PROG/GAME.BIN`...,
   `RUN/IOP/MODULES/<the 33 files>`, later `RUN/NETBIO0*.DAT`.
3. Decomp: `git clone --depth 1 https://github.com/GalaxyNostalgia/Outbreak-decomp` (was
   `eb6af38`); `python3 -m venv venv && venv/bin/pip install -r requirements.txt`; place the
   files as `orig/SLPM_654.28/english/{SLPM_654.28,PROG/DEMO.BIN,PROG/GAME.BIN,PROG/NETWK.BIN}`
   and `orig/SLPM_654.28/PROG/{SUBMAIN,SINGLE,NETDNAS,NETHTTP,NETAQ}.BIN`; run
   `venv/bin/python -m splat split config/SLPM_654.28/<t>.yaml` for `main.en demo.en game.en
   netwk.en submain single netdnas nethttp netaq`. Then
   `venv/bin/python -I tools/splat_functions.py <decomp>/asm/SLPM_654.28/en/main WORK/main-functions.csv`.
4. PS2Recomp tools + tests (from the vendored copy, or a clone of upstream `eb8688b` with
   `git am upstream/ps2recomp/*.patch`):
   `cmake -S $PS2RECOMP -B $PS2RECOMP/out/linux-gcc -G Ninja -DCMAKE_BUILD_TYPE=Release -DPS2X_BUILD_STUDIO=OFF -DPS2X_ENABLE_FFMPEG=OFF && cmake --build $PS2RECOMP/out/linux-gcc -j4`
   (~5 min). Tests: `cd $PS2RECOMP/ps2xTest && ../out/linux-gcc/ps2xTest/ps2x_tests` (run from
   that dir; 452/452). IOP: `cmake -S ps2xIOP -B out/iop-tests -DPS2X_IOP_BUILD_TESTS=ON`, build,
   `ctest --test-dir out/iop-tests` (4/4). GS cache: `cmake -S ps2xTest/gs_cache -B out/gscache`, ctest (45/45).
5. Runner build dir: `cmake -S $PS2RECOMP -B $PS2RECOMP/out/runner -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo "-DCMAKE_CXX_FLAGS_RELWITHDEBINFO=-O2 -DNDEBUG" -DPS2X_BUILD_STUDIO=OFF -DPS2X_BUILD_TEST=OFF -DPS2X_BUILD_ANALYZER=OFF -DPS2X_BUILD_RECOMP=OFF -DPS2X_ENABLE_FFMPEG=OFF -DPS2X_ENABLE_DEBUG_UI=OFF -DFETCHCONTENT_BASE_DIR=$PS2RECOMP/out/linux-gcc/_deps`
   (the runner dir must hold the recompiler output first; `tools/recomp_cycle.sh` copies it).
6. `WORK/`: copy of `SLPM_654.28` + `main-functions.csv`; then `tools/recomp_cycle.sh` with the
   variables in its header. First full runner build ~4 min; later ones ~15 s when only the
   runtime changed.
7. Android check (optional): NDK r27c from `dl.google.com/android/repository/android-ndk-r27c-linux.zip`;
   `cmake -DCMAKE_TOOLCHAIN_FILE=<ndk>/build/cmake/android.toolchain.cmake -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 ...` builds `libps2EntryRunner.so`.

If building inside the vendored copy, its nested `.gitignore` hides `out/`, `src/runner/` and
the generated headers; still check `git status` before every commit.

## Method that worked (per wall)

1. The runner's `[run:tick] ... pc=0x...` line gives the guest pc of the loop.
2. Map pc → function with `main-functions.csv`; read the decomp's C for it (`src/SLPM_654.28/main/*.c`).
3. Confirm with gdb: `break ps2_stubs::<name>` / `break ps2_syscalls::<name>` /
   `break func_<ADDR>_0x<addr>`; guest registers live at `$rsi + n*16` ($a0 = n 4, $a3 = n 7,
   $ra = n 31); `bt` shows the guest call chain through the generated functions.
4. Fix in PS2Recomp with a test that **fails without the fix** (prove it), as its own patch.
5. Record it: `results/` for measurements, `reports/` for the story, `upstream/ps2recomp/README.md` row.
