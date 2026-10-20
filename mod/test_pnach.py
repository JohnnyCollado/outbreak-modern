#!/usr/bin/env python3
"""The patch file, run the way ARMSX2 runs it (pcsx2/Patch.cpp handle_extended_t: a D-code that
fails sets skip_count, and each later extended line uses one up instead of running), against a
fake memory, every "frame". Run: python mod/test_pnach.py"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import build  # noqa: E402

FAILS = []
ANCHOR, SITES = 0x00345750, [0x00341234, 0x00349990]


def expect(cond, what):
    print(("  ok   " if cond else "  FAIL ") + what)
    if not cond:
        FAILS.append(what)


def run(text, ram):
    """One frame of patch=1 lines. Returns how many words changed."""
    skip, changed = 0, 0
    for line in text.splitlines():
        if not line.startswith("patch=1,EE,"):
            continue
        _, _, addr, kind, data = line.split(",")
        assert kind == "extended"
        a, d = int(addr, 16), int(data, 16)
        if skip:
            skip -= 1
            continue
        if a >> 28 == 0xD:
            assert (d >> 16) & 0xFF == 0, "only 'equal, 16-bit' D-codes are used"
            if struct.unpack_from("<H", ram, a & 0x0FFFFFFF)[0] != d & 0xFFFF:
                skip = (d >> 24) or 1
        elif a >> 28 == 0x2:
            if struct.unpack_from("<I", ram, a & 0x0FFFFFFF)[0] != d:
                struct.pack_into("<I", ram, a & 0x0FFFFFFF, d)
                changed += 1
        else:
            raise AssertionError(line)
    return changed


def main():
    words = [0x10000000 + i for i in range(700)]          # 3 chunks
    entry = build.MOD_BASE
    text = build.pnach(words, entry, ANCHOR, SITES)
    expect(all(len(l.split(",")[2]) == 8 and len(l.split(",")[4]) == 8 for l in text.splitlines() if l.startswith("patch=")),
           "every line has 8-digit address and value fields")
    ram = bytearray(4 << 20)
    expect(run(text, ram) == 0, "gameplay overlay not loaded: nothing is written")
    for s in SITES:
        struct.pack_into("<I", ram, s, build.jal(build.HOOKED))
    expect(run(text, ram) == 0, "...even with the call sites' words there but no anchor")
    struct.pack_into("<I", ram, ANCHOR, build.jal(build.ANCHOR_CALL))
    n = run(text, ram)
    image = struct.unpack_from(f"<{len(words)}I", ram, build.MOD_BASE)
    expect(n == len(words) + len(SITES), f"overlay loaded: code and call sites written ({n} words)")
    expect(list(image) == words, "...the code is all there, in order, across chunks")
    expect(all(struct.unpack_from("<I", ram, s)[0] == build.jal(entry) for s in SITES), "...each call site now calls us")
    expect(run(text, ram) == 0, "next frame: nothing left to write")
    struct.pack_into("<I", ram, SITES[0], build.jal(build.HOOKED))     # overlay reloaded
    struct.pack_into("<I", ram, build.MOD_BASE + 8, 0)
    expect(run(text, ram) == 2, "overlay reloaded: the call site and the damaged word are put back")
    struct.pack_into("<I", ram, SITES[1], 0x0C001234)                   # something else there
    run(text, ram)
    expect(struct.unpack_from("<I", ram, SITES[1])[0] == 0x0C001234, "a call site holding anything else is left alone")
    S = lambda funct: 0x46000000 | (0x10 << 21) | (2 << 16) | (4 << 11) | (6 << 6) | funct   # fmt S
    fixed, notes = build.ee_fixups([S(0x0D), S(0x35), S(0x3E), S(0x02)], build.MOD_BASE)
    expect(fixed == [S(0x24), S(0x34), S(0x36), S(0x02)] and len(notes) == 3,
           "EE: trunc.w.s -> cvt.w.s, unordered/signalling compares -> the ordered ones, others kept")
    for bad, what in ((S(0x21), "cvt.d.s"), (0x46200000, "add.d"), (0xD4000000, "ldc1"), (0xC0000000, "ll")):
        try:
            build.ee_fixups([bad], build.MOD_BASE)
            expect(False, f"EE: {what} refused")
        except SystemExit:
            expect(True, f"EE: {what} refused")
    print("\nALL PASS" if not FAILS else f"\n{len(FAILS)} FAILURE(S)")
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
