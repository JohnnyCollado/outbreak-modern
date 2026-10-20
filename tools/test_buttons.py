#!/usr/bin/env python3
"""Tests for tools/f1_buttons.py on a fake map laid out like the device dump. Run: python tools/test_buttons.py"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import f1_buttons as B  # noqa: E402

FAILS = []
DEVICE = [(0x1000, 1), (0x4000, 1), (0x8000, 1), (0x2000, 1), (0x0040, 2), (0x0020, 2), (0x0002, 3), (0x0001, 3),
          (0x0080, 2), (0x0010, 2), (0x0008, 3), (0x0004, 3), (0x0200, 0), (0x0400, 0), (0x0100, 0), (0x0800, 0)]


def expect(cond, what):
    print(("  ok   " if cond else "  FAIL ") + what)
    if not cond:
        FAILS.append(what)


class FakePine:
    def __init__(self, ram):
        self.ram = ram

    def read_block(self, addr, size):
        return bytes(self.ram[addr:addr + size])

    def write_multi(self, writes):
        for addr, words in writes:
            struct.pack_into(f"<{len(words)}I", self.ram, addr, *words)


def game_bits(ram, raw):
    """What padread.c:377 does: entry i sets bit i when raw & mask."""
    out = 0
    for i in range(16):
        mask = struct.unpack_from("<I", ram, B.PAD_MAP + 8 * i)[0]
        if raw & mask:
            out |= 1 << i
    return out


def main():
    ram = bytearray(32 << 20)
    struct.pack_into("<32I", ram, B.PAD_MAP, *[w for e in DEVICE for w in e])
    m = B.ButtonMap(FakePine(ram))
    expect(game_bits(ram, B.L2) == 0x80 and game_bits(ram, B.R2) == 0x40, "before: L2 -> 0x80, R2 -> aim 0x40")
    print("  " + m.apply())
    expect(game_bits(ram, B.L2) == 0x40, "L2 now aims (0x40)")
    expect(game_bits(ram, B.R2) == 0x20, "R2 now fires (0x20)")
    expect(game_bits(ram, B.CIRCLE) == 0x20, "circle still fires/interacts (0x20)")
    expect(game_bits(ram, 0x0008) == 0x400 and game_bits(ram, 0x0040) == 0x10 and game_bits(ram, 0x1000) == 0x1,
           "R1 lock-on, cross run and the D-pad unchanged")
    expect(not any(game_bits(ram, 1 << b) & 0x80 for b in range(16)), "nothing triggers L2's old action (0x80)")
    struct.pack_into("<32I", ram, B.PAD_MAP, *[w for e in DEVICE for w in e])     # controller reconnected
    expect(m.ensure() and game_bits(ram, B.L2) == 0x40, "re-applied after the game reset the map")
    expect(not m.ensure(), "no rewrite when already applied")
    print("  " + m.restore())
    expect([struct.unpack_from("<II", ram, B.PAD_MAP + 8 * i) for i in range(16)] == DEVICE, "restore = the device map")
    empty = B.ButtonMap(FakePine(bytearray(32 << 20)))
    expect("not changed" in empty.apply() and empty.orig is None, "an empty map is left alone")
    print("\nALL PASS" if not FAILS else f"\n{len(FAILS)} FAILURE(S)")
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
