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
        (mips.lwc1(0, 56, "t3"), "lwc1 $f0, 56($11)"),
        (mips.swc1(2, 24, "t2"), "swc1 $f2, 24($10)"),
        (mips.add_s(2, 0, 1), "add.s $f2, $f0, $f1"),
        (mips.lbu("t3", 0, "t0"), "lbu $11, 0($8)"),
        (mips.lh("t4", 52, "t0"), "lh $12, 52($8)"),
        (mips.sh("t4", 146, "t3"), "sh $12, 146($11)"),
        (mips.sb("t4", 3292, "t3"), "sb $12, 3292($11)"),
    ]
    for word, want in cases:
        got = llvm_disasm(word)
        expect(got == want, f"0x{word:08X}: {got!r} == {want!r}")


def run_cave(code, base, mem, prologue):
    """Interpret the cave until it jumps out. Returns (exit target, every word executed, in order).

    Instructions outside the cave's own vocabulary (the displaced prologue) are not interpreted,
    only recorded: they are the game's, and what they do is the game's business."""
    reg, freg = [0] * 32, [0.0] * 32
    pc, steps, trace, delay_target = base, 0, [], None
    f32 = lambda bits: struct.unpack("<f", struct.pack("<I", bits))[0]  # noqa: E731
    u32 = lambda x: struct.unpack("<I", struct.pack("<f", x))[0]  # noqa: E731
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
        elif op in (0x21, 0x29, 0x28):                  # lh / sh / sb on a word-keyed memory
            a = (reg[rs] + simm) & 0xFFFFFFFF
            word, sh_ = a & ~3, 8 * (a & 3)
            cur = mem.get(word, 0)
            if op == 0x21:
                v = (cur >> sh_) & 0xFFFF
                reg[rt] = (v - 0x10000 if v & 0x8000 else v) & 0xFFFFFFFF
            else:
                m = 0xFFFF if op == 0x29 else 0xFF
                mem[word] = (cur & ~(m << sh_)) | ((reg[rt] & m) << sh_)
        elif op == 0x31:
            freg[rt] = f32(mem.get((reg[rs] + simm) & 0xFFFFFFFF, 0))
        elif op == 0x39:
            mem[(reg[rs] + simm) & 0xFFFFFFFF] = u32(freg[rt])
        elif op == 0x11 and rs == 0x10 and w & 0x3F == 0:
            freg[(w >> 6) & 31] = f32(u32(freg[(w >> 11) & 31] + freg[rt]))  # single precision
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


def f2u(x):
    return struct.unpack("<I", struct.pack("<f", x))[0]


def test_cave():
    print("cave behaviour:")
    block, orig1, orig2 = 0x00090000, 0x27BDFFF0, 0xFFBF0000  # addiu sp,-16 ; sd ra,0(sp) (shape only)
    player = 0x00476DD0
    code = C.build_cave(block, orig1, orig2)
    ours = [f2u(v) for v in (10.0, 20.0, 30.0, 1.5, 2.5, 3.5)]
    for flags in range(64):
        for has_player in (True, False):
            mem = {block + C.B_FLAGS: flags, block + C.B_COUNT: 41, block + C.B_FOV: 0xAAAAAAAA,
                   block + C.B_PLAYER: player if has_player else 0,
                   block + C.B_AIM_HEADING: (0xF000 << 16) | 0x1234,       # heading 0x1234, elev -0x1000
                   player + C.P_TARGET: 0x004017C0, player + 0xCDC: 0x11223305,
                   player + 0x90: 0x7777AAAA, player + 0xBC8: 0x55550000,
                   block + C.B_VEL_X: f2u(1.5), block + C.B_VEL_Z: f2u(-2.0)}
            for i, v in enumerate((100.0, 200.0, 300.0)):
                mem[player + C.PLAYER_POS + 4 * i] = f2u(v)
            for i, v in enumerate(ours):
                mem[block + C.B_POS + 4 * i] = v
            exit_to, trace = run_cave(code, block + C.B_CODE, mem, (orig1, orig2))
            cam = [mem.get(C.CAMERA + 0x18 + 4 * i) for i in range(6)]
            tag = f"flags {flags} player {'set' if has_player else 'none'}"
            if not flags & C.F_POS:
                want = [None] * 6
            elif flags & C.F_RELATIVE:
                want = ([f2u(v) for v in (110.0, 220.0, 330.0, 101.5, 202.5, 303.5)] if has_player
                        else [None] * 6)
            else:
                want = ours
            expect(mem[block + C.B_COUNT] == 42, f"{tag}: counter incremented")
            expect(cam == want, f"{tag}: camera pos/target = {'player + offsets' if flags & 5 == 5 else 'copy' if flags & 1 else 'untouched'}")
            expect((mem.get(C.CAMERA + 0x60) == 0xAAAAAAAA) == bool(flags & C.F_FOV), f"{tag}: fov only with bit 1")
            expect([w for w in trace if w in (orig1, orig2)] == [orig1, orig2] and trace[-4:-2] == [orig1, orig2],
                   f"{tag}: original prologue replayed once, in order, right before the jump back")
            expect(exit_to == C.HOOK + 8, f"{tag}: returns to hook+8")
            aim_on = bool(flags & C.F_AIM) and has_player
            strafe_on = aim_on and bool(flags & C.F_STRAFE)
            pos = [mem.get(player + C.PLAYER_POS + 4 * i) for i in range(3)]
            want_pos = [f2u(101.5), f2u(200.0), f2u(298.0)] if strafe_on else [f2u(100.0), f2u(200.0), f2u(300.0)]
            expect(pos == want_pos, f"{tag}: position stepped by (1.5, -, -2) only with bits 3+4")
            expect((mem[player + C.P_TARGET] == 0) == aim_on, f"{tag}: lock-on cleared only with bit 3")
            expect(mem[player + 0xCDC] == (0x112233FF if aim_on else 0x11223305),
                   f"{tag}: target index byte set to 0xFF only with bit 3, neighbours untouched")
            face_on = aim_on or (bool(flags & C.F_FACE) and has_player)
            expect(mem[player + 0x90] == (0x1234AAAA if face_on else 0x7777AAAA),
                   f"{tag}: heading (+0x92, s16) written with bit 3 or bit 5, +0x90 untouched")
            expect(mem[player + 0xBC8] == (0x5555F000 if aim_on else 0x55550000),
                   f"{tag}: elevation (+0xBC8, s16) written only with bit 3, +0xBCA untouched")
            touched = {a for a in mem if not (block <= a < block + C.BLOCK_SIZE)} - {
                player + C.PLAYER_POS + 4 * i for i in range(3)} - {
                player + C.P_TARGET, player + 0xCDC, player + 0x90, player + 0xBC8}
            allowed = {C.CAMERA + 0x18 + 4 * i for i in range(6)} | {C.CAMERA + 0x60}
            expect(touched <= allowed, f"{tag}: writes only the block and camera pos/target/fov")
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
