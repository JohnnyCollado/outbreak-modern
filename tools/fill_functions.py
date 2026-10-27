#!/usr/bin/env python3
"""Add the functions splat missed to an overlay's function map.

  python -I tools/fill_functions.py <overlay.BIN> <splat.csv> <out.csv>

The decomp's splat output for NETDNAS lists 30 functions covering its first 0x29C4 bytes and
treats the rest of the 0x840C0-byte text as one data block (plan docs/plans/2026-10-26-online-play.md,
step S0.1). The rest is ordinary MIPS code: thousands of `jr $ra`, ~1,300 JAL targets.

New function starts, outside every function splat already lists:
  - JAL targets inside the text;
  - a stack-frame prologue (`addiu $sp, $sp, -N`) that directly follows a `jr $ra` and its delay
    slot (padding nops between them allowed).
Each new function ends at the next start or at the end of the text (end exclusive), the map
format splat_functions.py writes. Splat's own entries are kept as they are.

Reads the MWo3 image directly (tools/mwo3_to_elf.py parses the header). Writes names and
addresses only, no game code; still, keep the CSV with the other generated files, out of git.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mwo3_to_elf  # noqa: E402

JR_RA = 0x03E00008
NOP = 0


def read_csv(path):
    funcs = []
    with open(path) as fh:
        next(fh)
        for line in fh:
            name, start, end, _ = line.strip().split(",")
            funcs.append((name, int(start, 16), int(end, 16)))
    return funcs


def candidate_starts(text, base):
    """Yields addresses that look like function starts in `text` loaded at `base`."""
    end = base + len(text)
    words = struct.unpack_from("<%dI" % (len(text) // 4), text)
    after_return = False
    for i, w in enumerate(words):
        addr = base + i * 4
        if w >> 26 == 3:
            target = (addr & 0xF0000000) | ((w & 0x3FFFFFF) << 2)
            if base <= target < end:
                yield target
        if after_return and w != NOP:
            if (w & 0xFFFF8000) == 0x27BD8000:  # addiu $sp, $sp, -N
                yield addr
            after_return = False
        if i >= 1 and words[i - 1] == JR_RA:  # w is the delay slot
            after_return = True


def fill(image, splat):
    """Returns the full function list [(name, start, end)] for an MWo3 image and splat's list."""
    h = mwo3_to_elf.parse_header(image)
    base = h["load"] + mwo3_to_elf.HEADER_SIZE
    text = image[mwo3_to_elf.HEADER_SIZE:mwo3_to_elf.HEADER_SIZE + h["text"]]
    end = base + len(text)
    covered = sorted((s, e) for _, s, e in splat)

    def inside(a):
        return any(s <= a < e for s, e in covered)

    new = sorted({a for a in candidate_starts(text, base) if not inside(a)})
    starts = sorted(set(new) | {s for s, _ in covered})
    out = [(n, s, e) for n, s, e in splat]
    for a in new:
        nxt = starts[starts.index(a) + 1] if starts.index(a) + 1 < len(starts) else end
        out.append(("func_%08X" % a, a, nxt))
    return sorted(out, key=lambda f: f[1])


def write_csv(funcs, path):
    with open(path, "w") as fh:
        fh.write("name,start,end,size\n")
        for name, start, end in funcs:
            fh.write(f"{name},0x{start:08X},0x{end:08X},{end - start}\n")


def main(argv):
    if len(argv) != 4:
        sys.exit(__doc__.split("\n\n")[1])
    with open(argv[1], "rb") as fh:
        image = fh.read()
    splat = read_csv(argv[2])
    funcs = fill(image, splat)
    write_csv(funcs, argv[3])
    print(f"{len(splat)} splat + {len(funcs) - len(splat)} found = {len(funcs)} functions -> {argv[3]}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
