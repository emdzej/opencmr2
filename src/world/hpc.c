/* The stage's collision mesh (see hpc.h). */
#include "world/hpc.h"
#include "fixmath.h"
#include <stddef.h>

static const uint8_t *hpc;                       /* DAT_00591af8 */
static uint32_t level[5];                         /* 0x591b00: each level's nodes (offsets) */
static uint32_t verts, tris, list, grid;          /* 0x591b14, 0x591af0, 0x591b18, 0x591b20 */
int g_hpc_verts, g_hpc_tris;

static int32_t rd32(const uint8_t *p) { return (int32_t)((uint32_t)p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24); }
static int16_t rd16(const uint8_t *p) { return (int16_t)(p[0] | p[1] << 8); }

void Hpc_Free(void) { hpc = NULL; g_hpc_verts = g_hpc_tris = 0; }

/* The header: origin x and z (i32), five i16 node counts, two bytes; then the levels' 8-byte nodes
   (i16 triangle count or -1, two bytes, i32: the first list entry, or the first child in the next
   level); a u16 count, two bytes, the 12-byte vertices (x, y, z); a u16 count, two bytes, the 8-byte
   triangles (three u16 vertices, a surface byte, a byte); a u32 count and the u16 list; the top grid's
   width and height (i16). */
bool Hpc_Parse(const uint8_t *d, uint32_t size)
{
    Hpc_Free();
    if (!d || size < 0x14) return false;
    uint32_t o = 0x14;
    for (int i = 0; i < 5; i++) {
        level[i] = o;
        o += (uint32_t)rd16(d + 8 + 2 * i) * 8;
    }
    if (o + 4 > size) return false;
    uint32_t nv = (uint16_t)rd16(d + o);
    verts = o + 4;
    o = verts + nv * 12;
    if (o + 4 > size) return false;
    uint32_t nt = (uint16_t)rd16(d + o);
    tris = o + 4;
    o = tris + nt * 8;
    if (o + 4 > size) return false;
    uint32_t nl = (uint32_t)rd32(d + o);
    list = o + 4;
    grid = list + nl * 2;
    if (grid + 4 > size) return false;
    hpc = d;
    g_hpc_verts = (int)nv;
    g_hpc_tris = (int)nt;
    return true;
}

void Hpc_Triangle(int32_t out[9], int tri)
{
    const uint8_t *t = hpc + tris + (size_t)(int16_t)tri * 8;
    for (int k = 0; k < 3; k++) {
        const uint8_t *v = hpc + verts + (size_t)(uint16_t)rd16(t + 2 * k) * 12;
        for (int j = 0; j < 3; j++) out[3 * k + j] = rd32(v + 4 * j);
    }
}

void Hpc_TriVerts(int tri, uint16_t idx[3])
{
    const uint8_t *t = hpc + tris + (size_t)(int16_t)tri * 8;
    for (int k = 0; k < 3; k++) idx[k] = (uint16_t)rd16(t + 2 * k);
}

uint32_t Hpc_Surface(int tri) { return hpc[tris + (size_t)(int16_t)tri * 8 + 6] & 0x7f; }

static bool edge(const int32_t p[3], const int32_t *a, const int32_t *b)
{
    int32_t ex = b[2] - a[2], ez = -(b[0] - a[0]);
    return fx_mul(p[2] - a[2], ez) + fx_mul(p[0] - a[0], ex) >= 0;
}

bool Hpc_Inside(const int32_t p[3], int tri, const int32_t *t9)
{
    static int32_t own[9];   /* 0x591c58 */
    if (!t9) {
        Hpc_Triangle(own, tri);
        t9 = own;
    }
    int32_t x = p[0], z = p[2];
    if (!((t9[0] <= x || t9[3] <= x || t9[6] <= x) && (x <= t9[0] || x <= t9[3] || x <= t9[6]))) return false;
    if (!((t9[2] <= z || t9[5] <= z || t9[8] <= z) && (z <= t9[2] || z <= t9[5] || z <= t9[8]))) return false;
    return edge(p, t9, t9 + 3) && edge(p, t9 + 3, t9 + 6) && edge(p, t9 + 6, t9);
}

/* 0x4916a0 */
static bool pick(const int32_t p[3], int16_t *tri, int32_t y, int n, const uint8_t *cand)
{
    int best = -1;
    int32_t bd = 0;
    for (int i = 0; i < n; i++) {
        int16_t t = rd16(cand + 2 * i);
        int32_t t9[9];
        Hpc_Triangle(t9, t);
        if (!Hpc_Inside(p, t, t9)) continue;
        int32_t m = (t9[1] + t9[4] + t9[7]) / 3, d = y - m;
        if (d < 0) d = m - y;
        if (best < 0 || d < bd) best = i, bd = d;
    }
    if (best < 0) return false;
    *tri = rd16(cand + 2 * best);
    return true;
}

/* The top cell from the offset's top bytes (signed); each level down takes two more bits of x and z
   (child x + 4 z). */
bool Hpc_Find(const int32_t p[3], int16_t *tri, int32_t y)
{
    if (!hpc) return false;
    uint32_t ux = (uint32_t)(p[0] - rd32(hpc)), uz = (uint32_t)(p[2] - rd32(hpc + 4));
    int16_t cx = (int8_t)(ux >> 24), cz = (int8_t)(uz >> 24);
    int16_t w = rd16(hpc + grid), h = rd16(hpc + grid + 2);
    if (cx < 0 || cx >= w || cz < 0 || cz >= h) return false;
    int32_t idx = w * cz + cx;
    uint32_t mask = 0xffffff;
    int shift = 24;
    for (int l = 0;; l++) {
        const uint8_t *node = hpc + level[l] + (size_t)idx * 8;
        int16_t n = rd16(node);
        if (n != -1) return pick(p, tri, y, n, hpc + list + (size_t)rd32(node + 4) * 2);
        if (l >= 4) return false;
        ux &= mask;
        uz &= mask;
        mask = (uint32_t)((int32_t)mask >> 2);
        shift -= 2;
        idx = (int16_t)((int32_t)ux >> shift) + rd32(node + 4) + (int16_t)((int32_t)uz >> shift) * 4;
    }
}

static void unit(int32_t v[3])
{
    uint32_t l = Fx_Sqrt((uint32_t)fx_mul(v[0], v[0]) + (uint32_t)fx_mul(v[1], v[1]) + (uint32_t)fx_mul(v[2], v[2]));
    if (!l) { v[0] = v[1] = v[2] = 0; return; }
    int32_t inv = (int32_t)(0x100000000LL / (int32_t)l);
    for (int i = 0; i < 3; i++) v[i] = fx_mul(v[i], inv);
}

/* The normal is the cross product of the two unit edges from the first vertex, made unit. */
int32_t Hpc_Height(const int32_t p[3], int tri, int32_t y, int32_t n[3], uint16_t *surface)
{
    int32_t t[9];
    Hpc_Triangle(t, tri);
    *surface = (uint16_t)Hpc_Surface(tri);
    int32_t a[3] = {t[3] - t[0], t[4] - t[1], t[5] - t[2]}, b[3] = {t[6] - t[0], t[7] - t[1], t[8] - t[2]};
    unit(a);
    unit(b);
    n[0] = fx_mul(a[1], b[2]) - fx_mul(a[2], b[1]);
    n[1] = fx_mul(a[2], b[0]) - fx_mul(a[0], b[2]);
    n[2] = fx_mul(a[0], b[1]) - fx_mul(a[1], b[0]);
    unit(n);
    if (!n[1]) return y;
    int32_t d = fx_mul(n[2], t[2]) + fx_mul(n[0], t[0]) + fx_mul(n[1], t[1]) - fx_mul(p[2], n[2]) - fx_mul(p[0], n[0]);
    return (int32_t)(((int64_t)d * 65536) / n[1]);
}

int32_t Hpc_Ground(const int32_t p[3], int32_t n[3], int16_t *tri, uint16_t *surface, int32_t y)
{
    if (*tri < 0) *tri = -1;
    if (*tri == -1) return Hpc_Find(p, tri, y) ? Hpc_Height(p, *tri, y, n, surface) : y;
    if (Hpc_Inside(p, *tri, NULL)) return Hpc_Height(p, *tri, y, n, surface);
    if (Hpc_Find(p, tri, y)) return Hpc_Height(p, *tri, y, n, surface);
    *tri = -1;
    return y;
}
