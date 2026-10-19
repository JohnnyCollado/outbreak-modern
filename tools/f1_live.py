#!/usr/bin/env python3
"""Live, READ-ONLY view of File #1's camera, players and pad, from the decomp's addresses.

Purpose: verify on the user's own disc that the addresses published by the CC0 decomp
github.com/GalaxyNostalgia/Outbreak-decomp (File #1 = SLPM-65428 VER 1.04) are what it
says they are, before anything writes to them. Nothing here writes guest memory.

What to do while it runs (each one checks one claim; answers go in the saved log):
  1. Walk around       -> player 0 pos changes, heading turns, camera pos/target follow
  2. Go through a door  -> camera pos/target jump (a new room camera)
  3. Push each stick    -> raw pad sticks move off 128; Ctrl angles/magnitudes change
  4. Press buttons      -> raw buttons (active low) and Ctrl.cur bits change

Usage (Odin attached, `adb forward tcp:28011 tcp:28011` done, File #1 in gameplay):
  python tools/f1_live.py               # ~10 refreshes/s until Ctrl+C
  python tools/f1_live.py --seconds 60  # stop on its own; log saved to results/
"""
import argparse
import datetime
import json
import math
import os
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pine_diag  # noqa: E402

# --- Addresses: all from the decomp, File #1 JP VER 1.04 (SLPM-65428). NOT yet verified live. ---
# include/camera.h       Camera at 0x306320: pos +0x18, target +0x24, up +0x3C, far +0x58,
#                        near +0x5C, fov +0x60 (degrees), roll +0x6C (s32)
# include/pad.h          PadData "this frame" at 0x292D40: buttons u16 +2 (active low),
#                        sticks u8[4] +4 (lx, ly, rx, ry; 128 = centre)
# include/ctrl.h         Ctrl (0x30): cur u32 +8, rmag/rangle +0x20, lmag/langle +0x28
#                        one per pad at 0x306410, and each player's own at +0xF78
# include/object.h       players: DrawObjFull[] at 0x476DD0, stride 0x10E0; pos f32[3] +0x38,
#                        heading s16 +0x92; count at 0x23BE14 (game/talk.c, roomitems.c)
# game/charmove.c        "the camera cut this frame" u8[] at 0x3AEF54
CAMERA = 0x00306320
PAD_NOW = 0x00292D40
CTRL_PAD0 = 0x00306410
PLAYERS = 0x00476DD0
PLAYER_STRIDE = 0x10E0
PLAYER_COUNT = 0x0023BE14
CAM_CUT = 0x003AEF54
EXPECTED = {"id": "SLPM-65428", "game_version": "1.04"}


def f3(b, off):
    return struct.unpack_from("<3f", b, off)


def fmt3(v):
    return "(" + ", ".join(f"{x:9.2f}" for x in v) + ")"


def sane(v, limit=1e6):
    return all(math.isfinite(x) and abs(x) < limit for x in v)


def ctrl_fields(b):
    cur, = struct.unpack_from("<I", b, 0x08)
    rmag, rangle, rx, ry, lmag, langle, lx, ly = struct.unpack_from("<8h", b, 0x20)
    return {"cur": cur, "lmag": lmag, "langle": langle & 0xFFFF, "rmag": rmag, "rangle": rangle & 0xFFFF}


def sample(pine):
    cam = pine.read_block(CAMERA, 0x70)
    pad = pine.read_block(PAD_NOW, 0x08)
    ctrl = pine.read_block(CTRL_PAD0, 0x30)
    count = pine.read32(PLAYER_COUNT)
    p0 = pine.read_block(PLAYERS, 0x94)
    p0ctrl = pine.read_block(PLAYERS + 0xF78, 0x30)
    cut = pine.read_block(CAM_CUT, 4)[0]
    return {
        "t": time.time(),
        "cam_pos": f3(cam, 0x18), "cam_target": f3(cam, 0x24), "cam_up": f3(cam, 0x3C),
        "cam_far": struct.unpack_from("<f", cam, 0x58)[0], "cam_near": struct.unpack_from("<f", cam, 0x5C)[0],
        "cam_fov": struct.unpack_from("<f", cam, 0x60)[0], "cam_roll": struct.unpack_from("<i", cam, 0x6C)[0],
        "pad_status": pad[0], "pad_buttons_pressed": (~struct.unpack_from("<H", pad, 2)[0]) & 0xFFFF,
        "pad_sticks": list(pad[4:8]),
        "ctrl_pad0": ctrl_fields(ctrl), "ctrl_player0": ctrl_fields(p0ctrl),
        "player_count": count, "p0_pos": f3(p0, 0x38),
        "p0_heading": struct.unpack_from("<h", p0, 0x92)[0] & 0xFFFF, "cam_cut": cut,
    }


def checks(s):
    """Plausibility of each claim from one sample. Movement claims are judged by the user."""
    return {
        "camera floats finite": sane(s["cam_pos"] + s["cam_target"] + s["cam_up"]),
        "fov looks like degrees (1..120)": 1 <= s["cam_fov"] <= 120,
        "near < far": 0 < s["cam_near"] < s["cam_far"],
        "player count 1..4": 1 <= s["player_count"] <= 4,
        "player 0 position finite": sane(s["p0_pos"]),
        "pad status read (not 0xFF)": s["pad_status"] != 0xFF,
    }


def render(s):
    c, pc = s["ctrl_pad0"], s["ctrl_player0"]
    lines = [
        f"camera   pos {fmt3(s['cam_pos'])}  target {fmt3(s['cam_target'])}",
        f"         up  {fmt3(s['cam_up'])}  fov {s['cam_fov']:.2f}  near {s['cam_near']:.1f}  far {s['cam_far']:.1f}"
        f"  roll {s['cam_roll']}  cut {s['cam_cut']}",
        f"players  count {s['player_count']}  p0 pos {fmt3(s['p0_pos'])}  heading 0x{s['p0_heading']:04X}"
        f" ({s['p0_heading'] * 360 / 65536:6.1f} deg)",
        f"raw pad  status 0x{s['pad_status']:02X}  pressed 0x{s['pad_buttons_pressed']:04X}"
        f"  sticks lx {s['pad_sticks'][0]:3} ly {s['pad_sticks'][1]:3} rx {s['pad_sticks'][2]:3} ry {s['pad_sticks'][3]:3}",
        f"ctrl pad0     cur 0x{c['cur']:08X}  L mag {c['lmag']:4} ang 0x{c['langle']:04X}  R mag {c['rmag']:4} ang 0x{c['rangle']:04X}",
        f"ctrl player0  cur 0x{pc['cur']:08X}  L mag {pc['lmag']:4} ang 0x{pc['langle']:04X}  R mag {pc['rmag']:4} ang 0x{pc['rangle']:04X}",
        "checks   " + "  ".join(("OK " if v else "BAD ") + k for k, v in checks(s).items()),
    ]
    return "\n".join(lines)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", type=int, default=28011)
    ap.add_argument("--hz", type=float, default=10.0)
    ap.add_argument("--seconds", type=float, help="stop after this long (default: until Ctrl+C)")
    a = ap.parse_args(argv)

    try:
        pine = pine_diag.Pine("127.0.0.1", a.port, 5.0)
        info = pine_diag.identify(pine)
    except (OSError, pine_diag.PineError) as e:
        print(f"PINE not reachable: {e}. Run tools/phase2_check.py first.")
        return 1
    print(f"{info['version']} | {info['id']} | CRC {info['crc']} | disc version {info['game_version']}")
    mismatch = [k for k, v in EXPECTED.items() if (info.get(k) or "").strip() != v]
    if mismatch:
        print(f"!! Expected {EXPECTED}; got id={info.get('id')!r} version={info.get('game_version')!r}.\n"
              "!! The decomp's addresses are for File #1 VER 1.04 only. Reading anyway (read-only),\n"
              "!! but values will be meaningless on another build.")

    log = {"info": info, "addresses_source": "GalaxyNostalgia/Outbreak-decomp (CC0)", "samples": []}
    t_end = time.time() + a.seconds if a.seconds else None
    ansi = os.environ.get("TERM") or os.name == "nt"
    if os.name == "nt":
        os.system("")  # enables ANSI escapes in Windows consoles
    try:
        while t_end is None or time.time() < t_end:
            t0 = time.perf_counter()
            s = sample(pine)
            s["read_ms"] = (time.perf_counter() - t0) * 1000
            log["samples"].append(s)
            out = render(s) + f"\n(sample took {s['read_ms']:.1f} ms; Ctrl+C to stop)"
            print(("\x1b[H\x1b[J" if ansi else "\n") + out, flush=True)
            time.sleep(max(0.0, 1.0 / a.hz - (time.perf_counter() - t0)))
    except KeyboardInterrupt:
        pass
    except (OSError, pine_diag.PineError) as e:
        print(f"\nPINE error: {e}")
    finally:
        pine.close()

    out_dir = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "results")
    os.makedirs(out_dir, exist_ok=True)
    path = os.path.join(out_dir, f"f1live-{datetime.datetime.now():%Y%m%d-%H%M%S}.json")
    with open(path, "w", encoding="utf-8") as f:
        json.dump(log, f, indent=1)
    if log["samples"]:
        summary = {k: sum(checks(s)[k] for s in log["samples"]) for k in checks(log["samples"][0])}
        n = len(log["samples"])
        print("\nPlausibility over the run:\n" + "\n".join(f"  {v:>5}/{n}  {k}" for k, v in summary.items()))
        moved = {k: len({tuple(s[k]) if isinstance(s[k], (list, tuple)) else s[k] for s in log["samples"]})
                 for k in ("cam_pos", "p0_pos", "p0_heading", "pad_sticks")}
        print("Distinct values seen (1 = never changed): " + ", ".join(f"{k} {v}" for k, v in moved.items()))
    print(f"Saved {path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
