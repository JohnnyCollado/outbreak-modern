/* Segment vs the room's collision triangles: tools/f1_collide.py, inside the game. The data is
   read in place every call (no copy, no cache): a room change needs nothing from us. */
#include "collide.h"
#include "mathf.h"

#define MAX_STEPS   256     /* samples along one segment */
#define MAX_TRIS    512     /* triangles looked at per cell */
#define RAM_TOP     0x02000000u

typedef struct {
    u32 cw, cd, nx, nz;
    s32 ox, oz;
    u32 cells, list;
} Grid;

static const Grid *grid(int g) {
    const Grid *G = (const Grid *)(COLL_GRIDS + 0x20 * g);
    if (G->cw == 0 || G->cw >= 100000 || G->cd == 0 || G->cd >= 100000 || G->nx == 0 || G->nx > 512
        || G->nz == 0 || G->nz > 512 || G->cells == 0 || G->cells >= RAM_TOP) {
        return 0;
    }
    return G;
}

int coll_ready(void) {
    return grid(0) || grid(1);
}

/* Moller-Trumbore, either side: t in [0, 1] or -1. */
static float seg_tri(const float *a, const float *d, const float *v) {
    float e1[3], e2[3], p[3], s[3], q[3], det, inv, u, w, t;
    int i;
    for (i = 0; i < 3; i++) {
        e1[i] = v[3 + i] - v[i];
        e2[i] = v[6 + i] - v[i];
    }
    p[0] = d[1] * e2[2] - d[2] * e2[1];
    p[1] = d[2] * e2[0] - d[0] * e2[2];
    p[2] = d[0] * e2[1] - d[1] * e2[0];
    det = e1[0] * p[0] + e1[1] * p[1] + e1[2] * p[2];
    if (absf(det) < 1e-9f) return -1.0f;
    inv = 1.0f / det;
    for (i = 0; i < 3; i++) s[i] = a[i] - v[i];
    u = (s[0] * p[0] + s[1] * p[1] + s[2] * p[2]) * inv;
    if (u < 0.0f || u > 1.0f) return -1.0f;
    q[0] = s[1] * e1[2] - s[2] * e1[1];
    q[1] = s[2] * e1[0] - s[0] * e1[2];
    q[2] = s[0] * e1[1] - s[1] * e1[0];
    w = (d[0] * q[0] + d[1] * q[1] + d[2] * q[2]) * inv;
    if (w < 0.0f || u + w > 1.0f) return -1.0f;
    t = (e2[0] * q[0] + e2[1] * q[1] + e2[2] * q[2]) * inv;
    return t >= 0.0f && t <= 1.0f ? t : -1.0f;
}

/* What f1_collide.tri_is_sane accepts (the PC tool used only these; device: in the PC version the
   camera never "started zoomed in"): a unit normal at +0x28 (v + 9), and every vertex on the plane
   n.x + d = 0 or n.x = d, d at +0x34. Anything else in a list is not a wall to stop at. */
static int tri_sane(const float *v) {
    const float *n = v + 9, d = v[12];
    float ln = n[0] * n[0] + n[1] * n[1] + n[2] * n[2], dots[3], m = 0.0f, tol;
    int i, plus = 1, minus = 1;
    if (!(ln > 0.9604f && ln < 1.0404f)) return 0;          /* |n| in (0.98, 1.02) */
    for (i = 0; i < 3; i++) {
        dots[i] = n[0] * v[3 * i] + n[1] * v[3 * i + 1] + n[2] * v[3 * i + 2];
        if (absf(dots[i]) > m) m = absf(dots[i]);
    }
    tol = 0.5f + 1e-4f * m;
    for (i = 0; i < 3; i++) {
        if (!(absf(dots[i] + d) < tol)) plus = 0;
        if (!(absf(dots[i] - d) < tol)) minus = 0;
    }
    return plus || minus;
}

/* The nearest hit along a->b in one cell's list, below `best`. */
static float cell_hit(u32 list, const float *a, const float *d, float best, int steep) {
    const u32 *ptr = (const u32 *)list;
    int k;
    for (k = 0; k < MAX_TRIS; k++) {
        u32 tri = ptr[k];
        float t;
        if (tri == 0xFFFFFFFFu) break;
        if (tri == 0 || tri >= RAM_TOP || (tri & 3) || !tri_sane((const float *)(tri + 4))) continue;
        if (steep && absf(((const float *)(tri + 4))[10]) > 0.7071f) continue;   /* normal's y: not a wall */
        t = seg_tri(a, d, (const float *)(tri + 4));
        if (t >= 0.0f && (best < 0.0f || t < best)) best = t;
    }
    return best;
}

float coll_first_hit(const float *a, const float *b) {
    return coll_first_hit_in(a, b, 3);
}

float coll_first_hit_in(const float *a, const float *b, int mask) {
    float d[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]}, best = -1.0f;
    int g;
    for (g = 0; g < 2; g++) {
        const Grid *G = grid(g);
        u32 last = 0xFFFFFFFFu;
        int n, i;
        if (!G || !(mask & (1 << g))) continue;
        /* the cells the segment crosses, sampled every 0.4 cells as the game does */
        n = (int)(sqrtf_((d[0] / (float)G->cw) * (d[0] / (float)G->cw) + (d[2] / (float)G->cd) * (d[2] / (float)G->cd))
                  * 2.5f) + 1;
        if (n > MAX_STEPS) n = MAX_STEPS;
        for (i = 0; i <= n; i++) {
            float f = (float)i / (float)n;
            float x = a[0] + d[0] * f - (float)G->ox, z = a[2] + d[2] * f - (float)G->oz;
            u32 cx, cz, idx, list;
            if (x < 0.0f || z < 0.0f) continue;
            cx = (u32)(int)(x / (float)G->cw);
            cz = (u32)(int)(z / (float)G->cd);
            if (cx >= G->nx || cz >= G->nz) continue;
            idx = cx * G->nz + cz;
            if (idx == last) continue;
            last = idx;
            list = ((const u32 *)G->cells)[idx];
            if (list == 0 || list >= RAM_TOP || (list & 3)) continue;
            best = cell_hit(list, a, d, best, mask & COLL_STEEP);
        }
    }
    return best;
}

float coll_floor_below(float x, float y_top, float z, float depth, float none) {
    float a[3] = {x, y_top, z}, b[3] = {x, y_top - depth, z};
    float t = coll_first_hit(a, b);
    return t < 0.0f ? none : y_top - t * depth;
}
