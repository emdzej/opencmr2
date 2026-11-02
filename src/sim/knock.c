/* Knocked-down scenery (see knock.h). A record's ints, as the original's: [0] the entry, [1] the object,
   [2..0x11] the matrix, [0x12..] the last step's, [0x22..] the drawn one, [0x32] a pointer to [2],
   [0x33] the state, [0x34] the shape, then the fields below in order. */
#include "sim/knock.h"
#include "sim/scenery.h"
#include "sim/car.h"
#include "sim/sim.h"
#include "render/mesh.h"
#include "world/hpc.h"
#include "world/obj.h"
#include "world/relight.h"
#include "world/section.h"
#include "world/stage.h"
#include "fixmath.h"
#include "game/gstate.h"
#include "game/mem.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

enum { POOL = 40, STILL = 0, TIPPING = 1, FLUNG = 2 };

typedef struct {
    int entry;              /* [0]: the movable entry's index (the original: its pointer) */
    Obj *obj;               /* [1] */
    int32_t m[16];          /* [2]: object to world */
    int32_t prev[16];       /* [0x12] */
    int32_t drawn[16];      /* [0x22] */
    int32_t state;          /* [0x33]: STILL, TIPPING, FLUNG */
    int32_t shape;          /* [0x34]: 1 tall and thin (it tips), 0 otherwise */
    int32_t n[3];           /* [0x35]: the ground's normal */
    int32_t spin[3];        /* [0x38]: about the object's axes */
    int32_t vel[3];         /* [0x3b] */
    int32_t pivot[3];       /* [0x3e]: the box centre in the object */
    int32_t ext[3];         /* [0x41]: the box half extents */
    int32_t ground;         /* [0x44]: the ground height */
    int32_t light;          /* [0x45]: the sun-light handle (-1.0 none yet) */
    int32_t cell;           /* [0x46] */
    int16_t tri;            /* [0x47]: the ground triangle, -1 none */
    int32_t grounded;       /* [0x48] */
    int32_t relight;        /* [0x49]: the drawn matrix changed */
} Rec;

static Rec pool[POOL];       /* 0x5894e0 */
static uint8_t count;        /* DAT_0058c924 */
static int32_t dist[POOL][8];   /* 0x58c424: each record's flat distance to each car */
static uint8_t farthest[POOL];  /* 0x5894b8: the cars each record is farthest from */
static int32_t corner[8][3];    /* 0x589458: the box's corners, made each step */

/* the mesh copies (0x58c92c, counted by the byte 0x58c320), each movable entry's copy (0x58c928) and
   the copies' shapes (0x58c930); the counter is a byte as the original's */
static C3d clones[256];
static uint8_t *clone_mesh[256];
static uint8_t nclones;
static uint8_t *clone_of;
static int32_t shapes[256];

static int32_t rd32(const uint8_t *p) { return (int32_t)((uint32_t)p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24); }
static int32_t iabs(int32_t v) { return v < 0 ? -v : v; }
static int32_t dot(const int32_t a[3], const int32_t b[3]) { return fx_mul(a[0], b[0]) + fx_mul(a[1], b[1]) + fx_mul(a[2], b[2]); }
static uint32_t len(const int32_t v[3]) { return Fx_Sqrt((uint32_t)fx_mul(v[0], v[0]) + (uint32_t)fx_mul(v[1], v[1]) + (uint32_t)fx_mul(v[2], v[2])); }
static void cross(int32_t o[3], const int32_t a[3], const int32_t b[3])
{
    int32_t x = fx_mul(a[1], b[2]) - fx_mul(a[2], b[1]), y = fx_mul(a[2], b[0]) - fx_mul(a[0], b[2]),
            z = fx_mul(a[0], b[1]) - fx_mul(a[1], b[0]);
    o[0] = x, o[1] = y, o[2] = z;
}
/* Vec_Mul3 0x4b9c20: v times the 3 x 3 (row vector on the left) */
static void vmul(int32_t o[3], const int32_t v[3], const int32_t m[16])
{
    for (int k = 0; k < 3; k++) o[k] = fx_mul(m[8 + k], v[2]) + fx_mul(m[4 + k], v[1]) + fx_mul(m[k], v[0]);
}
/* Vec_RotateT 0x4b9da0: v through the transposed 3 x 3 */
static void vrot_t(int32_t o[3], const int32_t v[3], const int32_t m[16])
{
    for (int k = 0; k < 3; k++) o[k] = fx_mul(m[4 * k + 2], v[2]) + fx_mul(m[4 * k + 1], v[1]) + fx_mul(m[4 * k], v[0]);
}
/* 0x4bac60/80/a0 and 0x4bace0/0x4bad00/0x4bad20: a matrix's rows out and in (here into a 3 x 3,
   rows 3 apart, as 0x429f20 turns them); 0x4bacc0 the position */
static void rows_get(int32_t r[9], const int32_t m[16]) { for (int i = 0; i < 3; i++) memcpy(r + 3 * i, m + 4 * i, 12); }
static void rows_set(int32_t m[16], const int32_t r[9]) { for (int i = 0; i < 3; i++) memcpy(m + 4 * i, r + 3 * i, 12); }
static void dirty(Obj *o) { for (; o; o = o->parent) o->dirty = 1; }

static void clones_free(void)
{
    for (int i = 0; i < 256; i++) free(clones[i].data), clones[i].data = NULL, clone_mesh[i] = NULL;
    free(clone_of);
    clone_of = NULL;
    nclones = 0;
}

void Knock_Free(void)
{
    clones_free();
    memset(pool, 0, sizeof pool);
    count = 0;
}

/* The largest |coordinate| along each axis of the copy's vertices (rounded to 16.16), and the shape:
   0 when z is under 0.3 of x, else 1 when x is under 0.3 of y, else 0 (a zero divisor, which would
   stop the original, counts as not under) */
static int32_t shape_of(const C3d *m, const uint8_t *mesh)
{
    uint32_t e[3] = {0, 0, 0};
    const uint8_t *v = C3d_Ref(m, mesh + 0x0c, m->verts);
    int32_t nv = rd32(mesh + 0x10);
    for (int32_t i = 0; v && i < nv; i++) {
        float f[3];
        memcpy(f, v + (size_t)i * C3D_VERTEX, sizeof f);
        for (int k = 0; k < 3; k++) {
            int32_t a = (int32_t)llrint((double)f[k] * 65536.0);
            if (a < 0) a = (int32_t)llrint((double)f[k] * -65536.0);
            if (e[k] < (uint32_t)a) e[k] = (uint32_t)a;
        }
    }
    if (!e[0] || ((uint64_t)e[2] << 16) / e[0] < 0x4ccc) return 0;
    if (e[1] && ((uint64_t)e[0] << 16) / e[1] < 0x4ccc) return 1;
    return 0;
}

void Knock_Init(void)
{
    clones_free();
    memset(pool, 0, sizeof pool);
    for (int k = 0; k < POOL; k++) {
        pool[k].obj = Obj_New(&g_world);   /* under World_Root */
        if (pool[k].obj) pool[k].obj->kind = 3;
        pool[k].light = (int32_t)0xffff0000;
    }
    int n = Scenery_Movables();
    if (n <= 0) return;
    clone_of = malloc((size_t)n);
    if (!clone_of) return;
    for (int i = 0; i < n; i++) {
        Scenery *sc = Scenery_Movable(i);
        clone_of[i] = nclones;
        /* past 255 the byte count wraps and the copies start again from the first */
        free(clones[nclones].data);
        clone_mesh[nclones] = sc && sc->mesh ? Mesh_Clone(&clones[nclones], sc->model, sc->mesh, &sc->m) : NULL;
        if (!clone_mesh[nclones]) clone_of[i] = 0;
        else nclones++;
    }
    for (int c = 0; c < nclones; c++) shapes[c] = shape_of(&clones[c], clone_mesh[c]);
}

void Knock_Hide(void)
{
    count = 0;
    for (int k = 0; k < POOL; k++)
        if (pool[k].obj) pool[k].obj->local.m[13] -= 0x27100000;
}

/* 0x470240: entry i back up for car n; with one player (the car not +0xc0c) the static object is
   raised again and drawn */
static void restore(int i, int n)
{
    if (Scenery_UpFor(i, n)) return;
    Scenery_SetUp(i, n, true);
    Car *c = Car_Get(n);
    if (c && !CI32(c, 0xc0c) && Gs_SelBits14() == 1) {
        Scenery *sc = Scenery_Movable(i);
        sc->pos[1] += 0x3e80000;
        sc->rec[0x14] = 0xff;
    }
}

void Knock_Restore(void)
{
    int n = Scenery_Movables();
    for (int car = 0; car < g_all.n; car++)
        for (int i = 0; i < n; i++) restore(i, car);
    Knock_Hide();
}

/* 0x46f810: the record started for car n */
static void start(Rec *r, int n)
{
    int16_t tri = -1;
    uint16_t surf = 0;
    Car *c = Car_Get(n);
    if (r->entry < 0 || r->entry >= Scenery_Movables() || !r->obj || !c) {
        if (r->obj) r->obj->kind = 3;
        return;
    }
    Scenery *sc = Scenery_Movable(r->entry);
    uint8_t k = clone_of[r->entry];
    Obj *o = r->obj;
    r->shape = shapes[k];
    o->kind = 0;
    o->mesh = clone_mesh[k];
    o->model = &clones[k];
    o->mask = (uint8_t)(1u << (n & 31));
    r->light = (int32_t)0xffff0000;
    int32_t pos[3];
    Scenery_MovedPos(r->entry, pos);
    /* the placement's rows as they are, at the object's centre */
    for (int i = 0; i < 3; i++) o->local.m[12 + i] = pos[i];
    for (int i = 0; i < 3; i++) memcpy(o->local.m + 4 * i, sc->m.m + 4 * i, 12);
    memcpy(r->m, o->local.m, sizeof r->m);
    memcpy(r->drawn, o->local.m, sizeof r->drawn);
    memcpy(r->prev, o->local.m, sizeof r->prev);
    uint32_t speed = (uint32_t)CI32(c, 0x778);
    bool tip = r->shape == 1 || (r->shape == 0 && speed <= 0xc000);
    r->ground = 0;
    r->tri = -1;
    int32_t v[3] = {CI32(c, 0x408), CI32(c, 0x40c), CI32(c, 0x410)}, o3[3];
    const uint8_t *vol = C3d_Ref(sc->model, sc->mesh + 0x10c, sc->model->r5c);
    int32_t K = gms32(0x519c8c);
    if (tip) {
        Hpc_Ground(pos, r->n, &tri, &surf, 0);
        if (speed > 0x8000) {
            int32_t q = (int32_t)(0x100000000ULL / speed);
            for (int i = 0; i < 3; i++) v[i] = fx_mul(fx_mul(v[i], q), 0x8000);
        }
        vrot_t(o3, v, r->m);
        /* about the object's x from its forward speed, about z from its side speed (none for shape 0) */
        int32_t s0 = o3[2], s2 = r->shape == 0 ? 0 : -o3[0];
        if (vol) r->ext[0] = fx_mul(rd32(vol + 0x44), 0x8000), r->ext[1] = fx_mul(rd32(vol + 0x4c), 0x8000), r->ext[2] = fx_mul(rd32(vol + 0x48), 0x8000);
        r->pivot[0] = 0, r->pivot[1] = r->ext[1], r->pivot[2] = 0;
        r->spin[0] = fx_mul(s0, K), r->spin[1] = 0, r->spin[2] = fx_mul(s2, K);
        r->state = TIPPING;
    } else {
        vrot_t(o3, v, r->m);
        int32_t s0 = -fx_mul(o3[2], 0x6666), s2 = fx_mul(o3[0], 0x6666);
        r->spin[0] = fx_mul(s0, K), r->spin[1] = 0, r->spin[2] = fx_mul(s2, K);
        for (int i = 0; i < 3; i++) r->vel[i] = fx_mul(v[i], 0xcccc);
        r->vel[1] = fx_mul((int32_t)len(v), 0x4ccc);   /* up at 0.3 of the car's speed */
        if (vol) r->ext[0] = fx_mul(rd32(vol + 0x44), 0x8000), r->ext[1] = fx_mul(rd32(vol + 0x4c), 0x8000), r->ext[2] = fx_mul(rd32(vol + 0x48), 0x8000);
        r->pivot[1] = r->ext[1], r->pivot[0] = 0, r->pivot[2] = 0;
        r->grounded = 0;
        r->state = FLUNG;
        r->relight = 1;
    }
    o->world = o->local;
    if (o->section != -1) Section_Unlink(o);
    if (o->section == -1) Section_Link(o);
}

void Scenery_Knock(int i, int cell, int n)
{
    Car *c = Car_Get(n);
    if (!c) return;
    if (Scenery_UpFor(i, n)) {
        Scenery_SetUp(i, n, false);
        if (!CI32(c, 0xc0c) && Gs_SelBits14() == 1) {
            Scenery *sc = Scenery_Movable(i);
            sc->pos[1] -= 0x3e80000;
            sc->rec[0x14] = 0;
        }
    }
    if (CI32(c, 0xc0c)) return;
    if (count < POOL) {
        pool[count].entry = i;
        pool[count].cell = cell;
        start(&pool[count], n);
        count++;
        return;
    }
    /* full: each car's farthest record counted (flat distances, 0 within 0x290), the one farthest
       from the most cars (the first of equals) put back up and taken */
    for (int k = 0; k < count; k++) {
        int32_t p[3];
        Scenery_MovedPos(pool[k].entry, p);
        for (int j = 0; j < g_all.n; j++) {
            const Car *cj = Car_Get(j);
            int32_t dx = CI32(cj, 0x2d0) - p[0], dz = CI32(cj, 0x2d8) - p[2];
            uint32_t ax = (uint32_t)iabs(dx), az = (uint32_t)iabs(dz), m = az < ax ? ax : az;
            if (m < 0x290) {
                dist[k][j] = 0;
                continue;
            }
            int32_t q = (int32_t)(0x100000000LL / (int32_t)m);
            int32_t u[3] = {fx_mul(dx, q), 0, fx_mul(dz, q)};
            dist[k][j] = fx_mul((int32_t)len(u), (int32_t)m);
        }
        farthest[k] = 0;
    }
    for (int j = 0; j < g_all.n; j++) {
        int best = 0;
        int32_t bv = dist[0][j];
        for (int k = 1; k < count; k++)
            if (bv < dist[k][j]) best = k, bv = dist[k][j];
        farthest[best]++;
    }
    int pick = 0;
    for (int k = 1; k < count; k++)
        if (farthest[pick] < farthest[k]) pick = k;
    for (int car = 0; car < 8; car++) restore(pool[pick].entry, car);
    pool[pick].entry = i;
    pool[pick].cell = cell;
    start(&pool[pick], n);
}

/* The box's eight corners about the pivot (the car shadow's order: x, -y, z first) */
static void corners(const int32_t e[3])
{
    static const int8_t sgn[8][3] = {{1, -1, 1}, {1, -1, -1}, {-1, -1, 1}, {-1, -1, -1}, {1, 1, 1}, {1, 1, -1}, {-1, 1, 1}, {-1, 1, -1}};
    for (int i = 0; i < 8; i++)
        for (int k = 0; k < 3; k++) corner[i][k] = sgn[i][k] * e[k];
}

/* 0x4702f0: how far up p (the world pivot) must go for the box's lowest corner (the one furthest
   down the ground's normal, in the record's matrix) to sit on the ground; off the collision mesh the
   ground is flat, ten units under p */
static int32_t lift(Rec *r, const int32_t p[3])
{
    uint16_t surf = 0;
    r->ground = Hpc_Ground(p, r->n, &r->tri, &surf, r->ground);
    if (r->tri == -1) {
        r->n[0] = 0, r->n[1] = 0x10000, r->n[2] = 0;
        r->ground = p[1] - 0xa0000;
    }
    Vec_Unit(r->n);
    int32_t down[3] = {fx_mul(r->n[0], -0x10000), fx_mul(r->n[1], -0x10000), fx_mul(r->n[2], -0x10000)}, d[3], c[3];
    vrot_t(d, down, r->m);
    int i = (d[1] >= 0 ? 4 : 0) + (d[0] < 0 ? 2 : 0) + (d[2] < 0 ? 1 : 0);
    vmul(c, corner[i], r->m);
    int32_t s = fx_mul(r->n[0], p[0] - (c[0] + p[0])) + fx_mul(r->n[1], r->ground - (c[1] + p[1])) +
                fx_mul(r->n[2], p[2] - (c[2] + p[2]));
    return r->n[1] ? (int32_t)(((int64_t)s << 16) / r->n[1]) : 0;
}

/* the spin times the step as the three u16 angles of Rows_Turn3 (radians to 4,096ths, 0x511380) */
static void angles(uint16_t a[3], const int32_t spin[3], int32_t dt)
{
    for (int i = 0; i < 3; i++) a[i] = (uint16_t)X87_MulRound((double)fx_mul(spin[i], dt), 0.009947183943243459);
}

static void unit_into(int32_t d[3], int32_t a, int32_t b, int32_t c)
{
    d[0] = a, d[1] = b, d[2] = c;
    Vec_Unit(d);
}

/* State 1: the spin up by 1.1 a step about the object's own axes; once the up row has gone level
   with the ground (its dot with the normal at or below 0) it is laid in the ground's plane, the
   other rows squared to it, and the record is still. The lowest corner is kept on the ground
   (measured with the step's rows before the turn). */
static void tipping(Rec *r, int32_t dt)
{
    int32_t R[9];
    for (int i = 0; i < 3; i++) r->spin[i] = fx_mul(r->spin[i], 0x11999);
    rows_get(R, r->m);
    uint16_t a[3];
    angles(a, r->spin, dt);
    Rows_Turn3(R, 3, a);
    int32_t *r0 = R, *r1 = R + 3, *r2 = R + 6;
    int32_t d = dot(r1, r->n);
    if (d < 1) {
        unit_into(r1, r1[0] - fx_mul(r->n[0], d), r1[1] - fx_mul(r->n[1], d), r1[2] - fx_mul(r->n[2], d));
        int32_t d2 = dot(r0, r1);
        unit_into(r0, r0[0] - fx_mul(r1[0], d2), r0[1] - fx_mul(r1[1], d2), r0[2] - fx_mul(r1[2], d2));
        int32_t x[3];
        cross(x, r0, r1);
        unit_into(r2, x[0], x[1], x[2]);
        r->state = STILL;
    }
    corners(r->ext);
    int32_t p[3];
    vmul(p, r->pivot, r->m);
    for (int i = 0; i < 3; i++) p[i] += r->m[12 + i];
    r->m[13] += lift(r, p);
    rows_set(r->m, R);
}

/* State 2: gravity (0xfffff5c3 a step, halved with option 1) until on the ground, the move by the
   velocity less half the step's gravity; on the ground spin and velocity fall by 0.8 a step, and
   below 0.01 both stop and the z row eases onto the ground's normal (a tenth a step, then square, the
   record still). The world pivot is kept and the position made from it. */
static void flung(Rec *r, int32_t dt)
{
    int32_t R[9], p[3], g[3] = {0, r->grounded ? 0 : (int32_t)0xfffff5c3, 0};
    rows_get(R, r->m);
    vmul(p, r->pivot, r->m);
    for (int i = 0; i < 3; i++) p[i] += r->m[12 + i];
    corners(r->ext);
    uint16_t a[3];
    angles(a, r->spin, dt);
    Rows_Turn3(R, 3, a);
    if (Gs_Option(1))
        for (int i = 0; i < 3; i++) g[i] = fx_mul(g[i], 0x8000);
    for (int i = 0; i < 3; i++) g[i] = fx_mul(g[i], dt);
    for (int i = 0; i < 3; i++) r->vel[i] += g[i];
    int32_t mv[3];
    for (int i = 0; i < 3; i++) mv[i] = fx_mul(r->vel[i], dt);
    for (int i = 0; i < 3; i++) g[i] = fx_mul(g[i], dt / 2);
    for (int i = 0; i < 3; i++) p[i] += mv[i] - g[i];
    if (r->grounded) {
        for (int i = 0; i < 3; i++) r->spin[i] = fx_mul(r->spin[i], 0xcccc);
        int32_t ls = (int32_t)len(r->spin);
        for (int i = 0; i < 3; i++) r->vel[i] = fx_mul(r->vel[i], 0xcccc);
        int32_t lv = (int32_t)len(r->vel);
        if (ls < 0x28f && lv < 0x28f) {
            memset(r->spin, 0, sizeof r->spin);
            memset(r->vel, 0, sizeof r->vel);
            int32_t *r0 = R, *r1 = R + 3, *r2 = R + 6, u[3], e[3];
            if (dot(r->n, r2) < 0) {
                int32_t q = (int32_t)(0x100000000LL / (int32_t)0xffff0000);
                for (int i = 0; i < 3; i++) u[i] = fx_mul(r->n[i], q);
            } else
                memcpy(u, r->n, sizeof u);
            for (int i = 0; i < 3; i++) e[i] = u[i] - r2[i];
            int32_t l = (int32_t)len(e);
            if (l > 0x1999) {
                int32_t q = (int32_t)(0x100000000LL / l);
                for (int i = 0; i < 3; i++) e[i] = fx_mul(fx_mul(e[i], q), 0x1999);
                unit_into(r2, e[0] + r2[0], e[1] + r2[1], e[2] + r2[2]);
            } else {
                memcpy(r2, u, sizeof u);
                r->state = STILL;
            }
            int32_t d = dot(r2, r0);
            unit_into(r0, r0[0] - fx_mul(r2[0], d), r0[1] - fx_mul(r2[1], d), r0[2] - fx_mul(r2[2], d));
            int32_t x[3];
            cross(x, r2, r0);
            unit_into(r1, x[0], x[1], x[2]);
        }
        rows_set(r->m, R);
        p[1] += lift(r, p);
    } else {
        rows_set(r->m, R);
        if (r->vel[1] < 0) {
            int32_t h = lift(r, p);
            if (h > -0xccc) {
                r->grounded = 1;
                p[1] += h;
            }
        }
    }
    int32_t q[3];
    vmul(q, r->pivot, r->m);
    for (int i = 0; i < 3; i++) r->m[12 + i] = p[i] - q[i];
}

void Knock_Step(void)
{
    int32_t dt = gms32(0x519c8c);
    for (int k = 0; k < count; k++) {
        Rec *r = &pool[k];
        memcpy(r->prev, r->m, sizeof r->prev);
        if (r->state == TIPPING) tipping(r, dt);
        else if (r->state == FLUNG) flung(r, dt);
        else continue;
        dirty(r->obj);
        if (r->state == STILL) memcpy(r->prev, r->m, sizeof r->prev);
    }
}

/* Frame_Blend 0x4224e0, mode 1: forward and side rows and the position by k, side made unit, up from
   side and forward, forward from up and side */
static void blend(int32_t *o, const int32_t *a, const int32_t *b, int32_t k)
{
    int32_t f[3], s[3], u[3], p[3];
    for (int i = 0; i < 3; i++) {
        f[i] = a[i] + fx_mul(b[i] - a[i], k);
        s[i] = a[8 + i] + fx_mul(b[8 + i] - a[8 + i], k);
        p[i] = a[12 + i] + fx_mul(b[12 + i] - a[12 + i], k);
    }
    Vec_Unit(s);
    cross(u, s, f);
    Vec_Unit(u);
    cross(f, u, s);
    memset(o, 0, 16 * sizeof *o);
    o[15] = 0x10000;
    for (int i = 0; i < 3; i++) o[i] = f[i], o[4 + i] = u[i], o[8 + i] = s[i], o[12 + i] = p[i];
}

void Knock_Interp(int32_t frac)
{
    static const int at[12] = {0, 1, 2, 4, 5, 6, 8, 9, 10, 12, 13, 14};
    for (int k = 0; k < count; k++) {
        Rec *r = &pool[k];
        int32_t was[16];
        memcpy(was, r->drawn, sizeof was);
        blend(r->drawn, r->prev, r->m, frac);
        r->relight = 0;
        for (int i = 0; i < 12; i++)
            if (was[at[i]] != r->drawn[at[i]]) r->relight = 1;
    }
}

void Knock_Draw(void)
{
    for (int k = 0; k < count; k++) {
        Rec *r = &pool[k];
        if (!r->obj) continue;
        memcpy(r->obj->local.m, r->drawn, sizeof r->drawn);
        if (r->relight) {
            memcpy(r->obj->world.m, r->drawn, sizeof r->drawn);
            r->light = Light_SunObjects(r->obj, r->light);   /* 0x4b5320 */
            r->relight = 0;
        }
    }
}

int Knock_Count(void) { return count; }

int Knock_State(int k, int32_t pos[3])
{
    if (k < 0 || k >= count) return -1;
    for (int i = 0; i < 3; i++) pos[i] = pool[k].m[12 + i];
    return pool[k].state;
}
