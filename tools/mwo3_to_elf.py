#!/usr/bin/env python3
"""Wrap a Metrowerks "MWo3" code overlay as an ELF so PS2Recomp can recompile it.

  python -I tools/mwo3_to_elf.py <overlay.bin> <out.elf>

e.g. game_files/PROG/DEMO.BIN. PS2Recomp reads only ELF files; Outbreak's overlays
(PROG/*.BIN) are raw images that the game copies to a fixed address itself
(decomp `overlay.c`). An image is:
    0x00 "MWo3" magic   0x04 id      0x08 load address   0x0C text size
    0x10 data size      0x14 bss size  0x18 end (load + header + text + data)
    0x1C static-init end  0x20 name (32 bytes)
    0x40 text, then data; bss follows `end` in RAM.
The header is part of the image in RAM, so it gets its own allocated section,
`.mwo3`, at the load address: the recompiled module fingerprints its first
bytes to know when this overlay is the one in RAM.

Sections written: .mwo3 (header, data), .text (code), .data, .bss (no bits).
One PT_LOAD segment covers the image. Entry = first code word (unused by the
module build). Like the input, the output is game code: keep it out of git.
"""
import struct
import sys

HEADER_SIZE = 0x40
MAGIC = b"MWo"

SHT_PROGBITS, SHT_STRTAB, SHT_NOBITS = 1, 3, 8
SHF_WRITE, SHF_ALLOC, SHF_EXECINSTR = 1, 2, 4


def parse_header(image):
    """Returns the header fields of an MWo3 image, or raises ValueError."""
    if len(image) < HEADER_SIZE or image[:3] != MAGIC:
        raise ValueError("not an MWo3 image (magic)")
    _, ovl_id, load, text, data, bss, end, sinit_end = struct.unpack_from("<8I", image, 0)
    name = image[0x20:0x40].split(b"\0")[0].decode("ascii", "replace")
    if end - load != HEADER_SIZE + text + data:
        raise ValueError("header sizes don't add up: end - load = %#x, header + text + data = %#x"
                         % (end - load, HEADER_SIZE + text + data))
    if len(image) < end - load:
        raise ValueError("file is %#x bytes, header says %#x" % (len(image), end - load))
    return {"id": ovl_id, "load": load, "text": text, "data": data, "bss": bss,
            "end": end, "sinit_end": sinit_end, "name": name}


def to_elf(image):
    """Returns the ELF file bytes for an MWo3 image."""
    h = parse_header(image)
    load, size = h["load"], h["end"] - h["load"]
    image = image[:size]

    names = [b"", b".mwo3", b".text", b".data", b".bss", b".shstrtab"]
    shstrtab = b""
    name_offsets = []
    for n in names:
        name_offsets.append(len(shstrtab))
        shstrtab += n + b"\0"

    image_off = 0x100
    shstr_off = image_off + size
    shdr_off = (shstr_off + len(shstrtab) + 3) & ~3

    # (name index, type, flags, address, file offset, size, alignment)
    text_addr = load + HEADER_SIZE
    data_addr = text_addr + h["text"]
    sections = [
        (0, 0, 0, 0, 0, 0, 0),
        (1, SHT_PROGBITS, SHF_ALLOC, load, image_off, HEADER_SIZE, 16),
        (2, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, text_addr, image_off + HEADER_SIZE, h["text"], 16),
        (3, SHT_PROGBITS, SHF_ALLOC | SHF_WRITE, data_addr, image_off + HEADER_SIZE + h["text"], h["data"], 16),
        (4, SHT_NOBITS, SHF_ALLOC | SHF_WRITE, h["end"], image_off + size, h["bss"], 16),
        (5, SHT_STRTAB, 0, 0, shstr_off, len(shstrtab), 1),
    ]

    ehdr = struct.pack("<4sBBBB8xHHIIIIIHHHHHH",
                       b"\x7fELF", 1, 1, 1, 0,       # 32-bit, little-endian, version 1
                       2, 8, 1,                       # ET_EXEC, EM_MIPS, version
                       text_addr, 0x34, shdr_off,     # entry, phoff, shoff
                       0, 0x34, 0x20, 1,              # flags, ehsize, phentsize, phnum
                       0x28, len(sections), 5)        # shentsize, shnum, shstrndx
    phdr = struct.pack("<8I", 1, image_off, load, load, size, size + h["bss"], 7, 16)

    out = bytearray(ehdr + phdr)
    out += b"\0" * (image_off - len(out))
    out += image
    out += shstrtab
    out += b"\0" * (shdr_off - len(out))
    for name, typ, flags, addr, off, sz, align in sections:
        out += struct.pack("<10I", name_offsets[name] if typ or name else 0,
                           typ, flags, addr, off, sz, 0, 0, align, 0)
    return bytes(out)


def main(argv):
    if len(argv) != 3:
        print(__doc__.strip().splitlines()[2].strip())
        return 2
    with open(argv[1], "rb") as f:
        image = f.read()
    try:
        elf = to_elf(image)
    except ValueError as e:
        print("%s: %s" % (argv[1], e))
        return 1
    with open(argv[2], "wb") as f:
        f.write(elf)
    h = parse_header(image)
    print("%s: id %d, %s at %#x, text %#x, data %#x, bss %#x -> %s"
          % (argv[1], h["id"], h["name"], h["load"], h["text"], h["data"], h["bss"], argv[2]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
