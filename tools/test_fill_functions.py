#!/usr/bin/env python3
"""Tests for tools/fill_functions.py on a hand-made MWo3 image (no game data).
Run: python -I tools/test_fill_functions.py
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fill_functions  # noqa: E402

FAILS = []
LOAD = 0x6C0000
BASE = LOAD + 0x40  # text follows the 0x40-byte header

JR_RA, NOP = 0x03E00008, 0


def check(cond, msg):
    if not cond:
        FAILS.append(msg)


def jal(target):
    return (3 << 26) | ((target >> 2) & 0x3FFFFFF)


def addiu_sp(n):
    return 0x27BD0000 | (n & 0xFFFF)


def make_image(words):
    text = struct.pack("<%dI" % len(words), *words)
    end = LOAD + 0x40 + len(text)
    header = struct.pack("<4sIIIIIII", b"MWo3", 5, LOAD, len(text), 0, 0, end, end)
    header += b"netdnas.bin".ljust(0x20, b"\0")
    return header + text


# 0x00 splat's function (2 words); 0x08 a function reached only by JAL from 0x18;
# 0x10 a prologue right after 0x08's jr $ra + delay slot (no caller); 0x18 calls 0x08;
# 0x24 a prologue that does NOT follow a return (inside 0x10's body) must not start a function.
WORDS = [
    JR_RA, NOP,                                  # 0x00 splat
    JR_RA, NOP,                                  # 0x08 JAL target
    addiu_sp(-16), NOP,                          # 0x10 prologue after return
    jal(BASE + 0x08), NOP,                       # 0x18
    NOP, addiu_sp(-32), JR_RA, NOP,              # 0x20.. (0x24: prologue mid-body)
]


def test_fill():
    splat = [("func_splat", BASE, BASE + 8)]
    funcs = fill_functions.fill(make_image(WORDS), splat)
    starts = [s for _, s, _ in funcs]
    check(starts == [BASE, BASE + 0x08, BASE + 0x10], "starts %s" % [hex(s) for s in starts])
    check(funcs[0] == ("func_splat", BASE, BASE + 8), "splat entry kept as is: %s" % (funcs[0],))
    check(funcs[1][2] == BASE + 0x10, "JAL target ends at the next start")
    check(funcs[2][2] == BASE + len(WORDS) * 4, "last function ends at the end of the text")
    check(funcs[1][0] == "func_%08X" % (BASE + 8), "name from the address")


def test_inside_splat_ignored():
    # A JAL into the middle of splat's function adds nothing.
    words = [NOP, JR_RA, NOP, jal(BASE + 4), NOP]
    funcs = fill_functions.fill(make_image(words), [("f", BASE, BASE + 12)])
    check([s for _, s, _ in funcs] == [BASE], "JAL into splat's function: %s" % [hex(s) for _, s, _ in funcs])


def main():
    tests = [test_fill, test_inside_splat_ignored]
    passed = 0
    for t in tests:
        before = len(FAILS)
        t()
        passed += len(FAILS) == before
    for f in FAILS:
        print("FAIL", f)
    print("fill_functions: %d/%d" % (passed, len(tests)))
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
