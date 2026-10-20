"""READ-ONLY access to File #1's room collision, for keeping the camera inside the room.

Layout from the CC0 decomp (src/SLPM_654.28/game/collision.c, collgrid.c):
  0x3938A8   two CollGrid descriptors (0x20 bytes each):
               +0x00 cell width   +0x04 cell depth   (u32)
               +0x08 cells along x   +0x0C cells along z   (u32)
               +0x10 origin x   +0x14 origin z   (u32)
               +0x18 cells: pointer to (cells along x * cells along z) pointers,
                     each to a list of CollTri pointers ended by -1;  cell index = cx * nz + cz
  CollTri (0x38 bytes): info u32 (kind = low byte), v[3][3] f32 at +0x04, normal f32[3] at
                        +0x28, d f32 at +0x34
The game's own nearest-hit raycast (func_005494C0) walks grid 0. GAME.BIN differs in the English
patch, so we don't call it; we repeat the test here on data read through PINE.

Every room's data is sanity-checked (vertices on their plane, unit normals) before use.
"""
import math
import struct

GRIDS = 0x003938A8
TRI_SIZE = 0x38
LIST_CHUNK = 64          # pointers read per round trip when walking a cell's list
END = 0xFFFFFFFF


class GridError(Exception):
    pass


class Grid:
    def __init__(self, raw):
        (self.cw, self.cd, self.nx, self.nz, ox, oz, self.cells, self.list) = struct.unpack("<8I", raw)
        self.ox, self.oz = float(ox), float(oz)

    def valid(self):
        return (0 < self.cw < 100000 and 0 < self.cd < 100000 and 0 < self.nx <= 512 and 0 < self.nz <= 512
                and 0 < self.cells < 0x02000000)

    def key(self):
        return (self.cells, self.nx, self.nz, self.cw, self.cd)

    def cell_of(self, x, z):
        cx, cz = int((x - self.ox) // self.cw), int((z - self.oz) // self.cd)
        if 0 <= cx < self.nx and 0 <= cz < self.nz:
            return cx * self.nz + cz
        return None

    def cells_along(self, a, b, step=0.4):
        """Cell indices a segment crosses in x/z, sampled every `step` cells like the game does."""
        dx, dz = b[0] - a[0], b[2] - a[2]
        n = max(1, int(math.hypot(dx / self.cw, dz / self.cd) / step) + 1)
        out = []
        for i in range(n + 1):
            c = self.cell_of(a[0] + dx * i / n, a[2] + dz * i / n)
            if c is not None and c not in out:
                out.append(c)
        return out


def segment_triangle(a, b, tri):
    """Fraction t in [0, 1] along a->b where it crosses the triangle (either side), else None.
    Moller-Trumbore."""
    (v0, v1, v2) = tri
    e1 = [v1[i] - v0[i] for i in range(3)]
    e2 = [v2[i] - v0[i] for i in range(3)]
    d = [b[i] - a[i] for i in range(3)]
    p = [d[1] * e2[2] - d[2] * e2[1], d[2] * e2[0] - d[0] * e2[2], d[0] * e2[1] - d[1] * e2[0]]
    det = sum(e1[i] * p[i] for i in range(3))
    if abs(det) < 1e-9:
        return None
    inv = 1.0 / det
    s = [a[i] - v0[i] for i in range(3)]
    u = sum(s[i] * p[i] for i in range(3)) * inv
    if u < 0 or u > 1:
        return None
    q = [s[1] * e1[2] - s[2] * e1[1], s[2] * e1[0] - s[0] * e1[2], s[0] * e1[1] - s[1] * e1[0]]
    v = sum(d[i] * q[i] for i in range(3)) * inv
    if v < 0 or u + v > 1:
        return None
    t = sum(e2[i] * q[i] for i in range(3)) * inv
    return t if 0 <= t <= 1 else None


def parse_tri(raw):
    info = struct.unpack_from("<I", raw, 0)[0]
    v = [struct.unpack_from("<3f", raw, 0x04 + 12 * i) for i in range(3)]
    n = struct.unpack_from("<3f", raw, 0x28)
    d = struct.unpack_from("<f", raw, 0x34)[0]
    return info, v, n, d


def tri_is_sane(v, n, d):
    """Unit normal, and every vertex on the plane n.x + d = 0 or n.x = d (whichever the data uses)."""
    ln = math.sqrt(sum(c * c for c in n))
    if not 0.98 < ln < 1.02:
        return False
    dots = [sum(n[i] * p[i] for i in range(3)) for p in v]
    tol = 0.5 + 1e-4 * max(abs(x) for x in dots)
    return all(abs(x + d) < tol for x in dots) or all(abs(x - d) < tol for x in dots)


class RoomCollision:
    """Lazy, cached view of the room's collision triangles. Re-reads when the room changes."""

    def __init__(self, pine, which=(0, 1)):
        self.pine, self.which = pine, which
        self.grids, self.cell_tris, self.tris = {}, {}, {}
        self.stats = {"tris": 0, "insane": 0, "cells": 0}

    def refresh(self):
        """Re-read the grid descriptors; drop caches if the room changed. Returns True if usable."""
        raw = self.pine.read_block(GRIDS, 0x40)
        grids = {}
        for g in self.which:
            grid = Grid(raw[0x20 * g:0x20 * g + 0x20])
            if grid.valid():
                grids[g] = grid
        if {g: x.key() for g, x in grids.items()} != {g: x.key() for g, x in self.grids.items()}:
            self.cell_tris, self.tris = {}, {}
            self.stats = {"tris": 0, "insane": 0, "cells": 0}
        self.grids = grids
        return bool(grids)

    def _cell(self, g, idx):
        key = (g, idx)
        if key in self.cell_tris:
            return self.cell_tris[key]
        grid = self.grids[g]
        lst = struct.unpack("<I", self.pine.read_block(grid.cells + 4 * idx, 4))[0]
        ptrs = []
        while lst and len(ptrs) < 4096:
            chunk = struct.unpack(f"<{LIST_CHUNK}I", self.pine.read_block(lst + 4 * len(ptrs), 4 * LIST_CHUNK))
            if END in chunk:
                ptrs += chunk[:chunk.index(END)]
                break
            ptrs += chunk
        new = [p for p in ptrs if p not in self.tris and 0 < p < 0x02000000]
        for i in range(0, len(new), 256):
            raws = self.pine.read_multi([(p, TRI_SIZE) for p in new[i:i + 256]])
            for p, raw in zip(new[i:i + 256], raws):
                info, v, n, d = parse_tri(raw)
                sane = tri_is_sane(v, n, d)
                self.tris[p] = (info, v) if sane else None
                self.stats["tris"] += 1
                self.stats["insane"] += not sane
        self.cell_tris[key] = [p for p in ptrs if self.tris.get(p)]
        self.stats["cells"] += 1
        return self.cell_tris[key]

    def first_hit(self, a, b):
        """Smallest fraction t along a->b that hits a room triangle, or None."""
        hit = self.first_hit_tri(a, b)
        return hit[0] if hit else None

    def first_hit_tri(self, a, b):
        """(t, kind byte, grid) of the first triangle along a->b, or None."""
        best = None
        for g, grid in self.grids.items():
            for idx in grid.cells_along(a, b):
                for p in self._cell(g, idx):
                    t = segment_triangle(a, b, self.tris[p][1])
                    if t is not None and (best is None or t < best[0]):
                        best = (t, self.tris[p][0] & 0xFF, g)
        return best

    def floor_below(self, x, y_top, z, depth=1500.0):
        """Height of the first surface below (x, y_top, z), or None. Ceilings are not in the
        collision data (device probe on stairs, 2026-10-20), floors are."""
        hit = self.first_hit_tri((x, y_top, z), (x, y_top - depth, z))
        return None if hit is None else y_top - hit[0] * depth

    def kinds(self):
        """How many distinct cached triangles there are of each kind byte, per grid."""
        seen = {(g, p) for (g, _), ptrs in self.cell_tris.items() for p in ptrs}
        out = {}
        for g, p in seen:
            k = (g, self.tris[p][0] & 0xFF)
            out[k] = out.get(k, 0) + 1
        return out
