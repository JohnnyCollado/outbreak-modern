# T5 scratch: PCSX2 ref a vs b noise, ref value low-bit analysis, damage shares.
import sys, os
sys.path.insert(0, "D:/NEXTJJEN/repos/outbreak-modern/tools/gate")
import gate
from PIL import Image, ImageChops
R = "C:/ob/ref"
DS = [0, 7, 8, 9, 15, 16, 24, 32]
def dmap(x, y):
    diff = ImageChops.difference(x, y).split()
    return ImageChops.lighter(diff[0], ImageChops.lighter(diff[1], diff[2]))
for s in gate.SCREENS:
    a = gate.load_rgb(f"{R}/{s}-a.png"); b = gate.load_rgb(f"{R}/{s}-b.png")
    w, h = a.size
    print(f"== {s} {w}x{h}")
    print("  a-vs-b", " ".join(f"d{d}:{gate.metric(a,b,d)[1]:.6f}" for d in DS), "max", gate.metric(a,b,0)[0])
    m = dmap(a, b)
    bb = m.point(lambda v: 255 if v > 0 else 0).getbbox()
    print("  diff bbox", bb)
    if bb:
        px = m.load(); ev = od = 0
        for y in range(h):
            for x in range(w):
                if px[x, y]:
                    if y & 1: od += 1
                    else: ev += 1
        print("  diff px even/odd rows", ev, od)
    # 1-line shift: b shifted down/up 1 vs a
    for dy in (1, -1):
        bs = Image.new("RGB", (w, h)); bs.paste(b, (0, dy))
        print(f"  a-vs-b shift dy={dy} d8 share {gate.metric(a.crop((0,2,w,h-2)), bs.crop((0,2,w,h-2)), 8)[1]:.6f}")
    # low 3 bits histogram of ref a (FB format evidence)
    for nm, im in (("a", a), ("b", b)):
        cnt = [0]*8; tot = 0
        for ch in im.split():
            hh = ch.histogram()
            for v in range(256): cnt[v & 7] += hh[v]
        tot = sum(cnt)
        print(f"  ref {nm} low3 share", " ".join(f"{k}:{cnt[k]/tot:.3f}" for k in range(8)),
              "distinct colours", len(im.getcolors(1 << 24)))
    for name, fn in (("missing", gate.missing_layer), ("shifted", gate.shifted_glyph)):
        dmg, desc = fn(a)
        print(f"  {name} {desc}", " ".join(f"d{d}:{gate.metric(a,dmg,d)[1]:.6f}" for d in DS), "max", gate.metric(a,dmg,0)[0])
