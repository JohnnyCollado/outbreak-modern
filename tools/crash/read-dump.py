# A tester's crash dump in plain words: the crash, then names for the addresses on the crashing thread's stack.
# python tools/crash/read-dump.py <outbreak-*.dmp> <dist/symbols/<build id>/ps2EntryRunner.map>
# The build id is the "[build]" line of the log beside the dump; it must match, or the names are wrong.
# Scans the stack for addresses rather than unwinding it: old frames can appear, so read it as evidence, not proof.
import bisect
import re
import struct
import sys


def main():
    d = open(sys.argv[1], 'rb').read()
    syms = []
    for line in open(sys.argv[2], errors='replace'):
        x = re.match(r'\s*[0-9a-f]{4}:[0-9a-f]{8}\s+(\S+)\s+([0-9a-f]{16})', line)
        if x:
            syms.append((int(x.group(2), 16) - 0x140000000, x.group(1)))
    syms.sort()
    keys = [s[0] for s in syms]

    count, table = struct.unpack_from('<II', d, 8)
    streams = {}
    for i in range(count):
        kind, size, rva = struct.unpack_from('<III', d, table + i * 12)
        streams[kind] = (size, rva)

    mods = []
    at = streams[4][1]
    for i in range(struct.unpack_from('<I', d, at)[0]):
        base, size = struct.unpack_from('<QI', d, at + 4 + i * 108)
        name_rva = struct.unpack_from('<I', d, at + 4 + i * 108 + 20)[0]
        length = struct.unpack_from('<I', d, name_rva)[0]
        mods.append((base, size, d[name_rva + 4:name_rva + 4 + length].decode('utf-16le').split(chr(92))[-1]))

    def name_of(a):
        for base, size, name in mods:
            if base <= a < base + size:
                if name.lower().startswith(('outbreakmodern', 'ps2entryrunner')):
                    i = bisect.bisect_right(keys, a - base) - 1
                    return syms[i][1] if i >= 0 else name
                return name
        return None

    tid = None
    context = None
    if 6 in streams:  # an abort() dump has no exception record: the thread is found by its stack holding abort's caller
        ex = streams[6][1]
        tid = struct.unpack_from('<I', d, ex)[0]
        code, _, _, addr = struct.unpack_from('<IIQQ', d, ex + 8)
        context = struct.unpack_from('<II', d, ex + 8 + 152)[1]
        print('exception 0x%08x at %s (0x%x), thread %d' % (code, name_of(addr), addr, tid))
    else:
        print('no exception record: an abort(); every thread follows')

    threads = streams[3][1]
    for i in range(struct.unpack_from('<I', d, threads)[0]):
        q = threads + 4 + i * 48
        this = struct.unpack_from('<I', d, q)[0]
        if tid is not None and this != tid:
            continue
        low, size, rva = struct.unpack_from('<QII', d, q + 24)
        ctx = context if context is not None else struct.unpack_from('<I', d, q + 44)[0]
        rsp = struct.unpack_from('<Q', d, ctx + 0x98)[0]
        print('thread', this)
        last = None
        shown = 0
        for o in range(max(0, rsp - low), size - 8, 8):
            w = name_of(struct.unpack_from('<Q', d, rva + o)[0])
            if w and w != last and not w.startswith('??_'):
                print('   ', w[:110])
                last = w
                shown += 1
                if shown >= 60:
                    break
        text = d[rva + max(0, rsp - low):rva + size]
        for s in sorted(set(re.findall(rb'[ -~]{12,}', text)))[:30]:
            print('    text:', s[:150].decode('ascii'))


if __name__ == '__main__':
    main()
