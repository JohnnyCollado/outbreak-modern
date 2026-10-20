#!/usr/bin/env python3
"""Tests for tools/f1_freeaim.py. Run: python tools/test_freeaim.py

The servo stand-in is SYNTHETIC: built with tools/mips.py to have the shape find_servo() looks
for (a prologue after a `jr ra`, two 1170 step constants, a 2730 return constant, stores to
+0xBC8). It is not the game's code.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import f1_freeaim as F  # noqa: E402
import mips  # noqa: E402

FAILS = []


def expect(cond, what):
    print(("  ok   " if cond else "  FAIL ") + what)
    if not cond:
        FAILS.append(what)


def synthetic_servo():
    sh_elev = lambda rt: (0x29 << 26) | (mips.REG["s1"] << 21) | (mips.REG[rt] << 16) | F.AIM_ELEV  # noqa: E731
    body = [mips.addiu("sp", "sp", -64), mips.addiu("v1", "zero", 1)]
    body += [mips.NOP] * 20 + [sh_elev("zero"), sh_elev("v1"), sh_elev("v1"), sh_elev("v1")] + [mips.NOP] * 30
    body += [F.RATE_STEP, mips.NOP, mips.NOP, mips.NOP, mips.NOP, mips.NOP,
             F.RATE_STEP, mips.NOP, mips.NOP, mips.NOP, mips.NOP, mips.NOP, F.RATE_BACK]
    body += [mips.NOP] * 6 + [sh_elev("zero"), sh_elev("v1")] + [F.JR_RA, mips.addiu("sp", "sp", 64)]
    return [F.JR_RA, mips.addiu("sp", "sp", 32), mips.NOP] + body   # previous function's end first


class FakePine:
    def __init__(self, ram):
        self.ram = ram

    def read_block(self, addr, size):
        return bytes(self.ram[addr:addr + size])

    def write64(self, addr, value):
        assert addr % 8 == 0
        struct.pack_into("<Q", self.ram, addr, value)


def place(ram, at):
    words = synthetic_servo()
    struct.pack_into(f"<{len(words)}I", ram, at - 12, *words)    # function starts at `at`
    return words[3:5]


def main():
    print("finding:")
    for at, label in ((0x00556800, "inside a chunk"), (0x0059FF00, "across a chunk boundary"),
                      (0x00540010, "at the start of the scan range")):
        ram = bytearray(32 << 20)
        place(ram, at)
        got = F.find_servo(lambda a, n: bytes(ram[a:a + n]))
        expect(got == at, f"{label}: found 0x{(got or 0):08X}")
    empty = bytearray(32 << 20)
    expect(F.find_servo(lambda a, n: bytes(empty[a:a + n])) is None, "nothing found in empty memory")
    decoy = bytearray(32 << 20)
    struct.pack_into("<3I", decoy, 0x00600000, F.RATE_STEP, F.RATE_STEP, F.RATE_BACK)
    expect(F.find_servo(lambda a, n: bytes(decoy[a:a + n])) is None, "constants alone (no function around them) are ignored")

    print("patching:")
    ram = bytearray(32 << 20)
    orig = place(ram, 0x00556800)
    servo = F.Servo(FakePine(ram))
    msg = servo.disable()
    head = list(struct.unpack_from("<2I", ram, 0x00556800))
    expect(head == [F.JR_RA, mips.NOP], f"switched off: {msg}")
    expect(servo.orig == tuple(orig), "original words kept")
    expect("already off" in servo.disable(), "second disable is a no-op")
    struct.pack_into("<2I", ram, 0x00556800, *orig)                 # GAME.BIN reloaded
    expect(servo.ensure() and list(struct.unpack_from("<2I", ram, 0x00556800)) == [F.JR_RA, mips.NOP],
           "re-applied after a reload")
    expect("restored" in servo.restore() and list(struct.unpack_from("<2I", ram, 0x00556800)) == orig,
           "restore puts the original words back")
    other = bytearray(32 << 20)
    o2 = place(other, 0x00556804)                                    # not 8-aligned
    expect("refusing" in F.Servo(FakePine(other)).disable() and
           list(struct.unpack_from("<2I", other, 0x00556804)) == o2, "refuses a non 8-aligned site, writes nothing")

    print("\nALL PASS" if not FAILS else f"\n{len(FAILS)} FAILURE(S)")
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
