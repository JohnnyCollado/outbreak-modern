#!/usr/bin/env python3
"""READ-ONLY: find where File #1 keeps the aim elevation (up / level / down).

The decomp does not cover the aiming action's steps or the shot code yet, so this finds the
field empirically: you hold each aim pose while the tool snapshots our player's whole
DrawObjFull (0x10E0 bytes, one PINE round trip per sample), twice per pose, and it ranks the
fields that are stable within a pose and different between poses. Nothing is written.

Usage (in gameplay, a gun equipped, no enemy near so the auto-aim doesn't turn you):
  python tools/f1_aimscan.py              # find the field (done: +0xBC8, see AIM_ELEV)
  python tools/f1_aimscan.py watch 20     # log how the elevation changes over time
  python tools/f1_aimscan.py writers      # list the code that loads/stores +0xBC8/+0xBCC
  python tools/f1_aimscan.py dump         # print the servo candidates' code words (paste, don't commit)
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


# --------------------------------------------------------------- watch / writers (read-only)
AIM_ELEV = 0x0BC8      # s16, 0x10000 per turn: 0 level, +0x1FFF up, -0x1FFF down (aimscan, device)
REGS = ["zero", "at", "v0", "v1", "a0", "a1", "a2", "a3", "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7",
        "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7", "t8", "t9", "k0", "k1", "gp", "sp", "fp", "ra"]
MEM_OPS = {0x28: "sb", 0x29: "sh", 0x2B: "sw", 0x3F: "sd", 0x1F: "sq",
           0x20: "lb", 0x24: "lbu", 0x21: "lh", 0x25: "lhu", 0x23: "lw", 0x37: "ld", 0x1E: "lq"}
# Where code lives (docs/LAYOUT.md in the decomp): main ELF code, and the overlay slots.
REGIONS = ((0x00100000, 0x001D9B00, "main"), (0x00340000, 0x00540000, "submain slot"),
           (0x00540000, 0x00900000, "game/overlay slots"))


def cmd_watch(seconds):
    """Log every change of the aim fields at the fastest PINE rate, to see if the game ramps the
    elevation (a servo that would fight a written value) or jumps it."""
    pine, _ = connect()
    try:
        idx, base = our_player(pine)
        print(f"watching our player #{idx} for {seconds:g} s: aim, then move up/down slowly and quickly")
        t0, last = time.perf_counter(), None
        while time.perf_counter() - t0 < seconds:
            (pl, ctrl) = pine.read_multi([(base, 0xCD4), (base + 0xF78 + 8, 4)])
            cur = (pl[8], struct.unpack_from("<h", pl, AIM_ELEV)[0], struct.unpack_from("<I", pl, 0xBCC)[0],
                   pl[0xCD2], struct.unpack("<I", ctrl)[0])
            if cur != last:
                print(f"  {1000 * (time.perf_counter() - t0):7.0f} ms  action {cur[0]}  elevation {cur[1]:6} "
                      f"({cur[1] * 360 / 65536:6.1f} deg)  +0xBCC {cur[2]:#06x}  +0xCD2 {cur[3]:#04x}  "
                      f"controls {cur[4]:#06x}", flush=True)
                last = cur
    finally:
        pine.close()
    return 0


def cmd_writers(offsets):
    """List every load/store with an immediate offset in `offsets` in the loaded code. These are
    candidates for what writes (servo) and reads (shot direction) the aim elevation."""
    pine, _ = connect()
    found = []
    try:
        for lo, hi, name in REGIONS:
            print(f"scanning {name} 0x{lo:08X}-0x{hi:08X} ...", flush=True)
            for addr in range(lo, hi, 0x10000):
                data = pine.read_block(addr, 0x10000)
                for i in range(0, len(data), 4):
                    w = struct.unpack_from("<I", data, i)[0]
                    op, imm = w >> 26, w & 0xFFFF
                    if op in MEM_OPS and imm in offsets:
                        found.append((addr + i, name, w))
    finally:
        pine.close()
    for at, name, w in found:
        op = MEM_OPS[w >> 26]
        print(f"  0x{at:08X} [{name:>18}]  {op:4} ${REGS[(w >> 16) & 31]}, 0x{w & 0xFFFF:X}(${REGS[(w >> 21) & 31]})")
    print(f"{len(found)} match(es). Stores are servo candidates, loads shot-code candidates. "
          "Some may be data that happens to look like an instruction.")
    return 0


def cmd_dump(ranges):
    """Print raw code words so they can be pasted into a chat for disassembly.
    Deliberately NOT saved to a file: game code must not end up in the repository."""
    pine, _ = connect()
    try:
        for lo, hi in ranges:
            data = pine.read_block(lo, hi - lo)
            print(f"--- 0x{lo:08X}-0x{hi:08X}")
            for i in range(0, len(data), 32):
                words = struct.unpack_from(f"<{min(8, (len(data) - i) // 4)}I", data, i)
                print(f"{lo + i:08X}: " + " ".join(f"{w:08X}" for w in words))
    finally:
        pine.close()
    return 0


def main():
    if len(sys.argv) > 1 and sys.argv[1] == "dump":
        # The two store clusters `writers` found in the user's build (English-patched File #1)
        return cmd_dump([(0x00556700, 0x00556B00), (0x00648B00, 0x00648D80)])
    if len(sys.argv) > 1 and sys.argv[1] == "watch":
        return cmd_watch(float(sys.argv[2]) if len(sys.argv) > 2 else 20)
    if len(sys.argv) > 1 and sys.argv[1] == "writers":
        return cmd_writers({AIM_ELEV, AIM_ELEV + 4})
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
