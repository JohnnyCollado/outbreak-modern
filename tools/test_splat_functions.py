#!/usr/bin/env python3
"""Tests for tools/splat_functions.py on a hand-written splat-style listing.
Run: python -I tools/test_splat_functions.py
"""
import os
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import splat_functions  # noqa: E402

FAILS = []

LISTING = """\
.section .text, "ax"

nonmatching func_00100000, 0x4

glabel func_00100000
    /* 200 00100000 0800E003 */  jr         $ra
endlabel func_00100000

nonmatching _start, 0x210

glabel _start
    /* 208 00100008 280C0070 */  padduw     $at, $zero, $zero
    /* 20C 0010000C 28140070 */  padduw     $v0, $zero, $zero
endlabel _start
"""


def expect(cond, what):
    print(("  ok   " if cond else "  FAIL ") + what)
    if not cond:
        FAILS.append(what)


def test_parse():
    funcs = list(splat_functions.functions_in(LISTING.splitlines()))
    expect(funcs == [("func_00100000", 0x100000, 4), ("_start", 0x100008, 0x210)],
           "names, starts from the first instruction's vram, sizes from the nonmatching line")


def test_dir_and_csv():
    with tempfile.TemporaryDirectory() as d:
        os.makedirs(os.path.join(d, "data"))
        with open(os.path.join(d, "text.s"), "w") as fh:
            fh.write(LISTING)
        with open(os.path.join(d, "data", "skip.s"), "w") as fh:
            fh.write(LISTING.replace("00100008", "00200008"))
        funcs = splat_functions.collect(d)
        expect([f[1] for f in funcs] == [0x100000, 0x100008], "data/ is skipped and results are sorted by address")
        out = os.path.join(d, "map.csv")
        splat_functions.write_csv(funcs, out)
        lines = open(out).read().splitlines()
        expect(lines[0] == "name,start,end,size", "header line PS2Recomp skips")
        expect(lines[2] == "_start,0x00100008,0x00100218,528", "end is exclusive: start + size")


if __name__ == "__main__":
    for t in (test_parse, test_dir_and_csv):
        print(t.__name__)
        t()
    print(f"\n{len(FAILS)} failed" if FAILS else "\nall passed")
    sys.exit(1 if FAILS else 0)
