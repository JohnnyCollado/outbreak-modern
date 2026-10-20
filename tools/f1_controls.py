#!/usr/bin/env python3
"""READ-ONLY: how File #1 turns DualShock 2 buttons into actions, as groundwork for a modern
button layout (L2 aim, R2 fire, ...) that is active only while the camera mod runs.

  buttons   print the game's button map (PadMap D_00292C80[16], include/pad.h: "button map
            in use"; each entry's first word is a libpad2 button mask)
  watch     live: the raw buttons held (PadData at 0x292D40 +2) and the game bits they give in
            our player's Ctrl (+0xF78 +8), printed on every change. Press each button once.

Game bits known from the decomp (charmove.c): 1 forward, 2 back, 4/8 turn, 0x10 run,
0x40 aim at nothing, 0x400 aim at the nearest target.
"""
import os
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import phase2_check  # noqa: E402
import pine_diag  # noqa: E402
from f1_live import PAD_NOW, our_player  # noqa: E402

PAD_MAP = 0x00292C80
CTRL_CUR = 0xF78 + 0x08
# DualShock 2 button bits as the report carries them (bytes 2-3 as a little-endian u16, after the
# game flips them to active-high). Standard PS2 order; check against `watch`.
DS2 = ["SELECT", "L3", "R3", "START", "UP", "RIGHT", "DOWN", "LEFT",
       "L2", "R2", "L1", "R1", "TRIANGLE", "CIRCLE", "CROSS", "SQUARE"]
GAME_BITS = {0x1: "forward", 0x2: "back", 0x4: "turn", 0x8: "turn", 0x10: "run", 0x40: "aim (no target)",
             0x400: "aim (nearest target)"}


def names(mask, table):
    return "+".join(table[i] for i in range(16) if mask >> i & 1) or "-"


def connect():
    ok, msg = phase2_check.ensure_forward(phase2_check.shutil.which("adb") or "adb", 28011)
    print(("adb: " if ok else "adb problem: ") + msg)
    pine = pine_diag.Pine("127.0.0.1", 28011, 5.0)
    info = pine_diag.identify(pine)
    print(f"{info['id']} CRC {info['crc']} disc version {info['game_version']}")
    return pine


def cmd_buttons():
    pine = connect()
    try:
        raw = pine.read_block(PAD_MAP, 16 * 8)
    finally:
        pine.close()
    print("PadMap at 0x292C80 (entry: mask word, second word):")
    for i in range(16):
        mask, extra = struct.unpack_from("<II", raw, 8 * i)
        print(f"  [{i:2}] mask 0x{mask:08X} ({names(mask & 0xFFFF, DS2):>20})   second 0x{extra:08X}")
    return 0


def cmd_watch(seconds):
    pine = connect()
    try:
        idx, base = our_player(pine)
        print(f"watching our player #{idx} for {seconds:g} s: press each button on its own, one at a time")
        t0, last = time.perf_counter(), None
        while time.perf_counter() - t0 < seconds:
            pad, cur = pine.read_multi([(PAD_NOW + 2, 2), (base + CTRL_CUR, 4)])
            raw = struct.unpack("<H", pad)[0]
            game = struct.unpack("<I", cur)[0]
            if (raw, game) != last:
                gb = "+".join(n for b, n in GAME_BITS.items() if game & b) or "-"
                print(f"  {1000 * (time.perf_counter() - t0):7.0f} ms  raw 0x{raw:04X} {names(raw, DS2):>22}"
                      f"   game bits 0x{game:08X} ({gb})", flush=True)
                last = (raw, game)
    finally:
        pine.close()
    return 0


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "buttons":
        sys.exit(cmd_buttons())
    if len(sys.argv) > 1 and sys.argv[1] == "watch":
        sys.exit(cmd_watch(float(sys.argv[2]) if len(sys.argv) > 2 else 30))
    print(__doc__)
