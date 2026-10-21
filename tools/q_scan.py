#!/usr/bin/env python3
"""READ-ONLY: find VU0 macro-mode code that reads the Q register before a VDIV/VSQRT/VRSQRT has
finished, i.e. without a VWAITQ in between.

  python -I tools/q_scan.py <SLPM_654.28> [overlay.bin ...] [--window N] [--out report.md]

Why: on a PS2 the divide unit takes several cycles (PCSX2 VUops.cpp: DIV 7, SQRT 7, RSQRT 13 VU
cycles) and Q keeps its old value until it finishes. PS2Recomp writes Q at once. That only
changes what the game does if the game reads Q early. This tool answers whether it does.

What counts:
- producers: vdiv, vsqrt, vrsqrt
- safe: vwaitq before any read
- reads of Q: the q-variants of the vector ops (vaddq, vmulq, vmaddaq...) and cfc2 from $vi22
- every path from a producer is followed (both sides of a branch, jump targets) for up to --window
  instructions (default 32). A call (jal/jalr) or a return (jr) ends a path as "leaves", since
  what happens to Q there isn't known from this function alone.

The report gives addresses, mnemonics and distances only, never instruction listings, so it can
be kept in git (no game code). The binaries are read from wherever you point; never commit them.

Main ELF: only 0x100000-0x1d9b00 is scanned. That is the EE code; above it are the VU microcode,
the EE kernel-mode block and data (Outbreak-decomp docs/LAYOUT.md, File 1). Overlays: the text
section of each MWo3 container (64-byte header, then text; REO docs/mwo3-overlay-format.md).
Needs rabbitizer (pip install rabbitizer).
"""
import argparse
import os
import struct
import sys

import rabbitizer

MAIN_CODE = (0x100000, 0x1D9B00)  # File 1 (SLPM-65428), from the decomp's LAYOUT.md
PRODUCERS = {"vdiv", "vsqrt", "vrsqrt"}
Q_READERS = {"vaddq", "vsubq", "vmulq", "vmaddq", "vmsubq",
             "vaddaq", "vsubaq", "vmulaq", "vmaddaq", "vmsubaq"}
Q_CONTROL_REG = 22  # $vi22 is Q in the COP2 control registers
LATENCY = {"vdiv": 7, "vsqrt": 7, "vrsqrt": 13}  # VU cycles, PCSX2 VUops.cpp


def load_main(path, lo=MAIN_CODE[0], hi=MAIN_CODE[1]):
    """Returns (name, base, words) for the main ELF's EE code range."""
    data = open(path, "rb").read()
    if data[:4] != b"\x7fELF":
        raise ValueError(f"{path}: not an ELF")
    phoff, = struct.unpack_from("<I", data, 0x1C)
    phentsize, phnum = struct.unpack_from("<HH", data, 0x2A)
    for i in range(phnum):
        p_type, p_offset, p_vaddr, _, p_filesz = struct.unpack_from("<5I", data, phoff + i * phentsize)
        if p_type == 1 and p_vaddr <= lo and hi <= p_vaddr + p_filesz:
            start = p_offset + (lo - p_vaddr)
            return os.path.basename(path), lo, list(struct.unpack_from(f"<{(hi - lo) // 4}I", data, start))
    raise ValueError(f"{path}: no load segment covers {lo:#x}-{hi:#x}")


def load_overlay(path):
    """Returns (name, base, words) for the text section of an MWo3 overlay."""
    data = open(path, "rb").read()
    if data[:4] != b"MWo3":
        raise ValueError(f"{path}: not an uncompressed MWo3 overlay")
    _, addr, text, data_size = struct.unpack_from("<4I", data, 4)
    end, = struct.unpack_from("<I", data, 0x18)
    if end != addr + 0x40 + text + data_size:
        raise ValueError(f"{path}: MWo3 header sizes don't add up")
    name = data[0x20:0x40].split(b"\0")[0].decode("ascii", "replace")
    return name, addr + 0x40, list(struct.unpack_from(f"<{text // 4}I", data, 0x40))


def decode(word, addr):
    ins = rabbitizer.Instruction(word, addr, rabbitizer.InstrCategory.R5900)
    return ins if ins.isValid() else None


def mnemonic(ins):
    return ins.getOpcodeName().split(".")[0]


def reads_q(ins):
    name = mnemonic(ins)
    if name in Q_READERS:
        return True
    return name == "cfc2" and ((ins.getRaw() >> 11) & 0x1F) == Q_CONTROL_REG


def follow(words, base, start, window):
    """Every path from the instruction after the producer at `start`. Returns a list of
    (outcome, distance, address): outcome is 'read', 'waited', 'replaced', 'leaves' or 'window'."""
    results = []
    seen = set()
    stack = [(start + 4, 1)]
    end = base + 4 * len(words)
    while stack:
        addr, dist = stack.pop()
        while True:
            if dist > window:
                results.append(("window", dist, addr))
                break
            if not base <= addr < end:
                results.append(("leaves", dist, addr))
                break
            if (addr, dist) in seen:
                break
            seen.add((addr, dist))
            ins = decode(words[(addr - base) // 4], addr)
            if ins is None:
                results.append(("leaves", dist, addr))
                break
            name = mnemonic(ins)
            if reads_q(ins):
                results.append(("read", dist, addr))
                break
            if name == "vwaitq":
                results.append(("waited", dist, addr))
                break
            if name in PRODUCERS:
                results.append(("replaced", dist, addr))
                break
            if ins.isBranch() or ins.isJump():
                # The delay slot runs first.
                slot = addr + 4
                if base <= slot < end:
                    slot_ins = decode(words[(slot - base) // 4], slot)
                    if slot_ins is not None and reads_q(slot_ins):
                        results.append(("read", dist + 1, slot))
                        break
                    if slot_ins is not None and mnemonic(slot_ins) == "vwaitq":
                        results.append(("waited", dist + 1, slot))
                        break
                if name in ("jal", "jalr", "bal", "bltzal", "bgezal", "bltzall", "bgezall") or name in ("jr",):
                    results.append(("leaves", dist, addr))
                    break
                if ins.isBranch():
                    target = ins.getBranchVramGeneric()
                    stack.append((target, dist + 2))
                    if ins.isUnconditionalBranch():
                        break
                    addr, dist = addr + 8, dist + 2
                    continue
                if name == "j":
                    addr, dist = ins.getInstrIndexAsVram(), dist + 2
                    continue
                results.append(("leaves", dist, addr))
                break
            addr, dist = addr + 4, dist + 1
    return results


def scan(binary, window):
    name, base, words = binary
    findings = []
    for i, word in enumerate(words):
        addr = base + 4 * i
        ins = decode(word, addr)
        if ins is None or mnemonic(ins) not in PRODUCERS:
            continue
        paths = follow(words, base, addr, window)
        reads = [p for p in paths if p[0] == "read"]
        if reads:
            verdict = "EARLY READ"
        elif paths and all(p[0] in ("waited", "replaced") for p in paths):
            verdict = "safe"
        else:
            verdict = "unresolved"
        findings.append({"binary": name, "addr": addr, "op": mnemonic(ins), "verdict": verdict,
                         "paths": paths, "first_read": min((p[1] for p in reads), default=None)})
    return findings


def report(findings, binaries, window):
    lines = ["# Q-register scan", "",
             f"Producers (vdiv/vsqrt/vrsqrt) and what each path reaches first, within {window} instructions.",
             "", "| Binary | Producers | safe (vwaitq first) | EARLY READ | unresolved |", "|---|---|---|---|---|"]
    for name, _, _ in binaries:
        f = [x for x in findings if x["binary"] == name]
        count = lambda v: sum(1 for x in f if x["verdict"] == v)  # noqa: E731
        lines.append(f"| {name} | {len(f)} | {count('safe')} | {count('EARLY READ')} | {count('unresolved')} |")
    early = [x for x in findings if x["verdict"] == "EARLY READ"]
    lines += ["", "## Early reads", ""]
    if not early:
        lines.append("None.")
    for x in early:
        reads = sorted({(p[1], p[2]) for p in x["paths"] if p[0] == "read"})
        where = ", ".join(f"{a:#x} at {d}" for d, a in reads)
        lines.append(f"- {x['binary']} {x['addr']:#x} `{x['op']}` (latency {LATENCY[x['op']]} VU cycles): "
                     f"Q read at {where} instructions")
    unresolved = [x for x in findings if x["verdict"] == "unresolved"]
    lines += ["", "## Unresolved (a path left the function or the window before a wait or a read)", ""]
    if not unresolved:
        lines.append("None.")
    for x in unresolved:
        kinds = sorted({f"{p[0]}@{p[2]:#x}" for p in x["paths"] if p[0] in ("leaves", "window")})
        lines.append(f"- {x['binary']} {x['addr']:#x} `{x['op']}`: {', '.join(kinds)}")
    return "\n".join(lines) + "\n"


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("main_elf")
    ap.add_argument("overlays", nargs="*")
    ap.add_argument("--window", type=int, default=32)
    ap.add_argument("--out")
    args = ap.parse_args()
    binaries = [load_main(args.main_elf)] + [load_overlay(p) for p in args.overlays]
    findings = []
    for b in binaries:
        findings += scan(b, args.window)
    text = report(findings, binaries, args.window)
    if args.out:
        with open(args.out, "w") as fh:
            fh.write(text)
    print(text)


if __name__ == "__main__":
    sys.exit(main())
