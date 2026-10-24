"""emit.py: emitter of the VU1 ahead-of-time generator (reports/2026-10-24-vu1-aot-emitter.md).

  emit.py [--elf game_files/SLPM_654.28] [--captures C:/ob/vu1-captures] [--out C:/ob/vu1aot/gen]
          [--programs captured|all|D_00212BB0,...]

Turns Outbreak's VU1 microprograms (extracted by vu1aot.py) into C++: one function per program, a switch on the entry
pc, one label per (pc, pipeline state) node, goto for branches after their delay slot. The output is game-derived:
it goes under C:/ob, never into git.

Semantics are those of PS2Recomp's VU1 interpreter (C:/ob/up/ps2xRuntime/src/lib/vu/ps2_vu1_core.cpp, _upper.cpp,
_lower.cpp), which the result is compared against bit for bit. The interpreter models the pipeline cycle by cycle;
here that model runs once, at generation time:
  - Every run starts with the pipeline empty (the interpreter flushes at each E-bit stop, ps2_vu1_core.cpp:1855-1858),
    so the timing state at every pair is a function of the path alone. The generator simulates the interpreter's
    stall rules (calculatePairReadyCycle :1017, markPairWrites :1062) over the control-flow graph, keyed by
    (pc, timing state): two arrivals at one pc with different pending writes are two nodes.
  - VF/VI/ACC readers always stall until the write lands, so those values follow program order and are plain locals.
  - Q, P, CLIP and MAC are not interlocked: a reader sees the newest committed result. With static timing, the
    number k of results still in flight at the reader is a constant; the code keeps a short history per register
    (h0 = newest result) and the reader reads h[k].
  - Stall and flush cycles are constants per node, so the generated code also reproduces the interpreter's cycle
    count (a check of the timing model), except XGKICK, whose length depends on the packet (done at run time).
  - Status flags have no reader in these programs and are not computed (STATUS/MAC are left at their input values,
    microVU's pattern); MAC is computed only in programs that contain an FMAND/FMOR/FMEQ.
Unsupported constructs (FCSET, status readers, RINIT/RNEXT, a second XGKICK that may stall, branch in a delay slot,
E bit in a delay slot, writes to vf0 read in the same pair, ...) stop generation for that program with a reason.
"""
import argparse
import glob
import hashlib
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import vu1aot  # noqa: E402  (front end: extraction and decoding)

LOWER_NOP = 0x8000033C
FMAC_LATENCY = 4
LANES = "xyzw"


def lane_bit(c):
    return 1 << (3 - c)  # dest bit for component c (x = 8)


class Unsupported(Exception):
    pass


# ---- usage decoding: a port of decodeUpperUsage / decodeLowerUsage (ps2_vu1_core.cpp:1100-1530) ----
class Usage:
    __slots__ = ("vf_read", "vf_write", "vi_read", "vi_write", "acc_read", "acc_write", "latency", "vf_latency",
                 "vi_latency", "pipe", "waitq", "waitp", "reads_clip", "writes_clip", "delays_branch", "reserved")

    def __init__(self):
        self.vf_read = {}  # reg -> lanes
        self.vf_write = None  # (reg, lanes)
        self.vi_read = 0
        self.vi_write = 0
        self.acc_read = 0
        self.acc_write = 0
        self.latency = 0
        self.vf_latency = 0
        self.vi_latency = 0
        self.pipe = None
        self.waitq = self.waitp = self.reads_clip = self.writes_clip = self.delays_branch = self.reserved = False

    def rd(self, reg, lanes):
        if lanes:
            self.vf_read[reg] = self.vf_read.get(reg, 0) | lanes

    def wr(self, reg, lanes):
        if reg == 0 or lanes == 0:
            return
        if self.vf_write is None:
            self.vf_write = (reg, lanes)
        elif self.vf_write[0] == reg:
            self.vf_write = (reg, self.vf_write[1] | lanes)

    def rvi(self, reg):
        if reg:
            self.vi_read |= 1 << reg

    def wvi(self, reg):
        if reg:
            self.vi_write |= 1 << reg


def fld(w):
    return dict(dest=(w >> 21) & 0xF, ft=(w >> 16) & 0x1F, fs=(w >> 11) & 0x1F, fd=(w >> 6) & 0x1F,
                it=(w >> 16) & 0xF, is_=(w >> 11) & 0xF, id_=(w >> 6) & 0xF)


MADD_OPS = {0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x21, 0x23, 0x25, 0x27, 0x29, 0x2D, 0x2E}
MADDA_SPECIAL = {0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x21, 0x23, 0x25, 0x27, 0x29, 0x2D}


def upper_usage(w):
    u = Usage()
    u.pipe = "fmac"
    u.latency = FMAC_LATENCY
    op = w & 0x3F
    f = fld(w)
    dest, fs, ft, fd = f["dest"], f["fs"], f["ft"], f["fd"]
    if op <= 0x2F:
        u.rd(fs, dest)
        u.wr(fd, dest)
        if op <= 0x1B:
            u.rd(ft, lane_bit(op & 3))
        elif op >= 0x28:
            u.rd(ft, 0xE if op == 0x2E else dest)
        if op in MADD_OPS:
            u.acc_read = dest
        return u
    if op >= 0x3C:
        sp = (w & 3) | ((w >> 4) & 0x7C)
        writes_acc = sp <= 0x0F or 0x18 <= sp <= 0x1C or sp == 0x1E or 0x20 <= sp <= 0x2A or 0x2C <= sp <= 0x2E
        if writes_acc:
            u.rd(fs, dest)
            if sp <= 0x1B:
                u.rd(ft, lane_bit(sp & 3))
            elif 0x28 <= sp <= 0x2E:
                u.rd(ft, 0xE if sp == 0x2E else dest)
            u.acc_write = dest
            if sp in MADDA_SPECIAL:
                u.acc_read = dest
        elif 0x10 <= sp <= 0x17 or sp == 0x1D:
            u.rd(fs, dest)
            u.wr(ft, dest)
        elif sp == 0x1F:
            u.rd(fs, 0xE)
            u.rd(ft, 0x1)
            u.writes_clip = True
        elif sp not in (0x2F, 0x30):
            u.reserved = True
        return u
    u.reserved = True
    return u


EFU_LAT = {0x70: 11, 0x71: 18, 0x72: 18, 0x79: 18, 0x73: 24, 0x74: 54, 0x75: 54, 0x7D: 54, 0x76: 12, 0x78: 12,
           0x7A: 12, 0x7C: 29, 0x7E: 44}


def lower_usage(w):
    u = Usage()
    if w == 0 or w == LOWER_NOP:
        return u
    hi = (w >> 25) & 0x7F
    f = fld(w)
    dest, vft, vfs, vit, vis, vid = f["dest"], f["ft"], f["fs"], f["it"], f["is_"], f["id_"]
    if hi == 0x00:
        u.pipe, u.latency = "lsu", 4
        u.rvi(vis)
        u.wr(vft, dest)
        return u
    if hi == 0x01:
        u.pipe, u.latency = "lsu", 1
        u.rvi(vit)
        u.rd(vfs, dest)
        return u
    if hi == 0x04:
        u.pipe, u.latency = "lsu", 4
        u.rvi(vis)
        u.wvi(vit)
        return u
    if hi == 0x05:
        u.pipe, u.latency = "lsu", 1
        u.rvi(vis)
        u.rvi(vit)
        return u
    if hi in (0x08, 0x09):
        u.pipe, u.latency, u.delays_branch = "ialu", 1, True
        u.rvi(vis)
        u.wvi(vit)
        return u
    if hi in (0x10, 0x12, 0x13):
        u.pipe, u.latency, u.reads_clip = "ialu", 1, True
        u.wvi(1)
        return u
    if hi == 0x11:
        u.pipe, u.latency, u.writes_clip = "fmac", FMAC_LATENCY, True
        return u
    if hi in (0x14, 0x16, 0x17):
        u.pipe, u.latency = "ialu", 1
        u.wvi(vit)
        return u
    if hi == 0x15:
        u.pipe, u.latency = "fmac", FMAC_LATENCY
        return u
    if hi in (0x18, 0x1A, 0x1B):
        u.pipe, u.latency = "ialu", 1
        u.rvi(vis)
        u.wvi(vit)
        return u
    if hi == 0x1C:
        u.pipe, u.latency, u.reads_clip = "ialu", 1, True
        u.wvi(vit)
        return u
    if hi == 0x20:
        u.pipe = "branch"
        return u
    if hi == 0x21:
        u.pipe, u.latency = "branch", 1
        u.wvi(vit)
        return u
    if hi == 0x24:
        u.pipe = "branch"
        u.rvi(vis)
        return u
    if hi == 0x25:
        u.pipe, u.latency = "branch", 1
        u.rvi(vis)
        u.wvi(vit)
        return u
    if hi in (0x28, 0x29):
        u.pipe = "branch"
        u.rvi(vis)
        u.rvi(vit)
        return u
    if hi in (0x2C, 0x2D, 0x2E, 0x2F):
        u.pipe = "branch"
        u.rvi(vis)
        return u
    if hi != 0x40:
        u.reserved = True
        return u
    d = w & 0x3F
    if d in (0x30, 0x31, 0x34, 0x35):
        u.pipe, u.latency, u.delays_branch = "ialu", 1, True
        u.rvi(vis)
        u.rvi(vit)
        u.wvi(vid)
        return u
    if d == 0x32:
        u.pipe, u.latency, u.delays_branch = "ialu", 1, True
        u.rvi(vis)
        u.wvi(vit)
        return u
    if d < 0x3C:
        u.reserved = True
        return u
    sp = (w & 3) | ((w >> 4) & 0x7C)
    fsf_lane = lane_bit((w >> 21) & 3)
    ftf_lane = lane_bit((w >> 23) & 3)
    if sp in (0x30, 0x31):
        u.pipe, u.latency = "fmac", 4
        u.rd(vfs, 0xF if sp == 0x31 else dest)
        u.wr(vft, dest)
    elif sp in (0x34, 0x36):
        u.pipe, u.latency, u.vi_latency, u.delays_branch = "lsu", 4, 1, True
        u.rvi(vis)
        u.wvi(vis)
        u.wr(vft, dest)
    elif sp in (0x35, 0x37):
        u.pipe, u.latency, u.delays_branch = "lsu", 1, True
        u.rvi(vit)
        u.wvi(vit)
        u.rd(vfs, dest)
    elif sp == 0x38:
        u.pipe, u.latency = "fdiv", 7
        u.rd(vfs, fsf_lane)
        u.rd(vft, ftf_lane)
    elif sp == 0x39:
        u.pipe, u.latency = "fdiv", 7
        u.rd(vft, ftf_lane)
    elif sp == 0x3A:
        u.pipe, u.latency = "fdiv", 13
        u.rd(vfs, fsf_lane)
        u.rd(vft, ftf_lane)
    elif sp == 0x3B:
        u.pipe, u.waitq = "fdiv", True
    elif sp == 0x3C:
        u.pipe, u.latency, u.delays_branch = "ialu", 1, True
        u.rd(vfs, fsf_lane)
        u.wvi(vit)
    elif sp == 0x3D:
        u.pipe, u.latency = "fmac", 4
        u.rvi(vis)
        u.wr(vft, dest)
    elif sp == 0x3E:
        u.pipe, u.latency = "lsu", 4
        u.rvi(vis)
        u.wvi(vit)
    elif sp == 0x3F:
        u.pipe, u.latency = "lsu", 1
        u.rvi(vis)
        u.rvi(vit)
    elif sp in (0x40, 0x41):
        u.pipe, u.latency = "fmac", 4
        u.wr(vft, dest)
    elif sp in (0x42, 0x43):
        u.pipe, u.latency = "ialu", 1
        u.rd(vfs, fsf_lane)
    elif sp == 0x64:
        u.pipe, u.latency = "fmac", 4
        u.wr(vft, dest)
    elif sp in (0x68, 0x69):
        u.pipe, u.latency = "ialu", 1
        u.wvi(vit)
    elif sp == 0x6C:
        u.pipe, u.latency = "xgkick", 2
        u.rvi(vis)
    elif sp in EFU_LAT:
        u.pipe, u.latency = "efu", EFU_LAT[sp]
        if 0x70 <= sp <= 0x73:
            u.rd(vfs, 0xE)
        elif sp == 0x74:
            u.rd(vfs, 0xC)
        elif sp == 0x75:
            u.rd(vfs, 0xA)
        elif sp == 0x76:
            u.rd(vfs, 0xF)
        else:
            u.rd(vfs, fsf_lane)
    elif sp == 0x7B:
        u.pipe, u.waitp = "efu", True
    else:
        u.reserved = True
    return u


class Pair:
    def __init__(self, pc, lo, up):
        self.pc, self.lo, self.up = pc, lo, up
        self.ibit = bool(up >> 31 & 1)
        self.ebit = bool(up >> 30 & 1)
        if up & 0x38000000:
            raise Unsupported("M/D/T bit at 0x%x" % pc)
        self.uu = upper_usage(up)
        self.lu = Usage() if self.ibit else lower_usage(lo)
        if self.uu.reserved or self.lu.reserved:
            raise Unsupported("reserved instruction at 0x%x" % pc)
        uw = self.uu.vf_write[0] if self.uu.vf_write else 0
        self.suppressed_lower = 0
        if uw and self.lu.vf_write and self.lu.vf_write[0] == uw:
            self.suppressed_lower = uw
        self.lname = "(imm)" if self.ibit else ("NOP" if lo in (0, LOWER_NOP) else vu1aot.lower_name(lo))
        self.uname = vu1aot.upper_name(up)
        hi = (lo >> 25) & 0x7F
        self.is_branch = (not self.ibit) and self.lu.pipe == "branch"
        self.branch_kind = self.lname if self.is_branch else None
        self.upper_flags = self.uname not in ("NOP",) and upper_writes_flags(up)
        self.upper_clip = self.uname == "CLIP"
        self.hi = hi


def upper_writes_flags(w):
    """True for the upper ops that go through applyFmacDest/applyFmacDestAcc (a MAC/status flag entry when dest != 0)."""
    op = w & 0x3F
    if (w >> 21) & 0xF == 0:
        return False
    if op <= 0x2F:
        return op not in (0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x1D, 0x1F, 0x2B, 0x2F)
    if op >= 0x3C:
        sp = (w & 3) | ((w >> 4) & 0x7C)
        return sp <= 0x0F or 0x18 <= sp <= 0x1C or sp == 0x1E or 0x20 <= sp <= 0x2A or 0x2C <= sp <= 0x2E
    return False


# ---- timing state ----
# Remaining cycles until each pending item lands (only items > 0 are kept). Tuples so states hash.
#   vf: ((reg, lane_bit, rem), ...), vi: ((reg, rem), ...), acc: ((lane_bit, rem), ...)
#   q: (rem, ...) pending FDIV results, p: (rem, ...) pending EFU results, efures: EFU busy rem
#   clip: (rem, ...) pending CLIP entries, mac: (rem, ...) pending MAC entries (tracked programs only)
#   flag: max rem of any flag-pipeline entry (for the flush length), bk: VI branch backup register (0 = none),
#   xg: an XGKICK may still be transferring
class St:
    __slots__ = ("vf", "vi", "acc", "q", "p", "efures", "clip", "mac", "flag", "bk", "xg")

    def __init__(self, **kw):
        self.vf = kw.get("vf", ())
        self.vi = kw.get("vi", ())
        self.acc = kw.get("acc", ())
        self.q = kw.get("q", ())
        self.p = kw.get("p", ())
        self.efures = kw.get("efures", 0)
        self.clip = kw.get("clip", ())
        self.mac = kw.get("mac", ())
        self.flag = kw.get("flag", 0)
        self.bk = kw.get("bk", 0)
        self.xg = kw.get("xg", False)

    def key(self):
        return (self.vf, self.vi, self.acc, self.q, self.p, self.efures, self.clip, self.mac, self.flag, self.bk,
                self.xg)

    def copy(self):
        return St(**{k: getattr(self, k) for k in St.__slots__})

    def decay(self, n):
        """n cycles pass: every remaining count drops by n; items at <= 0 have landed (committed)."""
        if n <= 0:
            return self
        s = self.copy()
        s.vf = tuple((r, l, x - n) for r, l, x in self.vf if x > n)
        s.vi = tuple((r, x - n) for r, x in self.vi if x > n)
        s.acc = tuple((l, x - n) for l, x in self.acc if x > n)
        s.q = tuple(x - n for x in self.q if x > n)
        s.p = tuple(x - n for x in self.p if x > n)
        s.efures = max(0, self.efures - n)
        s.clip = tuple(x - n for x in self.clip if x > n)
        s.mac = tuple(x - n for x in self.mac if x > n)
        s.flag = max(0, self.flag - n)
        return s

    def pending_max(self):
        return max([0] + [x for _, _, x in self.vf] + [x for _, x in self.vi] + [x for _, x in self.acc] +
                   list(self.q) + list(self.p) + list(self.clip) + list(self.mac) + [self.flag])


def vf_rem(st, reg, lanes):
    return max([0] + [x for r, l, x in st.vf if r == reg and (l & lanes)])


def ready_stall(st, p):
    """calculatePairReadyCycle - m_cycle (ps2_vu1_core.cpp:1017-1060)."""
    s = 0
    for u in (p.uu, p.lu):
        for reg, lanes in u.vf_read.items():
            s = max(s, vf_rem(st, reg, lanes))
        for reg, x in st.vi:
            if u.vi_read & (1 << reg) & 0xFFFE:
                s = max(s, x)
        for l, x in st.acc:
            if u.acc_read & l:
                s = max(s, x)
    if p.lu.pipe == "fdiv" and st.q:
        s = max(s, max(st.q))
    if p.lu.pipe == "efu":
        s = max(s, st.efures)
    if p.lu.waitp and st.p:
        s = max(s, max(st.p))
    # an XGKICK while an earlier transfer may still run waits for it (m_cycle + 1 while active): a data-dependent
    # stall, handled by emit_segment before this pair (kick_wait), not here
    return s


def set_rem(items, key_fn, new):
    """Replace the items whose key matches new's key (markPairWrites overwrites the ready cycle)."""
    k = key_fn(new)
    return tuple(i for i in items if key_fn(i) != k) + (new,)


def step(st, p, track_mac):
    """One pair: returns (stall, state at issue, state after the pair's cycle, info). info holds the k values the
    readers in this pair see (pending results not yet landed)."""
    s = ready_stall(st, p)
    a = st.decay(s)  # state at issue (commits during the stall have landed)
    info = {}
    n = a.copy()
    # upper executes first (execUpper), then lower (execLower): an upper CLIP/MAC entry is pending for the lower
    if p.upper_flags:
        n.flag = max(n.flag, FMAC_LATENCY)
        if track_mac:
            n.mac = n.mac + (FMAC_LATENCY,)
    if p.upper_clip:
        n.clip = n.clip + (FMAC_LATENCY,)
        n.flag = max(n.flag, FMAC_LATENCY)
    info["clip_k"] = len(n.clip)
    info["mac_k"] = len(n.mac)
    info["q_k"] = len(a.q)
    info["p_k"] = len(a.p)
    info["bk"] = a.bk
    info["xg"] = a.xg
    lu, uu = p.lu, p.uu
    if lu.pipe == "fdiv" and not lu.waitq:
        n.q = n.q + (lu.latency,)
    if lu.pipe == "efu" and not lu.waitp:
        if len(n.p) >= 2:
            raise Unsupported("third pending EFU result at 0x%x" % p.pc)
        n.p = n.p + (lu.latency,)
        n.efures = lu.latency - 1
    if p.lname == "FSSET":
        n.flag = max(n.flag, FMAC_LATENCY)
    if p.lname == "XGKICK":
        n.xg = True
    # markPairWrites
    if lu.vf_write and p.suppressed_lower != lu.vf_write[0]:
        reg, lanes = lu.vf_write
        lat = lu.vf_latency or lu.latency
        for c in range(4):
            if lanes & lane_bit(c):
                n.vf = set_rem(n.vf, lambda i: (i[0], i[1]), (reg, lane_bit(c), lat))
    if uu.vf_write:
        reg, lanes = uu.vf_write
        lat = uu.vf_latency or uu.latency
        for c in range(4):
            if lanes & lane_bit(c):
                n.vf = set_rem(n.vf, lambda i: (i[0], i[1]), (reg, lane_bit(c), lat))
    written_vi = 0
    for reg in range(1, 16):
        if lu.vi_write & (1 << reg):
            written_vi = reg
            break
    for reg in range(1, 16):
        if lu.vi_write & (1 << reg):
            n.vi = set_rem(n.vi, lambda i: i[0], (reg, lu.vi_latency or lu.latency))
    for c in range(4):
        if uu.acc_write & lane_bit(c):
            n.acc = set_rem(n.acc, lambda i: i[0], (lane_bit(c), 1))
    n.bk = written_vi if (written_vi and lu.delays_branch) else 0
    info["written_vi"] = written_vi
    after = n.decay(1)  # advanceOneCycle
    # an XGKICK can only be known finished when a run ends; keep the flag for the rest of the run
    return s, a, after, info


# ---- C++ emission ----
def hexf(x):
    return "0x%x" % x


def blend_imm(dest):
    """_mm_blend_ps immediate: bit i takes lane i (x = 0) from the second operand; dest bit 3 is x."""
    return sum(1 << c for c in range(4) if dest & lane_bit(c))


def dest_str(dest):
    return "".join(LANES[c] for c in range(4) if dest & lane_bit(c))


class NormState:
    """Which lanes of each VF register (1-31, 32 = ACC) are known to hold a normalised value (no denormal, Inf or
    NaN: what normalizeOperand would return unchanged), and which lanes of its shadow nX hold vu_norm(vX). Masks in
    component bits (bit 0 = x). A read that needs lanes neither known normal nor shadowed computes the shadow."""
    __slots__ = ("norm", "shadow")

    def __init__(self, norm=None, shadow=None):
        self.norm = list(norm) if norm else [0] * 33
        self.shadow = list(shadow) if shadow else [0] * 33

    def copy(self):
        return NormState(self.norm, self.shadow)

    def meet(self, o):
        return NormState([a & b for a, b in zip(self.norm, o.norm)], [a & b for a, b in zip(self.shadow, o.shadow)])

    def key(self):
        return (tuple(self.norm), tuple(self.shadow))

    def write(self, r, lanes, normal):
        if r == 0 or lanes == 0:
            return
        self.norm[r] = (self.norm[r] & ~lanes) | (lanes if normal else 0)
        self.shadow[r] &= ~lanes


def comp_mask(dest):
    return blend_imm(dest)


class Emitter:
    """Three passes: discover the (pc, timing state) nodes and their pairs and edges; dataflow over the node graph
    (normalisation state forward, MAC liveness backward); emit C++."""

    def __init__(self, name, image, pairs, track_mac):
        self.name = name
        self.image = image
        self.npairs = pairs
        self.track_mac = track_mac
        self.code = {}
        self.nodes = {}  # (pc, state key, kicked) -> node id
        self.node_pc = {}
        self.work = []
        self.segs = {}  # node id -> items: ("pair", p, info, off) | ("text", line)
        self.succ = {}  # node id -> [successor node ids]
        self.cur = None
        self.max_clip_k = self.max_mac_k = self.max_q_k = self.max_p_k = 0
        self.norm_reads = self.norm_skipped = 0
        self.mac_needed = set()  # (node id, item index) of FMACs whose MAC flags can reach an FMAND
        self.mac_producers = 0
        for pc in range(0, pairs * 8, 8):
            lo, up = struct.unpack_from("<II", image, pc)
            self.code[pc] = Pair(pc, lo, up)
        self.find_labels()

    def pair(self, pc):
        if pc not in self.code:
            raise Unsupported("execution reaches 0x%x, past the program (%d pairs)" % (pc, self.npairs))
        return self.code[pc]

    def find_labels(self):
        self.entries = [0] + [pc + 16 for pc, p in sorted(self.code.items()) if p.ebit and pc + 16 < self.npairs * 8]
        self.bal_returns = sorted(pc + 16 for pc, p in self.code.items() if p.branch_kind in ("BAL", "JALR"))
        lab = set(self.entries) | set(self.bal_returns)
        for pc, p in self.code.items():
            if p.is_branch and p.branch_kind not in ("JR", "JALR"):
                lab.add(self.target(p))
            if p.is_branch:
                d = self.code.get(pc + 8)
                if d is None or d.is_branch:
                    raise Unsupported("branch at 0x%x has a branch (or nothing) in its delay slot" % pc)
                if d.ebit:
                    raise Unsupported("E bit in the delay slot of the branch at 0x%x" % pc)
            if p.ebit:
                d = self.code.get(pc + 8)
                if d is None or d.is_branch or d.ebit:
                    raise Unsupported("E bit at 0x%x followed by a branch, E bit or nothing" % pc)
            if p.lname in ("FCSET", "FSEQ", "FSAND", "FSOR", "RINIT", "RNEXT", "RGET", "RXOR", "FMEQ", "FMOR"):
                raise Unsupported("%s at 0x%x not supported" % (p.lname, pc))
            if not p.ibit and (p.up & 0x3F) <= 0x2F and ((p.up >> 6) & 0x1F) == 0 and ((p.up >> 21) & 0xF) and \
                    p.uname not in ("NOP",) and 0 in p.lu.vf_read:
                raise Unsupported("upper writes vf0 and lower reads it at 0x%x" % pc)
        self.label_pcs = lab

    def target(self, p):
        imm = p.lo & 0x7FF
        if imm & 0x400:
            imm -= 0x800
        return (p.pc + 8 + imm * 8) & 0x3FFF

    def node(self, pc, st, kicked=False):
        """kicked: the node starts at an XGKICK whose wait for the previous transfer was already taken. Records the
        edge from the node being discovered."""
        k = (pc, st.key(), kicked)
        if k not in self.nodes:
            self.nodes[k] = len(self.nodes)
            self.node_pc[self.nodes[k]] = pc
            self.work.append((pc, st, self.nodes[k], kicked))
        nid = self.nodes[k]
        if self.cur is not None:
            self.succ[self.cur].append(nid)
        return "N%d" % nid

    # ---- pass 1: discovery ----
    def discover(self):
        empty = St()
        for e in self.entries:
            self.node(e, empty)
        while self.work:
            pc, st, nid, kicked = self.work.pop(0)
            self.cur = nid
            self.segs[nid] = []
            self.succ[nid] = []
            self.segment(pc, st, nid, kicked)
            self.cur = None
            if len(self.nodes) > 20000:
                raise Unsupported("more than 20000 nodes")

    def text(self, line):
        self.segs[self.cur].append(("text", line))

    def add_pair(self, p, info, off):
        self.max_clip_k = max(self.max_clip_k, info["clip_k"])
        self.max_mac_k = max(self.max_mac_k, info["mac_k"])
        self.max_q_k = max(self.max_q_k, info["q_k"])
        self.max_p_k = max(self.max_p_k, info["p_k"])
        self.segs[self.cur].append(("pair", p, info, off))

    # one segment: from a node to its exits (goto another node or return)
    def segment(self, pc, st, nid, kicked=False):
        self.text("N%d: // 0x%04x" % (nid, pc))
        off = 0  # cycles since the segment started
        first = True
        while True:
            p = self.pair(pc)
            if not first and pc in self.label_pcs:
                self.text("    cyc += %d; goto %s;" % (off, self.node(pc, st)))
                return
            if p.lname == "XGKICK" and st.xg and not (first and kicked):
                self.kick_wait(pc, st, off)
                return
            first = False
            if p.is_branch:
                s, a, st2, info = step(st, p, self.track_mac)
                off += s
                self.add_pair(p, info, off)
                off += 1
                d = self.delay_pair(pc + 8, st2)
                s2, a2, st3, info2 = step(st2, d, self.track_mac)
                off += s2
                self.add_pair(d, info2, off)
                off += 1
                k = p.branch_kind
                if k in ("B", "BAL"):
                    self.text("    cyc += %d; goto %s;" % (off, self.node(self.target(p), st3)))
                elif k in ("JR", "JALR"):
                    self.text("    cyc += %d;" % off)
                    self.text("    switch (jt) {")
                    for r in self.bal_returns:
                        self.text("    case 0x%x: goto %s;" % (r, self.node(r, st3)))
                    self.text("    default: c.pc = jt; c.error = 2; goto Lout;")
                    self.text("    }")
                else:
                    t = self.node(self.target(p), st3)
                    self.text("    cyc += %d; if (bt) goto %s; goto %s;" % (off, t, self.node(pc + 16, st3)))
                return
            s, a, st2, info = step(st, p, self.track_mac)
            off += s
            self.add_pair(p, info, off)
            off += 1
            st = st2
            if p.ebit:
                d = self.delay_pair(pc + 8, st)
                s2, a2, st3, info2 = step(st, d, self.track_mac)
                off += s2
                self.add_pair(d, info2, off)
                off += 1
                flush = st3.pending_max()
                self.text("    cyc += %d; c.pc = 0x%x; goto Lend;" % (off + flush, (pc + 16) & 0x3FFF))
                return
            pc = (pc + 8) & 0x3FFF

    def delay_pair(self, pc, st):
        """The pair after a branch or an E bit: emitted inline, so it cannot hold a kick that may have to wait."""
        d = self.pair(pc)
        if d.lname == "XGKICK" and st.xg:
            raise Unsupported("XGKICK in a delay slot at 0x%x while an earlier XGKICK may still transfer" % pc)
        return d

    def kick_wait(self, pc, st, off):
        """XGKICK while an earlier transfer may still run: the pair issues at max(its static ready cycle, the cycle
        the transfer ends) (calculatePairReadyCycle :1057, finishXgkick). The transfer is completed now (no store
        can happen while the pair waits), the wait d is known, and the pipeline state is the state decayed by d:
        one node per distinct decayed state, d clamped at the point where nothing is left pending."""
        top = st.pending_max()
        self.text("    { // 0x%04x XGKICK waits for the previous PATH1 transfer" % pc)
        self.text("        const uint64_t kb = cyc + %d, ke = vu_xg_drain(c, xg);" % off)
        self.text("        const uint64_t kd = ke > kb ? ke - kb : 0;")
        self.text("        cyc += %d + kd;" % off)
        if top == 0:
            self.text("        goto %s;" % self.node(pc, st, True))
        else:
            self.text("        switch (kd >= %d ? %d : (int)kd) {" % (top, top))
            for d in range(top + 1):
                self.text("        case %d: goto %s;" % (d, self.node(pc, st.decay(d), True)))
            self.text("        }")
            self.text("        goto Lout;")
        self.text("    }")

    # ---- pass 2a: normalisation state, forward (meet = AND over predecessors) ----
    def norm_dataflow(self):
        self.entry_ns = {}
        for e in self.entries:
            self.entry_ns[self.nodes[(e, St().key(), False)]] = NormState()
        work = sorted(self.entry_ns)
        while work:
            nid = work.pop(0)
            ns = self.entry_ns[nid].copy()
            for idx, it in enumerate(self.segs[nid]):
                if it[0] == "pair":
                    self.gen_pair(nid, idx, it[1], it[2], it[3], ns, dry=True)
            for s in set(self.succ[nid]):
                old = self.entry_ns.get(s)
                new = ns if old is None else old.meet(ns)
                if old is None or new.key() != old.key():
                    self.entry_ns[s] = new.copy()
                    if s not in work:
                        work.append(s)

    # ---- pass 2b: MAC liveness, backward: which FMACs' flags can an FMAND read (history depth k) ----
    def mac_liveness(self):
        if not self.track_mac:
            return
        preds = {n: set() for n in self.segs}
        for n, ss in self.succ.items():
            for s in ss:
                preds[s].add(n)
        need_in = {n: frozenset() for n in self.segs}
        work = list(self.segs)
        while work:
            nid = work.pop()
            need = set()
            for s in self.succ[nid]:
                need |= need_in[s]
            for idx in range(len(self.segs[nid]) - 1, -1, -1):
                it = self.segs[nid][idx]
                if it[0] != "pair":
                    continue
                p, info = it[1], it[2]
                if p.lname == "FMAND":  # the lower reads after the upper of its own pair pushed
                    need.add(info["mac_k"])
                if p.upper_flags:
                    if 0 in need:
                        self.mac_needed.add((nid, idx))
                    need = {d - 1 for d in need if d >= 1}
            fz = frozenset(need)
            if fz != need_in[nid]:
                need_in[nid] = fz
                for q in preds[nid]:
                    if q not in work:
                        work.append(q)

    # ---- pass 3: emission ----
    def emit(self):
        self.discover()
        self.norm_dataflow()
        self.mac_liveness()
        out = []
        for nid in sorted(self.segs):
            if nid not in self.entry_ns:
                continue  # unreachable (no path from an entry): cannot happen, every node is discovered from one
            ns = self.entry_ns[nid].copy()
            for idx, it in enumerate(self.segs[nid]):
                if it[0] == "text":
                    out.append(it[1])
                else:
                    out.extend(self.gen_pair(nid, idx, it[1], it[2], it[3], ns, dry=False))
        self.out = out
        return out

    # ---- per pair ----
    def gen_pair(self, nid, idx, p, info, off, ns, dry):
        self.ns = ns
        self.dry = dry
        self.mac_wanted = (nid, idx) in self.mac_needed
        o = []
        o.append("    { // 0x%04x %s%s | %s%s" % (p.pc, p.uname, "." + dest_str((p.up >> 21) & 0xF)
                                               if (p.up >> 21) & 0xF and p.uname != "NOP" else "",
                                               p.lname, " [E]" if p.ebit else ""))
        self.pending_upper = None
        body = []
        # sources first (every op of the pair reads the old registers)
        up = self.upper_code(p, info, body)
        low = [] if p.ibit else self.lower_code(p, info, body, off)
        o.extend("        " + l for l in body)
        o.extend("        " + l for l in up)
        o.extend("        " + l for l in low)
        if p.ibit:
            wr = []
            self.write_upper(p, wr)
            o.extend("        " + l for l in wr)
            o.append("        I = vu_norm1(vu_bits(0x%08xu));" % p.lo)
        o.append("    }")
        return o

    def vf(self, r):
        return "K_VF0" if r == 0 else "v%d" % r

    def nvf(self, r, lanes, body):
        """A read of `lanes` (component bits) of VF r (32 = ACC) with normalizeOperand applied: the register itself
        where those lanes are known normal, else its shadow, computed here if stale."""
        if r == 0:
            return "K_VF0"
        raw, sh = ("acc", "nacc") if r == 32 else ("v%d" % r, "n%d" % r)
        if not self.dry:
            self.norm_reads += 1
        if (self.ns.norm[r] & lanes) == lanes:
            if not self.dry:
                self.norm_skipped += 1
            return raw
        if (self.ns.shadow[r] & lanes) != lanes:
            body.append("%s = vu_norm(%s);" % (sh, raw))
            self.ns.shadow[r] = 0xF
        elif not self.dry:
            self.norm_skipped += 1
        return sh

    def upper_code(self, p, info, body):
        w = p.up
        op = w & 0x3F
        f = fld(w)
        dest, fs, ft, fd = f["dest"], f["fs"], f["ft"], f["fd"]
        out = []
        if p.uname == "NOP" or dest == 0 and p.uname != "CLIP":
            # dest == 0: no lane written, no flag entry (updateFmacFlags returns), nothing else happens
            return out
        sp = (w & 3) | ((w >> 4) & 0x7C) if op >= 0x3C else None
        to_acc = False
        dl = comp_mask(dest)
        bcl = 1 << ((op if sp is None else sp) & 3)
        code = op if sp is None else sp
        if sp is None:
            table = {}
            for i in range(4):
                table[0x00 + i] = ("add", "bc")
                table[0x04 + i] = ("sub", "bc")
                table[0x08 + i] = ("madd", "bc")
                table[0x0C + i] = ("msub", "bc")
                table[0x10 + i] = ("max", "bc")
                table[0x14 + i] = ("mini", "bc")
                table[0x18 + i] = ("mul", "bc")
            table.update({0x1C: ("mul", "q"), 0x1D: ("max", "i"), 0x1E: ("mul", "i"), 0x1F: ("mini", "i"),
                          0x20: ("add", "q"), 0x21: ("madd", "q"), 0x22: ("add", "i"), 0x23: ("madd", "i"),
                          0x24: ("sub", "q"), 0x25: ("msub", "q"), 0x26: ("sub", "i"), 0x27: ("msub", "i"),
                          0x28: ("add", "v"), 0x29: ("madd", "v"), 0x2A: ("mul", "v"), 0x2B: ("max", "v"),
                          0x2C: ("sub", "v"), 0x2D: ("msub", "v"), 0x2E: ("opmsub", "v"), 0x2F: ("mini", "v")})
            kind, src = table[code]
            dst = fd
        else:
            table = {}
            for i in range(4):
                table[0x00 + i] = ("add", "bc")
                table[0x04 + i] = ("sub", "bc")
                table[0x08 + i] = ("madd", "bc")
                table[0x0C + i] = ("msub", "bc")
                table[0x18 + i] = ("mul", "bc")
            table.update({0x1C: ("mul", "q"), 0x1E: ("mul", "i"), 0x20: ("add", "q"), 0x21: ("madd", "q"),
                          0x22: ("add", "i"), 0x23: ("madd", "i"), 0x24: ("sub", "q"), 0x25: ("msub", "q"),
                          0x26: ("sub", "i"), 0x27: ("msub", "i"), 0x28: ("add", "v"), 0x29: ("madd", "v"),
                          0x2A: ("mul", "v"), 0x2C: ("sub", "v"), 0x2D: ("msub", "v"), 0x2E: ("opmula", "v")})
            if code in table:
                kind, src = table[code]
                to_acc = True
                dst = None
            elif 0x10 <= code <= 0x13:
                kind, src, dst = "itof%d" % (0, 4, 12, 15)[code - 0x10], None, ft
            elif 0x14 <= code <= 0x17:
                kind, src, dst = "ftoi%d" % (0, 4, 12, 15)[code - 0x14], None, ft
            elif code == 0x1D:
                kind, src, dst = "abs", None, ft
            elif code == 0x1F:
                kind, src, dst = "clip", None, None
            else:
                return out  # NOP
        if kind == "clip":
            out.append("const uint32_t cf = vu_clip(%s, %s);" % (self.vf(fs), self.vf(ft)))
            out.append("ch4 = ch3; ch3 = ch2; ch2 = ch1; ch1 = ch0; ch0 = ((ch0 << 6) | cf) & 0xFFFFFFu;")
            return out
        cross = kind in ("opmsub", "opmula")
        # lanes read: dest lanes, or for the cross products x<-y,z / y<-z,x / z<-x,y
        if cross:
            dxyz = dl & 7
            fs_l = sum(1 << {0: 1, 1: 2, 2: 0}[c] for c in range(3) if dxyz & (1 << c))
            ft_l = sum(1 << {0: 2, 1: 0, 2: 1}[c] for c in range(3) if dxyz & (1 << c))
        else:
            fs_l, ft_l = dl, (bcl if src == "bc" else dl)

        def bsrc(s):
            if s == "bc":
                return "vu_bc%d(%s)" % ((op if sp is None else sp) & 3, self.nvf(ft, ft_l, body))
            if s == "q":
                return "_mm_set1_ps(vu_norm1(qh%d))" % info["q_k"]
            if s == "i":
                return "_mm_set1_ps(vu_norm1(I))"
            return self.nvf(ft, ft_l, body)
        a = self.nvf(fs, fs_l, body) if not kind.startswith("itof") else self.vf(fs)
        mac = self.track_mac and p.upper_flags and self.mac_wanted
        if kind in ("add", "sub", "mul"):
            b = bsrc(src)
            out.append("const __m128 ua = %s, ub = %s;" % (a, b))
            r = "vu_%s(ua, ub)" % kind
        elif kind in ("madd", "msub"):
            b = bsrc(src)
            nacc = self.nvf(32, dl, body)
            out.append("const __m128 ua = %s, ub = %s, uc = %s;" % (a, b, nacc))
            r = "vu_%s<%d>(uc, ua, ub)" % (kind, dest)
        elif kind == "opmsub":
            b = bsrc(src)
            nacc = self.nvf(32, dl & 7, body)
            out.append("const __m128 ua = vu_yzx(%s), ub = vu_zxy(%s), uc = %s;" % (a, b, nacc))
            r = "vu_opmsub<%d>(uc, ua, ub)" % dest
        elif kind == "opmula":
            b = bsrc(src)
            out.append("const __m128 ua = vu_yzx(%s), ub = vu_zxy(%s);" % (a, b))
            r = "vu_opmula(ua, ub)"
        elif kind in ("max", "mini"):
            r = "_mm_%s_ps(%s, %s)" % ("max" if kind == "max" else "min", a, bsrc(src))
        elif kind.startswith("itof") or kind.startswith("ftoi"):
            r = "vu_%s(%s)" % (kind, a)
        elif kind == "abs":
            r = "vu_abs(%s)" % a
        out.append("const __m128 ur = %s;" % r)
        if self.track_mac and p.upper_flags:
            if mac:
                out.append("const uint32_t um = vu_mac_%s<%d>(%s);" % (kind, dest,
                           "ua, ub" if kind in ("add", "sub", "mul", "opmula") else "uc, ua, ub"))
            else:
                out.append("const uint32_t um = 0; // no FMAND reads these flags")
            out.append("mh4 = mh3; mh3 = mh2; mh2 = mh1; mh1 = mh0; mh0 = um;")
        # the write itself happens after the lower op has read its sources (pair semantics); FTOI writes integers
        self.pending_upper = (to_acc, dst, dest, not kind.startswith("ftoi"))
        return out

    def write_upper(self, p, out):
        if not self.pending_upper:
            return
        to_acc, dst, dest, normal = self.pending_upper
        self.pending_upper = None
        if to_acc:
            out.append("acc = %s;" % ("ur" if dest == 0xF else "_mm_blend_ps(acc, ur, %d)" % blend_imm(dest)))
            self.ns.write(32, comp_mask(dest), True)
        elif dst:
            out.append("v%d = %s;" % (dst, "ur" if dest == 0xF else "_mm_blend_ps(v%d, ur, %d)" % (dst, blend_imm(dest))))
            self.ns.write(dst, comp_mask(dest), normal)

    # lower ops: compute from the old registers, then write the upper result, then the lower's own writes
    def lower_code(self, p, info, body, off):
        out = []
        w = p.lo
        f = fld(w)
        dest, vft, vfs, vit, vis, vid = f["dest"], f["ft"], f["fs"], f["it"], f["is_"], f["id_"]
        n = p.lname
        late = []  # lower writes, after the upper write
        late_ns = []  # their effect on the normalisation state, applied after the upper write's

        def vi(r):
            return "0" if r == 0 else "i%d" % r

        def wvi(r, expr):
            if r:
                late.append("i%d = (int16_t)(%s);" % (r, expr))

        def wvf(r, expr, lanes, norm_lanes=0):
            """norm_lanes: the written lanes (component bits) known normal (MOVE/MR32 carry their source's)."""
            if r and p.suppressed_lower != r and lanes:
                late.append("v%d = %s;" % (r, expr if lanes == 0xF else "_mm_blend_ps(v%d, %s, %d)" %
                                           (r, expr, blend_imm(lanes))))
                cl = comp_mask(lanes)
                late_ns.append((r, cl, norm_lanes & cl))
        imm11 = w & 0x7FF
        if imm11 & 0x400:
            imm11 -= 0x800

        def store_guard():
            if info["xg"]:
                out.append("if (xg.active) vu_xg_progress(c, xg, cyc + %d);" % off)
        if n == "NOP":
            pass
        elif n == "LQ":
            out.append("const __m128 lv = vu_ld(vu_data, ((%s + %d) & 0x3FF));" % (vi(vis), imm11))
            wvf(vft, "lv", dest)
        elif n == "SQ":
            store_guard()
            out.append("vu_st<%d>(vu_data, ((%s + %d) & 0x3FF), %s);" % (dest, vi(vit), imm11, self.vf(vfs)))
        elif n == "LQI":
            out.append("const __m128 lv = vu_ld(vu_data, (uint16_t)%s & 0x3FF);" % vi(vis))
            wvf(vft, "lv", dest)
            wvi(vis, "%s + 1" % vi(vis))
        elif n == "LQD":
            out.append("const int32_t la = (int16_t)(%s - 1);" % vi(vis) if vis else "const int32_t la = 0;")
            out.append("const __m128 lv = vu_ld(vu_data, (uint16_t)la & 0x3FF);")
            wvf(vft, "lv", dest)
            wvi(vis, "la")
        elif n == "SQI":
            store_guard()
            out.append("vu_st<%d>(vu_data, (uint16_t)%s & 0x3FF, %s);" % (dest, vi(vit), self.vf(vfs)))
            wvi(vit, "%s + 1" % vi(vit))
        elif n == "SQD":
            store_guard()
            out.append("const int32_t sa = (int16_t)(%s - 1);" % vi(vit) if vit else "const int32_t sa = 0;")
            out.append("vu_st<%d>(vu_data, (uint16_t)sa & 0x3FF, %s);" % (dest, self.vf(vfs)))
            wvi(vit, "sa")
        elif n in ("ILW", "ILWR"):
            comp = 0 if dest & 8 else 1 if dest & 4 else 2 if dest & 2 else 3
            addr = "((%s + %d) & 0x3FF)" % (vi(vis), imm11) if n == "ILW" else "((uint16_t)%s & 0x3FF)" % vi(vis)
            out.append("const int32_t lw = vu_ldw(vu_data, %s, %d);" % (addr, comp))
            wvi(vit, "lw")
        elif n in ("ISW", "ISWR"):
            store_guard()
            addr = "((%s + %d) & 0x3FF)" % (vi(vis), imm11) if n == "ISW" else "((uint16_t)%s & 0x3FF)" % vi(vis)
            out.append("vu_stw<%d>(vu_data, %s, %s);" % (dest, addr, vi(vit)))
        elif n in ("IADDIU", "ISUBIU"):
            imm15 = (w & 0x7FF) | ((w >> 10) & 0x7800)
            wvi(vit, "%s %s %d" % (vi(vis), "+" if n == "IADDIU" else "-", imm15))
        elif n in ("IADD", "ISUB", "IAND", "IOR"):
            opc = {"IADD": "+", "ISUB": "-", "IAND": "&", "IOR": "|"}[n]
            wvi(vid, "%s %s %s" % (vi(vis), opc, vi(vit)))
        elif n == "IADDI":
            imm5 = (w >> 6) & 0x1F
            if imm5 & 0x10:
                imm5 -= 0x20
            wvi(vit, "%s + %d" % (vi(vis), imm5))
        elif n in ("FCAND", "FCOR", "FCEQ"):
            imm = w & 0xFFFFFF
            cl = "ch%d" % info["clip_k"]
            e = {"FCAND": "(%s & 0x%xu) != 0" % (cl, imm), "FCOR": "(%s | 0x%xu) == 0xFFFFFFu" % (cl, imm),
                 "FCEQ": "(%s & 0xFFFFFFu) == 0x%xu" % (cl, imm)}[n]
            wvi(1, "(%s) ? 1 : 0" % e)
        elif n == "FCGET":
            wvi(vit, "ch%d & 0xFFF" % info["clip_k"])
        elif n == "FMAND":
            if not self.track_mac:
                raise Unsupported("FMAND without MAC tracking")
            wvi(vit, "mh%d & (uint16_t)%s" % (info["mac_k"], vi(vis)))
        elif n == "FSSET":
            pass  # status sticky bits: no reader in the program, status left at its input value
        elif n in ("B", "BAL"):
            if n == "BAL":
                wvi(vit, "%d" % ((p.pc + 16) // 8))
        elif n in ("JR", "JALR"):
            out.append("jt = ((uint32_t)(uint16_t)%s * 8u) & 0x3FFFu;" % self.bvi(vis, info))
            if n == "JALR":
                wvi(vit, "%d" % ((p.pc + 16) // 8))
        elif n in ("IBEQ", "IBNE"):
            out.append("bt = (int16_t)%s %s (int16_t)%s;" % (self.bvi(vis, info), "==" if n == "IBEQ" else "!=",
                                                            self.bvi(vit, info)))
        elif n in ("IBLTZ", "IBGTZ", "IBLEZ", "IBGEZ"):
            opc = {"IBLTZ": "<", "IBGTZ": ">", "IBLEZ": "<=", "IBGEZ": ">="}[n]
            out.append("bt = (int16_t)%s %s 0;" % (self.bvi(vis, info), opc))
        elif n == "MOVE":
            wvf(vft, self.vf(vfs), dest, 0xF if vfs == 0 else self.ns.norm[vfs])
        elif n == "MR32":
            out.append("const __m128 mr = vu_mr32(%s);" % self.vf(vfs))
            src = 0xF if vfs == 0 else self.ns.norm[vfs]
            wvf(vft, "mr", dest, sum(1 << c for c in range(4) if src & (1 << ((c + 1) & 3))))
        elif n == "MTIR":
            comp = (w >> 21) & 3
            wvi(vit, "vu_lanebits(%s, %d) & 0xFFFF" % (self.vf(vfs), comp))
        elif n == "MFIR":
            out.append("const __m128 mf = _mm_castsi128_ps(_mm_set1_epi32((int16_t)%s));" % vi(vis))
            wvf(vft, "mf", dest)
        elif n == "MFP":
            out.append("const __m128 mp = _mm_set1_ps(ph%d);" % info["p_k"])
            wvf(vft, "mp", dest)
        elif n in ("XTOP", "XITOP"):
            wvi(vit, "c.%s & 0x3FF" % ("top" if n == "XTOP" else "itop"))
        elif n == "XGKICK":
            out.append("vu_xg_start(c, xg, (uint16_t)%s, cyc + %d);" % (vi(vis), off))
        elif n in ("DIV", "SQRT", "RSQRT"):
            fsf, ftf = (w >> 21) & 3, (w >> 23) & 3
            if n == "DIV":
                e = "vu_div(%s, %s)" % (self.lane(vfs, fsf), self.lane(vft, ftf))
            elif n == "SQRT":
                e = "vu_sqrt(%s)" % self.lane(vft, ftf)
            else:
                e = "vu_rsqrt(%s, %s)" % (self.lane(vfs, fsf), self.lane(vft, ftf))
            out.append("const float qr = %s;" % e)
            late.append("qh1 = qh0; qh0 = qr;")
        elif n in ("WAITQ", "WAITP"):
            pass
        elif n in ("ESADD", "ERSADD", "ELENG", "ERLENG", "ESUM", "ESQRT", "ERSQRT", "ERCPR", "ESIN", "EATAN", "EEXP",
                   "EATANxy", "EATANxz"):
            comp = (w >> 21) & 3
            if n in ("ESADD", "ERSADD", "ELENG", "ERLENG", "ESUM", "EATANxy", "EATANxz"):
                e = "vu_%s(%s)" % (n.lower(), self.vf(vfs))
            else:
                e = "vu_%s(%s)" % (n.lower(), self.lane(vfs, comp))
            out.append("const float pr = %s;" % e)
            late.append("ph2 = ph1; ph1 = ph0; ph0 = pr;")
        else:
            raise Unsupported("lower %s at 0x%x not emitted" % (n, p.pc))
        self.write_upper(p, out)
        out.extend(late)
        for r, cl, nl in late_ns:
            self.ns.norm[r] = (self.ns.norm[r] & ~cl) | nl
            self.ns.shadow[r] &= ~cl
        return out

    def lane(self, r, comp):
        return "vu_lane(%s, %d)" % (self.vf(r), comp)

    def bvi(self, r, info):
        """readBranchVi: the VI value a branch sees (the old value if the previous pair wrote it, ps2_vu1_core.cpp:146)."""
        if r == 0:
            return "0"
        if info["bk"] == r:
            return "bkv"
        return "i%d" % r


def gen_program(name, image, pairs, track_mac):
    em = Emitter(name, image, pairs, track_mac)
    em.emit()
    return em


HEADER = """// Generated by tools/vu1aot/emit.py from the game's VU1 microcode: game-derived, never in git.
#include "vu1aot_rt.h"
"""


def function_text(em, fname):
    body = []
    body.append("int %s(Vu1AotCtx &c)" % fname)
    body.append("{")
    body.append("    VU1AOT_PROLOGUE")
    body.append("    switch (c.pc) {")
    empty_key = St().key()
    for e in em.entries:
        body.append("    case 0x%x: goto N%d;" % (e, em.nodes[(e, empty_key, False)]))
    body.append("    default: c.error = 1; goto Lout;")
    body.append("    }")
    for l in em.out:
        # VI branch backup: save the old value before a VI write that a branch in the next pair may read
        body.append(l)
    body.append("Lend:")
    body.append("    VU1AOT_EPILOGUE")
    body.append("Lout:")
    body.append("    VU1AOT_STORE")
    body.append("    return c.error;")
    body.append("}")
    return body


def add_backup(lines):
    """Insert 'bkv = iN;' before every 'iN = (int16_t)(...)' write. The backup is read only by a branch in the next
    pair (bvi returns bkv when the state says the previous pair wrote that register); dead saves are removed by the
    compiler."""
    out = []
    for l in lines:
        s = l.strip()
        if s.startswith("i") and " = (int16_t)(" in s and s[1:s.index(" ")].isdigit():
            reg = s[:s.index(" ")]
            out.append(l[:len(l) - len(l.lstrip())] + "bkv = %s;" % reg)
        out.append(l)
    return out


def write_if_changed(path, text):
    """Rewrite a file only when its content changes (an unchanged program keeps its object file)."""
    if os.path.exists(path):
        with open(path, encoding="utf-8", newline="") as fh:
            if fh.read() == text:
                return False
    with open(path, "w", encoding="utf-8", newline="") as fh:
        fh.write(text)
    return True


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--elf", default="game_files/SLPM_654.28")
    ap.add_argument("--captures", default="C:/ob/vu1-captures")
    ap.add_argument("--out", default="C:/ob/vu1aot/gen")
    ap.add_argument("--programs", default="captured")
    a = ap.parse_args(argv)
    elf = vu1aot.Elf(a.elf)
    progs = vu1aot.extract(elf)
    images = set()
    for f in sorted(glob.glob(os.path.join(a.captures, "*.cap"))):
        with open(f, "rb") as fh:
            fh.seek(vu1aot.CAP_CODE_OFFSET)
            images.add(fh.read(vu1aot.CODE_SIZE))
    os.makedirs(a.out, exist_ok=True)
    table = []
    report = []
    for rec, image, pairs in progs:
        name = "D_%08X" % rec
        captured = any(img[:pairs * 8] == image[:pairs * 8] for img in images)
        if a.programs == "captured" and not captured:
            continue
        if a.programs not in ("captured", "all") and name not in a.programs.split(","):
            continue
        has_fmand = any(vu1aot.lower_name(struct.unpack_from("<I", image, pc)[0]) in ("FMAND",) and
                        not (struct.unpack_from("<I", image, pc + 4)[0] >> 31)
                        for pc in range(0, pairs * 8, 8))
        try:
            em = gen_program(name, image, pairs, has_fmand)
        except Unsupported as e:
            report.append("%s: NOT GENERATED: %s" % (name, e))
            continue
        fname = "vu1aot_" + name
        text = HEADER + "\n".join(add_backup(function_text(em, fname))) + "\n"
        write_if_changed(os.path.join(a.out, name + ".cpp"), text)
        table.append((name, fname, pairs, image[:pairs * 8]))
        report.append("%s: %d pairs, %d nodes (%.2f per pair), entries %s, max pending k: clip %d mac %d q %d p %d, "
                      "normalised reads %d of which %d skipped (known normal or shadowed)%s"
                      % (name, pairs, len(em.nodes), len(em.nodes) / pairs, [hex(e) for e in em.entries],
                         em.max_clip_k, em.max_mac_k, em.max_q_k, em.max_p_k, em.norm_reads, em.norm_skipped,
                         ", MAC tracked: %d of %d FMACs' flags read" % (len(em.mac_needed), sum(
                             1 for sg in em.segs.values() for it in sg if it[0] == "pair" and it[1].upper_flags))
                         if has_fmand else ""))
    # table: program bytes for dispatch (compare by content, not by a 16 KB hash)
    t = [HEADER]
    for name, fname, pairs, code in table:
        t.append("int %s(Vu1AotCtx &c);" % fname)
        t.append("static const uint8_t k%s[%d] = {%s};" % (name, len(code), ",".join(str(b) for b in code)))
    t.append("const Vu1AotProgram kVu1AotPrograms[] = {")
    for name, fname, pairs, code in table:
        t.append("    {\"%s\", %d, k%s, %s}," % (name, pairs, name, fname))
    t.append("};")
    t.append("const int kVu1AotProgramCount = %d;" % len(table))
    write_if_changed(os.path.join(a.out, "vu1aot_table.cpp"), "\n".join(t) + "\n")
    sys.stdout.write("\n".join(report) + "\n")
    return 0 if all("NOT GENERATED" not in r for r in report) else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
