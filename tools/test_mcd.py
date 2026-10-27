#!/usr/bin/env python3
"""Tests for tools/ob/mcd.py's card writer and network settings (no game data: stand-in icons of the checked sizes).
Run: python -I tools/test_mcd.py
"""
import datetime
import os
import struct
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'ob'))
import mcd  # noqa: E402

FAILS = []


def check(cond, msg):
    if not cond:
        FAILS.append(msg)


def name(entry):
    return entry[0x40:0x60].split(b'\0')[0].decode()


# Scrambling: a hand-worked word, and both directions agree (odd lengths take the 8-bit path).
check(mcd.netcnf_key()[:6] == [6, 3, 3, 6, 3, 3], 'key from 0xAA: (0xAA >> 5) + 1, ((0xAA >> 2) & 7) + 1, (0xAA & 3) + 1')
check(mcd.netcnf_encode(b'# ') == b'\x37\xf7', '"# " = 0x2023, rotated left 6 = 0x08C8, inverted = 0xF737')
for text in (mcd.NETCNF_MAGIC + b'type eth\ndhcp\n', b'odd', bytes(range(256)) * 3):
    check(mcd.netcnf_decode(mcd.netcnf_encode(text)) == text, f'decode(encode) of {len(text)} bytes')
other = b'\x01\x23\x45\x67\x89\xab\xcd\xef'
check(mcd.netcnf_decode(mcd.netcnf_encode(b'abcdefgh' * 8, other), other) == b'abcdefgh' * 8, 'another console ID')

with tempfile.TemporaryDirectory() as tmp:
    card_path, icons = os.path.join(tmp, 'Mcd001.ps2'), os.path.join(tmp, 'icons')
    os.mkdir(icons)
    open(os.path.join(icons, 'sys_net.ico'), 'wb').write(b'I' * 0x8398)
    open(os.path.join(icons, 'icon.sys'), 'wb').write(b'S' * 0x3C4)
    mcd.format_card(card_path)
    card = mcd.Card(card_path)
    card.add_dir('SAVE', [('a', b'x' * 3000)], datetime.datetime(2026, 10, 27))  # a folder already on the card
    card.save()
    mcd.write_netcnf(card_path, icons)

    card = mcd.Card(card_path)
    root_dot = card.page(card.entry_page(card.root, 0))
    root_len = struct.unpack_from('<L', root_dot, 4)[0]
    check(root_len == 4, f'root entries ., .., SAVE, BWNETCNF: got {root_len}')
    found = card.find(card.root, root_len, 'BWNETCNF')
    check(found is not None and found[0] == 3, 'BWNETCNF is root entry 3')
    mode, length, first = struct.unpack_from('<HxxL8xL', found[1])
    check((mode, length) == (0x8427, 8), f'folder mode 0x8427 with 8 entries: got {mode:#x} {length}')
    inside = card.entries(first, length)
    check([name(e) for e in inside] == ['.', '..', 'SYS_NET.ICO', 'icon.sys', 'BWNETCNF', 'net000.cnf', 'ifc000.dat',
                                        'dev000.dat'], 'folder entries')
    dot_cluster, dot_index = struct.unpack_from('<LL', inside[0], 0x10)
    check((dot_cluster, dot_index) == (card.root, 3), '"." points at the parent cluster and its index there')

    def read(entry):
        size, c = struct.unpack_from('<L', entry, 4)[0], struct.unpack_from('<L', entry, 0x10)[0]
        return b''.join(card.cluster(card.alloc + x) for x in card.chain(c))[:size]

    files = {name(e): read(e) for e in inside[2:]}
    check(files['SYS_NET.ICO'] == b'I' * 0x8398 and files['icon.sys'] == b'S' * 0x3C4, 'icons copied whole')
    check(files['BWNETCNF'].startswith(b'0,1,net000.cnf,'), 'index in plain text')
    check(files['net000.cnf'].startswith(mcd.NETCNF_MAGIC), 'combination file in plain text with the magic line')
    check(mcd.netcnf_decode(files['ifc000.dat']) == mcd.NETCNF_MAGIC + b'type eth\ndhcp\n', 'ifc000.dat scrambled')
    check(b'product "Ethernet (Network Adaptor)"' in mcd.netcnf_decode(files['dev000.dat']), 'dev000.dat scrambled')
    check(card.find(first, length, 'SAVE') is None, 'names looked up per folder')

    used = [c for c in range(card.alloc_end) if card.fat(c) != mcd.FAT_FREE]
    owners = {}
    for e in card.entries(card.root, root_len)[2:]:
        for sub in [e] + card.entries(struct.unpack_from('<L', e, 0x10)[0], struct.unpack_from('<L', e, 4)[0])[2:]:
            c = struct.unpack_from('<L', sub, 0x10)[0]
            for x in card.chain(c):
                owners.setdefault(x, []).append(name(sub))
    for x in card.chain(card.root):
        owners.setdefault(x, []).append('/')
    check(all(len(v) == 1 for v in owners.values()), 'no cluster in two chains')
    check(sorted(owners) == used, 'every allocated cluster belongs to exactly one chain (no leaks)')

    try:
        mcd.write_netcnf(card_path, icons)
        check(False, 'a second BWNETCNF must be refused')
    except SystemExit:
        pass

if FAILS:
    print('\n'.join('FAIL: ' + f for f in FAILS))
    sys.exit(1)
print('mcd: all checks passed')
