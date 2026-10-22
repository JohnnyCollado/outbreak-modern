# 16x16 block map of ref a vs b: '#' any delta>8, 'o' clean + content (edge energy), '.' clean flat
import sys
sys.path.insert(0, "D:/NEXTJJEN/repos/outbreak-modern/tools/gate")
import gate
from PIL import ImageChops, ImageFilter
for s in ("title", "mainmenu", "charselect"):
    a = gate.load_rgb(f"C:/ob/ref/{s}-a.png"); b = gate.load_rgb(f"C:/ob/ref/{s}-b.png")
    dd = ImageChops.difference(a, b).split()
    m = ImageChops.lighter(dd[0], ImageChops.lighter(dd[1], dd[2])).load()
    e = a.convert("L").filter(ImageFilter.FIND_EDGES).load()
    print("==", s, "cols x0=0..496 step 16")
    for by in range(28):
        row = ""
        for bx in range(32):
            bad = any(m[x, y] > 8 for y in range(by*16, by*16+16) for x in range(bx*16, bx*16+16))
            en = sum(e[x, y] for y in range(by*16, by*16+16) for x in range(bx*16, bx*16+16))
            row += "#" if bad else ("o" if en > 2000 else ".")
        print(f"{by*16:3d} {row}")
