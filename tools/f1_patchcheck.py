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


def main():
    pine, _ = phase2_check.connect_game()
    try:
        a = read(pine)
        time.sleep(1.0)
        b = read(pine)
    finally:
        pine.close()
    if b[0] != MAGIC:
        print(f"not running: no status at 0x{STATUS:08X} (magic 0x{b[0]:08X}). Is the patch enabled in "
              "ARMSX2 (Patch Manager), and are you in gameplay?")
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
    return 0 if rate > 0 else 2


if __name__ == "__main__":
    sys.exit(main())
