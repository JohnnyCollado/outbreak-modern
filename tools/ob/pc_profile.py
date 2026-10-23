"""Summarize a PS2X_PC_SAMPLE CSV (patch 0030) by guest function.

Usage: python -I tools/ob/pc_profile.py <pc.csv> [--from T] [--to T] [--top N] [--demo] [--pcs]
  --from/--to  keep rows whose 64-vsync bucket start is in [from, to)
  --demo       map 0x540000.. with DEMO's function list instead of GAME's (before the GAME load)
  --pcs        also list the top pcs inside each of the top functions
Function lists: C:/ob/work/main-functions.csv and C:/ob/ovl/<module>-functions.csv (name,start,end,size).
"""
import argparse
import bisect
import csv
import os
from collections import defaultdict

FLAGS = ("vu1", "iop", "gs", "gsvsync")


def load_functions(paths):
    funcs = []  # (start, end, name, module)
    for module, path in paths:
        if not os.path.exists(path):
            continue
        with open(path, newline="") as f:
            for row in csv.DictReader(f):
                funcs.append((int(row["start"], 16), int(row["end"], 16), row["name"], module))
    funcs.sort()
    return funcs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("csv")
    ap.add_argument("--from", dest="lo", type=int, default=0)
    ap.add_argument("--to", dest="hi", type=int, default=1 << 62)
    ap.add_argument("--top", type=int, default=15)
    ap.add_argument("--demo", action="store_true")
    ap.add_argument("--pcs", action="store_true")
    a = ap.parse_args()

    root = os.environ.get("OB_ROOT", "C:/ob")
    ovl = ["submain", "netdnas", "single", "demo" if a.demo else "game"]
    funcs = load_functions([("main", f"{root}/work/main-functions.csv")] +
                           [(m, f"{root}/ovl/{m}-functions.csv") for m in ovl])
    starts = [f[0] for f in funcs]

    def lookup(pc):
        if pc == 0:
            return "(outside guest code)", "-"
        i = bisect.bisect_right(starts, pc) - 1
        if i >= 0 and funcs[i][0] <= pc < max(funcs[i][1], funcs[i][0] + 4):
            return funcs[i][2], funcs[i][3]
        return f"(unmapped 0x{pc:08x})", "?"

    total = 0
    flag_tot = defaultdict(int)
    by_fn = defaultdict(lambda: [0, [0] * len(FLAGS), defaultdict(int), set()])
    ticks = set()
    with open(a.csv, newline="") as f:
        for row in csv.DictReader(f):
            t = int(row["tick"])
            if not (a.lo <= t < a.hi):
                continue
            ticks.add(t)
            pc, n = int(row["pc"], 16), int(row["count"])
            name, module = lookup(pc)
            e = by_fn[(name, module)]
            e[0] += n
            e[2][pc] += n
            e[3].add(int(row["tid"]))
            total += n
            for k, fl in enumerate(FLAGS):
                e[1][k] += int(row[fl])
                flag_tot[fl] += int(row[fl])
    if not total:
        print("no samples in range")
        return
    print(f"samples {total}, buckets {min(ticks)}..{max(ticks)} ({len(ticks)} x 64 vsyncs)")
    print("flags: " + ", ".join(f"{fl} {100.0 * flag_tot[fl] / total:.1f}%" for fl in FLAGS))
    print(f"{'#':>2} {'%':>6} {'samples':>8}  {'module':8} {'function':32} tids  " + " ".join(f"{fl:>7}" for fl in FLAGS))
    ranked = sorted(by_fn.items(), key=lambda kv: -kv[1][0])[: a.top]
    for i, ((name, module), (n, fl, pcs, tids)) in enumerate(ranked, 1):
        flags = " ".join(f"{100.0 * x / n:6.1f}%" for x in fl)
        print(f"{i:>2} {100.0 * n / total:5.1f}% {n:>8}  {module:8} {name:32} {','.join(map(str, sorted(tids))):5} {flags}")
        if a.pcs:
            for pc, c in sorted(pcs.items(), key=lambda kv: -kv[1])[:4]:
                print(f"{'':20}0x{pc:08x} {100.0 * c / n:5.1f}%")


if __name__ == "__main__":
    main()
