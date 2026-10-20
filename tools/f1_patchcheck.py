#!/usr/bin/env python3
"""READ-ONLY: is the standalone patch (mod/, patches/SLPM-65428_32088394.pnach) running?

Reads the status block our in-game code keeps at 0x000AFF00 (mod/mod.h) twice, a second apart:
it should count about one call per frame, from one of the patched call sites, and know where
our player is. Works from the PC (adb) or on the device (Termux).
"""
import json
import os
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import phase2_check  # noqa: E402

STATUS, MAGIC = 0x000AFF00, 0x434D4F31
SITES = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "mod", "sites.json")


def read(pine):
    return struct.unpack("<4I3fI", pine.read_block(STATUS, 32))


def read_v2(pine):
    """Milestone 2 fields: enabled, yaw, pitch, dt, eye[3], look[3], facing, callout dead zone."""
    return struct.unpack("<I3f3f3fII", pine.read_block(STATUS + 0x20, 0x30))


PNACH = os.path.join(os.path.dirname(SITES), "..", "patches", "SLPM-65428_32088394.pnach")


def check_patch(pine):
    """What the patch file says vs what is in memory: its D-code conditions, its writes."""
    conds, writes = [], []
    for line in open(PNACH):
        if line.startswith("patch=1,EE,"):
            _, _, addr, _, data = line.strip().split(",")
            a, d = int(addr, 16), int(data, 16)
            (conds if a >> 28 == 0xD else writes).append((a & 0x0FFFFFFF, d))
    seen = {}
    for a, d in conds:
        if a not in seen:
            seen[a] = (struct.unpack("<H", pine.read_block(a & ~3, 4)[a & 2:(a & 2) + 2])[0], d & 0xFFFF)
    anchor = conds[0][0]                      # the file's first condition is the anchor's low half
    anchor_ok = all(seen[a][0] == seen[a][1] for a in (anchor, anchor + 2))
    code = [(a, d) for a, d in writes if a < 0x00100000]
    hooks = [(a, d) for a, d in writes if a >= 0x00100000]
    bad = sum(1 for a, d in code if struct.unpack("<I", pine.read_block(a, 4))[0] != d) if code else 0
    print(f"patch file: {len(code)} code words, {len(hooks)} call sites")
    print(f"  anchor 0x{anchor:08X}: {'present (gameplay overlay loaded)' if anchor_ok else 'NOT present: are you in gameplay?'}")
    print(f"  our code at 0x{code[0][0]:08X}: " + ("all there" if bad == 0 else f"{bad} of {len(code)} words differ"))
    for a, d in hooks:
        w = struct.unpack("<I", pine.read_block(a, 4))[0]
        state = "calls us" if w == d else "still the game's call" if w == 0x0C000000 | (0x00345740 >> 2) else f"0x{w:08X}"
        print(f"  call site 0x{a:08X}: {state}")
    return anchor_ok, bad == 0 and bool(code)


def main():
    pine, _ = phase2_check.connect_game()
    try:
        patch = check_patch(pine) if os.path.exists(PNACH) else None
        a = read(pine)
        time.sleep(1.0)
        b = read(pine)
        v2 = read_v2(pine) if b[1] >= 2 else None
        pull = struct.unpack("<f", pine.read_block(STATUS + 0x50, 4))[0] if b[1] >= 3 else None
        aim = struct.unpack("<IIIf", pine.read_block(STATUS + 0x54, 16)) if b[1] >= 4 else None
        game_fov = struct.unpack("<f", pine.read_block(STATUS + 0x64, 4))[0] if b[1] >= 5 else None
        converge = struct.unpack("<f", pine.read_block(STATUS + 0x68, 4))[0] if b[1] >= 6 else None
        trim = struct.unpack("<2f", pine.read_block(STATUS + 0x6C, 8)) if b[1] >= 8 else None
        m5 = struct.unpack("<2I", pine.read_block(STATUS + 0x74, 8)) if b[1] >= 9 else None
    finally:
        pine.close()
    if b[0] != MAGIC:
        print(f"not running: no status at 0x{STATUS:08X} (magic 0x{b[0]:08X}).")
        if patch and patch[0] and not patch[1]:
            print("  The overlay is loaded but our code isn't in memory: ARMSX2 isn't applying the patch.\n"
                  "  Check it is enabled in Patch Manager for THIS game, then restart the game (patches are\n"
                  "  read when it boots).")
        elif patch and not patch[0]:
            print("  Get into gameplay (a room you can walk around in) and run this again.")
        return 2
    rate = (b[2] - a[2]) & 0xFFFFFFFF
    sites = []
    if os.path.exists(SITES):
        sites = [int(s, 16) for s in json.load(open(SITES))["sites"]]
    caller = b[3] - 8
    print(f"running: version {b[1]}, {rate} calls in the last second, started {b[7]} time(s)")
    print(f"  called from 0x{caller:08X}" + (" (a patched call site)" if caller in sites else
                                             " (not one of mod/sites.json's call sites!)" if sites else ""))
    print(f"  our player at ({b[4]:.1f}, {b[5]:.1f}, {b[6]:.1f})")
    if v2:
        import math
        on, yaw, pitch, dt = v2[0], v2[1], v2[2], v2[3]
        print(f"  camera: {'ON' if on else 'off (SELECT + R3 to switch on)'}, looking {math.degrees(yaw):.0f} deg, "
              f"tilt {math.degrees(pitch):.0f} deg, {dt * 1000:.1f} ms between calls")
        print(f"  eye ({v2[4]:.0f}, {v2[5]:.0f}, {v2[6]:.0f}) -> look ({v2[7]:.0f}, {v2[8]:.0f}, {v2[9]:.0f}); "
              f"walking facing {'active' if v2[10] else 'idle'}; right-stick dead zone "
              + (f"{v2[11]} (callouts on L1)" if v2[11] != 0xFFFF else "not found (callouts not gated)"))
    if pull is not None:
        print(f"  walls: camera at {pull * 100:.0f}% of its distance" + (" (pulled in by a wall)" if pull < 0.99 else ""))
    if m5 is not None:
        print("  buttons: " + ("modern layout (L2 aim, R2 fire, R1 run, cross: kick / menu back)" if m5[0] else
                               "the game's own (no controller yet, or its preset didn't look like a button map)")
              + ("; quick turn running" if m5[1] else ""))
    if game_fov is not None:
        print(f"  FOV: {game_fov:g} deg (the game's, left alone)")
    if aim is not None:
        import math
        servo = aim[2] & ~3
        print(f"  aim servo: " + (f"0x{servo:08X}, " + ("switched off (free aim ready)" if aim[2] & 1 else "found, not off")
                                  if servo else "not found yet (it scans GAME.BIN for about a second after gameplay starts)"))
        if aim[0]:
            print(f"  aiming: " + (f"free aim, gun at {math.degrees(aim[3]):+.1f} deg" if aim[1] else "the game's own aim (no servo)")
                  + (f", converging {converge:.0f} out" if aim[1] and converge else "")
                  + (f", aim trim up {trim[0]:+.1f} right {trim[1]:+.1f}" if aim[1] and trim else ""))
    return 0 if rate > 0 else 2


if __name__ == "__main__":
    sys.exit(main())
