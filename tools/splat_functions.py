#!/usr/bin/env python3
"""Turn the decomp's splat output into a function map for PS2Recomp.

  python -I tools/splat_functions.py <splat asm dir> <out.csv>

e.g. Outbreak-decomp/asm/SLPM_654.28/en/main. PS2Recomp reads the result as a "Ghidra map"
(`ghidra_output` in its TOML): a header line, then name,start,end,size per function, with end
exclusive. Its boundaries take priority over PS2Recomp's own guesses.

Why: PS2Recomp's fallback scanner finds functions only from JAL targets. On File 1's main ELF
it found 3,509 functions and decoded the VU microcode/data region as code. The decomp's splat
configs split code from data by hand and list 5,358 functions.

splat writes each function as
    nonmatching <name>, <size>
    glabel <name>
        /* <offset> <vram> <word> */  <instruction>
so the start is the vram of the first instruction after the glabel. The CSV holds names and
addresses only, no game code; still, keep it with the other generated files, out of git.
"""
import os
import re
import sys

NONMATCHING = re.compile(r"^\s*nonmatching\s+(\w+),\s*(0x[0-9A-Fa-f]+|\d+)")
GLABEL = re.compile(r"^\s*glabel\s+(\w+)")
INSTR = re.compile(r"^\s*/\*\s*[0-9A-Fa-f]+\s+([0-9A-Fa-f]{8})\s+[0-9A-Fa-f]{8}\s*\*/")


def functions_in(lines):
    """Yields (name, start, size) for every function in one splat .s file's lines."""
    pending = {}
    current = None
    for line in lines:
        m = NONMATCHING.match(line)
        if m:
            pending[m.group(1)] = int(m.group(2), 0)
            continue
        m = GLABEL.match(line)
        if m and m.group(1) in pending:
            current = m.group(1)
            continue
        if current:
            m = INSTR.match(line)
            if m:
                yield current, int(m.group(1), 16), pending.pop(current)
                current = None


def collect(asm_dir):
    found = {}
    for root, _, files in os.walk(asm_dir):
        if os.path.basename(root) == "data":
            continue
        for name in sorted(files):
            if name.endswith(".s"):
                with open(os.path.join(root, name)) as fh:
                    for fname, start, size in functions_in(fh):
                        found[start] = (fname, start, size)
    return [found[k] for k in sorted(found)]


def write_csv(funcs, path):
    with open(path, "w") as fh:
        fh.write("name,start,end,size\n")
        for name, start, size in funcs:
            fh.write(f"{name},0x{start:08X},0x{start + size:08X},{size}\n")


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__.split("\n\n")[1])
    funcs = collect(sys.argv[1])
    write_csv(funcs, sys.argv[2])
    lo = min(f[1] for f in funcs)
    hi = max(f[1] + f[2] for f in funcs)
    print(f"{len(funcs)} functions, 0x{lo:08X}-0x{hi:08X} -> {sys.argv[2]}")


if __name__ == "__main__":
    main()
