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
import f1_buttons  # noqa: E402
import f1_collide  # noqa: E402
import f1_crosshair  # noqa: E402
import f1_freeaim  # noqa: E402
import f1_hookcheck  # noqa: E402
import mips  # noqa: E402
import phase2_check  # noqa: E402
import pine_diag  # noqa: E402
from f1_live import CAMERA, PAD_NOW, our_player  # noqa: E402

HOOK = 0x00345740
FREE_LO, FREE_HI = 0x00080000, 0x00100000  # below the ELF (loads at 0x100000); verified by `freemem`
MAGIC = 0x434D424F  # "OBMC"

# Block layout (data first, code at +0x40)
B_FLAGS, B_POS, B_TARGET, B_FOV, B_COUNT, B_ORIG1, B_ORIG2, B_MAGIC, B_PLAYER, B_CODE = (
    0x00, 0x04, 0x10, 0x1C, 0x20, 0x24, 0x28, 0x2C, 0x30, 0x40)
B_AIM_HEADING, B_AIM_ELEV = 0x34, 0x36        # s16 each, used with F_AIM
B_VEL_X, B_VEL_Z = 0x38, 0x3C                 # f32 each: strafe step per hook call, used with F_STRAFE
BLOCK_SIZE = 0x200
# flags: bit 0 override pos/target, bit 1 override fov, bit 2 pos/target are OFFSETS from the
# player at B_PLAYER (the cave adds that frame's player position, so the camera moves with her
# every frame however late the PC's update is)
# bit 3: free aim. Each frame, for the player at B_PLAYER: clear the auto-aim lock (+0x24 = 0,
# +0xCDC = 0xFF, as the game's own "aim at nothing" does), set heading +0x92 and gun elevation
# +0xBC8 from the block. Done here, right after the game's logic each frame, so the game never
# gets a whole tick to re-lock or turn her (from the PC it would be a race).
# bit 4: strafe. Each call, add (B_VEL_X, B_VEL_Z) to the player's position (File #1 roots her
# while aiming; HeySnippy's notes: the engine still resolves her against walls afterwards).
# bit 5: face. Each call, set the player's heading (+0x92) to B_AIM_HEADING, nothing else. Used
# while walking with the stick: the game walks straight when her heading already matches the
# stick direction, instead of arcing or turning on the spot first (func_006307E0's sectors).
F_POS, F_FOV, F_RELATIVE, F_AIM, F_STRAFE, F_FACE = 1, 2, 4, 8, 16, 32
P_TARGET, P_TARGET_IDX, P_HEADING, P_ELEV = 0x24, 0xCDC, 0x92, 0xBC8
PLAYER_POS = 0x38  # DrawObjFull position, f32[3]
PLAYER_ACT = 0x08  # act[0]: the current action; 2 = aiming (game/charmove.c func_0061CE50)
ACT_AIM = 2
WALK_ACTS = (0, 1)    # standing / the movement steps (charmove.c: action 1 = walk, run, turn, back)
RIG = ("distance", "height", "target_height", "shoulder", "lookahead")
# Raw DualShock 2 report (PadData at PAD_NOW): +4 RIGHT x, +5 RIGHT y, +6 LEFT x, +7 LEFT y. The
# decomp's pad.h comment says left first; the device says otherwise (the camera orbit read +6/+7
# and moved with the LEFT stick: the "southpaw" report). PS2 pads send right stick first.
PAD_RX, PAD_RY, PAD_LX, PAD_LY = 4, 5, 6, 7
# func_006307E0 (charmove.c) turns the movement stick into direction buttons RELATIVE TO THE
# CAMERA (0x306338/0x306344), storing the wanted direction at +0xBCE (s16); the stick is Ctrl
# +0x20 (mag) / +0x22 (angle) of the player's own Ctrl at +0xF78.
P_MOVE_DIR, P_CTRL_STICK = 0xBCE, 0xF78 + 0x20
PLAYER_TARGET = 0x24  # (players) the aim's target: set by func_0062D6A0 when aiming (charmove.c)
# Pools from the decomp, to say what a target is. type byte at +0x02: 0 player, 1 NPC, 2 enemy
# (from submain/ingame.c's draw-by-type switch and charsync.c; to be confirmed by `run`'s report).
POOLS = ((0x00476DD0, 4, 0x10E0, "player"), (0x003E8230, 10, 0x1170, "NPC pool"),
         (0x003FFCA0, 64, 0xD90, "thing pool"))
TYPE_NAMES = {0: "player", 1: "NPC", 2: "enemy"}


def describe_target(pine, ptr):
    """What the aim target pointer refers to, for the log. Read-only."""
    if ptr == 0:
        return "nothing"
    if not 0 < ptr < 0x02000000:
        return f"0x{ptr:08X} (not an EE RAM address)"
    where = "unknown pool"
    for base, n, size, name in POOLS:
        if base <= ptr < base + n * size:
            where = f"{name} slot {(ptr - base) // size}" + ("" if (ptr - base) % size == 0 else " (mid-object?)")
            break
    hdr = pine.read_block(ptr, 4)
    return (f"0x{ptr:08X}: type {hdr[2]} ({TYPE_NAMES.get(hdr[2], '?')}), kind 0x{hdr[3]:02X}, "
            f"active {hdr[0]}, in {where}")
STATE_FILE = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "results", "f1hook-state.json")


def build_cave(block, orig1, orig2, hook=HOOK, camera=CAMERA):
    """The cave's machine code, to be written at block + B_CODE. Returns a list of u32.

    Registers: t0-t4 and $f0-$f2 only. All are caller-saved, and the hooked function takes no
    arguments, so nothing live is clobbered at its entry.
    """
    code, fixups, labels = [], [], {}
    base = block + B_CODE

    def emit(*words):
        code.extend(words)

    def branch(rs, rt, label):     # beq + delay-slot nop, resolved at the end
        fixups.append((len(code), rs, rt, label))
        emit(None, mips.NOP)

    def label(name):
        labels[name] = base + 4 * len(code)

    bh, bl = mips.hi_lo(block)
    ch, cl = mips.hi_lo(camera)
    emit(mips.lui("t0", bh), mips.addiu("t0", "t0", bl))                       # t0 = block
    emit(mips.lw("t1", B_COUNT, "t0"), mips.addiu("t1", "t1", 1), mips.sw("t1", B_COUNT, "t0"))
    emit(mips.lw("t1", B_FLAGS, "t0"), mips.lui("t2", ch), mips.addiu("t2", "t2", cl))  # t2 = camera
    emit(mips.andi("t3", "t1", F_POS))
    branch("t3", "zero", "aim")
    emit(mips.andi("t3", "t1", F_RELATIVE))
    branch("t3", "zero", "absolute")
    emit(mips.lw("t3", B_PLAYER, "t0"))
    branch("t3", "zero", "aim")                                                # no player: leave the game's
    for i in range(3):                                                         # camera = player + offsets
        emit(mips.lwc1(0, PLAYER_POS + 4 * i, "t3"),
             mips.lwc1(1, B_POS + 4 * i, "t0"), mips.add_s(2, 0, 1), mips.swc1(2, 0x18 + 4 * i, "t2"),
             mips.lwc1(1, B_TARGET + 4 * i, "t0"), mips.add_s(2, 0, 1), mips.swc1(2, 0x24 + 4 * i, "t2"))
    branch("zero", "zero", "aim")                                              # always
    label("absolute")
    for i in range(6):                                                         # pos[3] + target[3]
        emit(mips.lw("t3", B_POS + 4 * i, "t0"), mips.sw("t3", 0x18 + 4 * i, "t2"))
    label("aim")
    emit(mips.andi("t3", "t1", F_AIM))
    branch("t3", "zero", "face")
    emit(mips.lw("t3", B_PLAYER, "t0"))
    branch("t3", "zero", "face")
    emit(mips.sw("zero", P_TARGET, "t3"),                                      # no lock-on target
         mips.addiu("t4", "zero", 0xFF), mips.sb("t4", P_TARGET_IDX, "t3"),
         mips.lh("t4", B_AIM_HEADING, "t0"), mips.sh("t4", P_HEADING, "t3"),
         mips.lh("t4", B_AIM_ELEV, "t0"), mips.sh("t4", P_ELEV, "t3"))
    emit(mips.andi("t4", "t1", F_STRAFE))                                      # t3 = player still
    branch("t4", "zero", "face")
    for off, vel in ((0, B_VEL_X), (8, B_VEL_Z)):                              # pos.x, pos.z += step
        emit(mips.lwc1(0, PLAYER_POS + off, "t3"), mips.lwc1(1, vel, "t0"),
             mips.add_s(2, 0, 1), mips.swc1(2, PLAYER_POS + off, "t3"))
    label("face")
    emit(mips.andi("t4", "t1", F_FACE))
    branch("t4", "zero", "fov")
    emit(mips.lw("t3", B_PLAYER, "t0"))
    branch("t3", "zero", "fov")
    emit(mips.lh("t4", B_AIM_HEADING, "t0"), mips.sh("t4", P_HEADING, "t3"))
    label("fov")
    emit(mips.andi("t3", "t1", F_FOV))
    branch("t3", "zero", "replay")
    emit(mips.lw("t3", B_FOV, "t0"), mips.sw("t3", 0x60, "t2"))
    label("replay")
    emit(orig1, orig2)                                                         # the displaced prologue
    emit(mips.j(hook + 8, base + 4 * len(code)), mips.NOP)
    for at, rs, rt, name in fixups:
        code[at] = mips.beq(rs, rt, base + 4 * at, labels[name])
    return code


def trampoline(block):
    """The 8 bytes that replace the hook's first two instructions, as one u64 (little endian)."""
    return mips.j(block + B_CODE, HOOK) | (mips.NOP << 32)


def connect():
    pine, info = phase2_check.connect_game()
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
        old = words_at(pine, block, BLOCK_SIZE // 4)
        if any(old) and old[B_MAGIC // 4] != MAGIC:
            raise SystemExit(f"refusing: 0x{block:08X}..+0x{BLOCK_SIZE:X} is not all zero any more")
        if any(old):
            print(f"reusing our own earlier block at 0x{block:08X}")

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


def cmd_reinstall(a):
    """Swap an installed hook for the current version, at the same address."""
    pine, _ = connect()
    try:
        block, _ = find_block(pine)
    finally:
        pine.close()
    if block is None:
        raise SystemExit("hook not installed: use `install --at <address>`")
    cmd_uninstall(a)
    a.at = block
    return cmd_install(a)


def cmd_walls(a):
    """READ-ONLY: validate the room's collision data and report wall distances around her."""
    pine, _ = connect()
    try:
        room = f1_collide.RoomCollision(pine)
        if not room.refresh():
            raise SystemExit("no valid collision grid at 0x3938A8 (in gameplay?)")
        for g, grid in room.grids.items():
            print(f"grid {g}: {grid.nx} x {grid.nz} cells of {grid.cw} x {grid.cd}, origin ({grid.ox:g}, {grid.oz:g})")
        idx, base = our_player(pine)
        p = struct.unpack("<3f", pine.read_block(base + PLAYER_POS, 12))
        chest = (p[0], p[1] + a.height, p[2])
        print(f"our player #{idx} at ({p[0]:.0f}, {p[1]:.0f}, {p[2]:.0f}); ray origin {a.height:g} above the feet")
        def probe(label, end):
            hit = room.first_hit_tri(chest, end)
            span = math.dist(chest, end)
            print(f"  {label:>22}: " + (f"hit at {hit[0] * span:6.0f}  (kind {hit[1]}, grid {hit[2]})" if hit
                                         else f"clear to {span:g}"))

        for deg in range(0, 360, 45):
            r = math.radians(deg)
            probe(f"heading {deg:3} deg", (chest[0] + math.sin(r) * a.reach, chest[1], chest[2] + math.cos(r) * a.reach))
        probe("straight up", (chest[0], chest[1] + a.reach, chest[2]))
        probe("straight down", (chest[0], chest[1] - a.reach, chest[2]))
        h = (struct.unpack_from("<h", pine.read_block(base + 0x90, 4), 2)[0] & 0xFFFF) * 2 * math.pi / 65536
        for up in (20, 40, 60):                          # behind her and rising, like the camera's path
            r = math.radians(up)
            probe(f"behind, {up} deg up", (chest[0] - math.sin(h) * a.reach * math.cos(r), chest[1] + a.reach * math.sin(r),
                                          chest[2] - math.cos(h) * a.reach * math.cos(r)))
        st = room.stats
        print(f"read {st['tris']} triangles in {st['cells']} cells; failed the plane check: {st['insane']}")
        print("triangle kinds seen (grid, kind): count  " +
              ", ".join(f"({g},{k}): {n}" for (g, k), n in sorted(room.kinds().items())))
        if st["tris"] and st["insane"] > st["tris"] // 10:
            print("!! many triangles fail the plane check: the layout is probably misread. Paste this to Claude.")
    finally:
        pine.close()
    return 0


def hook_is_current(pine, block):
    hdr = words_at(pine, block, B_CODE // 4)
    want = build_cave(block, hdr[B_ORIG1 // 4], hdr[B_ORIG2 // 4])
    return words_at(pine, block + B_CODE, len(want)) == want


# ---------------------------------------------------------------- run
def stick(v, dead=0.15):
    x = (v - 127.5) / 127.5
    return 0.0 if abs(x) < dead else max(-1.0, min(1.0, (x - math.copysign(dead, x)) / (1 - dead)))


def ots_camera(p, heading, yaw_off, pitch, a):
    """Eye and look-at for an over-the-shoulder ORBIT camera.

    The camera turns around a pivot at her shoulder (`height` up, `shoulder` to the side) and
    looks along (yaw, elevation = -pitch): the eye sits `distance` behind the pivot on that line,
    the look-at `lookahead` in front. So the direction the camera looks IS (yaw, -pitch), which
    is what free aim gives the gun. (The first rig kept a fixed look-at height, so tilting moved
    the eye but barely changed the view direction.)

    She faces along the heading field (player +0x92), atan2(dx, dz): on the device,
    --yaw-offset 0 shows her back and 180 her face (user, 2026-10-19).
    """
    yaw = heading + yaw_off + math.radians(a.yaw_offset)
    fx, fz = math.sin(yaw), math.cos(yaw)           # where the camera looks, on the floor plane
    rx, rz = fz, -fx                                # perpendicular; sign of "right" set by --shoulder
    ce, se = math.cos(-pitch), math.sin(-pitch)     # look elevation: +up
    pivot = (p[0] + rx * a.shoulder, p[1] + a.height, p[2] + rz * a.shoulder)
    d = (fx * ce, se, fz * ce)
    eye = tuple(pivot[i] - d[i] * a.distance for i in range(3))
    look = tuple(pivot[i] + d[i] * a.lookahead for i in range(3))
    return eye, look


def collide_pull(room, p, eye, yaw, rig, margin):
    """(pivot, fraction of pivot->eye to use) keeping the eye `margin` inside the walls.

    The pivot is her SHOULDER point, not her middle, so a squeeze ends in an over-the-shoulder
    view instead of inside her back (device report). The shoulder point is itself first kept out
    of a wall right beside her, then the eye is traced from it.
    """
    rx, rz = math.cos(yaw), -math.sin(yaw)              # same "right" as ots_camera
    centre = (p[0], p[1] + rig.target_height, p[2])
    side = (centre[0] + rx * rig.shoulder, centre[1], centre[2] + rz * rig.shoulder)
    ts = room.first_hit(centre, side) if rig.shoulder else None
    if ts is not None:
        keep = max(0.0, ts - margin / max(abs(rig.shoulder), 1e-6))
        side = tuple(centre[i] + (side[i] - centre[i]) * keep for i in range(3))
    span = math.dist(side, eye)
    t = room.first_hit(side, eye)
    return side, (1.0 if t is None else max(0.0, t - margin / max(span, 1e-6)))


def cmd_run(a):
    pine, _ = connect()
    block, _ = find_block(pine)
    if block is None:
        raise SystemExit("hook not installed: run `install` first")
    if not hook_is_current(pine, block):
        raise SystemExit("the installed hook is an older version: python tools\\f1_camtest.py reinstall")
    yaw_off, pitch, last = 0.0, math.radians(a.pitch), time.perf_counter()
    smooth_heading = None
    flags = F_POS | F_RELATIVE | (F_FOV if a.fov else 0)
    free_aiming = False
    t_move = 0.0
    hook_rate = 60.0                                      # hook calls/s, measured below
    face_dir = None                                       # modern walking: her facing, radians
    prev_raw, qt = 0, None                                # quick turn (down + cross) in progress
    prev_heading, t_snap = None, 0.0                      # her facing last update: snap report
    good_pad, bad_pads = None, 0                          # the last believable pad read; glitches skipped
    aim_face = None                                       # free aim: her facing, turned toward the aim
    idx, player = our_player(pine)
    t_player, t_stats, loops, count0 = time.perf_counter(), time.perf_counter(), 0, None
    room = f1_collide.RoomCollision(pine) if a.collide else None
    if room and not room.refresh():
        print("  (no collision grid yet: camera collision off until one appears)")
    pull = 1.0                                             # fraction of the wanted distance in use
    aim = 0.0                                              # 0 = hip rig, 1 = aim rig (blended)
    pending, streak = (idx, player), 0
    last_target = None
    buttons = None
    if a.modern_buttons:
        buttons = f1_buttons.ButtonMap(pine, cross_runs=a.cross_runs)
        print(buttons.apply())
    cross = (f1_crosshair.Crosshair(pine, size=a.crosshair_size, style=a.crosshair_style)
             if a.crosshair else None)
    t_cross = 0.0
    servo = None
    if a.free_aim:
        servo = f1_freeaim.Servo(pine)
        print("free aim: " + servo.disable())
    was_aiming = False
    cycle = [0, 90, 180, 270] if a.cmd == "calibrate" else None
    t0, shown = time.perf_counter(), None
    print(f"following our player #{our_player(pine)[0]} (from 0x48BF7D)")
    print(f"settings: yaw-offset {a.yaw_offset:g}, distance {a.distance:g}, height {a.height:g}, "
          f"shoulder {a.shoulder:g}, camera {'chase (follow ' + format(a.follow, 'g') + 's, recenter ' + format(a.recenter, 'g') + 's)' if a.chase else 'moves only with the right stick'}, "
          f"invert-x {a.invert_x}, collide {a.collide}, free-aim {a.free_aim}, modern-buttons {a.modern_buttons}, "
          f"fov {a.fov or 'game'}\n"
          f"aim rig: distance {a.aim_distance:g}, height {a.aim_height:g}, shoulder {a.aim_shoulder:g}, "
          f"lookahead {a.aim_lookahead:g}, blend {a.aim_blend:g}s, follow {a.aim_follow:g}s")
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
            if now - t_player > 0.5:                      # who we follow can change (death, swap)
                cand = our_player(pine)
                # Switch only when the new index holds for 3 reads in a row (~1.5 s): one odd read
                # must never throw the camera onto another character.
                streak = streak + 1 if cand == pending else 1
                pending = cand
                if cand[0] != idx and streak >= 3:
                    idx, player = cand
                    print(f"  now following player #{idx}", flush=True)
                if room:
                    room.refresh()                        # new room -> new triangles
                if buttons and buttons.ensure():
                    print("  modern buttons re-applied (the game reset its button map)", flush=True)
                if servo and servo.ensure():
                    print(f"  free aim: GAME.BIN reloaded, servo switched off again at 0x{servo.addr:08X}", flush=True)
                elif servo and servo.addr is None:
                    print("  free aim: " + servo.disable(), flush=True)
                t_player = now
            pl, pad, cnt, mv, mstick = pine.read_multi([(player, 0x94), (PAD_NOW, 8), (block + B_COUNT, 4),
                                                        (player + P_MOVE_DIR, 2), (player + P_CTRL_STICK, 4)])
            # Now and then the read lands while the game rewrites the pad (device: every button
            # "held", sticks at the rim) and set off quick turns. Use the last good read instead.
            if f1_buttons.plausible(struct.unpack_from("<H", pad, 2)[0]):
                good_pad = pad
            else:
                pad, bad_pads = good_pad or bytes([0, 0, 0, 0, 128, 128, 128, 128]), bad_pads + 1
            count = struct.unpack("<I", cnt)[0]
            mag, ang = struct.unpack("<hH", mstick)               # the movement stick, as the game reads it
            p = struct.unpack_from("<3f", pl, 0x38)
            heading = (struct.unpack_from("<h", pl, 0x92)[0] & 0xFFFF) * 2 * math.pi / 65536
            if smooth_heading is None:
                smooth_heading = heading
            aiming = pl[PLAYER_ACT] == ACT_AIM
            raw_buttons = struct.unpack_from("<H", pad, 2)[0]
            lx, ly = stick(pad[PAD_LX]), stick(pad[PAD_LY])
            if aiming and not was_aiming:
                if a.free_aim and servo is not None and servo.active and not a.chase:
                    # RE5 style: she turns to the camera (the hook writes her heading), the view
                    # stays put. Fold the orbit into the base yaw so nothing moves.
                    smooth_heading, yaw_off = smooth_heading + yaw_off, 0.0
                else:
                    smooth_heading, yaw_off = heading, 0.0    # start the aim straight behind her
                print("  aiming" + ("" if not a.free_aim else " (free aim ON)" if servo and servo.active
                                    else " (free aim OFF: servo not switched off, see the start of the log)"), flush=True)
            elif was_aiming and not aiming:
                print("  aim released", flush=True)
                t_move = now - 2.5                        # its wanted-direction field is stale for a moment
                if free_aiming:
                    smooth_heading, yaw_off = smooth_heading + yaw_off, 0.0   # keep the view where it is
            was_aiming = aiming
            if cross:
                # "+" at the centre while aiming (the game's own text line, data only); topped up
                # every 5 s in case the game cleared it, removed when the aim ends.
                if aiming and now - t_cross > 5.0:
                    if not cross.show() and t_cross == 0.0:
                        print("  crosshair: the game is using its text line; not drawing", flush=True)
                    t_cross = now
                elif not aiming and cross.shown:
                    cross.hide()
                    t_cross = 0.0
            if buttons:
                moving = bool(raw_buttons & f1_buttons.DIRECTIONS or stick(pad[PAD_LX]) or stick(pad[PAD_LY]))
                buttons.set_context(aiming, moving)   # cross: kick / menu back / no run
            free_aiming = aiming and servo is not None and servo.active
            target = struct.unpack_from("<I", pl, PLAYER_TARGET)[0] if aiming else None
            if target != last_target and aiming:
                print(f"  aim target: {describe_target(pine, target)}", flush=True)
                if target and not free_aiming:
                    yaw_off = 0.0                         # a new lock: swing round behind her
            last_target = target
            # Follow her facing. With a lock-on the game turns her to the target (even one behind
            # her), so follow that too, but slower (--aim-follow), so hopping between nearby
            # targets is a pan, not a jerk (device reports: instant = jumpy, frozen = left behind).
            # With free aim the CAMERA drives her facing (the hook writes it), so don't follow it back.
            # Walking with the left stick is camera-relative in the game (func_006307E0), so a camera
            # that chases her facing makes a sideways push turn her forever (device: "the left
            # stick keeps rotating"). Hold the camera while the stick is pushed; follow again
            # once it's released (or with the D-pad, which is tank control).
            # Same signal as the facing below uses (the game's own stick reading, mag >= 0x10), or the
            # raw stick: if they ever disagreed, following while facing would feed back and spin.
            stick_moving = bool(stick(pad[PAD_LX]) or stick(pad[PAD_LY]) or mag >= 0x10)
            # Modern (default): the camera never moves by itself; only the right stick turns it.
            # --chase: it swings in behind her (except while the left stick is pushed, which would
            # feed back through the camera-relative walking) and lock-ons pull it round.
            chase = a.chase and not (stick_moving and not aiming and not a.follow_while_moving)
            if not free_aiming and not qt and (chase or (aiming and target and not a.free_aim)):
                tau = a.aim_follow if (aiming and target) else a.follow
                k = 1 - math.exp(-dt / tau) if tau > 0 else 1
                smooth_heading += ((heading - smooth_heading + math.pi) % (2 * math.pi) - math.pi) * k
            sx, sy = stick(pad[PAD_RX]), stick(pad[PAD_RY])
            # Stick right turns the view right. That is -yaw here: the device felt "southpaw" with +yaw.
            yaw_off -= sx * math.radians(a.orbit_speed) * dt * (-1 if a.invert_x else 1)
            low = -a.aim_max if (aiming and servo) else -30   # free aim may look further up
            pitch = max(math.radians(low), min(math.radians(60),
                        pitch + sy * math.radians(a.orbit_speed * 0.6) * dt * (-1 if a.invert_y else 1)))
            if sx == 0 and a.recenter > 0 and not aiming and a.chase:
                yaw_off *= math.exp(-dt / a.recenter)
            # Quick turn, down + cross (device: with the camera-relative stick, "down" only turned
            # her to face the camera, and the camera stayed put). She snaps to face the way "down"
            # points (D-pad: straight about, the classic turn) and the camera swings in behind her.
            # Her facing is held there until down is let go: the camera has turned, so a stick still
            # pulled down now means "back the way you came", and following it would undo the turn.
            kind = (f1_buttons.quick_turn(raw_buttons, prev_raw, lx, ly)
                    if a.quick_turn > 0 and not aiming and pl[PLAYER_ACT] in WALK_ACTS else None)
            prev_raw = raw_buttons
            if kind:
                cam_base = smooth_heading + yaw_off                    # the camera's yaw, minus --yaw-offset
                face = (cam_base if kind == "stick" else heading) + math.pi
                qt = {"face": face, "start": cam_base, "t0": now,
                      "turn": (face - cam_base + math.pi) % (2 * math.pi) - math.pi, "kind": kind}
                smooth_heading, yaw_off = cam_base, 0.0
                print(f"  quick turn ({kind}: stick x {lx:+.2f} y {ly:+.2f}, buttons 0x{raw_buttons:04X})", flush=True)
            if qt:
                f = min(1.0, (now - qt["t0"]) / a.quick_turn)
                smooth_heading = qt["start"] + qt["turn"] * f * f * (3 - 2 * f)   # ease in and out
                held = raw_buttons & f1_buttons.DPAD_DOWN if qt["kind"] == "dpad" else ly > 0.3
                if aiming or (f >= 1 and not held):
                    qt = None
            # Snap report: her facing jumped between two updates (~2 game frames) by more than our own
            # walking turn could do. Says who was in charge, to tell the game's turns from ours.
            if prev_heading is not None and not kind and not (qt and now - qt["t0"] < 0.15):
                jump = math.degrees((heading - prev_heading + math.pi) % (2 * math.pi) - math.pi)
                if abs(jump) > 45 and now - t_snap > 0.5:
                    print(f"  snap: her facing jumped {jump:+.0f} deg (action {pl[PLAYER_ACT]}/{pl[PLAYER_ACT + 1]}, "
                          f"{'aiming' if aiming else 'not aiming'}, {'quick turn' if qt else 'free aim' if free_aiming else 'game: hit reaction' if pl[PLAYER_ACT] == 5 or pl[PLAYER_ACT + 1] in (0xA1, 0xA2) else 'walk facing' if face_dir is not None else 'game'} "
                          f"in charge, stick x {lx:+.2f} y {ly:+.2f}, game stick {mag}/{ang * 360 / 65536:.0f} deg, "
                          f"buttons 0x{raw_buttons:04X})", flush=True)
                    t_snap = now
            prev_heading = heading
            k_aim = 1 - math.exp(-dt / a.aim_blend) if a.aim_blend > 0 else 1
            aim += ((1.0 if aiming else 0.0) - aim) * k_aim
            rig = type(a)(**vars(a))                      # hip rig blended toward the aim rig
            for f in RIG:
                setattr(rig, f, getattr(a, f) + (getattr(a, "aim_" + f) - getattr(a, f)) * aim)
            eye, look = ots_camera(p, smooth_heading, yaw_off, pitch, rig)
            if room and room.grids and a.headroom > 0:
                # No ceilings in the collision data (stairs probe), but floors are there, and
                # anywhere people walk has head-room above it: keep the eye within --headroom of
                # the floor under it. The wall trace below then works on the adjusted eye.
                floor = room.floor_below(eye[0], max(eye[1], p[1] + rig.target_height) + 50, eye[2])
                if floor is not None:
                    eye = (eye[0], min(max(eye[1], floor + 30), floor + a.headroom), eye[2])
            if room and room.grids:
                # Keep the eye inside the room: trace from her (at look height) to the eye, and stop
                # `wall_margin` short of the first triangle. Snap in, ease back out.
                yaw = smooth_heading + yaw_off + math.radians(a.yaw_offset)
                pivot, want = collide_pull(room, p, eye, yaw, rig, a.wall_margin)
                pull = want if want < pull else pull + (want - pull) * (1 - math.exp(-dt / 0.3))
                eye = tuple(pivot[i] + (eye[i] - pivot[i]) * pull for i in range(3))
            # Offsets from the player: the hook adds HER position for the frame it draws, so a late
            # update only delays rotation, never makes the camera lag behind her movement.
            rel = [eye[i] - p[i] for i in range(3)] + [look[i] - p[i] for i in range(3)]
            writes = [(block + B_POS, [*map(f2u, rel), f2u(a.fov or 0.0)]),
                      (block + B_PLAYER, [player])]
            strafing = False
            if free_aiming and a.strafe_speed > 0:
                # Left stick -> camera-relative step. ots_camera's "right" (cos yaw, -sin yaw) is
                # screen-LEFT on the device (+shoulder put her right of centre), so negate it.
                if lx or ly:
                    yaw = smooth_heading + yaw_off + math.radians(a.yaw_offset)
                    fwd = (math.sin(yaw), math.cos(yaw))
                    scr_right = (-math.cos(yaw), math.sin(yaw))
                    per_call = a.strafe_speed / max(hook_rate, 30.0)    # the hook runs 60-120 times/s
                    vx = (scr_right[0] * lx - fwd[0] * ly) * per_call    # stick up (ly < 0) = away
                    vz = (scr_right[1] * lx - fwd[1] * ly) * per_call
                    writes.append((block + B_VEL_X, [f2u(vx), f2u(vz)]))
                    strafing = True
            facing = False
            if qt:
                face_dir = qt["face"]                    # the quick turn holds her facing
                writes.append((block + B_AIM_HEADING, [round(face_dir / (2 * math.pi) * 65536) & 0xFFFF], 2))
                facing = True
            elif (not aiming and a.turn_speed > 0 and mag >= 0x10 and pl[PLAYER_ACT] in WALK_ACTS):
                # Modern walking: face her straight down the stick (camera yaw + stick angle, the
                # same target func_006307E0 computes), turning at most --turn-speed. The game then
                # sees "straight ahead" and walks, instead of arcing or turning on the spot.
                cam_units = math.atan2(look[0] - eye[0], look[2] - eye[2]) / (2 * math.pi) * 65536
                want = (cam_units + (ang & 0xFFF0)) * 2 * math.pi / 65536
                if face_dir is None:
                    face_dir = heading
                step = math.radians(a.turn_speed) * dt
                face_dir += max(-step, min(step, (want - face_dir + math.pi) % (2 * math.pi) - math.pi))
                writes.append((block + B_AIM_HEADING, [round(face_dir / (2 * math.pi) * 65536) & 0xFFFF], 2))
                facing = True
            else:
                face_dir = None
            if free_aiming:
                # She faces where the camera looks and the gun takes its elevation, so the shot
                # lines up with the view (0x10000 per turn). The hook applies these every frame.
                # Converge: the camera sits a shoulder to the side, so its centre ray (the crosshair)
                # runs parallel to her gun line and the shots landed beside it (device: "slightly
                # off to the right"). Find what the crosshair is on -- the first wall down that ray,
                # else --converge ahead -- and point her and the gun from her chest at that point.
                d = [look[i] - eye[i] for i in range(3)]
                n = math.sqrt(sum(c * c for c in d)) or 1.0
                d = [c / n for c in d]
                far = tuple(eye[i] + d[i] * a.converge for i in range(3))
                hit = room.first_hit(eye, far) if (room and room.grids) else None
                aim_pt = tuple(eye[i] + d[i] * a.converge * (hit if hit is not None else 1.0) for i in range(3))
                chest = (p[0], p[1] + rig.target_height, p[2])
                face = math.atan2(aim_pt[0] - chest[0], aim_pt[2] - chest[2]) - math.radians(a.yaw_offset)
                # Turn her to it at --aim-turn instead of in one frame (device: aiming with the camera
                # in front of her snapped her round 174 deg). Once she's on it, she tracks it exactly.
                if aim_face is None:
                    aim_face = heading
                step = math.radians(a.aim_turn) * dt if a.aim_turn > 0 else math.pi
                aim_face += max(-step, min(step, (face - aim_face + math.pi) % (2 * math.pi) - math.pi))
                face = aim_face
                elev = math.atan2(aim_pt[1] - chest[1], math.hypot(aim_pt[0] - chest[0], aim_pt[2] - chest[2]))
                lim = round(a.aim_max / 360 * 65536)
                writes.append((block + B_AIM_HEADING,
                               [round(face / (2 * math.pi) * 65536) & 0xFFFF,
                                max(-lim, min(lim, round(elev / (2 * math.pi) * 65536))) & 0xFFFF], 2))
            if not free_aiming:
                aim_face = None
            writes.append((block + B_FLAGS, [flags | (F_AIM if free_aiming else 0)
                                             | (F_STRAFE if strafing else 0)
                                             | (F_FACE if facing and not free_aiming else 0)]))   # flags last
            pine.write_multi(writes)
            # Which camera does the game's camera-relative movement use? wanted dir = camera yaw +
            # stick angle (&0xFFF0 both, as func_006307E0 does), so camera yaw = wanted - stick.
            # Only meaningful while walking: not while aiming (free aim holds her facing and the stick
            # isn't movement) nor while the game is paused or loading (its value goes stale).
            if mag >= 0x10 and now - t_move > 3.0 and not aiming and hook_rate > 30:
                game_cam = (struct.unpack("<H", mv)[0] - (ang & 0xFFF0)) & 0xFFF0
                ours = round(math.atan2(look[0] - eye[0], look[2] - eye[2]) / (2 * math.pi) * 65536) & 0xFFFF
                diff = ((game_cam - ours + 0x8000) & 0xFFFF) - 0x8000
                print(f"  moving: game's movement uses camera yaw {game_cam * 360 / 65536:6.1f} deg, ours is "
                      f"{ours * 360 / 65536:6.1f} deg -> {'OURS' if abs(diff) < 0x700 else f'NOT ours (off {diff * 360 / 65536:+.0f} deg)'}",
                      flush=True)
                t_move = now
            loops += 1
            if count0 is None:
                count0 = count
            if now - t_stats >= 2 and not cycle:
                fps = (count - count0) / (now - t_stats)
                hook_rate = fps if fps > 0 else hook_rate
                print(f"  {loops / (now - t_stats):5.1f} updates/s, game {fps:5.1f} frames/s, "
                      f"{fps / max(loops / (now - t_stats), 1e-9):4.2f} frames per update"
                      + (f", {bad_pads} glitched pad reads skipped" if bad_pads else ""), flush=True)
                bad_pads = 0
                t_stats, loops, count0 = now, 0, count
            time.sleep(max(0.0, 1 / a.hz - (time.perf_counter() - now)))
    except KeyboardInterrupt:
        pass
    finally:
        # Ctrl+C can land between a request and its reply, leaving this connection out of step,
        # so clean up on a FRESH one: the servo must never stay switched off by accident.
        pine.close()
        try:
            clean = pine_diag.Pine("127.0.0.1", 28011, 5.0)
            try:
                if servo:
                    servo.pine = clean
                    print("\nfree aim: " + servo.restore())
                if buttons:
                    buttons.pine = clean
                    print(buttons.restore())
                if cross and cross.shown:
                    cross.pine = clean
                    cross.hide()
                clean.write_words(block + B_FLAGS, [0])
                print("\ncamera handed back to the game (hook left installed, inert)")
            finally:
                clean.close()
        except (OSError, pine_diag.PineError) as e:
            print(f"\n!! clean-up could not reach PINE ({e}). Our camera"
                  + (", the free-aim patch" if servo and servo.orig else "")
                  + (", the modern buttons" if buttons and buttons.orig else "") + " may still be active.\n"
                  "!! Run `python tools\\f1_camtest.py uninstall` (and run `run` once more and Ctrl+C), "
                  "or simply load your save state.")
            return 1
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
    w = sub.add_parser("walls", help="read-only: check the room collision data and wall distances")
    w.add_argument("--height", type=float, default=140)
    w.add_argument("--reach", type=float, default=600)
    sub.add_parser("uninstall")
    sub.add_parser("reinstall", help="replace the installed hook with this version, same address")
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
    r.add_argument("--orbit-speed", type=float, default=100, help="degrees/s at full right stick (device: 150 felt fast)")
    r.add_argument("--turn-speed", type=float, default=900,
                   help="modern walking: deg/s she turns toward the stick direction (0 = the game's own arcs)")
    r.add_argument("--strafe-speed", type=float, default=0,
                   help="units/s at full left stick while free-aiming (0 = stand still, RE5 style; ~150 = half her walk)")
    r.add_argument("--quick-turn", type=float, default=0.25,
                   help="down + cross: seconds the camera takes to swing behind her (0 = off)")
    r.add_argument("--chase", action="store_true",
                   help="old camera: swings in behind her and drifts back after looking (default: only the right stick moves it)")
    r.add_argument("--follow-while-moving", action="store_true",
                   help="let the camera chase her facing while the left stick is pushed (old behaviour)")
    r.add_argument("--recenter", type=float, default=1.0,
                   help="seconds to ease back behind once the right stick is released (0 = stay put)")
    r.add_argument("--invert-x", action="store_true", help="stick right turns the view left")
    r.add_argument("--invert-y", action="store_true")
    r.add_argument("--yaw-offset", type=float, default=0,
                   help="degrees between the heading field and where she faces (device-confirmed: 0)")
    r.add_argument("--hz", type=float, default=60)
    r.add_argument("--aim-distance", type=float, default=120)
    r.add_argument("--aim-height", type=float, default=160)
    r.add_argument("--aim-target-height", type=float, default=150)
    r.add_argument("--aim-shoulder", type=float, default=50)
    r.add_argument("--aim-lookahead", type=float, default=400)
    r.add_argument("--aim-blend", type=float, default=0.12, help="seconds to move into/out of the aim rig")
    r.add_argument("--aim-follow", type=float, default=0.3,
                   help="seconds the camera takes to swing round to a locked-on target")
    r.add_argument("--classic-buttons", dest="modern_buttons", action="store_false",
                   help="keep the game's buttons (default: L2 aims, R2 fires while the camera runs)")
    r.add_argument("--cross-runs", action="store_true",
                   help="cross runs as in the original game (default: R1 runs, cross kicks/backs out only)")
    r.add_argument("--no-crosshair", dest="crosshair", action="store_false",
                   help="don't draw the '+' crosshair while aiming")
    r.add_argument("--crosshair-size", type=int, default=24, help="crosshair text size")
    r.add_argument("--crosshair-style", choices=sorted(f1_crosshair.STYLES), default="full",
                   help="full: square plus (default); half: the narrow ASCII +; dot")
    r.add_argument("--converge", type=float, default=1500,
                   help="free aim: aim at the point this far down the crosshair if no wall is nearer")
    r.add_argument("--free-aim", action="store_true",
                   help="switch off the game's 3-position aim servo; the gun follows where the camera looks")
    r.add_argument("--aim-turn", type=float, default=720,
                   help="free aim: deg/s she turns to where the camera looks when the aim starts (0 = at once)")
    r.add_argument("--aim-max", type=float, default=45, help="free aim limit up/down, degrees (game: 45)")
    r.add_argument("--no-collide", dest="collide", action="store_false", help="let the camera pass through walls")
    r.add_argument("--wall-margin", type=float, default=20, help="units kept between the camera and a wall")
    r.add_argument("--headroom", type=float, default=190,
                   help="max camera height above the floor under it (ceilings aren't in the collision data; 0 = off)")
    for action in r._actions[1:]:   # calibrate takes the same camera options
        sub_calibrate._add_action(action)
    a = ap.parse_args(argv)
    return {"freemem": cmd_freemem, "install": cmd_install, "status": cmd_status,
            "uninstall": cmd_uninstall, "reinstall": cmd_reinstall, "walls": cmd_walls, "run": cmd_run, "calibrate": cmd_run}[a.cmd](a)


if __name__ == "__main__":
    sys.exit(main())
