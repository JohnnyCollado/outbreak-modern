#!/usr/bin/env python3
"""Tests for tools/mwo3_to_elf.py on a hand-made MWo3 image (no game data).
Run: python -I tools/test_mwo3_to_elf.py
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mwo3_to_elf  # noqa: E402

FAILS = []


def check(cond, msg):
    if not cond:
        FAILS.append(msg)


def make_image(load=0x540000, text=b"\x08\x00\xe0\x03\x00\x00\x00\x00", data=b"DATA", bss=0x10):
    end = load + 0x40 + len(text) + len(data)
    header = struct.pack("<4sIIIIIII", b"MWo3", 2, load, len(text), len(data), bss, end, end)
    header += b"demo.bin".ljust(0x20, b"\0")
    return header + text + data


def read_sections(elf):
    """Returns {name: (type, flags, addr, size, bytes)} from an ELF32 LE file."""
    shoff, = struct.unpack_from("<I", elf, 0x20)
    shnum, shstrndx = struct.unpack_from("<HH", elf, 0x30)
    headers = [struct.unpack_from("<10I", elf, shoff + i * 0x28) for i in range(shnum)]
    strtab_off, strtab_size = headers[shstrndx][4], headers[shstrndx][5]
    strtab = elf[strtab_off:strtab_off + strtab_size]
    out = {}
    for name, typ, flags, addr, off, size, *_ in headers[1:]:
        n = strtab[name:strtab.index(b"\0", name)].decode()
        out[n] = (typ, flags, addr, size, elf[off:off + size] if typ != 8 else b"")
    return out


def test_layout():
    image = make_image()
    elf = mwo3_to_elf.to_elf(image)
    check(elf[:4] == b"\x7fELF" and elf[4] == 1 and elf[5] == 1, "ELF32 little-endian")
    machine, = struct.unpack_from("<H", elf, 0x12)
    entry, = struct.unpack_from("<I", elf, 0x18)
    check(machine == 8, "EM_MIPS")
    check(entry == 0x540040, "entry is the first code word, got %#x" % entry)

    s = read_sections(elf)
    check(s[".mwo3"][2:4] == (0x540000, 0x40), ".mwo3 is the header at the load address")
    check(s[".mwo3"][4] == image[:0x40], ".mwo3 holds the header bytes (the fingerprint)")
    check(s[".mwo3"][1] & 4 == 0, ".mwo3 is not executable")
    check(s[".text"][2:4] == (0x540040, 8) and s[".text"][1] & 4, ".text is the code, executable")
    check(s[".text"][4] == image[0x40:0x48], ".text bytes")
    check(s[".data"][2:5] == (0x540048, 4, b"DATA"), ".data follows the code")
    check(s[".bss"][0] == 8 and s[".bss"][2:4] == (0x54004C, 0x10), ".bss is NOBITS at end")

    ptype, poff, vaddr, _, filesz, memsz = struct.unpack_from("<6I", elf, 0x34)
    check((ptype, vaddr, filesz, memsz) == (1, 0x540000, 0x4C, 0x5C), "one PT_LOAD over the image + bss")
    check(elf[poff:poff + filesz] == image, "the segment is the image as the game copies it")


def test_rejects_bad_images():
    for bad, why in [(b"ELF!" + make_image()[4:], "magic"),
                     (make_image()[:0x44], "truncated")]:
        try:
            mwo3_to_elf.to_elf(bad)
            FAILS.append("accepted a bad image (%s)" % why)
        except ValueError:
            pass
    image = bytearray(make_image())
    struct.pack_into("<I", image, 0x18, 0x540100)  # end that doesn't match the sizes
    try:
        mwo3_to_elf.to_elf(bytes(image))
        FAILS.append("accepted inconsistent sizes")
    except ValueError:
        pass


if __name__ == "__main__":
    test_layout()
    test_rejects_bad_images()
    if FAILS:
        print("FAIL:\n  " + "\n  ".join(FAILS))
        sys.exit(1)
    print("ok")
