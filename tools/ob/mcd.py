"""PCSX2 .ps2 memory card files (8 MB, 528-byte raw pages: 512 data + 16 spare): make, format, inspect.

    python -I tools/ob/mcd.py blank  <card.ps2>   new unformatted card (all 0xFF), as PCSX2's "create" makes
    python -I tools/ob/mcd.py format <card.ps2>   new empty formatted card (an existing file is not overwritten)
    python -I tools/ob/mcd.py info   <card.ps2>   formatted or not; the root directory's entries
    python -I tools/ob/mcd.py netcnf <card.ps2> <icon dir>   add the network settings folder BWNETCNF (network
        adapter, DHCP); <icon dir> = the disc's ntgui2/conf/english (sys_net.ico, icon.sys)

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


FAT_CHAIN, FAT_END, FAT_FREE = 0x80000000, 0xFFFFFFFF, 0x7FFFFFFF
MODE_DIR, MODE_FILE = 0x8427, 0x8497  # as the game's MCMAN writes them (its save folder on a card it made)


class Card:
    """A formatted card loaded whole: follow and extend FAT chains, add folders and files (no deleting).

    Layout as MCMAN leaves it (seen in the game's own save on C:\\ob\\saves-net\\Mcd001.ps2, 2026-10-27): a folder's
    entry in its parent holds its entry count; inside, "." (cluster = the parent's first cluster, dir_entry = the
    folder's index in the parent, length 0) and ".." (cluster 0, length 0), both mode 0x8427; files 0x8497. Root's
    own "." holds the root's entry count. Clusters chain as 0x80000000 | next, the last 0xFFFFFFFF, free 0x7FFFFFFF."""

    def __init__(self, path):
        self.path = path
        self.d = bytearray(open(path, 'rb').read())
        sb = self.page(0)
        if not sb.startswith(MAGIC):
            sys.exit(f'{path}: not a formatted card')
        self.ppc = struct.unpack_from('<H', sb, 0x2A)[0]
        self.alloc, self.alloc_end, self.root = struct.unpack_from('<LLL', sb, 0x34)
        self.ifc_list = struct.unpack_from('<32L', sb, 0x50)
        self.size = PAGE * self.ppc
        self.epc = self.size // 4

    def page(self, n):
        return bytes(self.d[n * RAW:n * RAW + PAGE])

    def put_page(self, n, data):
        self.d[n * RAW:(n + 1) * RAW] = raw_page(data)

    def cluster(self, absolute):
        return b''.join(self.page(absolute * self.ppc + i) for i in range(self.ppc))

    def put_cluster(self, absolute, data):
        data = data.ljust(self.size, b'\0')
        for i in range(self.ppc):
            self.put_page(absolute * self.ppc + i, data[i * PAGE:(i + 1) * PAGE])

    def _fat_slot(self, c):
        indirect = self.ifc_list[c // self.epc // self.epc]
        fat_cluster = struct.unpack_from('<L', self.cluster(indirect), (c // self.epc % self.epc) * 4)[0]
        return fat_cluster, (c % self.epc) * 4

    def fat(self, c):
        fat_cluster, off = self._fat_slot(c)
        return struct.unpack_from('<L', self.cluster(fat_cluster), off)[0]

    def set_fat(self, c, value):
        fat_cluster, off = self._fat_slot(c)
        data = bytearray(self.cluster(fat_cluster))
        struct.pack_into('<L', data, off, value)
        self.put_cluster(fat_cluster, bytes(data))

    def chain(self, c):
        out = [c]
        while (nxt := self.fat(out[-1])) != FAT_END:
            if not nxt & FAT_CHAIN:
                sys.exit(f'{self.path}: broken FAT chain at cluster {out[-1]}')
            out.append(nxt & ~FAT_CHAIN)
        return out

    def allocate(self, count, after=None):
        """count free clusters (first fit), chained; linked after cluster `after` if given."""
        free = [c for c in range(self.alloc_end) if self.fat(c) == FAT_FREE][:count]
        if len(free) < count:
            sys.exit(f'{self.path}: card full')
        for a, b in zip(free, free[1:]):
            self.set_fat(a, FAT_CHAIN | b)
        self.set_fat(free[-1], FAT_END)
        if after is not None:
            self.set_fat(after, FAT_CHAIN | free[0])
        return free

    def entry_page(self, first, index):
        """Absolute page of entry `index` of the folder starting at cluster `first`, growing its chain if needed."""
        chain = self.chain(first)
        while index // self.ppc >= len(chain):
            new = self.allocate(1, after=chain[-1])[0]
            self.put_cluster(self.alloc + new, b'')
            chain.append(new)
        return (self.alloc + chain[index // self.ppc]) * self.ppc + index % self.ppc

    def entries(self, first, count):
        return [self.page(self.entry_page(first, i)) for i in range(count)]

    def find(self, first, count, name):
        for i, e in enumerate(self.entries(first, count)):
            if e[0x40:0x60].split(b'\0')[0] == name.encode() and struct.unpack_from('<H', e)[0] & 0x8000:
                return i, e
        return None

    def add_dir(self, name, files, now):
        """Folder `name` in the root with `files` [(name, bytes)]; refuses if the name exists."""
        root_dot = self.page(self.entry_page(self.root, 0))
        root_len = struct.unpack_from('<L', root_dot, 4)[0]
        if self.find(self.root, root_len, name):
            sys.exit(f'{self.path}: {name} exists: not overwritten')
        count = 2 + len(files)
        clusters = self.allocate((count + self.ppc - 1) // self.ppc)
        for c in clusters:
            self.put_cluster(self.alloc + c, b'')
        first = clusters[0]
        entries =[struct.pack('<HHL8sLL8sL28x448s', MODE_DIR, 0, 0, tod(now), self.root, root_len, tod(now), 0, b'.'),
                   dirent(MODE_DIR, 0, now, b'..')]
        for fname, data in files:
            n = max(1, (len(data) + self.size - 1) // self.size)
            chain = self.allocate(n)
            for i, c in enumerate(chain):
                self.put_cluster(self.alloc + c, data[i * self.size:(i + 1) * self.size])
            entries.append(struct.pack('<HHL8sLL8sL28x448s', MODE_FILE, 0, len(data), tod(now), chain[0], 0,
                                       tod(now), 0, fname.encode()))
        for i, e in enumerate(entries):
            self.put_page(self.entry_page(first, i), e)
        self.put_page(self.entry_page(self.root, root_len),
                      struct.pack('<HHL8sLL8sL28x448s', MODE_DIR, 0, count, tod(now), first, 0, tod(now), 0,
                                  name.encode()))
        root_dot = bytearray(root_dot)
        struct.pack_into('<L', root_dot, 4, root_len + 1)
        self.put_page(self.entry_page(self.root, 0), bytes(root_dot))

    def save(self):
        with open(self.path, 'wb') as f:
            f.write(self.d)


# Network settings (Sony netcnf, BWNETCNF): an index and a combination file in plain text, the interface and device
# files scrambled with a key from the console ID (sceCdRI; PS2Recomp 0093 answers eight 0xAA bytes). Checked against
# the game's iop/modules/netcnf.irx (2026-10-27): the key is 24 rotation amounts, three per ID byte ((b >> 5) + 1,
# ((b >> 2) & 7) + 1, (b & 3) + 1, code at 0x128c, all 8 bytes); reading inverts each 16-bit word then rotates it right
# by key[n % 24] (0xef0-0xf2c; an odd last byte the same in 8 bits). ps2sdk's netcnf.c (iop/network/netcnf) has the
# same with the key loop one byte short. Type numbers 0 net, 1 ifc, 2 dev; "type eth" = 1 (netcnf.c option parser);
# vendor/product as the game's SMAP.IRX and SLPM_654.28 name the adapter.
CONSOLE_ID = b'\xaa' * 8
NETCNF_MAGIC = b'# <Sony Computer Entertainment Inc.>\n\n'


def netcnf_key(ident=CONSOLE_ID):
    return [s for b in ident for s in ((b >> 5) + 1, ((b >> 2) & 7) + 1, (b & 3) + 1)]


def netcnf_encode(data, ident=CONSOLE_ID):
    key, out = netcnf_key(ident), bytearray()
    for n in range(0, len(data) - 1, 2):
        w, s = struct.unpack_from('<H', data, n)[0], key[n // 2 % 24]
        out += struct.pack('<H', ~((w << s | w >> (16 - s)) & 0xFFFF) & 0xFFFF)
    if len(data) & 1:
        b, s = data[-1], key[len(data) // 2 % 24]
        out.append(~((b << s | b >> (8 - s)) & 0xFF) & 0xFF)
    return bytes(out)


def netcnf_decode(data, ident=CONSOLE_ID):
    key, out = netcnf_key(ident), bytearray()
    for n in range(0, len(data) - 1, 2):
        w, s = ~struct.unpack_from('<H', data, n)[0] & 0xFFFF, key[n // 2 % 24]
        out += struct.pack('<H', (w >> s | w << (16 - s)) & 0xFFFF)
    if len(data) & 1:
        b, s = ~data[-1] & 0xFF, key[len(data) // 2 % 24]
        out.append((b >> s | b << (8 - s)) & 0xFF)
    return bytes(out)


def netcnf_files(icon_dir):
    """The BWNETCNF folder for "network adapter, automatic address (DHCP) and DNS": [(name, bytes)]."""
    files = [('SYS_NET.ICO', open(os.path.join(icon_dir, 'sys_net.ico'), 'rb').read()),
             ('icon.sys', open(os.path.join(icon_dir, 'icon.sys'), 'rb').read())]
    if (len(files[0][1]), len(files[1][1])) != (0x8398, 0x3C4):  # the sizes netcnf checks
        sys.exit(f'{icon_dir}: unexpected icon sizes')
    files += [
        ('BWNETCNF', b'0,1,net000.cnf,Combination1\n1,1,ifc000.dat,Setting 1\n'
                     b'2,1,dev000.dat,SCE/Ethernet (Network Adaptor)\n'),
        ('net000.cnf', NETCNF_MAGIC + b'interface "Combination1" "ifc000.dat" "dev000.dat"\n'),
        ('ifc000.dat', netcnf_encode(NETCNF_MAGIC + b'type eth\ndhcp\n')),
        ('dev000.dat', netcnf_encode(NETCNF_MAGIC + b'type eth\nvendor "SCE"\nproduct "Ethernet (Network Adaptor)"\n'
                                                    b'phy_config auto\n')),
    ]
    return files


def write_netcnf(path, icon_dir):
    card = Card(path)
    card.add_dir('BWNETCNF', netcnf_files(icon_dir), datetime.datetime.now())
    card.save()
    print(f'{path}: BWNETCNF written (network adapter, DHCP, automatic DNS; console ID {CONSOLE_ID.hex()})')


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
    if len(sys.argv) == 4 and sys.argv[1] == 'netcnf':
        write_netcnf(sys.argv[2], sys.argv[3])
        sys.exit()
    if len(sys.argv) != 3 or sys.argv[1] not in ('blank', 'format', 'info'):
        sys.exit(__doc__)
    {'blank': blank_card, 'format': format_card, 'info': info}[sys.argv[1]](sys.argv[2])
