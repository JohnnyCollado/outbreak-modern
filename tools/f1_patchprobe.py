#!/usr/bin/env python3
"""READ-ONLY: the addresses the standalone patch (mod/) needs from the running build.

The patch calls our code from the places that call func_00345740 ("hands the camera to the
renderer", the same moment the PC hook uses), by turning their `jal 0x00345740` into a jal to our
code, which then continues into 0x00345740. A patch file has no search, so it needs those call
sites' addresses, and an anchor that says SUBMAIN (the gameplay overlay) is loaded: the
`jal 0x0018F0E0` inside func_00345740. Both are found here by their encoding, computed from the
target address (0x0C000000 | target >> 2): no game code is read into or stored by this tool.

Run during gameplay (SUBMAIN must be loaded). Paste the output to Claude.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import phase2_check  # noqa: E402
from f1_hookcheck import jal  # noqa: E402

SUBMAIN_TEXT = (0x00340040, 0x00340040 + 0xF3C0)   # decomp config/SLPM_654.28/submain.yaml
HOOKED = 0x00345740
ANCHOR_FUNC, ANCHOR_SIZE, ANCHOR_CALL = 0x00345740, 0x130, 0x0018F0E0
CODE_AT, CODE_SIZE = 0x000A0000, 0x10000          # where mod/ puts its code and state


def scan(pine, lo, hi, word):
    hits = []
    for addr in range(lo, hi, 0x1000):
        n = min(0x1000, hi - addr)
        data = pine.read_block(addr, n)
        hits += [addr + i for i in range(0, n, 4) if struct.unpack_from("<I", data, i)[0] == word]
    return hits


def main():
    pine, info = phase2_check.connect_game()
    try:
        sites = scan(pine, *SUBMAIN_TEXT, jal(HOOKED))
        anchor = scan(pine, ANCHOR_FUNC, ANCHOR_FUNC + ANCHOR_SIZE, jal(ANCHOR_CALL))
        region = pine.read_block(CODE_AT, CODE_SIZE)
        used = sum(1 for i in range(0, CODE_SIZE, 4) if region[i:i + 4] != b"\0\0\0\0")
    finally:
        pine.close()
    print(f"calls to 0x{HOOKED:08X} in SUBMAIN: " + (" ".join(f"0x{a:08X}" for a in sites) or "NONE"))
    print(f"anchor (jal 0x{ANCHOR_CALL:08X} in 0x{ANCHOR_FUNC:08X}): " + (" ".join(f"0x{a:08X}" for a in anchor) or "NONE"))
    print(f"code region 0x{CODE_AT:08X}-0x{CODE_AT + CODE_SIZE:08X}: {used} non-zero words "
          f"({'free' if used == 0 else 'IN USE (the PC hook? run `f1_camtest.py uninstall`, or pick another)'})")
    ok = sites and len(anchor) == 1
    print("\nOK: paste this to Claude." if ok else "\nNot found: in gameplay? Paste this to Claude anyway.")
    return 0 if ok else 2


if __name__ == "__main__":
    sys.exit(main())
