#!/usr/bin/env python3
"""Tests for tools/mips.py and the f1_camtest cave. Run: python tools/test_mips.py

1. Every encoder output is disassembled by LLVM (llvm-mc) and compared with the intended
   assembly. Skipped if llvm-mc is not installed.
2. The generated cave runs on a tiny interpreter for exactly the instructions it uses, and
   its effect on memory and control flow is checked for every flag combination.
"""
import os
import shutil
import struct
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import f1_camtest as C  # noqa: E402
import mips  # noqa: E402

FAILS = []


def expect(cond, what):
    print(("  ok   " if cond else "  FAIL ") + what)
    if not cond:
        FAILS.append(what)


def llvm_disasm(word):
    mc = shutil.which("llvm-mc")
    b = struct.pack("<I", word)
    out = subprocess.run([mc, "--disassemble", "-triple=mipsel", "-mcpu=mips3"],
                         input=" ".join(f"0x{x:02x}" for x in b), capture_output=True, text=True).stdout
    return " ".join(out.split("\n")[-2].split()) if out.strip() else "?"


def test_encodings():
    print("encodings vs llvm-mc:")
    if not shutil.which("llvm-mc"):
        print("  skipped (no llvm-mc)")
        return
    cases = [
        (mips.lui("t0", 0x0009), "lui $8, 9"),
        (mips.addiu("t0", "t0", -0x100), "addiu $8, $8, -256"),
        (mips.andi("t3", "t1", 2), "andi $11, $9, 2"),
        (mips.lw("t1", 0x20, "t0"), "lw $9, 32($8)"),
        (mips.sw("t3", 0x60, "t2"), "sw $11, 96($10)"),
        (mips.beq("t3", "zero", 0x1000, 0x1010), "beqz $11, 16"),
        (mips.j(0x00090040, 0x00345740), "j 589888"),
        (mips.NOP, "nop"),
    ]
    for word, want in cases:
        got = llvm_disasm(word)
        expect(got == want, f"0x{word:08X}: {got!r} == {want!r}")


def run_cave(code, base, mem, prologue):
    """Interpret the cave until it jumps out. Returns (exit target, every word executed, in order).

    Instructions outside the cave's own vocabulary (the displaced prologue) are not interpreted,
    only recorded: they are the game's, and what they do is the game's business."""
    reg = [0] * 32
    pc, steps, trace, delay_target = base, 0, [], None
    code_words = {base + 4 * i: w for i, w in enumerate(code)}
    while steps < 200:
        w = code_words[pc]
        op, rs, rt, imm = w >> 26, (w >> 21) & 31, (w >> 16) & 31, w & 0xFFFF
        simm = imm - 0x10000 if imm & 0x8000 else imm
        nxt, target = pc + 4, None
        trace.append(w)
        if w in prologue:
            pass
        elif w == 0:
            pass
        elif op == 0x0F:
            reg[rt] = imm << 16
        elif op == 0x09:
            reg[rt] = (reg[rs] + simm) & 0xFFFFFFFF
        elif op == 0x0C:
            reg[rt] = reg[rs] & imm
        elif op == 0x23:
            reg[rt] = mem.get((reg[rs] + simm) & 0xFFFFFFFF, 0)
        elif op == 0x2B:
            mem[(reg[rs] + simm) & 0xFFFFFFFF] = reg[rt]
        elif op == 0x04:
            if reg[rs] == reg[rt]:
                target = pc + 4 + (simm << 2)
        elif op == 0x02:
            target = ((pc + 4) & 0xF0000000) | ((w & 0x03FFFFFF) << 2)
        else:
            raise AssertionError(f"unexpected instruction 0x{w:08X}")
        reg[0] = 0
        if delay_target is not None:
            if delay_target not in code_words:
                return delay_target, trace
            pc, delay_target = delay_target, None
        else:
            pc = nxt
            delay_target = target
        steps += 1
    raise AssertionError("cave did not exit")


def test_cave():
    print("cave behaviour:")
    block, orig1, orig2 = 0x00090000, 0x27BDFFF0, 0xFFBF0000  # addiu sp,-16 ; sd ra,0(sp) (shape only)
    code = C.build_cave(block, orig1, orig2)
    ours = [0x11111111 * (i + 1) for i in range(6)]
    for flags in (0, 1, 2, 3):
        mem = {block + C.B_FLAGS: flags, block + C.B_COUNT: 41, block + C.B_FOV: 0xAAAAAAAA}
        for i, v in enumerate(ours):
            mem[block + C.B_POS + 4 * i] = v
        exit_to, trace = run_cave(code, block + C.B_CODE, mem, (orig1, orig2))
        cam = [mem.get(C.CAMERA + 0x18 + 4 * i) for i in range(6)]
        expect(mem[block + C.B_COUNT] == 42, f"flags {flags}: counter incremented")
        expect((cam == ours) == bool(flags & 1), f"flags {flags}: pos/target copied only when bit 0 set")
        expect((mem.get(C.CAMERA + 0x60) == 0xAAAAAAAA) == bool(flags & 2), f"flags {flags}: fov copied only when bit 1 set")
        expect([w for w in trace if w in (orig1, orig2)] == [orig1, orig2] and trace[-4:-2] == [orig1, orig2],
               f"flags {flags}: original prologue replayed once, in order, right before the jump back")
        expect(exit_to == C.HOOK + 8, f"flags {flags}: returns to hook+8 (0x{exit_to:08X})")
        touched = {a for a in mem if not (block <= a < block + C.BLOCK_SIZE)}
        allowed = {C.CAMERA + 0x18 + 4 * i for i in range(6)} | {C.CAMERA + 0x60}
        expect(touched <= allowed, f"flags {flags}: writes only the block and camera pos/target/fov")
    tw = C.trampoline(block)
    expect(tw >> 32 == 0 and (tw & 0xFFFFFFFF) == mips.j(block + C.B_CODE, C.HOOK), "trampoline = j cave ; nop")
    expect(len(code) * 4 + C.B_CODE <= C.BLOCK_SIZE, f"cave fits its block ({len(code) * 4 + C.B_CODE} bytes)")
    for w in (orig1, orig2):
        expect(not mips.is_branch_or_jump(w), f"0x{w:08X} classed as plain")
    for w in (mips.j(0x100000, 0x100000), mips.beq("t0", "t1", 0, 8), 0x03E00008):
        expect(mips.is_branch_or_jump(w), f"0x{w:08X} classed as control flow")


if __name__ == "__main__":
    test_encodings()
    test_cave()
    print("\nALL PASS" if not FAILS else f"\n{len(FAILS)} FAILURE(S)")
    sys.exit(1 if FAILS else 0)
