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
    struct.pack_into("<32I", ram, B.PRESETS[1], *[w for e in DEVICE for w in e])   # the analog preset
    ram[B.PAD_KIND] = 2
    m = B.ButtonMap(FakePine(ram))
    expect(game_bits(ram, B.L2) == 0x80 and game_bits(ram, B.R2) == 0x40, "before: L2 -> 0x80, R2 -> aim 0x40")
    print("  " + m.apply())
    expect(game_bits(ram, B.L2) == 0x40, "L2 now aims (0x40)")
    expect(game_bits(ram, B.R2) == 0x20, "R2 now fires (0x20)")
    expect(game_bits(ram, B.CIRCLE) == 0x20, "circle still fires/interacts (0x20)")
    expect(game_bits(ram, B.R1) == 0x10 and game_bits(ram, B.CROSS) == 0x10,
           "standing, not aiming: R1 runs, cross gives 0x10 (menus: back)")
    expect(m.set_context(False, True) and game_bits(ram, B.R1) == 0x10 and game_bits(ram, B.CROSS) == 0,
           "moving, not aiming: R1 runs, cross gives nothing (no cross-run)")
    expect(m.set_context(True, True) and game_bits(ram, B.CROSS) == 0x10 and game_bits(ram, B.R1) == 0,
           "aiming: cross gives 0x10 (Kevin's kick with an aim bit held)")
    expect(not m.set_context(True, False), "aiming, moving or not: same entry, no rewrite")
    expect(m.set_context(False, False) and game_bits(ram, B.CROSS) == 0x10, "back to standing: cross backs out again")
    expect(not any(game_bits(ram, 1 << b) & 0x400 for b in range(16)), "nothing triggers lock-on aim (0x400)")
    expect(game_bits(ram, 0x1000) == 0x1 and game_bits(ram, 0x0004) == 0x800, "D-pad and L1 (item) unchanged")
    expect(not any(game_bits(ram, 1 << b) & 0x80 for b in range(16)), "nothing triggers L2's old action (0x80)")
    struct.pack_into("<32I", ram, B.PAD_MAP, *[w for e in DEVICE for w in e])     # controller reconnected
    expect(m.ensure() and game_bits(ram, B.L2) == 0x40, "re-applied after the game reset the map")
    expect(not m.ensure(), "no rewrite when already applied")
    print("  " + m.restore())
    expect([struct.unpack_from("<II", ram, B.PAD_MAP + 8 * i) for i in range(16)] == DEVICE, "restore = the device map")
    # An earlier run left the map changed: the original must still be the game's preset.
    print("  " + m.apply())
    left = B.ButtonMap(FakePine(ram))
    print("  " + left.apply())
    print("  " + left.restore())
    expect([struct.unpack_from("<II", ram, B.PAD_MAP + 8 * i) for i in range(16)] == DEVICE,
           "a map left changed by an earlier run is restored to the game's preset")
    junk = bytearray(ram)
    struct.pack_into("<I", junk, B.PRESETS[1], 0x3)                     # not a single-button mask
    before = [struct.unpack_from("<II", junk, B.PAD_MAP + 8 * i) for i in range(16)]
    fallback = B.ButtonMap(FakePine(junk))
    expect(fallback._preset() is None, "a preset that doesn't look like a button map is rejected")
    fallback.apply()
    expect(fallback.orig == before, "...and the current map is kept as the original instead")
    legacy = bytearray(ram)
    lm = B.ButtonMap(FakePine(legacy), cross_runs=True)
    lm.apply()
    lm.set_context(False, True)
    expect(game_bits(legacy, B.CROSS) == 0x10 and game_bits(legacy, B.R1) == 0x10, "--cross-runs: cross runs as originally")
    empty = B.ButtonMap(FakePine(bytearray(32 << 20)))
    expect("not changed" in empty.apply() and empty.orig is None, "an empty map is left alone")
    expect(not B.plausible(0xFFFF) and not B.plausible(0xFFF7), "glitched pad reads (device: 0xFFFF, 0xFFF7) are rejected")
    expect(all(B.plausible(r) for r in (0, B.CROSS | B.DPAD_DOWN, 0x1000 | 0x2000, B.L2 | B.R2 | B.R1)),
           "...real button states are kept")
    qt = B.quick_turn
    expect(qt(B.CROSS, 0, 0.0, 0.9) == "stick", "quick turn: cross pressed with the stick pulled down")
    expect(qt(B.CROSS, B.CROSS, 0.0, 0.9) is None, "...only on the press, not while cross is held")
    expect(qt(B.CROSS | B.DPAD_DOWN, 0, 0.0, 0.0) == "dpad", "...or with D-pad down")
    expect(qt(B.CROSS, 0, 0.8, 0.6) is None and qt(B.CROSS, 0, 0.0, -0.9) is None and qt(B.CROSS, 0, 0.0, 0.6) is None,
           "...not sideways, up, or only half pushed")
    expect(qt(B.CROSS, 0, 0.45, 0.85) == "stick" and qt(B.CROSS, 0, 0.6, 0.8) is None, "...within about 30 degrees of down")
    expect(qt(B.DPAD_DOWN, 0, 0.0, 0.9) is None, "...not without cross")
    print("\nALL PASS" if not FAILS else f"\n{len(FAILS)} FAILURE(S)")
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
