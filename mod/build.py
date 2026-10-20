#!/usr/bin/env python3
"""Build the standalone mod into a patch file ARMSX2 (or PCSX2) loads by itself: no PC.

  python mod/build.py            compile + link + check -> mod/build/mod.bin, and with
                                 mod/sites.json present, patches/SLPM-65428_32088394.pnach

Needs clang and ld.lld (LLVM 15+; the container that develops this has them). The PS2's CPU (the
EE, an R5900) runs MIPS II code, with a single-precision FPU that lacks some standard
instructions; `ee_fixups` rewrites the ones clang emits that have an exact EE equivalent and
refuses the rest, so nothing the EE can't run reaches the patch.

The patch (see `pnach`): only while SUBMAIN (the gameplay overlay) is loaded -- checked by the
`jal 0x0018F0E0` the overlay has at the anchor address -- it writes our code to MOD_BASE, then
turns each call site's `jal 0x00345740` into `jal mod_entry`. Every value it compares or writes
is our own code or an encoding computed from an address (0x0C000000 | target >> 2): no game code.
ARMSX2 applies patch lines every frame but only writes what differs, so this costs nothing once
in place, and puts itself back when the game reloads the overlay.
"""
import json
import os
import shutil
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
BUILD = os.path.join(HERE, "build")
SITES = os.path.join(HERE, "sites.json")
PNACH = os.path.join(ROOT, "patches", "SLPM-65428_32088394.pnach")
MOD_BASE = 0x000A0000
HOOKED = 0x00345740
ANCHOR_CALL = 0x0018F0E0
CFLAGS = ["--target=mipsel-unknown-none-elf", "-march=mips2", "-mabi=32", "-mhard-float", "-msingle-float",
          "-mno-abicalls", "-fno-pic", "-G0", "-O2", "-ffreestanding", "-fno-builtin", "-nostdlib",
          "-mno-check-zero-division", "-ffast-math", "-fno-strict-aliasing", "-Wall", "-Werror",
          "-Wdouble-promotion"]


def jal(target):
    return 0x0C000000 | (target >> 2)


# ------------------------------------------------------------------ what the EE can run
COP1, FMT_S, FMT_W = 0x11, 0x10, 0x14
S_OK = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x24, 0x30, 0x32, 0x34, 0x36}
TRUNC_W, CVT_W, SQRT = 0x0D, 0x24, 0x04
BAD_OPS = {0x13: "COP1X", 0x30: "ll", 0x35: "ldc1", 0x38: "sc", 0x3D: "sdc1", 0x1C: "SPECIAL2",
           0x1F: "SPECIAL3", 0x37: "ld", 0x3F: "sd", 0x1A: "ldl", 0x1B: "ldr", 0x2C: "sdl", 0x2D: "sdr"}


def ee_fixups(words, base, code_words=None):
    """Rewrite clang's FPU instructions for the EE; returns (words, notes). Raises on anything the
    EE can't run. sqrt.s: the EE takes its operand from the ft field (pcsx2/FPU.cpp SQRT_S reads
    _Ft_), standard MIPS from fs with ft = 0 -- so clang's sqrt.s computed sqrt($f0) on the device
    and the aim's direction came out the wrong length (device log, 2026-10-20). Moved to ft. Only the first `code_words` are code (the rest: constants and strings). The EE FPU has no NaN or infinity, so ordered and unordered compares agree, and
    its cvt.w.s always truncates (= trunc.w.s)."""
    out, notes = [], []
    for i, w in enumerate(words):
        op, addr = w >> 26, base + 4 * i
        if code_words is not None and i >= code_words:
            out.append(w)
            continue
        if op == COP1 and (w >> 21) & 0x1F in (FMT_S, FMT_W, 0x11, 0x15):
            fmt, funct = (w >> 21) & 0x1F, w & 0x3F
            if fmt == FMT_S and funct == SQRT and (w >> 16) & 0x1F == 0 and (w >> 11) & 0x1F:
                w = (w & ~(0x1F << 11)) | (((w >> 11) & 0x1F) << 16)
                notes.append(f"0x{addr:08X} sqrt.s: operand fs -> ft (EE)")
            elif fmt == FMT_S and funct == TRUNC_W:
                w = (w & ~0x3F) | CVT_W
                notes.append(f"0x{addr:08X} trunc.w.s -> cvt.w.s")
            elif fmt == FMT_S and funct >= 0x30 and funct not in S_OK:
                w = (w & ~0x3F) | (funct & 0x36)
                notes.append(f"0x{addr:08X} c.{funct:02X}.s -> c.{funct & 0x36:02X}.s")
            elif fmt == FMT_S and funct not in S_OK:
                raise SystemExit(f"0x{addr:08X}: FPU instruction 0x{w:08X} (S funct 0x{funct:02X}) doesn't exist on the EE")
            elif fmt == FMT_W and funct != 0x20:
                raise SystemExit(f"0x{addr:08X}: FPU instruction 0x{w:08X} (W funct 0x{funct:02X}) doesn't exist on the EE")
            elif fmt in (0x11, 0x15):
                raise SystemExit(f"0x{addr:08X}: double-precision instruction 0x{w:08X}: the EE FPU is single only")
        elif op in BAD_OPS and not (op == 0x1F and i * 4 < 0x200):
            raise SystemExit(f"0x{addr:08X}: {BAD_OPS[op]} (0x{w:08X}) isn't for this target")
        out.append(w)
    return out, notes


# ------------------------------------------------------------------ build
def tool(*names):
    for n in names:
        p = shutil.which(n)
        if p:
            return p
    raise SystemExit(f"need {names[0]} (LLVM) on PATH")


def build():
    os.makedirs(BUILD, exist_ok=True)
    clang, lld, objcopy, nm = tool("clang"), tool("ld.lld"), tool("llvm-objcopy"), tool("llvm-nm")
    objs = []
    for src in ("stub.S", "mod.c", "collide.c", "servo.c"):
        obj = os.path.join(BUILD, src + ".o")
        subprocess.run([clang, *CFLAGS, "-c", os.path.join(HERE, src), "-o", obj], check=True)
        objs.append(obj)
    elf, raw = os.path.join(BUILD, "mod.elf"), os.path.join(BUILD, "mod.raw")
    subprocess.run([lld, "-T", os.path.join(HERE, "link.ld"), "-o", elf, *objs], check=True)
    subprocess.run([objcopy, "-O", "binary", "-j", ".text", "-j", ".rodata", elf, raw], check=True)
    syms = {}
    for line in subprocess.run([nm, elf], check=True, capture_output=True, text=True).stdout.splitlines():
        parts = line.split()
        if len(parts) == 3:
            syms[parts[2]] = int(parts[0], 16)
    data = open(raw, "rb").read()
    data += b"\0" * (-len(data) % 4)
    words = list(struct.unpack(f"<{len(data) // 4}I", data))
    # The stub's sq/lq are EE-only (opcodes 0x1E/0x1F); ee_fixups allows them in the stub only.
    words, notes = ee_fixups(words, MOD_BASE, (syms["_text_end"] - MOD_BASE) // 4)
    if syms.get("mod_entry") != MOD_BASE:
        raise SystemExit(f"mod_entry must be at 0x{MOD_BASE:08X}, is at {syms.get('mod_entry')}")
    with open(os.path.join(BUILD, "mod.bin"), "wb") as f:
        f.write(struct.pack(f"<{len(words)}I", *words))
    return words, syms, notes


# ------------------------------------------------------------------ the patch file
def cond16(addr, value, skip):
    """D-code: unless the u16 at addr is value, skip the next `skip` extended lines."""
    assert 0 < skip <= 0xFF and addr % 2 == 0
    return f"patch=1,EE,D{addr & 0x0FFFFFFF:07X},extended,{skip:02X}00{value & 0xFFFF:04X}"


def cond32(addr, value, skip):
    """Both halves (two lines): unless the word at addr is value, skip the `skip` lines after."""
    return [cond16(addr, value, skip + 1), cond16(addr + 2, value >> 16, skip)]


def write32(addr, value):
    return f"patch=1,EE,2{addr & 0x0FFFFFFF:07X},extended,{value:08X}"


def pnach(words, entry, anchor, sites, chunk=250):
    out = ["gametitle=Biohazard Outbreak (SLPM-65428, English patch OK) - Modern Camera",
           "",
           "[Modern Camera (standalone)]",
           "author=outbreak-modern-camera-mod-android",
           "description=Over-the-shoulder camera, modern aiming and controls for File #1. Our own code "
           "(mod/ in the repository), written while the gameplay overlay is loaded.",
           ""]
    out.append(f"// our code -> 0x{MOD_BASE:08X} ({len(words) * 4} bytes), only while SUBMAIN is loaded "
               f"(0x{anchor:08X} holds jal 0x{ANCHOR_CALL:08X})")
    for i in range(0, len(words), chunk):
        part = words[i:i + chunk]
        out += cond32(anchor, jal(ANCHOR_CALL), len(part))
        out += [write32(MOD_BASE + 4 * (i + j), w) for j, w in enumerate(part)]
    # The call sites, under the same anchor check: never point the game at code we didn't write.
    hooks = []
    for s in sites:
        hooks += cond32(s, jal(HOOKED), 1) + [write32(s, jal(entry))]
    out.append("// then, still only while SUBMAIN is loaded, each call site's jal 0x%08X -> jal mod_entry "
               "(0x%08X): %s" % (HOOKED, entry, " ".join(f"0x{s:08X}" for s in sites)))
    out += cond32(anchor, jal(ANCHOR_CALL), len(hooks)) + hooks
    return "\n".join(out) + "\n"


def main():
    words, syms, notes = build()
    print(f"mod.bin: {len(words) * 4} bytes at 0x{MOD_BASE:08X}; mod_frame 0x{syms['mod_frame']:08X}")
    for n in notes:
        print("  EE fix: " + n)
    if not os.path.exists(SITES):
        print(f"no {os.path.relpath(SITES, ROOT)} yet: run tools/f1_patchprobe.py on the device first; "
              "the patch file needs its addresses")
        return 0
    cfg = json.load(open(SITES))
    sites = [int(s, 16) for s in cfg["sites"]]
    text = pnach(words, syms["mod_entry"], int(cfg["anchor"], 16), sites)
    os.makedirs(os.path.dirname(PNACH), exist_ok=True)
    with open(PNACH, "w", newline="\n") as f:
        f.write(text)
    print(f"wrote {os.path.relpath(PNACH, ROOT)} ({text.count(chr(10))} lines, {len(sites)} call site(s))")
    return 0


if __name__ == "__main__":
    sys.exit(main())
