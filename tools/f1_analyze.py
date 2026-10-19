#!/usr/bin/env python3
"""Analyze an f1_live.py log: angle conventions, input encoding, camera behaviour.

Usage: python tools/f1_analyze.py results/f1live-XXXXXXXX-XXXXXX.json
"""
import collections
import json
import math
import statistics
import sys


def wrap(a):
    return (a + math.pi) % (2 * math.pi) - math.pi


def main(path):
    log = json.load(open(path, encoding="utf-8"))
    S = log["samples"]
    print(log["info"])
    print(f"{len(S)} samples over {S[-1]['t'] - S[0]['t']:.1f} s; "
          f"read p50 {statistics.median(s['read_ms'] for s in S):.1f} ms")

    # Heading convention: compare heading with the direction actually travelled.
    rows = []
    for a, b in zip(S, S[1:]):
        dx, dz = b["p0_pos"][0] - a["p0_pos"][0], b["p0_pos"][2] - a["p0_pos"][2]
        if math.hypot(dx, dz) >= 2 and a["p0_heading"] == b["p0_heading"]:
            rows.append((a["p0_heading"] * 2 * math.pi / 65536, dx, dz))
    candidates = {
        "atan2(dx, dz)": lambda dx, dz: math.atan2(dx, dz),
        "atan2(-dx, dz)": lambda dx, dz: math.atan2(-dx, dz),
        "atan2(dz, dx)": lambda dx, dz: math.atan2(dz, dx),
        "atan2(-dz, dx)": lambda dx, dz: math.atan2(-dz, dx),
    }
    print(f"\nheading vs travel ({len(rows)} moving samples; ~180 deg outliers = walking backwards):")
    for name, f in candidates.items():
        if rows:
            err = sorted(abs(wrap(f(dx, dz) - h)) * 180 / math.pi for h, dx, dz in rows)
            fwd = sum(e < 10 for e in err)
            print(f"  heading = {name:15} median err {err[len(err) // 2]:6.2f} deg, within 10 deg: {fwd}/{len(err)}")

    # Right stick -> the game's angle/magnitude.
    print("\nright stick (raw rx, ry) -> Ctrl rangle / rmag:")
    seen = set()
    for s in S:
        rx, ry = s["pad_sticks"][2:]
        c = s["ctrl_pad0"]
        if c["rmag"] and (rx, ry) not in seen and len(seen) < 8:
            seen.add((rx, ry))
            print(f"  rx {rx:3} ry {ry:3} -> rangle 0x{c['rangle']:04X} ({c['rangle'] * 360 / 65536:5.1f} deg) rmag {c['rmag']}")

    # Buttons: stored value is active-HIGH (all zero at rest), contrary to the header comment.
    rest = collections.Counter(s["pad_buttons_pressed"] for s in S).most_common(1)[0][0]
    print(f"\nmost common decoded 'pressed' value: 0x{rest:04X} "
          f"({'field is active-high as stored' if rest == 0xFFFF else 'field is active-low'})")

    # Camera.
    dist = [math.dist(s["cam_pos"], s["p0_pos"]) for s in S]
    height = [s["cam_target"][1] - s["p0_pos"][1] for s in S]
    jumps = sum(math.dist(a["cam_pos"], b["cam_pos"]) > 300 for a, b in zip(S, S[1:]))
    print(f"\ncamera->player distance p50 {statistics.median(dist):.0f} (range {min(dist):.0f}-{max(dist):.0f}); "
          f"target height above player p50 {statistics.median(height):.0f}")
    print(f"camera jumps >300 units between samples: {jumps}; "
          f"cam_cut flag values: {dict(collections.Counter(s['cam_cut'] for s in S))}")
    print(f"player count values: {dict(collections.Counter(s['player_count'] for s in S))}")


if __name__ == "__main__":
    main(sys.argv[1])
