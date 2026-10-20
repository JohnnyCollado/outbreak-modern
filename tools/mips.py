"""Minimal MIPS (R5900 subset) encoder for the small hooks this project installs.

Only the instructions the hooks need. Every encoding is cross-checked against LLVM's
assembler in tools/test_mips.py, so a typo here cannot reach the game unnoticed.
"""

REG = {"zero": 0, "at": 1, "v0": 2, "v1": 3, "a0": 4, "a1": 5, "a2": 6, "a3": 7,
       "t0": 8, "t1": 9, "t2": 10, "t3": 11, "t4": 12, "t5": 13, "t6": 14, "t7": 15,
       "s0": 16, "s1": 17, "s2": 18, "s3": 19, "s4": 20, "s5": 21, "s6": 22, "s7": 23,
       "t8": 24, "t9": 25, "k0": 26, "k1": 27, "gp": 28, "sp": 29, "fp": 30, "ra": 31}

NOP = 0x00000000


def _i(op, rs, rt, imm):
    return (op << 26) | (REG[rs] << 21) | (REG[rt] << 16) | (imm & 0xFFFF)


def lui(rt, imm):
    return _i(0x0F, "zero", rt, imm)


def addiu(rt, rs, imm):
    assert -0x8000 <= imm < 0x8000
    return _i(0x09, rs, rt, imm)


def andi(rt, rs, imm):
    assert 0 <= imm < 0x10000
    return _i(0x0C, rs, rt, imm)


def lw(rt, off, base):
    assert -0x8000 <= off < 0x8000
    return _i(0x23, base, rt, off)


def sw(rt, off, base):
    assert -0x8000 <= off < 0x8000
    return _i(0x2B, base, rt, off)


def lh(rt, off, base):
    assert -0x8000 <= off < 0x8000
    return _i(0x21, base, rt, off)


def sh(rt, off, base):
    assert -0x8000 <= off < 0x8000
    return _i(0x29, base, rt, off)


def sb(rt, off, base):
    assert -0x8000 <= off < 0x8000
    return _i(0x28, base, rt, off)


def lbu(rt, off, base):
    assert -0x8000 <= off < 0x8000
    return _i(0x24, base, rt, off)


def lwc1(ft, off, base):
    """Load a float into $f<ft> (ft is the register number)."""
    assert 0 <= ft < 32 and -0x8000 <= off < 0x8000
    return (0x31 << 26) | (REG[base] << 21) | (ft << 16) | (off & 0xFFFF)


def swc1(ft, off, base):
    assert 0 <= ft < 32 and -0x8000 <= off < 0x8000
    return (0x39 << 26) | (REG[base] << 21) | (ft << 16) | (off & 0xFFFF)


def add_s(fd, fs, ft):
    """$f<fd> = $f<fs> + $f<ft> (single precision)."""
    return (0x11 << 26) | (0x10 << 21) | (ft << 16) | (fs << 11) | (fd << 6)


def beq(rs, rt, from_addr, to_addr):
    """Branch from the instruction at from_addr to to_addr (offset counts from the delay slot)."""
    off = (to_addr - (from_addr + 4)) // 4
    assert (to_addr - from_addr) % 4 == 0 and -0x8000 <= off < 0x8000
    return _i(0x04, rs, rt, off)


def j(target, from_addr):
    """Absolute jump; target must be in the same 256 MiB region as the delay slot."""
    assert target % 4 == 0 and (target & 0xF0000000) == ((from_addr + 4) & 0xF0000000)
    return (0x02 << 26) | ((target >> 2) & 0x03FFFFFF)


def hi_lo(addr):
    """(hi, lo) such that lui hi ; addiu lo == addr (addiu sign-extends lo)."""
    lo = addr & 0xFFFF
    if lo >= 0x8000:
        lo -= 0x10000
    return ((addr - lo) >> 16) & 0xFFFF, lo


def is_branch_or_jump(w):
    """True for anything PC-relative or control-flow, which must never be relocated."""
    op, rs = w >> 26, (w >> 21) & 0x1F
    if op in (0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x14, 0x15, 0x16, 0x17):  # REGIMM, j/jal, b*, b*l
        return True
    if op in (0x10, 0x11, 0x12) and rs == 0x08:  # BC0x / BC1x / BC2x coprocessor branches
        return True
    return op == 0 and (w & 0x3F) in (0x08, 0x09, 0x0C, 0x0D)  # jr, jalr, syscall, break
