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
- `tools/f1_buttons.py`: modern button layout while `run` is active (L2 aims, R2 fires, R1 runs; ✕ kicks while aiming and backs out of menus; ↓ + ✕ quick-turns and swings the camera behind her; restored on exit)
- `tools/test_buttons.py`: layout tests on a map laid out like the device's
- `tools/f1_crosshair.py`: `check` (read-only) and `show`; `run` draws a "+" while aiming using the
  game's own on-screen text lines (data only, removed on exit)
