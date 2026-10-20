# Standalone mod (no PC)

The PC tools in `tools/` drive the camera from outside the game over PINE. This folder is the
same mod as our own code running *inside* the game, delivered as a patch file ARMSX2 (or PCSX2)
loads by itself: `patches/SLPM-65428_32088394.pnach`.

- `collide.c`: segment vs the room's collision triangles, read in place (the game's grids).
- `stub.S`: the entry. A patched call site calls it; it saves every register (128-bit, the EE's),
  runs `mod_frame()`, and continues into the game's own `func_00345740`.
- `mod.c`: the mod, run every frame. `game.h`: the game addresses (from the decomp, confirmed on
  the device by the PC tools). `mod.h`: our memory and the status block.
- `build.py`: clang + ld.lld for the EE (MIPS II, single-precision FPU), a check that every
  instruction exists on the EE, and the patch file generator. `sites.json`: the call-site and
  anchor addresses, from `tools/f1_patchprobe.py`.
- `test_mod.py`: runs the built code in a MIPS emulator (needs `pip install unicorn`).
  `test_pnach.py`: runs the patch file the way ARMSX2 does, against a fake memory.

## Using it (ARMSX2 on Android)

1. Copy `patches/SLPM-65428_32088394.pnach` to the device: `python tools/push_patch.py` finds
   ARMSX2's data folder over adb (the app folder, or the one picked in ARMSX2's setup) and puts
   it in its `patches` folder. Or import it from ARMSX2's Patch Manager. Then enable "Modern
   Camera (standalone)" there.
2. Start File #1 (a fresh boot: patches are read when the game boots). When gameplay starts,
   "Modern Camera: on (SELECT + R3: off)" shows at the top left for a few seconds.
3. Optional: `python tools/f1_patchcheck.py` (PC or Termux) shows the code's status.

Don't run the PC mod (`f1_camtest.py run`) at the same time: both would drive the camera.
Not in yet: aiming (milestone 4), the button layout and quick turn (milestone 5).

## Milestones

1. Pipeline: our code runs every frame and reports. Done (device, 2026-10-20).
2. The over-the-shoulder camera on the right stick, modern walking (she faces down the stick,
   relative to the camera), callouts only with L1 + right stick, SELECT + R3 (hold 1 s) to switch
   it off and on. Done (device, 2026-10-20).
3. Collision: the camera stops short of walls (traced from her shoulder point, eased back out)
   and stays within 190 of the floor under it (stairs: no ceilings in the data). This version.
4. Aiming, servo, crosshair. 5. Button layout, quick turn.
