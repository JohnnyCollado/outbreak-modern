#!/usr/bin/env python3
"""Tests for tools/f1_collide.py and the camera pull-in. Run: python tools/test_collide.py

Builds a synthetic room (a wall at x = 800, in the decomp's CollGrid/CollTri layout) in a fake
EE RAM and checks the raycast, the cell walk, the plane sanity check and how far the camera is
pulled in when the wall is behind her.
"""
import math
import os
import struct
import sys
import types

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import f1_camtest as C  # noqa: E402
import f1_collide as K  # noqa: E402

FAILS = []


def expect(cond, what):
    print(("  ok   " if cond else "  FAIL ") + what)
    if not cond:
        FAILS.append(what)


class FakePine:
    def __init__(self, ram):
        self.ram = ram

    def read_block(self, addr, size):
        return bytes(self.ram[addr:addr + size])

    def read_multi(self, regions):
        return [self.read_block(a, n) for a, n in regions]


def tri_bytes(v, n, d, info=0x00000002):
    return struct.pack("<I9f3ff", info, *[c for p in v for c in p], *n, d)


def build_room(ram, wall_x=800.0, bad_plane=False):
    """Grid 0: 4 x 4 cells of 500, origin (0, 0); every cell lists the same two wall triangles."""
    tris, lst, cells = 0x00500000, 0x00501000, 0x00502000
    quad = [(wall_x, -100, 0), (wall_x, 500, 0), (wall_x, 500, 2000), (wall_x, -100, 2000)]
    d = -wall_x if not bad_plane else 123.0
    ram[tris:tris + 0x38] = tri_bytes([quad[0], quad[1], quad[2]], (1, 0, 0), d)
    ram[tris + 0x38:tris + 0x70] = tri_bytes([quad[0], quad[2], quad[3]], (1, 0, 0), d)
    struct.pack_into("<3I", ram, lst, tris, tris + 0x38, K.END)
    for i in range(16):
        struct.pack_into("<I", ram, cells + 4 * i, lst)
    struct.pack_into("<8I", ram, K.GRIDS, 500, 500, 4, 4, 0, 0, cells, lst)


def main():
    print("geometry:")
    tri = ((0, 0, 0), (0, 10, 0), (0, 0, 10))
    expect(abs(K.segment_triangle((-5, 2, 2), (5, 2, 2), tri) - 0.5) < 1e-9, "segment crosses triangle at t = 0.5")
    expect(K.segment_triangle((5, 2, 2), (-5, 2, 2), tri) is not None, "hit from the back side too")
    expect(K.segment_triangle((-5, 9, 9), (5, 9, 9), tri) is None, "miss outside the triangle")
    expect(K.segment_triangle((-5, 2, 2), (-1, 2, 2), tri) is None, "segment stopping short misses")
    g = K.Grid(struct.pack("<8I", 500, 500, 4, 4, 0, 0, 1, 1))
    expect(g.cell_of(1000, 1000) == 2 * 4 + 2, "cell index = cx * nz + cz")
    expect(g.cell_of(-1, 10) is None and g.cell_of(2001, 10) is None, "outside the grid -> None")
    expect(g.cells_along((100, 0, 100), (1900, 0, 100)) == [0, 4, 8, 12], "walk along x visits each cell once")
    expect(K.tri_is_sane(tri, (1, 0, 0), 0.0), "plane check accepts a good triangle")
    expect(not K.tri_is_sane(tri, (0.5, 0, 0), 0.0), "plane check rejects a non-unit normal")

    print("synthetic room:")
    ram = bytearray(32 << 20)
    build_room(ram)
    room = K.RoomCollision(FakePine(ram))
    expect(room.refresh() and 0 in room.grids, "grid 0 recognised")
    t = room.first_hit((1000, 140, 1000), (500, 140, 1000))
    expect(t is not None and abs(t - 0.4) < 1e-6, f"wall 200 units behind -> t = 0.4 (got {t})")
    expect(room.first_hit((1000, 140, 1000), (1500, 140, 1000)) is None, "clear the other way")
    expect(room.stats["insane"] == 0 and room.stats["tris"] == 2, "both triangles read and sane")

    bad = bytearray(32 << 20)
    build_room(bad, bad_plane=True)
    broken = K.RoomCollision(FakePine(bad))
    broken.refresh()
    expect(broken.first_hit((1000, 140, 1000), (500, 140, 1000)) is None and broken.stats["insane"] == 2,
           "misread triangles are rejected, not used")

    print("floor under a point:")
    fl = bytearray(32 << 20)
    tris, lst, cells = 0x00500000, 0x00501000, 0x00502000
    # a ramp (stairs) rising along +x from y=0 at x=0 to y=400 at x=2000, in grid 1
    v = [(0, 0, 0), (2000, 400, 0), (2000, 400, 2000), (0, 0, 2000)]
    n = (-0.196116, 0.980581, 0.0)
    fl[tris:tris + 0x38] = tri_bytes([v[0], v[1], v[2]], n, 0.0)
    fl[tris + 0x38:tris + 0x70] = tri_bytes([v[0], v[2], v[3]], n, 0.0)
    struct.pack_into("<3I", fl, lst, tris, tris + 0x38, K.END)
    for i in range(16):
        struct.pack_into("<I", fl, cells + 4 * i, lst)
    struct.pack_into("<8I", fl, K.GRIDS + 0x20, 500, 500, 4, 4, 0, 0, cells, lst)
    ramp = K.RoomCollision(FakePine(fl))
    ramp.refresh()
    y = ramp.floor_below(1000, 900, 700)
    expect(y is not None and abs(y - 200) < 1e-3, f"floor under x=1000 on the ramp is y=200 (got {y})")
    expect(ramp.floor_below(1000, 100, 700) is None, "nothing below a point under the ramp")
    expect(ramp.stats["insane"] == 0, "ramp triangles pass the plane check")

    print("camera pull-in:")
    a = types.SimpleNamespace(distance=220, height=150, target_height=140, shoulder=0, lookahead=200,
                              yaw_offset=0, wall_margin=20)
    p = (1000.0, 0.0, 1000.0)
    eye, _ = C.ots_camera(p, math.pi / 2, 0, 0, a)            # faces +X: camera at x = 780, past the wall
    pivot, pull = C.collide_pull(room, p, eye, math.pi / 2, a, a.wall_margin)
    pulled = tuple(pivot[i] + (eye[i] - pivot[i]) * pull for i in range(3))
    expect(eye[0] < 800 < pulled[0], f"eye moved from x = {eye[0]:.0f} to the room side, x = {pulled[0]:.0f}")
    expect(abs(pulled[0] - 820) < 1, "and stops wall_margin (20) in front of the wall")

    a.shoulder = 50                                          # squeezed: ends at her shoulder, not her middle
    eye, _ = C.ots_camera(p, math.pi / 2, 0, 0, a)
    pivot, pull = C.collide_pull(room, p, eye, math.pi / 2, a, a.wall_margin)
    pulled = tuple(pivot[i] + (eye[i] - pivot[i]) * pull for i in range(3))
    expect(abs(abs(pulled[2] - p[2]) - 50) < 1, f"pulled-in eye keeps the 50 shoulder offset (z off by {pulled[2] - p[2]:.1f})")

    side_wall = bytearray(32 << 20)                          # a wall 30 to her right: shoulder point shrinks
    build_room(side_wall, wall_x=1030.0)
    near = K.RoomCollision(FakePine(side_wall))
    near.refresh()
    yaw = 0.0                                               # faces +Z, so "right" is +X
    eye, _ = C.ots_camera(p, yaw, 0, 0, a)
    pivot, _ = C.collide_pull(near, p, eye, yaw, a, a.wall_margin)
    expect(abs(pivot[0] - 1010) < 1, f"shoulder point stops wall_margin short of a wall beside her (x = {pivot[0]:.1f})")
    print("\nALL PASS" if not FAILS else f"\n{len(FAILS)} FAILURE(S)")
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
