#!/usr/bin/env python3
"""READ-ONLY: record what the standalone patch and the game do while you aim, to see why the gun
doesn't point where the camera looks.

  python tools/f1_aimlog.py [seconds]      (default 60)

Play normally with the patch on: aim at things, look up and down. Every time the gun looks wrong,
click L3 (press the left stick in): that puts a numbered MARK in the log; say what you saw at each
mark ("mark 3: gun at the floor, crosshair on the zombie's head").

About 60 samples a second, each: our player's action and aim target, her heading, the 6 bytes at
+0xBC8 (the elevation we write is the first s16; the others are logged in case the gun pose uses
them), the camera's position and look-at, the pad, and the patch's own status block (its yaw,
pitch, the elevation it wrote, convergence distance, gun-angle factor). Saved to
results/aimlog-<time>.json -- send that file. A summary prints every half second.
From a PC with adb, each mark also saves a screenshot of the device (results/aimlog-<time>-mark<N>.png),
so the gun and the crosshair can be seen at that moment.
"""
import datetime
import json
import math
import os
import shutil
import struct
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import phase2_check  # noqa: E402
from f1_live import our_player  # noqa: E402

STATUS = 0x000AFF00
CAMERA = 0x00306320
PAD = 0x00292D40
L3 = 0x0200
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def deg(units):
    return units * 360.0 / 65536.0


def adb_serial():
    """The device to screenshot, or None (on the device itself, or no adb)."""
    if phase2_check.on_android():
        return None
    adb = shutil.which("adb")
    if not adb:
        return None
    rc, out, _ = phase2_check.run([adb, "devices"])
    serials = [l.split()[0] for l in out.splitlines()[1:] if l.strip() and "device" in l.split()[1:2]]
    serial, _ = phase2_check.pick_device(adb, serials)
    return (adb, serial) if serial else None


def screenshot(dev, path):
    """Start a device screenshot into `path` without waiting for it."""
    adb, serial = dev
    f = open(path, "wb")
    subprocess.Popen([adb, "-s", serial, "exec-out", "screencap", "-p"], stdout=f, stderr=subprocess.DEVNULL)
    f.close()


def sample(pine, base):
    st, pl, aim, elev6, cdc, cam, pad = pine.read_multi([
        (STATUS, 0x70), (base, 0x94), (base + 0x24, 4), (base + 0xBC8, 6), (base + 0xCDC, 1),
        (CAMERA + 0x18, 24), (PAD, 8)])
    s = {
        "t": time.perf_counter(),
        "act": list(pl[0x08:0x0C]),
        "pos": struct.unpack_from("<3f", pl, 0x38),
        "heading": deg(struct.unpack_from("<H", pl, 0x92)[0]),
        "target": struct.unpack("<I", aim)[0],
        "target_idx": cdc[0],
        "elev_raw": list(struct.unpack("<3h", elev6)),
        "cam_pos": struct.unpack_from("<3f", cam, 0),
        "cam_look": struct.unpack_from("<3f", cam, 12),
        "buttons": struct.unpack_from("<H", pad, 2)[0],
        "sticks": list(pad[4:8]),
    }
    magic, version = struct.unpack_from("<2I", st, 0)
    if magic == 0x434D4F31:
        f = struct.unpack_from("<4I3fI I3f 3f3f I I f IIIf f f f", st, 0)
        # mod/mod.h ModStatus: 0..3 magic version frames caller, 4..6 player, 7 starts, 8 enabled,
        # 9 yaw, 10 pitch, 11 dt, 12..14 eye, 15..17 look, 18 facing, 19 callout_dz, 20 pull,
        # 21 aiming, 22 free_aim, 23 servo, 24 elev, 25 game_fov, 26 converge, 27 gain
        s["patch"] = {"version": version, "enabled": f[8], "yaw": math.degrees(f[9]), "pitch": math.degrees(f[10]),
                      "pull": f[20], "aiming": f[21], "free_aim": f[22], "servo": f[23],
                      "elev_written": math.degrees(f[24]), "fov": f[25], "converge": f[26],
                      "gain": f[27] if version >= 7 else None}
    return s


def main(argv):
    seconds = float(argv[1]) if len(argv) > 1 else 60.0
    pine, info = phase2_check.connect_game()
    idx, base = our_player(pine)
    print(f"logging our player #{idx} for {seconds:g} s. Aim around; click L3 whenever the gun looks wrong.")
    stamp = f"{datetime.datetime.now():%Y%m%d-%H%M%S}"
    os.makedirs(os.path.join(ROOT, "results"), exist_ok=True)
    dev = adb_serial()
    print("screenshots at each mark: " + (f"yes (adb {dev[1]})" if dev else "no (no adb here)"))
    log, marks, last_b, t0, t_print = [], 0, 0, time.perf_counter(), 0.0
    try:
        while time.perf_counter() - t0 < seconds:
            s = sample(pine, base)
            s["t"] -= t0
            if s["buttons"] & L3 and not last_b & L3 and s["buttons"] != 0xFFFF:
                marks += 1
                s["mark"] = marks
                print(f"  ---- MARK {marks} at {s['t']:.2f} s ----", flush=True)
                if dev:
                    screenshot(dev, os.path.join(ROOT, "results", f"aimlog-{stamp}-mark{marks}.png"))
            last_b = s["buttons"]
            log.append(s)
            if s["t"] - t_print >= 0.5:
                t_print = s["t"]
                p = s.get("patch") or {}
                look = [s["cam_look"][i] - s["cam_pos"][i] for i in range(3)]
                cam_elev = math.degrees(math.atan2(look[1], math.hypot(look[0], look[2])))
                view = math.degrees(math.atan2(look[0], look[2])) % 360
                game_elev = deg(s["elev_raw"][0])
                print(f"  {s['t']:6.2f}s act {s['act'][0]}/{s['act'][1]:<3} view {view:6.1f} heading {s['heading']:6.1f}  "
                      f"camera tilt {cam_elev:+6.1f}  gun written {p.get('elev_written', float('nan')):+6.1f} "
                      f"read {game_elev:+6.1f} (raw {s['elev_raw']})  conv {p.get('converge', 0):5.0f} "
                      f"x{(p.get('gain') or 0):.2f}" + ("  FREE" if p.get("free_aim") else ""), flush=True)
            time.sleep(max(0.0, 1 / 60 - (time.perf_counter() - t0 - s["t"])))
    except KeyboardInterrupt:
        pass
    finally:
        pine.close()
    out = os.path.join(ROOT, "results", f"aimlog-{stamp}.json")
    with open(out, "w") as f:
        json.dump({"game": info, "player": idx, "samples": log}, f)
    print(f"\n{len(log)} samples, {marks} marks -> {out}" + (f" (+ {marks} screenshots next to it)" if dev and marks else "")
          + "\nSend the file(s), and say what you saw at each mark.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
