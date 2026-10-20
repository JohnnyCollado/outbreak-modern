# Outbreak Modern Camera on ARMSX2 (Android)

Research project: run a modern over-the-shoulder camera for Resident Evil Outbreak
File #1 / #2 (JP) on ARMSX2 on the AYN Odin 3 Max.

- `docs/phase1-feasibility-report.md`: Phase 1 research and Phase 2 test plan
- `tools/phase2_check.py`: automated Phase 2 checks (PC → ADB → ARMSX2 PINE). Needs Python 3.8+ and adb
- `tools/pine_diag.py`: read-only PINE diagnostic client (`--bench N`, `--read ADDR`)

Quick start (PC with the Odin attached over USB, PINE enabled, game booted, save state made):

    python tools/phase2_check.py

Results land in `results/`.
- `tools/f1_live.py`: live read-only view of File #1's camera, players and pad, using the CC0
  decomp's addresses (to verify them on your disc)
- `tools/f1_analyze.py <log>`: angle conventions, input encoding and camera behaviour from an `f1_live` log
- `tools/f1_hookcheck.py`: read-only check that the camera hook functions sit at the decomp's addresses in the running build
- `tools/f1_camtest.py`: first write test. Installs a render-time camera override hook in File #1
  and drives an over-the-shoulder camera from the PC (`freemem` → `install` → `run` → `uninstall`; `reinstall` swaps in a newer hook).
  **Make a save state first**; loading it undoes everything.
- `tools/test_mips.py`: checks the hook's machine code against LLVM and simulates it
- `tools/f1_collide.py`: read-only access to the room's collision triangles (decomp layout), used to
  keep the camera inside the room; `python tools/f1_camtest.py walls` checks it on your disc
- `tools/test_collide.py`: geometry and synthetic-room tests for the camera collision
- `tools/f1_aimscan.py`: read-only guided capture to find where the aim elevation (up/level/down) is stored
- `tools/f1_freeaim.py`: finds the game's 3-position aim servo by its contents and switches it off so
  `f1_camtest.py run --free-aim` can set the gun's elevation from where the camera looks
- `tools/test_freeaim.py`: finder/patch tests on a synthetic stand-in (no game code)
- `tools/f1_controls.py`: read-only `buttons` (the game's button map) and `watch` (buttons pressed →
  game action bits), groundwork for the modern button layout
- `tools/f1_buttons.py`: modern button layout while `run` is active (L2 aims, R2 fires, R1 runs; ✕ kicks while aiming and backs out of menus; ↓ + ✕ quick-turns and swings the camera behind her; callouts only with L1 + right stick; restored on exit)
- `tools/test_buttons.py`: layout tests on a map laid out like the device's
- `tools/f1_crosshair.py`: `check` (read-only) and `show`; `run` draws a "+" while aiming using the
  game's own on-screen text lines (data only, removed on exit)

## Running on the device, no PC (Termux)

The tools only need Python's standard library, and ARMSX2's PINE listens on the device's own
loopback, so they run on the Android device itself. On Android they skip adb and connect
directly.

1. Install Termux from F-Droid (the Play Store build is outdated), then:
   `pkg install python git` and `termux-wake-lock` (keeps Android from pausing Termux).
2. Get this repository onto the device: `git clone` it (a private repository needs a GitHub
   token), or copy the folder over once and reach it after `termux-setup-storage`.
3. In ARMSX2: PINE on (Settings > Advanced), load File #1 and get into gameplay.
4. Android freezes ARMSX2 whenever it is off screen, so start each tool with a delay and switch
   back to ARMSX2 straight away:
   `sleep 5 && python tools/f1_camtest.py install --at 0x00084000` (once per game boot), then
   `sleep 5 && python tools/f1_camtest.py run --free-aim`.
5. To stop, hold **SELECT + R3** for a second in the game: the servo, buttons and crosshair are
   restored while ARMSX2 is still on screen. (Ctrl+C in Termux works too, but by then ARMSX2 is
   off screen and frozen, so the cleanup may not get through.)
