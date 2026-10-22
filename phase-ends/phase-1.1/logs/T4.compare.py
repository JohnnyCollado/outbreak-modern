"""Scratch: per-screen capture ticks of two scripted boots and byte comparison of their dumps.
compare.py <logA> <framesA> <logB> <framesB>"""
import re
import sys
from pathlib import Path

la, fa, lb, fb = sys.argv[1:5]


def ticks(log):
    out = {}
    for line in Path(log).read_text(errors="replace").splitlines():
        m = re.match(r"\[script\] capture (\S+) k=(\d+) tick=(\d+)", line)
        if m:
            out.setdefault(m[1], []).append(int(m[3]))
    return out


ta, tb = ticks(la), ticks(lb)
for screen in ta:
    files = sorted(p.name for p in Path(fa).glob(f"{screen}-*.png"))
    same = [(Path(fa) / f).read_bytes() == (Path(fb) / f).read_bytes() if (Path(fb) / f).exists() else None
            for f in files]
    print(f"{screen}: A ticks {ta[screen]} B ticks {tb.get(screen)} identical {same}")
