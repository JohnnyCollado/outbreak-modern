#!/usr/bin/env python3
"""First WRITE test for File #1: a render-time camera override hook, driven from the PC.

  freemem    READ-ONLY. Samples EE RAM 0x80000-0x100000 while you play and lists runs that
             stayed zero the whole time: candidate homes for the hook. Writes nothing.
  install    Writes our hook into a free run and redirects the start of func_00345740 (the
             function that hands the camera to the renderer, every frame) to it.
  run        Over-the-shoulder camera from player 0's position/heading, right stick orbits.
             Ctrl+C hands the camera back to the game (the hook stays, inert).
  status     Is the hook installed, is it being called, is the cave intact?
  uninstall  Puts the two original instructions back.

Safety: MAKE A SAVE STATE FIRST. Loading it undoes everything this tool does (EE RAM is
part of the state). Writes go only to the free run you chose and to the 8 bytes at the hook.
`install` refuses unless the build is SLPM-65428 1.04, the hook site still matches the decomp
(tools/f1_hookcheck.py), the 2 instructions it displaces are plain (no branch, no PC use), and
the chosen run is still all zero.

How the hook works (all addresses from the CC0 decomp, verified live except where noted):
  func_00345740 starts with two ordinary instructions (its prologue). We copy them into our
  cave, then replace them with `j cave ; nop` in ONE aligned 8-byte write. The cave:
     counter += 1                              (proves it runs)
     if flags & 1: camera.pos, camera.target = ours   (6 words into 0x306320 + 0x18)
     if flags & 2: camera.fov = ours
     <original instruction 1> <original instruction 2>
     j func_00345740 + 8
  The game computes its own camera as usual; ours replaces it just before it is handed to the
  renderer, so the PC's timing no longer matters. Only t0-t3 are used: at function entry the
  caller cannot expect them preserved (MIPS calling convention), and the function takes no
  arguments.
"""
import argparse
import json
import math
import os
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import f1_hookcheck  # noqa: E402
import mips  # noqa: E402
import phase2_check  # noqa: E402
import pine_diag  # noqa: E402
from f1_live import CAMERA, PAD_NOW, our_player  # noqa: E402

HOOK = 0x00345740
FREE_LO, FREE_HI = 0x00080000, 0x00100000  # below the ELF (loads at 0x100000); verified by `freemem`
MAGIC = 0x434D424F  # "OBMC"

# Block layout (data first, code at +0x40)
B_FLAGS, B_POS, B_TARGET, B_FOV, B_COUNT, B_ORIG1, B_ORIG2, B_MAGIC, B_CODE = (
    0x00, 0x04, 0x10, 0x1C, 0x20, 0x24, 0x28, 0x2C, 0x40)
BLOCK_SIZE = 0x100
STATE_FILE = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "results", "f1hook-state.json")


def build_cave(block, orig1, orig2, hook=HOOK, camera=CAMERA):
    """The cave's machine code, to be written at block + B_CODE. Returns a list of u32."""
    code = []
    pc = lambda: block + B_CODE + 4 * len(code)  # noqa: E731
    bh, bl = mips.hi_lo(block)
    ch, cl = mips.hi_lo(camera)
    code += [mips.lui("t0", bh), mips.addiu("t0", "t0", bl)]                    # t0 = block
    code += [mips.lw("t1", B_COUNT, "t0"), mips.addiu("t1", "t1", 1), mips.sw("t1", B_COUNT, "t0")]
    code += [mips.lw("t1", B_FLAGS, "t0"), mips.lui("t2", ch), mips.addiu("t2", "t2", cl)]  # t2 = camera
    code += [mips.andi("t3", "t1", 1)]
    skip_pos_at = len(code)
    code += [None, mips.NOP]                                                   # beq t3, zero, skip_pos
    for i in range(6):                                                         # pos[3] + target[3]
        code += [mips.lw("t3", B_POS + 4 * i, "t0"), mips.sw("t3", 0x18 + 4 * i, "t2")]
    skip_pos = pc()
    code[skip_pos_at] = mips.beq("t3", "zero", block + B_CODE + 4 * skip_pos_at, skip_pos)
    code += [mips.andi("t3", "t1", 2)]
    skip_fov_at = len(code)
    code += [None, mips.NOP]
    code += [mips.lw("t3", B_FOV, "t0"), mips.sw("t3", 0x60, "t2")]
    code[skip_fov_at] = mips.beq("t3", "zero", block + B_CODE + 4 * skip_fov_at, pc())
    code += [orig1, orig2]                                                     # the displaced prologue
    code += [mips.j(hook + 8, pc()), mips.NOP]
    return code


def trampoline(block):
    """The 8 bytes that replace the hook's first two instructions, as one u64 (little endian)."""
    return mips.j(block + B_CODE, HOOK) | (mips.NOP << 32)


def connect():
    ok, msg = phase2_check.ensure_forward(phase2_check.shutil.which("adb") or "adb", 28011)
    print(("adb: " if ok else "adb problem: ") + msg)
    pine = pine_diag.Pine("127.0.0.1", 28011, 5.0)
    info = pine_diag.identify(pine)
    print(f"{info['id']} CRC {info['crc']} disc version {info['game_version']}")
    return pine, info


def words_at(pine, addr, n):
    return list(struct.unpack(f"<{n}I", pine.read_block(addr, 4 * n)))


# ---------------------------------------------------------------- freemem
def cmd_freemem(a):
    pine, _ = connect()
    chunk, size = 0x10000, FREE_HI - FREE_LO
    ever_nonzero = bytearray(size)
    t_end, passes = time.time() + a.seconds, 0
    print(f"Sampling 0x{FREE_LO:06X}-0x{FREE_HI:06X} for {a.seconds:.0f} s. Play normally: walk, open a door, "
          "open the menu.")
    while time.time() < t_end or passes == 0:
        for off in range(0, size, chunk):
            data = pine.read_block(FREE_LO + off, chunk)
            for i in range(0, chunk, 4):
                if data[i:i + 4] != b"\0\0\0\0":
                    ever_nonzero[off + i] = 1
        passes += 1
        print(f"  pass {passes} done", flush=True)
    pine.close()
    runs, start = [], None
    for i in range(0, size + 4, 4):
        zero = i < size and not ever_nonzero[i]
        if zero and start is None:
            start = i
        elif not zero and start is not None:
            runs.append((FREE_LO + start, i - start))
            start = None
    runs.sort(key=lambda r: -r[1])
    print(f"\n{passes} passes. Largest regions that stayed zero:")
    for addr, n in runs[:8]:
        print(f"  0x{addr:08X} - 0x{addr + n:08X}  ({n // 1024} KiB)")
    if runs and runs[0][1] >= 0x1000:
        best = (runs[0][0] + 0x400 + 0xFF) & ~0xFF  # keep 1 KiB away from whatever borders it
        print(f"\nSuggested hook address: 0x{best:08X}   (python tools\\f1_camtest.py install --at 0x{best:08X})")
    else:
        print("\nNo zero run of 4 KiB or more: do not install. Paste this output to Claude.")
    return 0


# ---------------------------------------------------------------- install / status / uninstall
def preflight(pine, info):
    if info.get("id") != "SLPM-65428" or info.get("game_version") != "1.04":
        raise SystemExit("refusing: this hook is only for SLPM-65428 disc version 1.04")
    for addr, f in f1_hookcheck.FUNCS.items():
        words = words_at(pine, addr, f["size"] // 4)
        pro, in_order, _ = f1_hookcheck.check(words, f["calls"])
        if addr == HOOK and words[0] >> 26 == 0x02:
            return words  # already redirected; caller decides
        if not (pro and in_order):
            raise SystemExit(f"refusing: 0x{addr:08X} does not match the decomp (in gameplay? run f1_hookcheck)")
    return words_at(pine, HOOK, 2)


def cmd_install(a):
    block = a.at
    if not (FREE_LO <= block and block + BLOCK_SIZE <= FREE_HI and block % 0x100 == 0):
        raise SystemExit(f"refusing: --at must be 256-aligned inside 0x{FREE_LO:X}-0x{FREE_HI:X}")
    pine, info = connect()
    try:
        orig1, orig2 = preflight(pine, info)[:2]
        if orig1 >> 26 == 0x02:
            raise SystemExit("hook already installed (run `status`, or `uninstall` first)")
        for w in (orig1, orig2):
            if mips.is_branch_or_jump(w):
                raise SystemExit(f"refusing: displaced instruction 0x{w:08X} is control flow")
        if not f1_hookcheck.is_prologue(orig1):
            raise SystemExit(f"refusing: first instruction 0x{orig1:08X} is not a prologue")
        if any(words_at(pine, block, BLOCK_SIZE // 4)):
            raise SystemExit(f"refusing: 0x{block:08X}..+0x{BLOCK_SIZE:X} is not all zero any more")

        cave = build_cave(block, orig1, orig2)
        header = [0] * (B_CODE // 4)
        header[B_ORIG1 // 4], header[B_ORIG2 // 4], header[B_MAGIC // 4] = orig1, orig2, MAGIC
        pine.write_words(block, header + cave)                 # 1. data + code, hook still inactive
        if words_at(pine, block, len(header + cave)) != header + cave:
            raise SystemExit("cave did not read back intact; nothing redirected")
        pine.write64(HOOK, trampoline(block))                  # 2. redirect, one atomic store
        os.makedirs(os.path.dirname(STATE_FILE), exist_ok=True)
        with open(STATE_FILE, "w") as f:
            json.dump({"block": block, "orig1": orig1, "orig2": orig2, "crc": info["crc"]}, f)
        time.sleep(0.5)
        n = words_at(pine, block + B_COUNT, 1)[0]
        print(f"installed at 0x{block:08X}; hook called {n} times in 0.5 s "
              f"({'running' if n else 'NOT called yet: are you in gameplay?'})")
    finally:
        pine.close()
    return 0


def find_block(pine):
    w = words_at(pine, HOOK, 2)
    if w[0] >> 26 != 0x02:
        return None, w
    block = ((w[0] & 0x03FFFFFF) << 2) - B_CODE
    return block, w


def cmd_status(a):
    pine, _ = connect()
    try:
        block, w = find_block(pine)
        if block is None:
            print(f"hook NOT installed (0x{HOOK:08X} = 0x{w[0]:08X} 0x{w[1]:08X})")
            return 1
        hdr = words_at(pine, block, B_CODE // 4)
        c0 = hdr[B_COUNT // 4]
        time.sleep(1.0)
        c1 = words_at(pine, block + B_COUNT, 1)[0]
        print(f"hook -> 0x{block:08X}; magic {'ok' if hdr[B_MAGIC // 4] == MAGIC else 'MISSING'}; "
              f"flags {hdr[B_FLAGS // 4]}; calls/s {c1 - c0}")
    finally:
        pine.close()
    return 0


def cmd_uninstall(a):
    pine, _ = connect()
    try:
        block, _ = find_block(pine)
        if block is None:
            print("hook not installed; nothing to do")
            return 0
        hdr = words_at(pine, block, B_CODE // 4)
        if hdr[B_MAGIC // 4] != MAGIC:
            raise SystemExit("refusing: cave magic missing, cannot trust its saved instructions. Load your save state.")
        pine.write_words(block + B_FLAGS, [0])
        pine.write64(HOOK, hdr[B_ORIG1 // 4] | (hdr[B_ORIG2 // 4] << 32))
        print("original instructions restored; the game's camera is back")
    finally:
        pine.close()
    return 0


# ---------------------------------------------------------------- run
def stick(v, dead=0.15):
    x = (v - 127.5) / 127.5
    return 0.0 if abs(x) < dead else max(-1.0, min(1.0, (x - math.copysign(dead, x)) / (1 - dead)))


def ots_camera(p, heading, yaw_off, pitch, a):
    """Eye and look-at behind/over the shoulder.

    She faces along the heading field (player +0x92), atan2(dx, dz): on the device,
    --yaw-offset 0 shows her back and 180 her face (user, 2026-10-19).
    """
    yaw = heading + yaw_off + math.radians(a.yaw_offset)
    fx, fz = math.sin(yaw), math.cos(yaw)           # where the camera looks, on the floor plane
    rx, rz = fz, -fx                                # perpendicular; sign of "right" set by --shoulder
    cp, sp = math.cos(pitch), math.sin(pitch)
    eye = (p[0] - fx * a.distance * cp + rx * a.shoulder,
           p[1] + a.height + a.distance * sp,
           p[2] - fz * a.distance * cp + rz * a.shoulder)
    look = (p[0] + fx * a.lookahead + rx * a.shoulder,
            p[1] + a.target_height,
            p[2] + fz * a.lookahead + rz * a.shoulder)
    return eye, look


def cmd_run(a):
    pine, _ = connect()
    block, _ = find_block(pine)
    if block is None:
        raise SystemExit("hook not installed: run `install` first")
    yaw_off, pitch, last = 0.0, math.radians(a.pitch), time.perf_counter()
    smooth_heading = None
    flags = 1 | (2 if a.fov else 0)
    cycle = [0, 90, 180, 270] if a.cmd == "calibrate" else None
    t0, shown = time.perf_counter(), None
    print(f"following our player #{our_player(pine)[0]} (from 0x48BF7D)")
    print(f"settings: yaw-offset {a.yaw_offset:g}, distance {a.distance:g}, height {a.height:g}, "
          f"shoulder {a.shoulder:g}, follow {a.follow:g}s, recenter {a.recenter:g}s, "
          f"invert-x {a.invert_x}, fov {a.fov or 'game'}")
    if cycle:
        print("CALIBRATE: the camera visits 4 angles around her, 4 s each, twice. Stand still (stick and\n"
              "D-pad untouched). Note the yaw-offset whose view shows HER BACK.")
    else:
        print("camera live. Right stick orbits/tilts, Ctrl+C hands control back to the game.")
    try:
        while True:
            now = time.perf_counter()
            dt, last = now - last, now
            if cycle:
                step = int((now - t0) // 4)
                if step >= 2 * len(cycle):
                    break
                a.yaw_offset = cycle[step % len(cycle)]
                if a.yaw_offset != shown:
                    shown = a.yaw_offset
                    print(f"  now showing yaw-offset {shown:3}  (heading field 0x{struct.unpack_from('<h', pine.read_block(our_player(pine)[1] + 0x90, 4), 2)[0] & 0xFFFF:04X})", flush=True)
            pl = pine.read_block(our_player(pine)[1], 0x94)
            pad = pine.read_block(PAD_NOW, 8)
            p = struct.unpack_from("<3f", pl, 0x38)
            heading = (struct.unpack_from("<h", pl, 0x92)[0] & 0xFFFF) * 2 * math.pi / 65536
            if smooth_heading is None:
                smooth_heading = heading
            k = 1 - math.exp(-dt / a.follow) if a.follow > 0 else 1
            smooth_heading += ((heading - smooth_heading + math.pi) % (2 * math.pi) - math.pi) * k
            sx, sy = stick(pad[6]), stick(pad[7])
            # Stick right turns the view right. That is -yaw here: the device felt "southpaw" with +yaw.
            yaw_off -= sx * math.radians(a.orbit_speed) * dt * (-1 if a.invert_x else 1)
            pitch = max(math.radians(-30), min(math.radians(60),
                        pitch + sy * math.radians(a.orbit_speed * 0.6) * dt * (-1 if a.invert_y else 1)))
            if sx == 0 and a.recenter > 0:
                yaw_off *= math.exp(-dt / a.recenter)
            eye, look = ots_camera(p, smooth_heading, yaw_off, pitch, a)
            pine.write_words(block + B_POS, [*map(f2u, eye), *map(f2u, look), f2u(a.fov or 0.0)])
            pine.write_words(block + B_FLAGS, [flags])
            time.sleep(max(0.0, 1 / a.hz - (time.perf_counter() - now)))
    except KeyboardInterrupt:
        pass
    finally:
        try:
            pine.write_words(block + B_FLAGS, [0])
            print("\ncamera handed back to the game (hook left installed, inert)")
        finally:
            pine.close()
    return 0


def f2u(x):
    return struct.unpack("<I", struct.pack("<f", x))[0]


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    f = sub.add_parser("freemem")
    f.add_argument("--seconds", type=float, default=60)
    i = sub.add_parser("install")
    i.add_argument("--at", type=lambda s: int(s, 0), required=True, help="address suggested by `freemem`")
    sub.add_parser("status")
    sub.add_parser("uninstall")
    r = sub.add_parser("run")
    sub_calibrate = sub.add_parser("calibrate", help="cycle the camera around her to find --yaw-offset")
    r.add_argument("--distance", type=float, default=220)
    r.add_argument("--height", type=float, default=150)
    r.add_argument("--target-height", type=float, default=140)
    r.add_argument("--shoulder", type=float, default=35, help="sideways offset; negate for the other shoulder")
    r.add_argument("--lookahead", type=float, default=200)
    r.add_argument("--pitch", type=float, default=8, help="starting tilt, degrees")
    r.add_argument("--fov", type=float, default=0, help="override FOV in degrees (0 = keep the game's)")
    r.add_argument("--follow", type=float, default=0.15, help="seconds to swing behind a turn (0 = rigid)")
    r.add_argument("--orbit-speed", type=float, default=150, help="degrees/s at full right stick")
    r.add_argument("--recenter", type=float, default=1.0,
                   help="seconds to ease back behind once the right stick is released (0 = stay put)")
    r.add_argument("--invert-x", action="store_true", help="stick right turns the view left")
    r.add_argument("--invert-y", action="store_true")
    r.add_argument("--yaw-offset", type=float, default=0,
                   help="degrees between the heading field and where she faces (device-confirmed: 0)")
    r.add_argument("--hz", type=float, default=60)
    for action in r._actions[1:]:   # calibrate takes the same camera options
        sub_calibrate._add_action(action)
    a = ap.parse_args(argv)
    return {"freemem": cmd_freemem, "install": cmd_install, "status": cmd_status,
            "uninstall": cmd_uninstall, "run": cmd_run, "calibrate": cmd_run}[a.cmd](a)


if __name__ == "__main__":
    sys.exit(main())
