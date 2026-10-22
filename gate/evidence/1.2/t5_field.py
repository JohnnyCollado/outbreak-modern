# share of line pairs (2k,2k+1) identical, and (2k+1,2k+2): field-doubled images score ~1 on one parity
import sys, glob
from PIL import Image
def pairs(path):
    im = Image.open(path).convert("RGB"); w, h = im.size
    rows = [im.crop((0, y, w, y + 1)).tobytes() for y in range(h)]
    nb = [y for y in range(h) if len(set(rows[y])) > 1 or rows[y][0] != 0]  # non-blank rows
    ev = sum(rows[2*k] == rows[2*k+1] for k in range(h // 2) if 2*k in nb)
    od = sum(rows[2*k+1] == rows[2*k+2] for k in range(h // 2 - 1) if 2*k+1 in nb)
    ne = sum(1 for k in range(h // 2) if 2*k in nb); no = sum(1 for k in range(h//2-1) if 2*k+1 in nb)
    return f"even-pair-equal {ev}/{ne} odd-pair-equal {od}/{no}"
for p in sys.argv[1:]:
    for f in sorted(glob.glob(p)):
        print(f.replace("\\", "/").split("/")[-1], pairs(f))
