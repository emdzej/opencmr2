/* The service car's damage on its stand (0x506bb0-0x5091c0, 0x509be0-0x509d90): the car's body meshes
   (the second model's slots, types 5-19) are put back to their rest shape and dented again by the hits
   in the player's carried log (0x407610), the glass cracked or broken by its panels, the fallen-off
   parts hidden, and the body set on the four corners' heights of the ride height and the suspension
   damage. The dents are the race's (sim/damage.c) on the stand's own record. */
#include "game/service_int.h"
#include "c3d.h"
#include "crt.h"
#include "exe.h"
#include "fixmath.h"
#include "fe/menu.h"
#include "game/mem.h"
#include "render/mesh.h"
#include "world/obj.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

enum { SLOTS = 15 };

/* a player's record (0x82d220 + 0x2ac n) */
typedef struct {
    uint8_t *mesh[SLOTS];          /* +0x000 */
    Obj *obj[SLOTS];               /* +0x03c */
    uint8_t *rest[SLOTS];          /* +0x078: the rest vertices (0x20 bytes each, as the race's) */
    int32_t ctr[SLOTS][3];         /* +0x0b4: the box's centre */
    int32_t half[SLOTS][3];        /* +0x168: its half size */
    int32_t ext[4];                /* +0x21c: the largest x, the smallest, the largest z, the smallest */
    int32_t susp[4];               /* +0x22c: the corners' suspension damage */
    int32_t axle[4];               /* +0x23c: the axles' (read by 0x509dc0, not used) */
    uint16_t nv[SLOTS];            /* +0x24c */
    uint8_t n;                     /* +0x26a */
    int32_t shown[SLOTS];          /* +0x26c */
    int32_t ready;                 /* +0x2a8 */
} SvDmg;

/* a player's points (0x82fd00 + 0x138 n): the box's eight corners, the meshes' extremes at the floor
   and raised, four wheel points (the dents' hull from +0x64), the wheels' places and turns */
typedef struct {
    int32_t pt[20][3];             /* +0x004 */
    int32_t wheel[4][3];           /* +0x0f4 */
    int32_t spin;                  /* +0x124 */
    int32_t turn[4];               /* +0x128 */
} SvHull;

static SvDmg rec[4];
static SvHull hull[4];
static uint8_t *rest_tab[4][SLOTS];   /* DAT_00831198 */
static uint8_t rest_type[4][SLOTS + 1];   /* DAT_0082d1dc (the port: a zero past the end, read there) */

/* the hit being replayed (0x82d120-0x82d158) */
static int32_t hit[3], nrm[3], ax[3], size_r, strength, r1, r2, r3;
static uint8_t kind_;

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static void wr32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8), p[2] = (uint8_t)(v >> 16), p[3] = (uint8_t)(v >> 24); }
static int32_t clamp127(int32_t v) { return v < 0x80 ? (v < -0x7f ? -0x7f : v) : 0x7f; }
static int32_t inv(int32_t v) { return v ? (int32_t)(0x100000000LL / v) : 0; }   /* the original traps on 0 */
static int32_t by255(uint8_t b) { return (int32_t)(((uint64_t)b << 32) / 0xff0000); }
static uint32_t carried(int p) { return 0x52ebc0 + 0x148u * (uint32_t)p; }   /* 0x407610 */

static uint8_t *verts(const SvDmg *d, int s)
{
    const Obj *o = d->obj[s];
    if (!o || !o->model || !d->mesh[s]) return NULL;
    return (uint8_t *)C3d_Ref(o->model, d->mesh[s] + 0x0c, o->model->verts);
}
static int32_t vget(const uint8_t *v, uint32_t o) { float f; memcpy(&f, v + o, 4); return (int32_t)nearbyint((double)f * 65536.0); }
static void vset(uint8_t *v, uint32_t o, int32_t x) { float f = (float)x * (float)(1.0 / 65536.0); memcpy(v + o, &f, 4); }
static void relight(const SvDmg *d, int s) { if (d->obj[s] && d->mesh[s]) Mesh_RestoreColours(d->obj[s]->model, d->mesh[s]); }   /* 0x4b1ea0 */

/* 0x507290: a mesh object into the slot of its type, its vertices' box and the car's extremes */
static void add_slot(SvDmg *d, Obj *o)
{
    int s = (int)(o->type & 0xff) - 5;
    if (!o->mesh || o->kind != 0 || s < 0 || s >= SLOTS) return;
    d->obj[s] = o, d->mesh[s] = o->mesh;
    d->nv[s] = (uint16_t)rd32(o->mesh + 0x10);   /* 0x4ab710 */
    uint8_t *v = verts(d, s);
    if (!v || !d->nv[s]) return;
    int32_t lo[3] = {0x640000, 0x640000, 0x640000}, hi[3] = {-0x640000, -0x640000, -0x640000};
    for (int k = 0; k < d->nv[s]; k++) {
        const uint8_t *x = v + (size_t)k * C3D_VERTEX;
        int32_t p[3] = {vget(x, 0), vget(x, 4), vget(x, 8)};
        for (int a = 0; a < 3; a++) {
            if (hi[a] < p[a]) hi[a] = p[a];
            if (p[a] < lo[a]) lo[a] = p[a];
        }
        if (p[0] < 0) { if (p[0] < d->ext[1]) d->ext[1] = p[0]; }
        else if (d->ext[0] < p[0]) d->ext[0] = p[0];
        if (p[2] < 0) { if (p[2] < d->ext[3]) d->ext[3] = p[2]; }
        else if (d->ext[2] < p[2]) d->ext[2] = p[2];
    }
    for (int a = 0; a < 3; a++) {
        d->ctr[s][a] = hi[a] + fx_mul(lo[a] - hi[a], 0x8000);
        d->half[s][a] = hi[a] - d->ctr[s][a];
    }
    d->n++;
}

/* 0x506bb0: the rest vertices of every slot, as the race's 0x46afe0 (the limits from the vertex
   colours less 0x80, the dent direction their negation); made again each time, the old ones kept */
static void save_rest(int p, const SvDmg *d)
{
    for (int s = 0; s < d->n; s++) {
        int nv = d->nv[s];
        rest_tab[p][s] = calloc((size_t)(nv > 0 ? nv : 1), 0x20);
        rest_type[p][s] = d->obj[s] ? (uint8_t)d->obj[s]->type : 0;
        const uint8_t *v = verts(d, s);
        for (int k = 0; v && k < nv; k++) {
            uint8_t *r = rest_tab[p][s] + 0x20 * k;
            const uint8_t *x = v + (size_t)k * C3D_VERTEX;
            for (uint32_t a = 0; a < 6; a++) wr32(r + 4 * a, (uint32_t)vget(x, 4 * a));
            int32_t lim[3], dir[3];
            uint32_t col = rd32(x + 0x18);
            lim[0] = clamp127((int32_t)(col >> 16 & 0xff) - 0x80);
            lim[1] = clamp127((int32_t)(col >> 8 & 0xff) - 0x80);
            lim[2] = clamp127((int32_t)(col & 0xff) - 0x80);
            for (int a = 0; a < 3; a++) r[0x1b + a] = (uint8_t)lim[a], dir[a] = lim[a] * -0x200;
            uint32_t l = Fx_Sqrt((uint32_t)fx_mul(dir[0], dir[0]) + (uint32_t)fx_mul(dir[1], dir[1]) + (uint32_t)fx_mul(dir[2], dir[2]));
            for (int a = 0; a < 3; a++) r[0x18 + a] = l ? (uint8_t)clamp127(dir[a] >> 9) : 0;
        }
    }
}

/* 0x506fc0: each slot's rest vertices found by its type (looking one past the last), then ready */
static void match_rest(int p, SvDmg *d)
{
    save_rest(p, d);
    for (int s = 0; s < d->n; s++) {
        int m = -1;
        for (int j = 0; j <= d->n && m < 0; j++)
            if (d->obj[s] && rest_type[p][j] == (uint8_t)d->obj[s]->type) m = j;
        if (m >= 0) d->rest[s] = m < SLOTS ? rest_tab[p][m] : NULL;
    }
    d->ready = 1;
}

/* 0x507080: the record from the body (type 5) and its children down each chain short of a type 0x14,
   the empty slots squeezed out (a slot looked at again while something moved) */
static void build(int p, Obj *second)
{
    SvDmg *d = &rec[p];
    memset(d, 0, sizeof *d);
    if (!second) return;
    add_slot(d, second);
    for (Obj *a = second->child; a; a = a->next)
        if ((uint8_t)a->type != 0x14)
            for (Obj *b = a; b; b = b->child) add_slot(d, b);
    for (int s = 0; s < SLOTS; s++) {
        if (d->obj[s] || s >= SLOTS - 1) continue;
        bool any = false;
        for (int i = s; i < SLOTS - 1; i++) {
            if (d->obj[i] || d->obj[i + 1]) any = true;
            d->mesh[i] = d->mesh[i + 1], d->obj[i] = d->obj[i + 1];
            d->nv[i] = d->nv[i + 1];
            memcpy(d->ctr[i], d->ctr[i + 1], 12), memcpy(d->half[i], d->half[i + 1], 12);
        }
        if (any) s--;
    }
    match_rest(p, d);
}

/* 0x507a10: the box of the car's class (its half sizes; the original's per-class constants), the
   extremes at its floor and raised to the class's front and rear heights, four wheel points in from
   the corners, the wheels' places and a random turn each */
static void make_hull(int p, int cls, Obj *const wheel[4])
{
    SvHull *h = &hull[p];
    const SvDmg *d = &rec[p];
    h->spin = 0;
    for (int k = 0; k < 4; k++) {
        if (wheel[k]) memcpy(h->wheel[k], &wheel[k]->rest.m[12], 12);   /* 0x4bac40 */
        float f, g;
        memcpy(&f, exe_data(0x5112f0, 4), 4);
        double e;
        memcpy(&e, exe_data(0x5112e0, 8), 8);
        g = (float)crt_rand() * f * (float)e;
        h->turn[k] = fx_mul((int32_t)lrintf(g), 0x1680000);
    }
    int32_t l = 0, w = 0, t = 0, ix = 0, fz = 0, iz = 0, fy = 0, ry = 0;
    switch (cls) {
    case 0: l = 0x426e9, w = 0x16b85, t = 0x1c51e, ix = 0x1cccc, iz = 0x8000, fz = 0x3d70, fy = 0xcf5c, ry = 0x10ccc; break;
    case 1: l = 0x4451e, w = 0x15999, t = 0x1d70a, ix = 0x1cccc, iz = 0xf851, fz = 0x570a, fy = 0xcf5c, ry = 0x1147a; break;
    case 2: l = 0x40f5c, w = 0x163d7, t = 0x1c51e, ix = 0x1cccc, iz = 0x451e, fz = 0x4ccc, fy = 0xcf5c, ry = 0x1147a; break;
    case 3: l = 0x44560, w = 0x15eb8, t = 0x1cfdf, ix = 0x1cccc, iz = 0x14ccc, fz = 0x4ccc, fy = 0xb0a3, ry = 0xfa9f; break;
    case 4: l = 0x40312, w = 0x14ccc, t = 0x1c51e, iz = 0xcccc, ix = 0x1c000, fz = 0x4ccc, fy = 0xe3d7, ry = 0x12dd2; break;
    case 5: l = 0x41c28, w = 0x154bc, t = 0x1d47a, iz = 0x10000, ix = 0x1c000, fz = 0x4ccc, fy = 0xe3d7, ry = 0x12dd2; break;
    case 6: l = 0x3e3d7, w = 0x15eb8, ix = 0x1ae14, iz = 0x9c28, t = 0x1c28f, fz = 0x4ccc, fy = 0xcf5c, ry = 0xfae1; break;
    case 7: l = 0x475c2, w = 0x1570a, ix = 0x1e147, iz = 0x1028f, t = 0x1c28f, fz = 0x4ccc, fy = 0xcf5c, ry = 0xfae1; break;
    case 8: l = 0x30083, w = 0x14041, t = 0x18000, ix = 0x13333, iz = 0x4ccc, fz = 0x2666, fy = 0xb5c2, ry = 0xfa9f; break;
    case 9: l = 0x3b958, w = 0x15db2, t = 0x1e041, ix = 0x1a666, iz = 0x9999, fz = 0x4ccc, fy = 0xe3d7, ry = 0xe106; break;
    case 10: l = 0x3b333, w = 0x106a7, t = 0x1cf5c, ix = 0x1a666, iz = 0x13333, fz = 0x4ccc, fy = 0xca3d, ry = 0xe106; break;
    case 11: l = 0x3d333, w = 0x15999, t = 0x1c312, ix = 0x1a666, iz = 0x9999, fz = 0x4ccc, fy = 0xe3d7, ry = 0xe106; break;
    case 12: l = 0x3ec49, w = 0x146a7, t = 0x1c28f, ix = 0x1a666, iz = 0x13333, fz = 0x4ccc, fy = 0xca3d, ry = 0xe106; break;
    case 13: l = 0x41687, w = 0x16147, t = 0x1bb22, ix = 0x1ae14, iz = 0xfd70, fz = 0x428f, fy = 0xd70a, ry = 0xf581; break;
    default: break;   /* the original's are left over from the stack */
    }
    int32_t X = fx_mul(l, 0x8000), Y = fx_mul(w, 0x8000), Z = fx_mul(t, 0x8000);
    static const int8_t box[8][3] = {{1, -1, 1}, {1, -1, -1}, {-1, -1, 1}, {-1, -1, -1}, {1, 1, 1}, {1, 1, -1}, {-1, 1, 1}, {-1, 1, -1}};
    for (int i = 0; i < 8; i++) h->pt[i][0] = box[i][0] * X, h->pt[i][1] = box[i][1] * Y, h->pt[i][2] = box[i][2] * Z;
    for (int i = 0; i < 4; i++) {
        int32_t x = d->ext[i < 2 ? 0 : 1], z = d->ext[(i & 1) ? 3 : 2];
        h->pt[8 + i][0] = x, h->pt[8 + i][1] = -Y, h->pt[8 + i][2] = z;
        h->pt[12 + i][0] = x, h->pt[12 + i][1] = (i < 2 ? fy : ry) - Y, h->pt[12 + i][2] = z;
    }
    int32_t wp[4][3] = {{X - ix, Y, fz - Z}, {X - ix, Y, Z - fz}, {iz - X, Y, Z - fz}, {iz - X, Y, fz - Z}};
    memcpy(h->pt[16], wp, sizeof wp);
}

void Sv_DmgInit(int p, Obj *second, Obj *const wheel[4], int cls)
{
    if (p < 0 || p > 3) return;
    build(p, second);
    make_hull(p, cls, wheel);
}

/* 0x508f60: the slot of an object type, -1 none */
static int slot_of_type(const SvDmg *d, uint32_t type)
{
    for (int s = 0; s < d->n; s++)
        if (d->obj[s] && (d->obj[s]->type & 0xff) == type) return s;
    return -1;
}

static void frames(const SvDmg *d, int s, uint32_t mask, uint32_t f0, uint32_t f1, bool both)
{
    if (s < 0 || !d->obj[s]) return;
    Mesh_SetFrame(d->obj[s]->model, d->mesh[s], mask, f0);    /* 0x49c440 */
    if (both) Mesh_SetFrame1(d->obj[s]->model, d->mesh[s], mask, f1);   /* 0x49c4b0 */
}

/* 0x508fa0: glass i to state 0 (whole), 1 (cracked; the lights 4 and 5 have no cracked look) or 2,
   as the race's 0x4694a0 for every model, the lights' stage 1 frames left alone */
static void glass(int p, int state, int i)
{
    const SvDmg *d = &rec[p];
    uint32_t a, b;
    if (state == 0) a = 0, b = 1;
    else if (state == 1 || i == 4 || i == 5) a = 3, b = 4;
    else a = 5, b = 7;
    switch (i) {
    case 0: frames(d, slot_of_type(d, 7), 0x100, a, b, true); break;
    case 1:
        frames(d, slot_of_type(d, 0xc), 0x20, a, b, true);
        frames(d, slot_of_type(d, 7), 0x20, a, b, true);
        break;
    case 2: frames(d, slot_of_type(d, 7), 0x40, a, b, true); break;
    case 3: frames(d, slot_of_type(d, 7), 0x80, a, b, true); break;
    case 4: frames(d, slot_of_type(d, 0xe), 4, a, 0, false); break;
    case 5: frames(d, slot_of_type(d, 0xe), 8, a, 0, false); break;
    default: break;
    }
}

/* 0x5074d0: every slot back to its rest positions and normals, shown, the glass whole */
void Sv_DmgRest(int p)
{
    SvDmg *d = &rec[p];
    if (!d->ready) return;
    for (int s = 0; s < d->n; s++) {
        uint8_t *v = verts(d, s);
        for (int k = 0; v && d->rest[s] && k < d->nv[s]; k++) {
            const uint8_t *r = d->rest[s] + 0x20 * k;
            for (uint32_t a = 0; a < 6; a++) vset(v + (size_t)k * C3D_VERTEX, 4 * a, (int32_t)rd32(r + 4 * a));
        }
        relight(d, s);
        d->shown[s] = 1;
    }
    for (int i = 0; i < 8; i++) glass(p, 0, i);
}

/* 0x507710: a log entry back into the hit (the race's 0x4688b0) */
static void decode_hit(uint32_t e)
{
    for (uint32_t a = 0; a < 3; a++) {
        hit[a] = fx_mul((int8_t)gm8(e + 9 + a) * 0x10000, 0x1428);
        nrm[a] = fx_mul((int8_t)gm8(e + 3 + a) * 0x10000, 0x204);
        ax[a] = fx_mul((int8_t)gm8(e + 6 + a) * 0x10000, 0x204);
    }
    strength = by255(gm8(e));
    kind_ = gm8(e + 1);
    if (kind_ == 1) size_r = fx_mul((int32_t)((uint32_t)gm8(e + 2) << 16), 0xa0a);
}

/* 0x508740: the vertex's move from rest in 64ths, to the side and within the limit of each axis */
static void clamp_vertex(int32_t pt[3], SvDmg *d, int s, int k)
{
    const uint8_t *r = d->rest[s] + 0x20 * k;
    for (uint32_t a = 0; a < 3; a++) {
        int32_t base = (int32_t)rd32(r + 4 * a), lim = (int8_t)r[0x1b + a];
        int32_t m = (pt[a] - base) >> 6;
        if ((0 < m && lim < 1) || (m < 0 && -1 < lim)) m = 0;
        if ((lim < m && 0 < m) || (m < lim && m < 0)) m = lim;
        pt[a] = m * 0x40 + base;
    }
    uint8_t *v = verts(d, s) + (size_t)k * C3D_VERTEX;
    for (uint32_t a = 0; a < 3; a++) vset(v, 4 * a, pt[a]);
}

static int32_t jitter(int32_t a, int32_t b)
{
    int32_t m = a + b;
    if (m < 0) m = -m;
    uint32_t u = (uint32_t)m & 0x80000380u;
    if ((int32_t)u < 0) u = ((u - 1) | 0xfffffc00u) + 1;
    int32_t j = (int32_t)(u * 0x40);
    return j < 0x8000 ? j - 0x10000 : j;
}

static void move_vertex(SvDmg *d, int s, int k, int32_t pt[3], const int32_t was[3])
{
    clamp_vertex(pt, d, s, k);
    uint8_t *v = verts(d, s) + (size_t)k * C3D_VERTEX;
    for (uint32_t a = 0; a < 3; a++) vset(v, 0xc + 4 * a, vget(v, 0xc + 4 * a) + fx_mul(pt[a] - was[a], 0x30000));
}

static int32_t dot(const int32_t a[3], const int32_t b[3]) { return fx_mul(a[2], b[2]) + fx_mul(a[0], b[0]) + fx_mul(a[1], b[1]); }

/* 0x507fe0: a flat hit, as the race's 0x467700 on the stand's hull points (+0x64, twelve) */
static void dent_flat(SvDmg *d, const SvHull *h)
{
    int32_t mh[3] = {fx_mul(hit[0], -0x10000), fx_mul(hit[1], -0x10000), fx_mul(hit[2], -0x10000)};
    bool flip = fx_mul(mh[2], nrm[2]) + fx_mul(mh[0], nrm[0]) + fx_mul(mh[1], nrm[1]) < 0;
    int32_t lo = 0, front = 0;
    for (int i = 8; i < 20; i++) {
        int32_t q[3] = {h->pt[i][0] - hit[0], h->pt[i][1] - hit[1], h->pt[i][2] - hit[2]};
        int32_t e = dot(q, nrm);
        if (flip) e = -e;
        if (e < lo) lo = e;
        if (0 < e && (front == 0 || e < front)) front = e;
    }
    if (flip) lo = -lo, front = -front;
    int32_t by = lo != 0 ? lo : front;
    for (int a = 0; a < 3; a++) hit[a] += fx_mul(nrm[a], by);
    bool side = 0 <= dot(hit, nrm);
    int32_t r1sq = fx_mul(r1, r1), i1 = inv(r1), outer = fx_mul(r2 + r1, r2 + r1), i2 = inv(r2), j3 = fx_mul(r3, 0x3333);
    for (int s = 0; s < d->n; s++) {
        bool dirty = false;
        uint8_t *v = verts(d, s);
        for (int k = 0; v && d->rest[s] && k < d->nv[s]; k++) {
            const uint8_t *x = v + (size_t)k * C3D_VERTEX;
            int32_t pt[3] = {vget(x, 0), vget(x, 4), vget(x, 8)};
            int32_t df[3] = {hit[0] - pt[0], hit[1] - pt[1], hit[2] - pt[2]};
            int32_t e = fx_mul(nrm[2], df[2]) + fx_mul(nrm[0], df[0]) + fx_mul(nrm[1], df[1]);
            int32_t q = fx_mul(e, e);
            if (outer < q) continue;
            int32_t np[3];
            if (r1sq < q) {
                int32_t t = fx_mul(fx_mul(fx_mul((int32_t)Fx_Sqrt((uint32_t)q) - r1, i2), j3), jitter(df[2], df[0]));
                const uint8_t *r = d->rest[s] + 0x20 * k;
                for (int a = 0; a < 3; a++) np[a] = pt[a] + fx_mul((int8_t)r[0x18 + a] << 9, t);
            } else {
                int32_t t = r1 - fx_mul(i1, q);
                for (int a = 0; a < 3; a++) np[a] = side ? pt[a] - fx_mul(nrm[a], t) : pt[a] + fx_mul(nrm[a], t);
            }
            move_vertex(d, s, k, np, pt);
            dirty = true;
        }
        if (dirty) relight(d, s);
    }
}

/* 0x508890: a round hit, as the race's 0x467e90 */
static void dent_round(SvDmg *d)
{
    if (0 <= fx_mul(nrm[2], hit[2]) + fx_mul(nrm[0], hit[0]) + fx_mul(nrm[1], hit[1]))
        for (int a = 0; a < 3; a++) nrm[a] = fx_mul(nrm[a], -0x10000);
    for (int a = 0; a < 3; a++) hit[a] += fx_mul(nrm[a], r1);
    int32_t outer = fx_mul(r2 + size_r, r2 + size_r), rsq = fx_mul(size_r, size_r), i2 = inv(r2), iR = inv(size_r), j3 = fx_mul(r3, 0x3333);
    for (int s = 0; s < d->n; s++) {
        bool dirty = false;
        uint8_t *v = verts(d, s);
        for (int k = 0; v && d->rest[s] && k < d->nv[s]; k++) {
            const uint8_t *x = v + (size_t)k * C3D_VERTEX;
            int32_t pt[3] = {vget(x, 0), vget(x, 4), vget(x, 8)};
            int32_t df[3] = {hit[0] - pt[0], hit[1] - pt[1], hit[2] - pt[2]};
            int32_t along = fx_mul(ax[2], df[2]) + fx_mul(ax[0], df[0]) + fx_mul(ax[1], df[1]);
            int32_t pp[3];
            for (int a = 0; a < 3; a++) pp[a] = df[a] - fx_mul(ax[a], along);
            int32_t q = fx_mul(pp[2], pp[2]) + fx_mul(pp[0], pp[0]) + fx_mul(pp[1], pp[1]);
            if (outer < q) continue;
            int32_t off[3];
            if (rsq < q) {
                int32_t t = fx_mul(fx_mul(fx_mul((int32_t)Fx_Sqrt((uint32_t)q) - size_r, i2), j3), jitter(pp[2], pp[0]));
                const uint8_t *r = d->rest[s] + 0x20 * k;
                for (int a = 0; a < 3; a++) off[a] = fx_mul((int8_t)r[0x18 + a] << 9, t);
            } else {
                int32_t t = size_r - fx_mul(iR, q);
                for (int a = 0; a < 3; a++) off[a] = fx_mul(nrm[a], t);
            }
            int32_t np[3] = {pt[0] + off[0], pt[1] + off[1], pt[2] + off[2]};
            move_vertex(d, s, k, np, pt);
            dirty = true;
        }
        if (dirty) relight(d, s);
    }
}

/* 0x5078e0: the hit's radii from its strength: a flat hit 0.35, 0.6 and 0.7 of it; a round one half
   of it up to 0.25 for all three (an object's corner of radius 0.25) */
static void dent(int p)
{
    SvDmg *d = &rec[p];
    if (!d->n || !d->ready) return;
    if (kind_ == 0) {
        r1 = fx_mul(strength, 0x5999), r2 = fx_mul(strength, 0x9999), r3 = fx_mul(strength, 0xb333);
        dent_flat(d, &hull[p]);
        return;
    }
    if (kind_ != 1) size_r = 0x4000;
    r1 = fx_mul(strength, 0x8000);
    if (0x4000 < r1) r1 = 0x4000;
    r2 = r3 = r1;
    dent_round(d);
}

/* 0x508ee0: each glass panel (the carried panels' +0x122) cracked or broken past the stand's limits
   (0x527344, 0x527324) */
static void glass_all(int p)
{
    uint32_t b = carried(p);
    for (uint32_t i = 0; i < 8; i++) {
        int32_t v = by255(gm8(b + 0x122 + i));
        if (gms32(0x527324 + 4 * i) < v) glass(p, 2, (int)i);
        else if (gms32(0x527344 + 4 * i) < v) glass(p, 1, (int)i);
    }
}

/* 0x507650: the rest shape, the carried log's hits from its first along the list, the glass, the
   fallen-off parts' slots (0x527364) hidden, the suspension and axles */
void Sv_DmgApply(int p)
{
    if (p < 0 || p > 3) return;
    Sv_DmgRest(p);
    uint32_t b = carried(p);
    if (gm8(b + 0x104)) {
        uint32_t e = b + 13u * gm8(b + 0x105);
        for (;;) {
            decode_hit(e);
            dent(p);
            int8_t next = (int8_t)gm8(e + 0xc);
            if (next == -1) break;
            e = b + 13u * (uint32_t)(int)next;
        }
    }
    glass_all(p);
    for (uint32_t k = 0; k < 4; k++)
        if (gm32(b + 0x12c + 4 * k)) {
            uint8_t s = gm8(0x527364 + k);
            if (s < SLOTS) rec[p].shown[s] = 0;
        }
    Sv_SuspDamaged(p);
    Sv_AxlesDamaged(p);
}

/* 0x509150: the slots drawn or not by their shown flags */
void Sv_DmgShow(int p)
{
    SvDmg *d = &rec[p];
    if (!d->ready) return;
    for (int s = 0; s < d->n; s++)
        if (d->obj[s]) d->obj[s]->mask = d->shown[s] ? 0xff : 0;
}

/* 0x509be0: the corners' sag from the carried suspension panels (+0x10e): the fronts down by 0.2 of
   theirs, the rears 0.1 up and 0.1 down */
void Sv_SuspDamaged(int p)
{
    SvDmg *d = &rec[p];
    uint32_t b = carried(p);
    for (uint32_t k = 0; k < 4; k++) d->susp[k] = by255(gm8(b + 0x10e + k));
    d->susp[0] = fx_mul(d->susp[0], -0x3333);
    d->susp[1] = fx_mul(d->susp[1], -0x3333);
    d->susp[2] = fx_mul(d->susp[2], 0x1999);
    d->susp[3] = fx_mul(d->susp[3], -0x199a);
}

void Sv_SuspRepaired(int p) { memset(rec[p].susp, 0, sizeof rec[p].susp); }   /* 0x509d00 */

/* 0x509d30: the axles' panels (+0x108) */
void Sv_AxlesDamaged(int p)
{
    uint32_t b = carried(p);
    for (uint32_t k = 0; k < 4; k++) rec[p].axle[k] = by255(gm8(b + 0x108 + k));
}

void Sv_AxlesRepaired(int p) { memset(rec[p].axle, 0, sizeof rec[p].axle); }   /* 0x509d90 */

static void cross(int32_t o[3], const int32_t a[3], const int32_t b[3])
{
    o[0] = fx_mul(a[1], b[2]) - fx_mul(a[2], b[1]);
    o[1] = fx_mul(a[2], b[0]) - fx_mul(a[0], b[2]);
    o[2] = fx_mul(a[0], b[1]) - fx_mul(a[1], b[0]);
}

static void up_unit(int32_t v[3])
{
    Vec_Unit(v);
    if (v[1] < 0)
        for (int a = 0; a < 3; a++) v[a] = fx_mul(v[a], -0x10000);
}

/* 0x5091c0: the body on its corners. The ride height r (the set-up's, 0.1 a step; while the set-up
   panel shows the ride height item, the value being set) lowers every corner by 0.1 (1 - r) on its
   sag, not below -0.1; the body's up is the mean of the normals of the front and rear triangles
   (each corner pair and the other axle's middle), x is the car's x off it, z their cross; at the mean
   height */
void Sv_DmgPose(int p, Obj *second, int ride)
{
    if (!second || p < 0 || p > 3) return;
    const SvHull *h = &hull[p];
    int32_t rv = fx_mul(ride << 16, 0x1999);
    int32_t drop = fx_mul(0x10000 - rv, 0x1999), c[4];
    for (int k = 0; k < 4; k++) {
        c[k] = rec[p].susp[k] + drop;
        if (c[k] < -0x1999) c[k] = -0x1999;
    }
    int32_t rear = fx_mul(c[3] + c[2], 0x8000), front = fx_mul(c[1] + c[0], 0x8000);
    const int32_t *P0 = h->pt[0], *P1 = h->pt[1], *P2 = h->pt[2], *P3 = h->pt[3];
    int32_t a[3] = {P1[0] - P0[0], c[1] - c[0], P1[2] - P0[2]}, b[3] = {P2[0] - P0[0], rear - c[0], -P0[2]}, n1[3];
    cross(n1, a, b);
    up_unit(n1);
    int32_t e[3] = {P3[0] - P2[0], c[3] - c[2], P3[2] - P2[2]}, f[3] = {P0[0] - P2[0], front - c[2], -P2[2]}, n2[3];
    cross(n2, e, f);
    up_unit(n2);
    int32_t up[3] = {n1[0] + n2[0], n1[1] + n2[1], n1[2] + n2[2]};
    Vec_Unit(up);
    int32_t k = fx_mul(up[0], 0x10000);
    int32_t x[3] = {0x10000 - fx_mul(up[0], k), -fx_mul(up[1], k), -fx_mul(up[2], k)}, z[3];
    Vec_Unit(x);
    cross(z, x, up);
    Vec_Unit(z);
    int32_t pos[3] = {0, fx_mul(front + rear, 0x8000), 0};
    second->inherit = 0;
    int32_t *m = second->local.m;
    for (int i = 0; i < 3; i++) m[i] = x[i], m[4 + i] = up[i], m[8 + i] = z[i], m[12 + i] = pos[i];
    second->dirty = 1;
}
