/* The room's collision triangles, read where the game keeps them (decomp game/collgrid.c,
   collision.c; device-checked by tools/f1_collide.py: 181 triangles, all on their planes):
   0x3938A8: two grids of 0x20 bytes {cell width, cell depth, cells along x, cells along z,
   origin x (s32), origin z (s32), cells -> (x * nz + z) -> list of CollTri pointers ended by -1}.
   Grid 0 holds the walls, grid 1 the floors. A CollTri is 0x38 bytes, its vertices f32[3][3] at
   +0x04. There are no ceilings in the data (stairs probe, 2026-10-20). */
#ifndef COLLIDE_H
#define COLLIDE_H
#include "game.h"

#define COLL_GRIDS 0x003938A8u

/* Fraction t in [0, 1] along a->b of the first room triangle it crosses, or -1. */
float coll_first_hit(const float *a, const float *b);
/* The same, in the grids of `mask` only (1: walls, 2: floors). */
float coll_first_hit_in(const float *a, const float *b, int mask);
/* Height of the first surface below (x, y_top, z) within `depth`, or `none`. */
float coll_floor_below(float x, float y_top, float z, float depth, float none);
/* Whether a usable grid is loaded. */
int coll_ready(void);

#endif
