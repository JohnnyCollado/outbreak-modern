#!/usr/bin/env python3
"""READ-ONLY: find where File #1 keeps the aim elevation (up / level / down).

The decomp does not cover the aiming action's steps or the shot code yet, so this finds the
field empirically: you hold each aim pose while the tool snapshots our player's whole
DrawObjFull (0x10E0 bytes, one PINE round trip per sample), twice per pose, and it ranks the
fields that are stable within a pose and different between poses. Nothing is written.

Usage (in gameplay, a gun equipped, no enemy near so the auto-aim doesn't turn you):
  python tools/f1_aimscan.py
Then follow the prompts. Results go to results/aimscan-<time>.json and the console.
"""
import datetime
import json
import os
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import phase2_check  # noqa: E402
import pine_diag  # noqa: E402
from f1_live import our_player  # noqa: E402

SIZE = 0x10E0
POSES = ["level", "up", "down"]
SAMPLES_PER_POSE = 10


def connect():
    ok, msg = phase2_check.ensure_forward(phase2_check.shutil.which("adb") or "adb", 28011)
    print(("adb: " if ok else "adb problem: ") + msg)
    pine = pine_diag.Pine("127.0.0.1", 28011, 5.0)
    info = pine_diag.identify(pine)
    print(f"{info['id']} CRC {info['crc']} disc version {info['game_version']}")
    return pine, info


def rank(snaps):
    """Offsets (4-byte aligned words and single bytes) that are constant within each pose and
    differ between poses, best separated first."""
    found = []
    for width, fmt in ((4, "<I"), (2, "<H"), (1, "<B")):
        for off in range(0, SIZE, width):
            per_pose = {}
            ok = True
            for pose, raws in snaps.items():
                vals = {struct.unpack_from(fmt, r, off)[0] for r in raws}
                if len(vals) != 1:
                    ok = False
                    break
                per_pose[pose] = vals.pop()
            if ok and len(set(per_pose.values())) == len(POSES):
                found.append((width, off, per_pose))
    # Prefer the widest view of an offset, and drop narrower ones it already covers.
    seen, out = set(), []
    for width, off, per_pose in found:
        if any(o in seen for o in range(off, off + width)):
            continue
        seen.update(range(off, off + width))
        out.append((width, off, per_pose))
    return out


def main():
    pine, info = connect()
    idx, base = our_player(pine)
    print(f"recording our player #{idx} at 0x{base:08X}\n")
    snaps = {p: [] for p in POSES}
    try:
        for rnd in (1, 2):
            for pose in POSES:
                input(f"[round {rnd}] Hold the aim button and aim {pose.upper()} (hold it), then press Enter here...")
                for _ in range(SAMPLES_PER_POSE // 2):
                    snaps[pose].append(pine.read_block(base, SIZE))
                    time.sleep(0.05)
                act = snaps[pose][-1][8]
                print(f"   captured; action byte = {act} ({'aiming' if act == 2 else 'NOT aiming: was the aim held?'})")
    finally:
        pine.close()

    hits = rank(snaps)
    print(f"\n{len(hits)} field(s) constant within each pose and different across all three:")
    for width, off, per_pose in hits[:25]:
        line = "  ".join(f"{p} {per_pose[p]:#x}" for p in POSES)
        hint = ""
        if width == 4:
            fs = {p: struct.unpack("<f", struct.pack("<I", per_pose[p]))[0] for p in POSES}
            if all(abs(v) < 1e6 for v in fs.values()):
                hint = "   as f32: " + "  ".join(f"{p} {fs[p]:.4f}" for p in POSES)
            halves = {p: struct.unpack("<hh", struct.pack("<I", per_pose[p])) for p in POSES}
            hint += "   as 2 x s16: " + "  ".join(f"{p} {halves[p][0]}/{halves[p][1]}" for p in POSES)
        if width == 2:
            ss = {p: struct.unpack("<h", struct.pack("<H", per_pose[p]))[0] for p in POSES}
            hint = "   as s16: " + "  ".join(f"{p} {ss[p]}" for p in POSES)
        print(f"  +0x{off:04X} ({width} byte{'s' if width > 1 else ''}): {line}{hint}")
    out_dir = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "results")
    os.makedirs(out_dir, exist_ok=True)
    path = os.path.join(out_dir, f"aimscan-{datetime.datetime.now():%Y%m%d-%H%M%S}.json")
    with open(path, "w", encoding="utf-8") as f:
        json.dump({"info": info, "player": idx, "base": base,
                   "hits": [{"offset": off, "width": w, "values": v} for w, off, v in hits],
                   "snapshots": {p: [r.hex() for r in rs] for p, rs in snaps.items()}}, f)
    print(f"\nSaved {path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
