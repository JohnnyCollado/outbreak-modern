# scan NETBIO*.DAT inside the ISO for Sofdec streams; duration = last SCR - first SCR (MPEG-1 pack, 90 kHz)
import struct, re
ISO = "D:/NEXTJJEN/repos/outbreak-modern/ps2_emu/roms/Biohazard - Outbreak (English v2.1).iso"
FILES = {"NETBIO00.DAT": None, "NETBIO01.DAT": None, "NETBIO02.DAT": None}
exec(open("D:/NEXTJJEN/repos/outbreak-modern/.run/1.2/t5_sfd.py").read().split("def scr")[0].replace("ISO = sys.argv[1]", "ISO = %r" % ISO))
PK = re.compile(rb"\x00\x00\x01\xba")
def scr(b, j):
    return ((b[j+4] >> 1) & 7) << 30 | b[j+5] << 22 | (b[j+6] >> 1) << 15 | b[j+7] << 7 | b[j+8] >> 1
for name, lba, sz in out:
    if name.strip("/") not in FILES: continue
    base = lba * S; starts = []; CH = 64 << 20; pos = 0
    while pos < sz:
        f.seek(base + pos); buf = f.read(min(CH + 64, sz - pos))
        for m in re.finditer(rb"SofdecStream", buf[:CH]): starts.append(pos + m.start())
        pos += CH
    for i, st in enumerate(starts):
        s0 = (st // S) * S; end = starts[i+1] if i + 1 < len(starts) else sz
        f.seek(base + s0); data = f.read(min(end - s0, 200 << 20))
        sc = [scr(data, m.start()) for m in PK.finditer(data) if m.start() + 12 < len(data) and (data[m.start()+4] & 0xF0) == 0x20]
        e = data.find(b"\x00\x00\x01\xb9")
        if sc:
            print(f"{name} off={s0} span={end-s0} packs={len(sc)} dur_s={(max(sc)-sc[0])/90000:.2f} ticks60={(max(sc)-sc[0])/1500:.0f} endcode_at={e}")
