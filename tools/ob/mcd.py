"""PCSX2 .ps2 memory card files (8 MB, 528-byte raw pages: 512 data + 16 spare): make, format, inspect.

    python -I tools/ob/mcd.py blank  <card.ps2>   new unformatted card (all 0xFF), as PCSX2's "create" makes
    python -I tools/ob/mcd.py format <card.ps2>   new empty formatted card (an existing file is not overwritten)
    python -I tools/ob/mcd.py info   <card.ps2>   formatted or not; the root directory's entries

Format layout as mymcplus ps2mc.py `format` (github.com/thestr4ng3r/mymcplus, mymcplus/ps2mc.py and ps2mc_dir.py,
read 2026-10-26), which agrees with PCSX2 v2.9.134 MemoryCardFolder.cpp (alloc_offset = clusters / 0x100 + 9,
alloc_end = clusters - 0x10 - alloc_offset, backup blocks = last two erase blocks, root "." mode 0x8427 and ".." 0xA426):
superblock "Sony PS2 Memory Card Format " "1.2.0.0", 512-byte pages, 2 pages per cluster, 16 per erase block, indirect
FAT at cluster 8, FAT at 9-40, data from cluster 41 (8135 clusters), card type 2, flags 0x2B. ECC: three bytes per
128-byte chunk, PCSX2 MemoryCardFile.cpp CalculateECC (ported from mymc ps2mc_ecc.py, public domain).
"""
import datetime
import os
import struct
import sys

PAGE = 512
SPARE = 16
RAW = PAGE + SPARE
PAGES = 16384
PAGES_PER_CLUSTER = 2
PAGES_PER_BLOCK = 16
CLUSTERS = PAGES // PAGES_PER_CLUSTER  # 8192
BLOCKS = PAGES // PAGES_PER_BLOCK      # 1024
FIRST_IFC = 8
FAT_CLUSTERS = 32
ALLOC_OFFSET = FIRST_IFC + 1 + FAT_CLUSTERS              # 41
GOOD_BLOCK1, GOOD_BLOCK2 = BLOCKS - 1, BLOCKS - 2         # 1023, 1022
ALLOC_END = GOOD_BLOCK2 * (PAGES_PER_BLOCK // PAGES_PER_CLUSTER) - ALLOC_OFFSET  # 8135
MAGIC = b'Sony PS2 Memory Card Format '

_PARITY = [bin(b).count('1') & 1 for b in range(256)]
_COLUMN_MASK = [0] * 256
for _b in range(256):  # PCSX2 column_parity_mask: bits 0-6 from the byte's bit groups (mymc ps2mc_ecc.py)
    m = 0
    if _PARITY[_b & 0x55]: m |= 0x01
    if _PARITY[_b & 0x33]: m |= 0x02
    if _PARITY[_b & 0x0F]: m |= 0x04
    if _PARITY[_b & 0xAA]: m |= 0x10
    if _PARITY[_b & 0xCC]: m |= 0x20
    if _PARITY[_b & 0xF0]: m |= 0x40
    _COLUMN_MASK[_b] = m


def ecc128(chunk):
    """PCSX2 CalculateECC: column parity, then the two line parities, 3 bytes."""
    column, line0, line1 = 0x77, 0x7F, 0x7F
    for i, b in enumerate(chunk):
        column ^= _COLUMN_MASK[b]
        if _PARITY[b]:
            line0 ^= ~i & 0xFF
            line1 ^= i
    return bytes((column, line0 & 0x7F, line1 & 0x7F))


def raw_page(data):
    data = data.ljust(PAGE, b'\0')
    return data + b''.join(ecc128(data[i:i + 128]) for i in range(0, PAGE, 128)) + b'\xff' * 4


def tod(now):
    return struct.pack('<xBBBBBH', now.second, now.minute, now.hour, now.day, now.month, now.year)


def dirent(mode, length, now, name):
    return struct.pack('<HHL8sLL8sL28x448s', mode, 0, length, tod(now), 0, 0, tod(now), 0, name)


def format_card(path):
    if os.path.exists(path):
        sys.exit(f'{path} exists: not overwritten')
    now = datetime.datetime.now()
    pages = [raw_page(b'')] * PAGES
    # Superblock (mymcplus pack "<28s12sHHHHLLLLLL8x128s128sbbxx").
    ifc = struct.pack('<32L', FIRST_IFC, *([0] * 31))
    bad = struct.pack('<32L', *([0xFFFFFFFF] * 32))
    sb = struct.pack('<28s12sHHHHLLLLLL8x128s128sbbxx', MAGIC, b'1.2.0.0', PAGE, PAGES_PER_CLUSTER, PAGES_PER_BLOCK,
                     0xFF00, CLUSTERS, ALLOC_OFFSET, ALLOC_END, 0, GOOD_BLOCK1, GOOD_BLOCK2, ifc, bad, 2, 0x2B)
    pages[0] = raw_page(sb)

    def put_cluster(cluster, data):
        data = data.ljust(PAGE * PAGES_PER_CLUSTER, b'\0')
        for i in range(PAGES_PER_CLUSTER):
            pages[cluster * PAGES_PER_CLUSTER + i] = raw_page(data[i * PAGE:(i + 1) * PAGE])

    epc = PAGE * PAGES_PER_CLUSTER // 4  # 256 entries per cluster
    put_cluster(FIRST_IFC, struct.pack(f'<{epc}L', *(list(range(FIRST_IFC + 1, FIRST_IFC + 1 + FAT_CLUSTERS))
                                                      + [0xFFFFFFFF] * (epc - FAT_CLUSTERS))))
    fat = [0xFFFFFFFF] + [0x7FFFFFFF] * (ALLOC_END - 1) + [0xFFFFFFFF] * (FAT_CLUSTERS * epc - ALLOC_END)
    for i in range(FAT_CLUSTERS):
        put_cluster(FIRST_IFC + 1 + i, struct.pack(f'<{epc}L', *fat[i * epc:(i + 1) * epc]))
    put_cluster(ALLOC_OFFSET, dirent(0x8427, 2, now, b'.') + dirent(0xA426, 0, now, b'..'))
    for p in range(GOOD_BLOCK2 * PAGES_PER_BLOCK, (GOOD_BLOCK2 + 1) * PAGES_PER_BLOCK):
        pages[p] = b'\xff' * RAW
    with open(path, 'wb') as f:
        f.write(b''.join(pages))
    print(f'formatted card {path} ({PAGES * RAW} bytes, {ALLOC_END} data clusters)')


def blank_card(path):
    if os.path.exists(path):
        sys.exit(f'{path} exists: not overwritten')
    with open(path, 'wb') as f:
        f.write(b'\xff' * (PAGES * RAW))
    print(f'blank card {path} ({PAGES * RAW} bytes)')


def _entries(d, alloc, ppc, ifc, cluster, count):
    """A directory's entries (mode, length, cluster, name), following its FAT chain (one entry per page)."""
    epc = PAGE * ppc // 4

    def fat(c):
        fat_cluster = struct.unpack_from('<L', d, (ifc * ppc + (c // epc // epc) // (PAGE // 4)) * RAW
                                         + ((c // epc) % (PAGE // 4)) * 4)[0]  # indirect FAT (first cluster)
        page = fat_cluster * ppc + (c % epc) // (PAGE // 4)
        return struct.unpack_from('<L', d, page * RAW + (c % (PAGE // 4)) * 4)[0]

    out = []
    while len(out) < count:
        for p in range(ppc):
            if len(out) == count:
                break
            e = d[((alloc + cluster) * ppc + p) * RAW:][:PAGE]
            mode, length = struct.unpack_from('<HxxL', e, 0)
            out.append((mode, length, struct.unpack_from('<L', e, 0x10)[0], e[0x40:0x60].split(b'\0')[0].decode(errors='replace')))
        nxt = fat(cluster)
        if nxt == 0xFFFFFFFF or not nxt & 0x80000000:
            break
        cluster = nxt & 0x7FFFFFFF
    return out


def info(path):
    d = open(path, 'rb').read()
    print('size', len(d))
    sb = d[:PAGE]
    if not sb.startswith(MAGIC):
        print('unformatted')
        return
    ppc, alloc, root = struct.unpack_from('<H', sb, 0x2A)[0], struct.unpack_from('<L', sb, 0x34)[0], struct.unpack_from('<L', sb, 0x3C)[0]
    ifc = struct.unpack_from('<L', sb, 0x50)[0]
    print('formatted', sb[0x1C:0x28].split(b'\0')[0].decode(), 'pages/cluster', ppc, 'alloc_offset', alloc)
    root_len = struct.unpack_from('<L', d, (alloc + root) * ppc * RAW + 4)[0]
    for mode, length, cluster, name in _entries(d, alloc, ppc, ifc, root, root_len):
        print(f'  {name!r} mode=0x{mode:04x} {"dir" if mode & 0x20 else "file"} length={length}')
        if mode & 0x20 and name not in ('.', '..'):
            for m2, l2, _, n2 in _entries(d, alloc, ppc, ifc, cluster, length):
                if n2 not in ('.', '..'):
                    print(f'      {n2!r} {"dir" if m2 & 0x20 else "file"} {l2} bytes')


if __name__ == '__main__':
    if len(sys.argv) != 3 or sys.argv[1] not in ('blank', 'format', 'info'):
        sys.exit(__doc__)
    {'blank': blank_card, 'format': format_card, 'info': info}[sys.argv[1]](sys.argv[2])
