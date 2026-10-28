# Online play: working from another computer (handoff, 2026-10-27)

Everything decided and found is in git on `netplay-research`: the plan (`docs/plans/2026-10-26-online-play.md`,
`docs/plans/2026-10-27-ob-server.md`), the research notes (`research/2026-10-2{6,7}-*.md`), the struggle report
(`reports/2026-10-26-online-play.md`, entries 1-24: read 19-24 first), the PS2Recomp patches
(`upstream/ps2recomp/0087-0099`, pins in `docs/ops/pins.md`), `native/ob_server`, `native/pcsx2_dev9`,
`tools/ob/mcd.py`, and the drive harness in `tools/net/` (copied from the ignored `.run/spike/`).

## What is not in git and how to get it back

| On the old PC | What it is | How to recreate |
|---|---|---|
| `D:\...\game_files\` | the disc, extracted (SLPM_654.28, `iop/modules`, `ntgui2`, saves) | `docs/ops/build.md`: extract your own disc image; never in git |
| `C:\ob\up` | PS2Recomp git clone at `eb8688b` + patches 0001-0086 (`git am`) | `docs/ops/pins.md`; `tools/ob.sh` sets it up |
| `C:\ob\net-up` | git worktree of `C:\ob\up`, branch `net-online` = main's 0086 + 0087-0099 | `git worktree add C:\ob\net-up -b net-online 30bd436`, then `git am upstream/ps2recomp/0087-*.patch ... 0099-*.patch` |
| `C:\ob\net-src`, `net-r`, `net-work`, `net-ovl`, `net-t`, `net-logs`, `net-ioptests`, `net-ob-server`, `net-pcsx2-gs-build` | the online-play build trees (a copy of `net-up` + generated code, the runner, recompiler work dir, overlays, tools, logs) | `source tools/net/net-env.sh` then `tools/net/net-tools.sh` (tools + ps2x_tests), `tools/net/net-recompile.sh` (overlays incl. netwk/netaq/nethttp, ~20 min once), `tools/net/net-runner.sh` (runner only, seconds), `tools/net/dev9-tests.sh`, `tools/net/ob-server-tests.sh` |
| `C:\ob\pcsx2`, `pcsx2-deps` | PCSX2 v2.9.134 + its deps for the GS/DEV9 libraries | `tools/ob.sh pcsx2-gs` (clones and builds) |
| `C:\ob\mbedtls\v2.28.10` | mbedTLS for ob_server's old TLS | `tools/net/mbedtls-build.sh` (clones from GitHub, builds); pin in `docs/ops/pins.md` |
| `C:\ob\saves-net\Mcd001.ps2` | a formatted PCSX2 card with the game's NET save (made by the game itself: title → NETWORK PLAY → create) | `python -I tools/ob/mcd.py format C:\ob\saves-net\Mcd001.ps2`, then let the game create its NET file once (`tools/net/to-network.pad` route). `mcd.py netcnf` is optional: the client serves the settings (`PS2X_NETCNF=dhcp`) |
| `ob_server.exe` running on 127.0.0.2 | the login/lobby server for the drives | `C:\ob\net-ob-server\ob_server.exe --bind 127.0.0.2` (see `native/ob_server/README` / `tools/ob_server_main.cpp`) |

`tools/net/net-env.sh` holds the `OB_*` paths; edit it if the drives live elsewhere. The `up-*.sh` scripts commit,
amend, export (`format-patch`), sync (`net-up` → `net-src`, keeping the generated `register_functions.cpp`) and
prove "fails without" (`up-without.sh save|restore`) for PS2Recomp patches.

## Fresh machine: what to install, what to download, what to carry over

Install (vendor sites; versions in `pins.md`): Git for Windows (Git Bash), Visual Studio 2022 Community with the
C++ workload (MSVC 19.44), CMake 3.30 and Ninja (`winget install Kitware.CMake Ninja-build.Ninja`), Python 3.14
(`OB_PY` if not on PATH), and for Android only the NDK `27.0.12077973` + SDK through Android Studio. `tools/ob/vc.bat`
finds vcvars64; set `OB_VCVARS` if VS is elsewhere.

Download (all GitHub, public; the scripts pin the versions):

| What | Command |
|---|---|
| PS2Recomp (`C:\ob\up`, branch `outbreak-patches` = `eb8688b` + our patches) | `git clone https://github.com/ran-j/PS2Recomp C:\ob\up; git -C C:\ob\up checkout -b outbreak-patches eb8688b; git -C C:\ob\up am (Get-ChildItem upstream\ps2recomp\00*.patch)` — 0001-0086 for main's tree; the online branch `net-online` adds 0087-0099 in a worktree (`git -C C:\ob\up worktree add C:\ob\net-up -b net-online`) |
| PCSX2 v2.9.134 + its deps (zstd, shaderc, glslang, SPIRV, zlib, libpng; checksummed) | `bash tools/ob.sh pcsx2-gs` (clones, patches, builds) |
| mbedTLS 2.28.10 | `git clone --depth 1 --branch v2.28.10 https://github.com/Mbed-TLS/mbedtls.git C:\ob\mbedtls\v2.28.10`, then `tools/net/mbedtls-build.sh` |
| Outbreak decomp (names only, optional) | `git clone https://github.com/GalaxyNostalgia/Outbreak-decomp C:\ob\decomp-src` at `eb6af38` |
| PS2Recomp's own CMake dependencies (FetchContent) | downloaded by the first `tools/ob.sh tools` configure into `C:\ob\t\_deps`; `tools/net/net-tools.sh` reuses them when present and downloads otherwise |

Carry over by drive (not downloadable, not in git): the game data (`game_files/` extracted from your disc, or the
ISO under `ps2_emu/roms/`), the PCSX2 BIOS in `ps2_emu/` if you want the oracle, and `C:\ob\saves-net\Mcd001.ps2`
(or recreate it: `mcd.py format` + one run of the pad route). Everything under `C:\ob\net-*` is rebuilt from the
above; the first recompile is the one long build.

## Running the game to the network menu

`tools/net/net-live.sh 1500` boots hidden with `PS2X_DEV9=pcsx2`, the game's two host names → 127.0.0.2,
`PS2X_SAVES=C:\ob\saves-net`, `PS2X_NETCNF=dhcp`, live pad on `\\.\pipe\obnet`. Then
`python -I tools/ob/pad.py --pipe obnet --wait 150 --timeout 600 < tools/net/to-network.pad` drives title →
NETWORK PLAY → NET CONNECTION list; `press down 8 / wait 60` ×3 reaches "1. Setting 1", `press circle 8` is OK.
`peek <addr> <1|2|4>` reads EE RAM (state block pointer at 0x3065DC; see report entries 21-23 for the fields).
Logs: `C:\ob\net-logs\boot-net-live.{log,err}`; shots: `C:\ob\frames-net-live`.

## Where it stands (2026-10-27, entry 24)

OK on the setting loads the drivers and DHCP completes (192.0.2.100 from PCSX2's internal server); the game then
shows 通信エラー without sending any DNS, ARP or TCP. Open: what the AVE-TCP wrapper (sid 0x01270030) functions
30/74/56 do and why nothing is transmitted. The planned check: a PCSX2 reference run of the same screen (what the
real game sends right after the lease). Patch 0095 is correct but unused by this game.

## Preferences carried in memory on the old PC (not in git)

Test before implementation; commands in PowerShell syntax; web research on GitHub and well-known sites only, no
downloads; the engine is performance-only (diagnostics in ob_diag); speed runs set `PS2X_VU1=mvu`; the Odin 3 Max is
the test device, not the target; the gate's green runs use `PS2X_GS=pcsx2-sw`.
