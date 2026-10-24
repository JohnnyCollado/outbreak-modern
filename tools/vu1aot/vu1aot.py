"""vu1aot.py: front end of the VU1 ahead-of-time generator (research/2026-10-24-vu1-aot-and-shaders.md).

  vu1aot.py facts [--elf game_files/SLPM_654.28] [--captures C:/ob/vu1-captures] [--out FILE]

Extracts Outbreak's 27 VU1 microprograms from the boot ELF (descriptor table D_00212760: 27 records of 0x30 bytes;
word 0 = the program's DMA chain, word 3 = its size; the chain is one DMA tag whose upper half holds VIF NOP + MPG at
micro address 0, then the code, then more MPGs), decodes every instruction pair (opcode tables as in PCSX2's
microVU_Tables.inl), checks each program against the 16 KB code images in the capture files (offset 104,
native/ob_diag/include/ob_vu1_capture.h), and prints the facts that decide the generator's design: E-bit stops
(MSCNT entry points), branches and their targets, branch-to-delay-slot cases, Q/P producers and the first reader after
each (in pairs, straight line), flag readers, XGKICKs, I-bit pairs.

No game data is written: the report holds addresses, mnemonics and counts only.
"""
import argparse
import glob
import os
import struct
import sys

DESC_TABLE = 0x00212760
DESC_COUNT = 27
DESC_TABLE_END = 0x00212CB0
CODE_SIZE = 16 * 1024
CAP_CODE_OFFSET = 104

UPPER = {}
for i, bc in enumerate("xyzw"):
    UPPER[0x00 + i] = "ADD" + bc
    UPPER[0x04 + i] = "SUB" + bc
    UPPER[0x08 + i] = "MADD" + bc
    UPPER[0x0C + i] = "MSUB" + bc
    UPPER[0x10 + i] = "MAX" + bc
    UPPER[0x14 + i] = "MINI" + bc
    UPPER[0x18 + i] = "MUL" + bc
UPPER.update({0x1C: "MULq", 0x1D: "MAXi", 0x1E: "MULi", 0x1F: "MINIi", 0x20: "ADDq", 0x21: "MADDq", 0x22: "ADDi",
              0x23: "MADDi", 0x24: "SUBq", 0x25: "MSUBq", 0x26: "SUBi", 0x27: "MSUBi", 0x28: "ADD", 0x29: "MADD",
              0x2A: "MUL", 0x2B: "MAX", 0x2C: "SUB", 0x2D: "MSUB", 0x2E: "OPMSUB", 0x2F: "MINI"})
UPPER_SPECIAL = {}
for i, bc in enumerate("xyzw"):
    UPPER_SPECIAL[0x00 + i] = "ADDA" + bc
    UPPER_SPECIAL[0x04 + i] = "SUBA" + bc
    UPPER_SPECIAL[0x08 + i] = "MADDA" + bc
    UPPER_SPECIAL[0x0C + i] = "MSUBA" + bc
    UPPER_SPECIAL[0x18 + i] = "MULA" + bc
UPPER_SPECIAL.update({0x10: "ITOF0", 0x11: "ITOF4", 0x12: "ITOF12", 0x13: "ITOF15", 0x14: "FTOI0", 0x15: "FTOI4",
                      0x16: "FTOI12", 0x17: "FTOI15", 0x1C: "MULAq", 0x1D: "ABS", 0x1E: "MULAi", 0x1F: "CLIP",
                      0x20: "ADDAq", 0x21: "MADDAq", 0x22: "ADDAi", 0x23: "MADDAi", 0x24: "SUBAq", 0x25: "MSUBAq",
                      0x26: "SUBAi", 0x27: "MSUBAi", 0x28: "ADDA", 0x29: "MADDA", 0x2A: "MULA", 0x2C: "SUBA",
                      0x2D: "MSUBA", 0x2E: "OPMULA", 0x2F: "NOP"})
LOWER = {0x00: "LQ", 0x01: "SQ", 0x04: "ILW", 0x05: "ISW", 0x08: "IADDIU", 0x09: "ISUBIU", 0x10: "FCEQ",
         0x11: "FCSET", 0x12: "FCAND", 0x13: "FCOR", 0x14: "FSEQ", 0x15: "FSSET", 0x16: "FSAND", 0x17: "FSOR",
         0x18: "FMEQ", 0x1A: "FMAND", 0x1B: "FMOR", 0x1C: "FCGET", 0x20: "B", 0x21: "BAL", 0x24: "JR", 0x25: "JALR",
         0x28: "IBEQ", 0x29: "IBNE", 0x2C: "IBLTZ", 0x2D: "IBGTZ", 0x2E: "IBLEZ", 0x2F: "IBGEZ"}
LOWER_OP = {0x30: "IADD", 0x31: "ISUB", 0x32: "IADDI", 0x34: "IAND", 0x35: "IOR"}
LOWER_SPECIAL = {0x30: "MOVE", 0x31: "MR32", 0x34: "LQI", 0x35: "SQI", 0x36: "LQD", 0x37: "SQD", 0x38: "DIV",
                 0x39: "SQRT", 0x3A: "RSQRT", 0x3B: "WAITQ", 0x3C: "MTIR", 0x3D: "MFIR", 0x3E: "ILWR", 0x3F: "ISWR",
                 0x40: "RNEXT", 0x41: "RGET", 0x42: "RINIT", 0x43: "RXOR", 0x64: "MFP", 0x68: "XTOP", 0x69: "XITOP",
                 0x6C: "XGKICK", 0x70: "ESADD", 0x71: "ERSADD", 0x72: "ELENG", 0x73: "ERLENG", 0x74: "EATANxy",
                 0x75: "EATANxz", 0x76: "ESUM", 0x78: "ESQRT", 0x79: "ERSQRT", 0x7A: "ERCPR", 0x7B: "WAITP",
                 0x7C: "ESIN", 0x7D: "EATAN", 0x7E: "EEXP"}

BRANCHES = {"B", "BAL", "JR", "JALR", "IBEQ", "IBNE", "IBLTZ", "IBGTZ", "IBLEZ", "IBGEZ"}
Q_PRODUCERS = {"DIV", "SQRT", "RSQRT"}
P_PRODUCERS = {"ESADD", "ERSADD", "ELENG", "ERLENG", "EATANxy", "EATANxz", "ESUM", "ESQRT", "ERSQRT", "ERCPR",
               "ESIN", "EATAN", "EEXP"}
FLAG_READERS = {"FCEQ", "FCAND", "FCOR", "FCGET", "FSEQ", "FSAND", "FSOR", "FMEQ", "FMAND", "FMOR"}


def upper_name(w):
    op = w & 0x3F
    if op >= 0x3C:
        return UPPER_SPECIAL.get(((w >> 6) & 0x1F) * 4 + (w & 3), "UPPER?%08X" % w)
    return UPPER.get(op, "UPPER?%08X" % w)


def lower_name(w):
    op = (w >> 25) & 0x7F
    if op == 0x40:
        f = w & 0x3F
        if f >= 0x3C:
            return LOWER_SPECIAL.get(((w >> 6) & 0x1F) * 4 + (w & 3), "LOWER?%08X" % w)
        return LOWER_OP.get(f, "LOWER?%08X" % w)
    return LOWER.get(op, "LOWER?%08X" % w)


def is_nop_lower(w):
    return w == 0x8000033C  # MOVE vf0, vf0 with no fields: the assembler's lower NOP


def branch_target(pc_pair, w):
    imm = w & 0x7FF
    if imm & 0x400:
        imm -= 0x800
    return pc_pair + 1 + imm  # in pairs, relative to the pair after the branch


class Elf:
    def __init__(self, path):
        self.b = open(path, "rb").read()
        phoff, = struct.unpack_from("<I", self.b, 0x1C)
        phnum, = struct.unpack_from("<H", self.b, 0x2C)
        self.segs = [struct.unpack_from("<8I", self.b, phoff + i * 32) for i in range(phnum)]

    def off(self, va):
        for t, o, v, p, fs, ms, fl, al in self.segs:
            if t == 1 and v <= va < v + fs:
                return o + va - v
        raise ValueError("address %08X not in a loaded segment" % va)

    def words(self, va, n):
        return struct.unpack_from("<%dI" % n, self.b, self.off(va))


def descriptors(elf):
    """The program records between D_00212760 and D_00212CB0. The 7 small side records in between (bone-palette
    parameters) are skipped: a program record has its DMA chain in word 0 (0x001D0000-0x001EFFFF, the chains), a size
    below 8 KB in word 3 and its EE packet builder (0x0019xxxx) in word 4."""
    recs = []
    for rec in range(DESC_TABLE, DESC_TABLE_END + 1, 0x10):
        w = elf.words(rec, 12)
        if 0x001D0000 <= w[0] < 0x001F0000 and 0 < w[3] < 0x2000 and 0x00190000 <= w[4] < 0x001A0000:
            recs.append((rec, w))
    if len(recs) != DESC_COUNT:
        raise ValueError("found %d program records, expected %d" % (len(recs), DESC_COUNT))
    return recs


def extract(elf):
    """[(record address, image bytes, pairs written)] for the 27 programs."""
    progs = []
    for rec, w in descriptors(elf):
        chain = w[0]
        # The record's size word (w[3]) is a little short of the chain: take the length from the DMA tag (QWC
        # quadwords after the tag's own), so the last MPG's tail is not cut off.
        tag, = struct.unpack_from("<I", elf.b, elf.off(chain))
        data = elf.b[elf.off(chain):elf.off(chain) + 16 + (tag & 0xFFFF) * 16]
        image = bytearray(CODE_SIZE)
        written = 0
        # The chain: a DMA tag (8 bytes) whose upper 8 bytes are two VIFcodes, then the VIF stream.
        stream = data[8:]
        pos = 0
        while pos + 4 <= len(stream):
            cmd, = struct.unpack_from("<I", stream, pos)
            pos += 4
            op = (cmd >> 24) & 0x7F
            if op == 0x4A:  # MPG
                addr = (cmd & 0xFFFF) * 8
                n = (cmd >> 16) & 0xFF or 256
                image[addr:addr + n * 8] = stream[pos:pos + n * 8]
                written = max(written, addr // 8 + n)
                pos += n * 8
            elif (op & 0x60) == 0x60:  # UNPACK (the 7 programs' constant): skip its payload
                vn, vl = (op >> 2) & 3, op & 3
                bits = (32, 16, 8, 16)[vl] if not (vl == 3 and vn == 3) else 4
                per = 2 if (vl == 3 and vn == 3) else ((vn + 1) * bits + 7) // 8
                num = (cmd >> 16) & 0xFF or 256
                pos += (num * per + 3) & ~3
            elif op in (0x20,):  # STMASK
                pos += 4
            elif op in (0x30, 0x31):  # STROW/STCOL
                pos += 16
        progs.append((rec, bytes(image), written))
    return progs


def decode(image, pairs):
    out = []
    for p in range(pairs):
        lo, up = struct.unpack_from("<II", image, p * 8)
        flags = "".join(c for c, bit in (("I", 31), ("E", 30), ("M", 29), ("D", 28), ("T", 27)) if (up >> bit) & 1)
        u = upper_name(up)
        l = "(imm)" if "I" in flags else ("NOP" if is_nop_lower(lo) else lower_name(lo))
        out.append((p, up, lo, u, l, flags))
    return out


def facts(rec, image, pairs):
    code = decode(image, pairs)
    lines = []
    stops = [p for p, up, lo, u, l, f in code if "E" in f]
    entries = [0] + [p + 2 for p in stops if p + 2 < pairs]  # E-bit: the next pair runs, MSCNT resumes after it
    targets, delay_targets, branch_count = set(), [], 0
    for p, up, lo, u, l, f in code:
        if l in BRANCHES:
            branch_count += 1
            if l not in ("JR", "JALR"):
                targets.add(branch_target(p, lo))
    branch_pairs = {p for p, up, lo, u, l, f in code if l in BRANCHES}
    for t in sorted(targets):
        if t - 1 in branch_pairs:
            delay_targets.append(t)
    # Q/P: first reader after each producer, counted in pairs along the straight line (no branch following).
    q_gaps, p_gaps = [], []
    for p, up, lo, u, l, f in code:
        if l in Q_PRODUCERS or l in P_PRODUCERS:
            want_q = l in Q_PRODUCERS
            gap = None
            for d in range(1, 64):
                if p + d >= pairs:
                    break
                _, _, _, u2, l2, f2 = code[p + d]
                reads = (u2.endswith("q") or l2 == "WAITQ") if want_q else (l2 in ("MFP", "WAITP"))
                if reads:
                    gap = (d, l2 if l2 in ("WAITQ", "WAITP") else (u2 if want_q else l2))
                    break
                if l2 in BRANCHES or "E" in f2:
                    gap = (d, "branch/stop first")
                    break
            (q_gaps if want_q else p_gaps).append((p, l, gap))
    readers = [(p, l) for p, up, lo, u, l, f in code if l in FLAG_READERS]
    mnem = {}
    for p, up, lo, u, l, f in code:
        mnem[u] = mnem.get(u, 0) + 1
        mnem[l] = mnem.get(l, 0) + 1
    unknown = sorted(k for k in mnem if "?" in k)
    lines.append("== D_%08X: %d pairs, %d E-bit stops at %s, entries %s" %
                 (rec, pairs, len(stops), [hex(s * 8) for s in stops], [hex(e * 8) for e in entries]))
    lines.append("   branches %d, static targets %s, JR/JALR %d, branch target in a delay slot: %s" %
                 (branch_count, [hex(t * 8) for t in sorted(targets)],
                  sum(1 for c in code if c[4] in ("JR", "JALR")), [hex(t * 8) for t in delay_targets] or "none"))
    lines.append("   Q producers -> first reader (pairs): %s" %
                 ["%s@%x->%s" % (l, p * 8, g) for p, l, g in q_gaps])
    lines.append("   P producers -> first reader (pairs): %s" %
                 ["%s@%x->%s" % (l, p * 8, g) for p, l, g in p_gaps])
    lines.append("   flag readers: %s" % (["%s@%x" % (l, p * 8) for p, l in readers] or "none"))
    lines.append("   XGKICK %d, I-bit pairs %d, M/D/T bits %d, unknown opcodes %s" %
                 (mnem.get("XGKICK", 0), sum(1 for c in code if "I" in c[5]),
                  sum(1 for c in code if set(c[5]) & set("MDT")), unknown or "none"))
    return lines, mnem


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("cmd", choices=["facts"])
    ap.add_argument("--elf", default="game_files/SLPM_654.28")
    ap.add_argument("--captures", default="C:/ob/vu1-captures")
    ap.add_argument("--out")
    a = ap.parse_args(argv)
    elf = Elf(a.elf)
    progs = extract(elf)
    # Captured code images (16 KB), deduplicated by their bytes.
    images = {}
    for f in sorted(glob.glob(os.path.join(a.captures, "*.cap"))):
        with open(f, "rb") as fh:
            fh.seek(CAP_CODE_OFFSET)
            img = fh.read(CODE_SIZE)
        images.setdefault(img, f)
    out = []
    totals = {}
    matched = 0
    for rec, image, pairs in progs:
        lines, mnem = facts(rec, image, pairs)
        hits = [os.path.basename(f) for img, f in images.items() if img[:pairs * 8] == image[:pairs * 8]]
        if hits:
            matched += 1
        lines.insert(1, "   captured: %s" % (", ".join(hits[:3]) + (" (+%d)" % (len(hits) - 3) if len(hits) > 3 else "")
                                             if hits else "no"))
        out += lines
        for k, v in mnem.items():
            totals[k] = totals.get(k, 0) + v
    out.insert(0, "vu1aot facts: %d programs from %s, %d match a captured code image (of %d distinct images), "
                  "%d pairs in all" % (len(progs), a.elf, matched, len(images), sum(p for _, _, p in progs)))
    out.append("== opcode totals over all programs (upper and lower slots):")
    out.append("   " + ", ".join("%s %d" % (k, v) for k, v in sorted(totals.items(), key=lambda x: -x[1])))
    text = "\n".join(out) + "\n"
    if a.out:
        open(a.out, "w").write(text)
    sys.stdout.write(text)


if __name__ == "__main__":
    main(sys.argv[1:])
