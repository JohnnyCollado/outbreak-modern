# Outbreak Modern Camera on ARMSX2 (Android)

A modern over-the-shoulder camera, free aim and modern controls for Resident Evil Outbreak
File #1 (SLPM-65428, the English fan patch included), running on ARMSX2 on Android (built and
tested on an AYN Odin 3). It is a patch file ARMSX2 loads by itself: no PC and no helper app
while you play. It holds only our own code and addresses; no game code is stored here.

## Install

1. Get `patches/SLPM-65428_32088394.pnach` onto the device and enable it:
   - from a PC with adb: `python tools/push_patch.py` (with the game closed). It finds ARMSX2's
     data folder, copies the patch into its `patches` folder and enables it for the game;
   - or by hand: put the file in ARMSX2's `patches` folder (or Patch Manager > Import) and enable
     **"Modern Camera (standalone)"** in Patch Manager.
2. Start File #1 from the game list (a fresh boot: patches are read when the game boots). In the
   first room, "Modern Camera: on" shows at the top left for a moment.

## Playing

| | |
|---|---|
| Right stick | turn the camera (it never moves by itself) |
| Left stick | walk, relative to the camera |
| L2 | aim (the gun follows the camera, up and down too) |
| R2 (or ○) | fire / interact |
| R1 | run |
| ✕ | Kevin's kick while aiming, back in menus; doesn't run |
| ↓ + ✕ | quick turn: she turns about, the camera swings in behind her |
| L1 + right stick | callouts |
| SELECT + R3 (hold 1 s) | switch the whole mod off and on (off gives the game's camera, aim and buttons back) |

The camera stays inside walls and under stairs; aiming shows a "＋" and the shot converges, from
her gun hand, on the enemy or wall under it.

**Debug** is a second entry in the patch file, off by default: switch it on in Patch Manager (and
restart the game) for tuning tools. Today that is the aim trim: while aiming, R3 + D-pad turns the
aim in 0.1 deg steps, shown on screen.

## If something is off

From a PC with adb (or Termux on the device), with the game running:

- `python tools/f1_patchcheck.py`: is the patch applied and running; camera, walls, aim, buttons, Debug
- `python tools/f1_aimlog.py 90`: logs aiming frame by frame; click L3 at a bad moment to mark it
  (and, from a PC, save a screenshot of the device). Send the log and the screenshots.

## How it is built

`mod/` (see `mod/README.md`): C and a small assembly entry for the PS2's EE, built with clang and
ld.lld into `patches/SLPM-65428_32088394.pnach` by `python mod/build.py`, with emulator tests
(`mod/test_mod.py`, needs `pip install unicorn`) and a patch-file interpreter test
(`mod/test_pnach.py`). The game's addresses come from the CC0
[Outbreak decomp](https://github.com/GalaxyNostalgia/Outbreak-decomp) and were checked on the
device with the tools below. `docs/phase1-feasibility-report.md` is the full research and
device-test record. `docs/android-port-map.md` maps what a native Android build of the game
(rather than this patch) would have to implement: rendering, sound, files, input, saves, timing.

## Development tools (PC → adb → ARMSX2 PINE)

PINE on in ARMSX2 (Settings > Advanced). Python 3.8+ (standard library only) and adb. Results land
in `results/`.

- Connection: `tools/phase2_check.py` (checks and diagnosis), `tools/pine_diag.py` (PINE client).
- Reading the game: `tools/f1_live.py` (camera, players, pad), `tools/f1_analyze.py` (its logs),
  `tools/f1_hookcheck.py` (hook functions where the decomp says), `tools/f1_controls.py` (button
  map, buttons -> game bits), `tools/f1_aimscan.py` (finding the aim fields).
- For the patch: `tools/f1_patchprobe.py` (the call-site addresses `mod/sites.json` holds),
  `tools/push_patch.py`, `tools/f1_patchcheck.py`, `tools/f1_aimlog.py`.
- The PC-driven version of the mod, for trying ideas live without a rebuild:
  `tools/f1_camtest.py` (`freemem` → `install` → `run` → `uninstall`; **make a save state first**)
  with `f1_collide.py`, `f1_freeaim.py`, `f1_buttons.py`, `f1_crosshair.py`, `mips.py`. Don't run it
  while the patch is enabled. Its tests: `tools/test_*.py`.

### The PC-driven version on the device (Termux)

The tools only need the standard library and PINE listens on the device's own loopback, so they
also run in Termux (from F-Droid: `pkg install python git`, `termux-wake-lock`). Android freezes
ARMSX2 when it is off screen, so start a tool with a delay and switch back at once, e.g.
`sleep 5 && python tools/f1_camtest.py run --free-aim`; hold SELECT + R3 for a second to stop it.
