#!/usr/bin/env python3
"""Run the built mod in a MIPS emulator (unicorn) against a fake game memory, through a fake
patched call site: the stub must hand every register back unchanged and continue into
0x00345740, and mod_frame must keep the status block and show its line.

  python mod/test_mod.py        (needs `pip install unicorn`; skipped without it)

Unicorn emulates a MIPS32 CPU, not the EE: the stub's sq/lq (EE-only) run here as 32-bit
sw/lw. (Its FPU rounds float -> int to nearest, the EE's toward zero: tests of
code that converts must allow for that.)
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import build  # noqa: E402

try:
    from unicorn import Uc, UcError, UC_ARCH_MIPS, UC_MODE_MIPS32, UC_MODE_LITTLE_ENDIAN
    from unicorn.mips_const import UC_MIPS_REG_PC, UC_MIPS_REG_29, UC_MIPS_REG_31
    import unicorn.mips_const as M
except ImportError:
    print("unicorn not installed: skipped (pip install unicorn)")
    sys.exit(0)

FAILS = []
RAM = 8 << 20
# A fake caller: load every FPU register from FP_IN (unicorn can't set them directly), the patched
# call site (jal mod_entry; nop), then store them all to FP_OUT. k0 holds the table address: no C
# code uses k0/k1.
FP_IN, FP_OUT = 0x00600000, 0x00600100
CALLER = 0x00340100
SITE = CALLER + 4 + 32 * 4
STOP = SITE + 8 + 32 * 4
GAME_FN = 0x00345740
SP = 0x007F0000


def expect(cond, what):
    print(("  ok   " if cond else "  FAIL ") + what)
    if not cond:
        FAILS.append(what)


def reg(n):
    return getattr(M, f"UC_MIPS_REG_{n}")


def mips32(w):
    """sq/lq (EE-only, opcodes 0x1F/0x1E) as sw/lw of the low word: unicorn's CPU has no 128-bit
    registers. The EE requires 16-byte alignment for them: the stub saves at sp + r*16 with sp
    16-aligned, so the offset must be too."""
    if w >> 26 in (0x1E, 0x1F):
        assert (w & 0xF) == 0 and (w >> 21) & 31 == 29, f"sq/lq 0x{w:08X} not at a 16-byte offset from sp"
        return (w & 0x03FFFFFF) | ((0x2B if w >> 26 == 0x1F else 0x23) << 26)
    return w


def machine(words):
    uc = Uc(UC_ARCH_MIPS, UC_MODE_MIPS32 + UC_MODE_LITTLE_ENDIAN)
    uc.ctl_set_cpu_model(M.UC_CPU_MIPS32_24KF)                          # one with an FPU
    uc.reg_write(M.UC_MIPS_REG_CP0_STATUS, uc.reg_read(M.UC_MIPS_REG_CP0_STATUS) | 0x20000000)   # CU1: FPU on
    uc.mem_map(0, RAM)
    uc.mem_write(build.MOD_BASE, struct.pack(f"<{len(words)}I", *map(mips32, words)))
    code = [0x3C1A0000 | (FP_IN >> 16)]                                   # lui k0, FP_IN >> 16
    code += [0xC7400000 | (f << 16) | (4 * f) for f in range(32)]         # lwc1 $f, 4f(k0)
    code += [build.jal(build.MOD_BASE), 0]
    code += [0xE7400000 | (f << 16) | (0x100 + 4 * f) for f in range(32)]  # swc1 $f, 0x100+4f(k0)
    uc.mem_write(CALLER, struct.pack(f"<{len(code)}I", *code))
    uc.mem_write(GAME_FN, struct.pack("<2I", 0x03E00008, 0))          # jr ra; nop

    return uc


def call_site(uc):
    uc.reg_write(UC_MIPS_REG_29, SP)
    uc.emu_start(CALLER, STOP)


def main():
    words, syms, notes = build.build()
    uc = machine(words)
    # fake game state: one player, ours is #0, at (100, -5, 2500); text slot 31 free
    uc.mem_write(0x0023BE14, struct.pack("<I", 1))
    uc.mem_write(0x0048BF7D, b"\0")
    uc.mem_write(0x00476DD0 + 0x38, struct.pack("<3f", 100.0, -5.0, 2500.0))
    live = [n for n in range(1, 32) if n not in (26, 27, 29, 31)]      # all but k0/k1/sp/ra
    for n in live:
        uc.reg_write(reg(n), 0x1000 * n + 0x77)
    uc.mem_write(FP_IN, struct.pack("<32I", *[0x3F800000 + f for f in range(32)]))
    try:
        call_site(uc)
    except UcError as e:
        expect(False, f"one call runs to the end ({e} at 0x{uc.reg_read(UC_MIPS_REG_PC):08X})")
        return 1
    live = [n for n in live if n != 26]                                     # k0: the caller's own
    expect(all(uc.reg_read(reg(n)) & 0xFFFFFFFF == 0x1000 * n + 0x77 for n in live),
           "the stub hands every integer register back unchanged")
    expect(struct.unpack("<32I", uc.mem_read(FP_OUT, 128)) == tuple(0x3F800000 + f for f in range(32)),
           "...and every FPU register")
    expect(uc.reg_read(UC_MIPS_REG_29) == SP, "...and the stack pointer")
    expect(uc.reg_read(UC_MIPS_REG_31) == SITE + 8, "the game function returns to the call site")
    st = struct.unpack("<4I3fI", uc.mem_read(0x000AFF00, 32))
    expect(st[0] == 0x434D4F31 and st[1] == 1 and st[2] == 1 and st[7] == 1, "status: magic, version, 1 frame, 1 start")
    expect(st[3] == SITE + 8, "status: the caller")
    expect(st[4:7] == (100.0, -5.0, 2500.0), "status: our player's position")
    for _ in range(40):
        call_site(uc)
    line = uc.mem_read(0x002F7450 + 31 * 0x8C, 0x8C)
    used, x, y, timer = struct.unpack_from("<4h", line, 0)
    text = bytes(line[0x0C:]).split(b"\0")[0]
    expect(used == 1 and line[0x0B] == 0xFF and text.startswith(b"Modern Camera"),
           f"on-screen line shown once the room is up ({text!r}, timer {timer})")
    uc.mem_write(0x002F7450 + 31 * 0x8C, struct.pack("<h", 1) + b"\0" * 9 + b"\x10")   # the game's line, prio 0x10
    uc.mem_write(0x000AFF08, struct.pack("<I", 29))
    call_site(uc)
    expect(uc.mem_read(0x002F7450 + 31 * 0x8C + 0x0B, 1)[0] == 0x10, "a line the game is using is left alone")
    print("\nALL PASS" if not FAILS else f"\n{len(FAILS)} FAILURE(S)")
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
