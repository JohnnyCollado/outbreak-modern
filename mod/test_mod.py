#!/usr/bin/env python3
"""Run the built mod in a MIPS emulator (unicorn) against a fake game memory, through a fake
patched call site: the stub must hand every register back unchanged and continue into
0x00345740, and mod_frame must keep the status block and show its line.

  python mod/test_mod.py        (needs `pip install unicorn`; skipped without it)

Unicorn emulates a MIPS32 CPU, not the EE: the stub's sq/lq (EE-only) run here as 32-bit
sw/lw. (Its FPU rounds float -> int to nearest, the EE's toward zero: tests of
code that converts must allow for that.)
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import build  # noqa: E402

try:
    from unicorn import Uc, UcError, UC_ARCH_MIPS, UC_MODE_MIPS32, UC_MODE_LITTLE_ENDIAN
    from unicorn.mips_const import UC_MIPS_REG_PC, UC_MIPS_REG_29, UC_MIPS_REG_31
    import unicorn.mips_const as M
except ImportError:
    print("unicorn not installed: skipped (pip install unicorn)")
    sys.exit(0)

FAILS = []
RAM = 32 << 20            # the EE has 32 MB
# A fake caller: load every FPU register from FP_IN (unicorn can't set them directly), the patched
# call site (jal mod_entry; nop), then store them all to FP_OUT. k0 holds the table address: no C
# code uses k0/k1.
FP_IN, FP_OUT = 0x00600000, 0x00600100
CALLER = 0x00340100
SITE = CALLER + 4 + 32 * 4
STOP = SITE + 8 + 32 * 4
GAME_FN = 0x00345740
SP = 0x007F0000


def expect(cond, what):
    print(("  ok   " if cond else "  FAIL ") + what)
    if not cond:
        FAILS.append(what)


def reg(n):
    return getattr(M, f"UC_MIPS_REG_{n}")


COUNT_AT = 0x7F00          # the test's stand-in for the EE cycle counter (COP0 Count)
COUNT_HZ = 294912000


def mips32(w):
    """mfc0 rt, $9 (Count) as lw rt, COUNT_AT($zero): the test sets the time.
    sq/lq (EE-only, opcodes 0x1F/0x1E) as sw/lw of the low word: unicorn's CPU has no 128-bit
    registers. The EE requires 16-byte alignment for them: the stub saves at sp + r*16 with sp
    16-aligned, so the offset must be too."""
    if w == 0x0000000C:                         # syscall (FlushCache): nothing to flush here
        return 0
    if w & 0xFFE0FFFF == 0x40004800:
        return 0x8C000000 | (w & 0x001F0000) | COUNT_AT
    if w >> 26 in (0x1E, 0x1F):
        assert (w & 0xF) == 0 and (w >> 21) & 31 == 29, f"sq/lq 0x{w:08X} not at a 16-byte offset from sp"
        return (w & 0x03FFFFFF) | ((0x2B if w >> 26 == 0x1F else 0x23) << 26)
    return w


def machine(words):
    uc = Uc(UC_ARCH_MIPS, UC_MODE_MIPS32 + UC_MODE_LITTLE_ENDIAN)
    uc.ctl_set_cpu_model(M.UC_CPU_MIPS32_24KF)                          # one with an FPU
    uc.reg_write(M.UC_MIPS_REG_CP0_STATUS, uc.reg_read(M.UC_MIPS_REG_CP0_STATUS) | 0x20000000)   # CU1: FPU on
    uc.mem_map(0, RAM)
    uc.mem_write(build.MOD_BASE, struct.pack(f"<{len(words)}I", *map(mips32, words)))
    code = [0x3C1A0000 | (FP_IN >> 16)]                                   # lui k0, FP_IN >> 16
    code += [0xC7400000 | (f << 16) | (4 * f) for f in range(32)]         # lwc1 $f, 4f(k0)
    code += [build.jal(build.MOD_BASE), 0]
    code += [0xE7400000 | (f << 16) | (0x100 + 4 * f) for f in range(32)]  # swc1 $f, 0x100+4f(k0)
    uc.mem_write(CALLER, struct.pack(f"<{len(code)}I", *code))
    uc.mem_write(GAME_FN, struct.pack("<2I", 0x03E00008, 0))          # jr ra; nop

    return uc


def call_site(uc):
    uc.reg_write(UC_MIPS_REG_29, SP)
    uc.emu_start(CALLER, STOP)


PLAYER = 0x00476DD0
PAD = 0x00292D40
DZ = 0x0023BAE8 + 4


def frame(uc, t, pad=(0, 128, 128, 128, 128), stick=None):
    """One call at time t seconds: pad = (buttons, rx, ry, lx, ly); stick = the game's (mag, ang)."""
    uc.mem_write(COUNT_AT, struct.pack("<I", int(t * COUNT_HZ) & 0xFFFFFFFF))
    b, rx, ry, lx, ly = pad
    uc.mem_write(PAD + 2, struct.pack("<H6B", b, rx, ry, lx, ly, 0, 0))
    mag, ang = stick or (0, 0)
    uc.mem_write(PLAYER + 0xF78 + 0x20, struct.pack("<hH", mag, ang))
    call_site(uc)


def status(uc):
    return struct.unpack("<4I3fI I3f 3f3f I I", uc.mem_read(0x000AFF00, 0x50))


def cam(uc):
    return struct.unpack("<3f", uc.mem_read(0x00306320 + 0x18, 12)), struct.unpack("<3f", uc.mem_read(0x00306320 + 0x24, 12))


def heading(uc):
    return struct.unpack("<H", uc.mem_read(PLAYER + 0x92, 2))[0] * 360 / 65536


def close(a, b, tol=0.05):
    return all(abs(x - y) <= tol for x, y in zip(a, b))


def milestone2(uc):
    import math
    # her at (100, -5, 2500) facing +z (heading 0); dead zone 0x20; standing
    uc.mem_write(DZ, struct.pack("<h", 0x20))
    uc.mem_write(0x000AFF04, struct.pack("<I", 0))                         # force a fresh start
    t = 10.0
    frame(uc, t)
    eye, look = cam(uc)
    p8 = math.radians(8)
    pivot = (135.0, 145.0, 2500.0)
    d = (0.0, math.sin(-p8), math.cos(p8))
    want_eye = tuple(pivot[i] - d[i] * 220 for i in range(3))
    want_look = tuple(pivot[i] + d[i] * 200 for i in range(3))
    expect(close(eye, want_eye) and close(look, want_look),
           f"camera behind her shoulder: eye {tuple(round(v, 1) for v in eye)}, look {tuple(round(v, 1) for v in look)}")
    expect(struct.unpack("<h", uc.mem_read(DZ, 2))[0] == 0x80, "right stick alone: the game sees it centred (no callouts)")
    uc.mem_write(0x00306320 + 0x60, struct.pack("<f", 27.0))                # a narrow shot
    t += 1 / 60
    frame(uc, t)
    fov = struct.unpack("<f", uc.mem_read(0x00306320 + 0x60, 4))[0]
    expect(fov == 27.0, f"FOV: the shot's own, left alone as in the PC tool ({fov:g})")
    for i in range(60):                                                    # 1 s full right
        t += 1 / 60
        frame(uc, t, (0, 255, 128, 128, 128))
    st = status(uc)
    expect(abs(math.degrees(st[9]) + 100) < 1.5, f"1 s of full right stick turns the view 100 deg right (yaw {math.degrees(st[9]):.1f})")
    expect(abs(st[11] - 1 / 60) < 1e-4, f"dt from the cycle counter ({st[11]:.5f} s)")
    t += 1 / 60
    frame(uc, t, (0x0004, 255, 128, 128, 128))                              # L1 + right stick
    expect(struct.unpack("<h", uc.mem_read(DZ, 2))[0] == 0x20, "L1 + right stick: the game's dead zone back (callouts)")
    expect(abs(math.degrees(status(uc)[9]) + 100) < 1.5, "...and the camera doesn't turn")
    # walk: the game reads the stick as 90 deg (0x4000) from the camera's forward
    yaw = status(uc)[9]
    for i in range(30):
        t += 1 / 60
        frame(uc, t, stick=(127, 0x4000))
    eye, look = cam(uc)
    cam_deg = math.degrees(math.atan2(look[0] - eye[0], look[2] - eye[2]))
    h = heading(uc)
    want = (cam_deg + 90) % 360
    expect(abs((h - want + 180) % 360 - 180) < 1.0, f"modern walking: she faces camera + stick angle ({h:.1f} vs {want:.1f} deg)")
    t += 1 / 60
    frame(uc, t)                                                           # stick let go
    start = (want - 90) % 360                                              # 90 deg from where she should face
    uc.mem_write(PLAYER + 0x92, struct.pack("<H", int(start / 360 * 65536) & 0xFFFF))
    t += 1 / 60
    frame(uc, t, stick=(127, 0x4000))                                      # pushed again
    h2 = heading(uc)
    step = (h2 - start + 180) % 360 - 180
    expect(abs(abs(step) - 15.0) < 0.3, f"...turning at most 900 deg/s (one 1/60 s frame: {step:+.1f} deg)")
    uc.mem_write(PLAYER + 0x08, b"\x02")                                   # aiming: hands off
    uc.mem_write(PLAYER + 0x92, struct.pack("<H", 0x1234))
    t += 1 / 60
    frame(uc, t, stick=(127, 0x4000))
    expect(struct.unpack("<H", uc.mem_read(PLAYER + 0x92, 2))[0] == 0x1234, "aiming: her facing is left to the game")
    uc.mem_write(PLAYER + 0x08, b"\x00")
    # SELECT + R3 for 1.2 s: off; the game's camera and dead zone back
    for i in range(72):
        t += 1 / 60
        frame(uc, t, (0x0500, 128, 128, 128, 128))
    expect(status(uc)[8] == 0, "SELECT + R3 held 1 s: off")
    expect(struct.unpack("<h", uc.mem_read(DZ, 2))[0] == 0x20, "...the right stick handed back to the game")
    uc.mem_write(0x00306320 + 0x18, struct.pack("<3f", 1.0, 2.0, 3.0))
    t += 1 / 60
    frame(uc, t)
    expect(cam(uc)[0] == (1.0, 2.0, 3.0), "...and the camera left alone while off")
    for i in range(72):
        t += 1 / 60
        frame(uc, t, (0x0500, 128, 128, 128, 128))
    expect(status(uc)[8] == 1, "held again: on")
    t += 1 / 60
    frame(uc, t, (0xFFFF, 255, 255, 255, 255))
    expect(status(uc)[8] == 1, "a garbage pad read (every button) toggles nothing")


GRIDS = 0x003938A8
TRIS = 0x00500000


def room(uc, walls=(), floors=()):
    """A 4x4-cell grid per kind (cells 1000 wide from (-2000, 0)); every cell lists every triangle."""
    uc.mem_write(GRIDS, b"\0" * 0x40)
    addr = TRIS
    for g, tris in enumerate((walls, floors)):
        if not tris:
            continue
        ptrs = []
        for tri in tris:
            # with its unit normal and plane (n.x + d = 0), as the game's are: the patch skips others
            e1 = [tri[1][i] - tri[0][i] for i in range(3)]
            e2 = [tri[2][i] - tri[0][i] for i in range(3)]
            n = [e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]]
            ln = sum(c * c for c in n) ** 0.5
            n = [c / ln for c in n]
            d = -sum(n[i] * tri[0][i] for i in range(3))
            uc.mem_write(addr, struct.pack("<I9f3ff", 0, *[c for v in tri for c in v], *n, d))
            ptrs.append(addr)
            addr += 0x40
        lst = addr
        uc.mem_write(lst, struct.pack(f"<{len(ptrs) + 1}I", *ptrs, 0xFFFFFFFF))
        addr += 4 * (len(ptrs) + 1) + 0x10
        cells = addr
        uc.mem_write(cells, struct.pack("<16I", *[lst] * 16))
        addr += 0x40
        uc.mem_write(GRIDS + 0x20 * g, struct.pack("<4I2i2I", 1000, 1000, 4, 4, -2000, 0, cells, lst))


def quad(x0, x1, y0, y1, z):
    """Two triangles of the wall z = const."""
    return [((x0, y0, z), (x1, y0, z), (x1, y1, z)), ((x0, y0, z), (x1, y1, z), (x0, y1, z))]


def flat(x0, x1, z0, z1, y):
    return [((x0, y, z0), (x1, y, z0), (x1, y, z1)), ((x0, y, z0), (x1, y, z1), (x0, y, z1))]


def milestone3(uc):
    # fresh start behind her (heading 0, so the camera looks along +z and sits at z ~2282)
    uc.mem_write(PLAYER + 0x92, struct.pack("<H", 0))
    uc.mem_write(0x000AFF04, struct.pack("<I", 0))
    t = 100.0
    room(uc)
    frame(uc, t)
    free_eye, _ = cam(uc)
    expect(abs(free_eye[2] - 2282.1) < 0.5, f"no room data: camera as before (z {free_eye[2]:.1f})")
    room(uc, walls=quad(-1500, 1500, -500, 1500, 2400.0))      # a wall 100 behind her
    t += 1 / 60
    frame(uc, t)
    eye, _ = cam(uc)
    pull = struct.unpack("<f", uc.mem_read(0x000AFF50, 4))[0]
    expect(2400.0 < eye[2] < 2430.0, f"a wall behind her: the camera stops just in front of it (z {eye[2]:.1f}, pull {pull:.2f})")
    room(uc)
    t += 1 / 60
    frame(uc, t)
    eye2, _ = cam(uc)
    expect(eye[2] - 30 < eye2[2] < eye[2], f"the wall gone: it eases back out, not in one jump (z {eye2[2]:.1f})")
    for i in range(120):
        t += 1 / 60
        frame(uc, t)
    eye3, _ = cam(uc)
    expect(abs(eye3[2] - free_eye[2]) < 2.0, f"...and is back after 2 s (z {eye3[2]:.1f})")
    # something in the wall lists that isn't a proper triangle (no unit normal): not a wall
    room(uc, walls=quad(-1500, 1500, -500, 1500, 2400.0))
    for a in range(TRIS, TRIS + 0x80, 0x40):
        uc.mem_write(a + 0x28, b"\0" * 16)
    for i in range(60):
        t += 1 / 60
        frame(uc, t)
    eye5, _ = cam(uc)
    expect(abs(eye5[2] - free_eye[2]) < 2.0, f"an entry that isn't a real triangle doesn't pull the camera in (z {eye5[2]:.1f})")
    room(uc, floors=flat(-1500, 1500, 1000, 3500, -200.0))      # the floor far below (a stairwell)
    t += 1 / 60
    frame(uc, t)
    eye4, _ = cam(uc)
    expect(abs(eye4[1] - (-200.0 + 190.0)) < 0.5, f"head-room: the camera at most 190 above the floor under it (y {eye4[1]:.1f})")
    room(uc)


SERVO = 0x00556800
JR_RA, RATE_STEP, RATE_BACK = 0x03E00008, 0x24060492, 0x24060AAA


def plant_servo(uc):
    """A function shaped like the servo, built from the signature alone (its step constants and
    stores to +0xBC8), not from game code."""
    sh = lambda rt: 0xA4000000 | (4 << 21) | (rt << 16) | 0x0BC8               # sh rt, 0xBC8(a0)
    words = [0x27BDFFC0, 0x03E00008 ^ 0x10] + [sh(r) for r in range(2, 7)]     # prologue, filler, 5 stores
    words += [RATE_STEP, 0, RATE_STEP, 0, RATE_BACK, 0, JR_RA, 0]
    uc.mem_write(SERVO - 8, struct.pack("<2I", JR_RA, 0))                      # the previous function's end
    uc.mem_write(SERVO, struct.pack(f"<{len(words)}I", *words))
    return words[:2]


def milestone4(uc):
    import math
    orig = plant_servo(uc)
    uc.mem_write(PLAYER + 0x92, struct.pack("<H", 0))
    uc.mem_write(PLAYER + 0x08, b"\x00")
    uc.mem_write(0x000AFF04, struct.pack("<I", 0))
    t = 200.0
    room(uc)
    for i in range(70):                                       # the scan covers GAME.BIN in ~60 frames
        t += 1 / 60
        frame(uc, t)
    words = struct.unpack("<2I", uc.mem_read(SERVO, 8))
    srv = struct.unpack("<I", uc.mem_read(0x000AFF5C, 4))[0]
    expect(words == (JR_RA, 0) and srv == SERVO | 1, f"servo found and switched off within ~1 s (status 0x{srv:08X})")
    yaw0 = status(uc)[9]
    uc.mem_write(PLAYER + 0x24, struct.pack("<I", 0x00412345))                # a lock-on the game made
    uc.mem_write(PLAYER + 0xCDC, b"\x03")
    uc.mem_write(PLAYER + 0x08, b"\x02")                                     # aim
    uc.mem_write(PLAYER + 0x92, struct.pack("<H", 0x4000))                    # she faces 90 deg off the view
    t += 1 / 60
    frame(uc, t)
    a = struct.unpack("<IIIf", uc.mem_read(0x000AFF54, 16))
    expect(a[0] == 1 and a[1] == 1, "aiming with the servo off: free aim")
    expect(abs(status(uc)[9] - yaw0) < 1e-6, "RE5 style: the view doesn't move when the aim starts")
    expect(struct.unpack("<I", uc.mem_read(PLAYER + 0x24, 4))[0] == 0 and uc.mem_read(PLAYER + 0xCDC, 1)[0] == 0xFF,
           "the game's lock-on is cleared")
    h1 = heading(uc)
    expect(abs(((h1 - 90) + 180) % 360 - 180) - 12.0 < 0.5, f"she turns into the aim at 720 deg/s (one frame: 90 -> {h1:.1f})")
    for i in range(40):
        t += 1 / 60
        frame(uc, t)
    eye, look = cam(uc)
    view = math.degrees(math.atan2(look[0] - eye[0], look[2] - eye[2]))
    h2 = heading(uc)
    expect(abs(((h2 - view) + 180) % 360 - 180) < 3.0, f"...then faces where the camera looks ({h2:.1f} vs view {view:.1f}; convergence adds a little)")
    p = struct.unpack("<3f", uc.mem_read(PLAYER + 0x38, 12))
    pivot_d = math.dist(eye, (p[0] + math.cos(math.radians(view)) * 50, p[1] + 160, p[2] - math.sin(math.radians(view)) * 50))
    expect(abs(pivot_d - 120) < 2.0, f"aim rig: the camera 120 behind her aim shoulder ({pivot_d:.1f})")
    for i in range(30):                                                         # tilt up (stick up = y 0)
        t += 1 / 60
        frame(uc, t, (0, 128, 0, 128, 128))
    elev = struct.unpack("<h", uc.mem_read(PLAYER + 0xBC8, 2))[0] * 360 / 65536
    pitch = math.degrees(status(uc)[10])
    expect(elev > 5 and pitch < 0, f"looking up raises the gun (camera pitch {pitch:.1f}, gun {elev:+.1f} deg)")
    for i in range(120):
        t += 1 / 60
        frame(uc, t, (0, 128, 0, 128, 128))
    elev = struct.unpack("<h", uc.mem_read(PLAYER + 0xBC8, 2))[0] * 360 / 65536
    expect(elev <= 45.01, f"...up to 45 deg ({elev:.1f})")
    line = uc.mem_read(0x002F7450 + 30 * 0x8C, 0x10)
    used, x, y = struct.unpack_from("<3h", line, 0)
    expect(used == 1 and (x, y) == (308, 212) and bytes(line[0x0C:0x0E]) == b"\x81\x7b",
           f"crosshair: the square plus, centred ({x}, {y})")
    # device report: looking a little down pointed the gun at the floor, and a little up sent it
    # all the way up. With a floor under her, sweep the view from down to up across the horizon.
    room(uc, floors=flat(-1500, 1500, 1000, 4000, -5.0))
    guns = []
    for i in range(90):
        t += 1 / 60
        frame(uc, t, (0, 128, 255 if i < 45 else 0, 128, 128))              # down, then up
        cam_elev = -math.degrees(status(uc)[10])
        gun = struct.unpack("<h", uc.mem_read(PLAYER + 0xBC8, 2))[0] * 360 / 65536
        guns.append((cam_elev, gun))
    worst = max(abs(c - g) for c, g in guns)
    jump = max(abs(guns[i + 1][1] - guns[i][1]) for i in range(len(guns) - 1))
    expect(worst < 2.0, f"the gun follows the camera's elevation over a floor, the floor ignored (worst {worst:.2f} deg: "
                        "the camera sits above her chest)")
    expect(jump < 1.5, f"...with no jump crossing the horizon (largest step {jump:.2f} deg per frame)")
    room(uc)
    # an enemy on the crosshair, 500 out: the shot must go from her chest through the point on
    # the camera ray where the enemy is (device: "off a bit diagonally bottom right")
    for i in range(200):                                                        # level the view
        pitch = math.degrees(status(uc)[10])
        if abs(pitch) < 0.6:
            break
        t += 1 / 60
        frame(uc, t, (0, 128, 255 if pitch < 0 else 0, 128, 128))
    for i in range(30):
        t += 1 / 60
        frame(uc, t)                                                            # settle
    eye, look = cam(uc)
    dvec = [look[i] - eye[i] for i in range(3)]
    n = math.sqrt(sum(c * c for c in dvec))
    dvec = [c / n for c in dvec]
    hit = [eye[i] + dvec[i] * 500 for i in range(3)]
    p = struct.unpack("<3f", uc.mem_read(PLAYER + 0x38, 12))
    scen = struct.unpack("<H", uc.mem_read(PLAYER + 0x1E2, 2))[0]
    thing = 0x003FFCA0 + 7 * 0xD90
    uc.mem_write(thing, b"\x01\x01\x02\x01")
    uc.mem_write(thing + 0x38, struct.pack("<3f", hit[0] + 20, hit[1] - 100, hit[2]))   # its feet; the ray at chest height
    uc.mem_write(thing + 0x1E2, struct.pack("<H", scen))
    uc.mem_write(thing + 0x544, struct.pack("<h", 100))
    for i in range(30):
        t += 1 / 60
        frame(uc, t)
    conv = struct.unpack("<f", uc.mem_read(0x000AFF68, 4))[0]
    chest = (p[0], p[1] + 150, p[2])
    want_face = math.degrees(math.atan2(hit[0] - chest[0], hit[2] - chest[2]))
    want_elev = math.degrees(math.atan2(hit[1] - chest[1], math.hypot(hit[0] - chest[0], hit[2] - chest[2])))
    gun = struct.unpack("<h", uc.mem_read(PLAYER + 0xBC8, 2))[0] * 360 / 65536
    h = heading(uc)
    expect(abs(conv - 500) < 5, f"an enemy on the crosshair: converges on it ({conv:.0f} out)")
    expect(abs(((h - want_face) + 180) % 360 - 180) < 0.3 and abs(gun - want_elev) < 0.3,
           f"...her heading and gun go from her chest through it ({h:.2f}/{want_face:.2f}, {gun:.2f}/{want_elev:.2f} deg)")
    uc.mem_write(thing + 0x544, struct.pack("<h", 0))                          # dead
    t += 1 / 60
    frame(uc, t)
    conv = struct.unpack("<f", uc.mem_read(0x000AFF68, 4))[0]
    expect(abs(conv - 1500) < 5, f"...a dead one is ignored ({conv:.0f})")
    uc.mem_write(thing, b"\0")
    uc.mem_write(PLAYER + 0x08, b"\x00")
    t += 1 / 60
    frame(uc, t)
    expect(struct.unpack("<h", uc.mem_read(0x002F7450 + 30 * 0x8C, 2))[0] == 0, "aim released: crosshair gone")
    for i in range(72):
        t += 1 / 60
        frame(uc, t, (0x0500, 128, 128, 128, 128))
    expect(list(struct.unpack("<2I", uc.mem_read(SERVO, 8))) == orig, "SELECT + R3 off: the servo's own words are back")
    t += 1 / 60
    frame(uc, t)                                                                # let go
    for i in range(72):
        t += 1 / 60
        frame(uc, t, (0x0500, 128, 128, 128, 128))
    t += 1 / 60
    frame(uc, t)
    expect(struct.unpack("<2I", uc.mem_read(SERVO, 8)) == (JR_RA, 0), "on again: switched off again (GAME.BIN 'reloaded')")


def main():
    words, syms, notes = build.build()
    uc = machine(words)
    # fake game state: one player, ours is #0, at (100, -5, 2500); text slot 31 free
    uc.mem_write(0x0023BE14, struct.pack("<I", 1))
    uc.mem_write(0x0048BF7D, b"\0")
    uc.mem_write(0x00476DD0 + 0x38, struct.pack("<3f", 100.0, -5.0, 2500.0))
    uc.mem_write(DZ, struct.pack("<h", 0x20))      # the game sets its dead zones at boot, before SUBMAIN
    live = [n for n in range(1, 32) if n not in (26, 27, 29, 31)]      # all but k0/k1/sp/ra
    for n in live:
        uc.reg_write(reg(n), 0x1000 * n + 0x77)
    uc.mem_write(FP_IN, struct.pack("<32I", *[0x3F800000 + f for f in range(32)]))
    try:
        call_site(uc)
    except UcError as e:
        expect(False, f"one call runs to the end ({e} at 0x{uc.reg_read(UC_MIPS_REG_PC):08X})")
        return 1
    live = [n for n in live if n != 26]                                     # k0: the caller's own
    expect(all(uc.reg_read(reg(n)) & 0xFFFFFFFF == 0x1000 * n + 0x77 for n in live),
           "the stub hands every integer register back unchanged")
    expect(struct.unpack("<32I", uc.mem_read(FP_OUT, 128)) == tuple(0x3F800000 + f for f in range(32)),
           "...and every FPU register")
    expect(uc.reg_read(UC_MIPS_REG_29) == SP, "...and the stack pointer")
    expect(uc.reg_read(UC_MIPS_REG_31) == SITE + 8, "the game function returns to the call site")
    st = struct.unpack("<4I3fI", uc.mem_read(0x000AFF00, 32))
    expect(st[0] == 0x434D4F31 and st[1] == 6 and st[2] == 1 and st[7] == 1, "status: magic, version 6, 1 frame, 1 start")
    expect(st[3] == SITE + 8, "status: the caller")
    expect(st[4:7] == (100.0, -5.0, 2500.0), "status: our player's position")
    for _ in range(40):
        call_site(uc)
    line = uc.mem_read(0x002F7450 + 31 * 0x8C, 0x8C)
    used, x, y, timer = struct.unpack_from("<4h", line, 0)
    text = bytes(line[0x0C:]).split(b"\0")[0]
    expect(used == 1 and line[0x0B] == 0xFF and text.startswith(b"Modern Camera"),
           f"on-screen line shown once the room is up ({text!r}, timer {timer})")
    uc.mem_write(0x002F7450 + 31 * 0x8C, struct.pack("<h", 1) + b"\0" * 9 + b"\x10")   # the game's line, prio 0x10
    uc.mem_write(0x000AFF08, struct.pack("<I", 29))
    call_site(uc)
    expect(uc.mem_read(0x002F7450 + 31 * 0x8C + 0x0B, 1)[0] == 0x10, "a line the game is using is left alone")
    milestone2(uc)
    milestone3(uc)
    milestone4(uc)
    print("\nALL PASS" if not FAILS else f"\n{len(FAILS)} FAILURE(S)")
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
