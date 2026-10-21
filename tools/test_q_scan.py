#!/usr/bin/env python3
"""Tests for tools/q_scan.py on hand-built instruction sequences. Run: python -I tools/test_q_scan.py

Each case is a short list of R5900 words with a known answer: a VWAITQ first (safe), a Q read
first (early read), both sides of a branch, a read in a delay slot, a call, and the window limit.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import q_scan  # noqa: E402

FAILS = []

VDIV = 0x4BE003BC      # vdiv $Q, $vf0w, $vf0w
VRSQRT = 0x4BE003BE    # vrsqrt $Q, $vf0w, $vf0w
VWAITQ = 0x4A0003BF    # vwaitq
VMULQ = 0x4BE0191C     # vmulq.xyzw $vf4, $vf3, $Q
CFC2_Q = 0x4842B000    # cfc2 $v0, $vi22 (Q)
CFC2_OTHER = 0x4842A800  # cfc2 $v0, $vi21 (I), not Q
NOP = 0x00000000
JR_RA = 0x03E00008
JAL = 0x0C040000       # jal 0x100000


def beq(a0_zero_offset_words):
    """beq $a0, $zero, +offset: a conditional branch."""
    return 0x10800000 | (a0_zero_offset_words & 0xFFFF)


def expect(cond, what):
    print(("  ok   " if cond else "  FAIL ") + what)
    if not cond:
        FAILS.append(what)


def verdicts(words, window=32):
    findings = q_scan.scan(("test", 0x100000, words), window)
    return [(f["op"], f["verdict"], f["first_read"]) for f in findings]


def test_decoding():
    for word, name in [(VDIV, "vdiv"), (VRSQRT, "vrsqrt"), (VWAITQ, "vwaitq"), (VMULQ, "vmulq")]:
        ins = q_scan.decode(word, 0x100000)
        expect(ins is not None and q_scan.mnemonic(ins) == name, f"{word:#010x} decodes as {name}")
    expect(q_scan.reads_q(q_scan.decode(CFC2_Q, 0)), "cfc2 from $vi22 reads Q")
    expect(not q_scan.reads_q(q_scan.decode(CFC2_OTHER, 0)), "cfc2 from $vi21 does not read Q")


def test_straight_line():
    expect(verdicts([VDIV, NOP, VWAITQ, VMULQ, JR_RA, NOP]) == [("vdiv", "safe", None)],
           "vwaitq before the read is safe")
    expect(verdicts([VDIV, NOP, NOP, VMULQ, VWAITQ, JR_RA, NOP]) == [("vdiv", "EARLY READ", 3)],
           "a read before vwaitq is an early read, at distance 3")
    expect(verdicts([VRSQRT, CFC2_Q, JR_RA, NOP]) == [("vrsqrt", "EARLY READ", 1)],
           "cfc2 of Q right after vrsqrt is an early read at distance 1")
    expect(verdicts([VDIV, NOP, VDIV, VWAITQ, VMULQ, JR_RA, NOP]) == [("vdiv", "safe", None), ("vdiv", "safe", None)],
           "a second producer before any read replaces the first")


def test_control_flow():
    # vdiv; beq -> +3 (to VMULQ); nop (slot); vwaitq; jr; nop; [target] vmulq
    words = [VDIV, beq(4), NOP, VWAITQ, JR_RA, NOP, VMULQ, JR_RA, NOP]
    v = verdicts(words)
    expect(v and v[0][1] == "EARLY READ", "a read on the taken side of a branch is found")
    words = [VDIV, beq(2), VMULQ, VWAITQ, JR_RA, NOP]
    v = verdicts(words)
    expect(v and v[0][1] == "EARLY READ" and v[0][2] == 2, "a read in a branch delay slot is found, at distance 2")
    expect(verdicts([VDIV, JAL, NOP, VMULQ])[0][1] == "unresolved", "a call before any wait or read is unresolved")
    expect(verdicts([VDIV, JR_RA, NOP])[0][1] == "unresolved", "a return before any wait or read is unresolved")


def test_window():
    words = [VDIV] + [NOP] * 40 + [VMULQ]
    expect(verdicts(words, window=32)[0][1] == "unresolved", "a read past the window is not called early")
    expect(verdicts(words, window=64)[0][1] == "EARLY READ", "with a larger window the same read is found")


if __name__ == "__main__":
    for t in (test_decoding, test_straight_line, test_control_flow, test_window):
        print(t.__name__)
        t()
    print(f"\n{len(FAILS)} failed" if FAILS else "\nall passed")
    sys.exit(1 if FAILS else 0)
