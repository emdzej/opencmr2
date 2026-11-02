/* The car damage (see damage.h). */
#include "sim/damage.h"
#include "sim/aerials.h"
#include "world/particles.h"
#include "audio/crash.h"
#include "game/camera.h"
#include "game/gstate.h"
#include "game/mem.h"
#include "game/effects.h"
#include "game/timer.h"
#include "render/mesh.h"
#include "sim/sim.h"
#include "sim/walls.h"
#include "sim/parts.h"
#include "c3d.h"
#include "crt.h"
#include "fixmath.h"
#include "pending.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

enum { SLOTS = 15, REC = 0x4d0, LOG = 0x290, ENTRY = 0xd, ENTRIES = 20 };

/* A car's record (0x588b94 + 0x4d0 n), its pointers beside it: +0x00 the slots' meshes, +0x3c their
   objects, +0x78 their rest vertices (in 0x588b9c's arrays). */
typedef struct {
    uint8_t s[REC];
    uint8_t *mesh[SLOTS];
    Obj *obj[SLOTS];
    uint8_t *rest[SLOTS];
} Dmg;
static Dmg dmg[CAR_MAX];
/* 0x588b98 + 0x290 n: the hit logs (+0x000 the one carried from the last stages, +0x106 this stage's;
   20 entries of 13 bytes, then the count and the first entry; +0x20c and +0x24c the panels carried
   and this stage's: 34 bytes, +0x230 four part flags, +0x240 three values; +0x28c no more logging) */
static uint8_t logs[CAR_MAX][LOG];
/* 0x588b9c / 0x588ba0: per car the rest vertices of each slot (0x20 bytes a vertex: the position and
   normal in 16.16, the dent direction and the limit per axis, signed bytes at +0x18 and +0x1b) and the
   slots' object types; made at the first set-up of a race. The port gives the types a zero byte past
   the end, which the original reads there (0x46acb0 looks one past). */
static uint8_t *rest_tab[CAR_MAX][SLOTS];
static uint8_t rest_type[CAR_MAX][SLOTS + 1];
static bool rest_have[CAR_MAX];
static int rest_n[CAR_MAX];

/* the hit being worked on (0x588a40-0x588a7c): the point and normal in the car's frame, the up axis
   (the orientation's second column), the radius of a round hit, the strength, the kind, the car, the
   three radii */
static int32_t hit[3], nrm[3], ax[3], size_r, strength;
static uint8_t kind_;
static Car *car_;
static int32_t r1, r2, r3;
static int32_t hull[12][3];   /* 0x5889b0: the box's bottom corners, the same raised, four wheel points */

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static void wr32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8), p[2] = (uint8_t)(v >> 16), p[3] = (uint8_t)(v >> 24); }
static int32_t R(const Dmg *d, uint32_t o) { return (int32_t)rd32(d->s + o); }
static void W(Dmg *d, uint32_t o, int32_t v) { wr32(d->s + o, (uint32_t)v); }
static int count(const Dmg *d) { return R(d, 0x45c); }
static int nverts(const Dmg *d, int s) { return R(d, 0x420 + 4 * (uint32_t)s); }
static Dmg *of(const Car *c) { int n = CI8(c, 0xb1a); return &dmg[n >= 0 && n < CAR_MAX ? n : 0]; }
static int idx(const Car *c) { int n = CI8(c, 0xb1a); return n >= 0 && n < CAR_MAX ? n : 0; }
static bool dents(int n) { return gm32(0x588970 + 4 * (uint32_t)n) != 0; }
static int players(void) { return (int)Gs_SelBits14(); }   /* 0x4074f0 */
static int32_t inv(int32_t v) { return v ? (int32_t)(0x100000000LL / v) : 0; }   /* the original traps on 0 */
static int32_t clamp127(int32_t v) { return v < 0x80 ? (v < -0x7f ? -0x7f : v) : 0x7f; }

uint8_t *Damage_Get(int n) { return dmg[n >= 0 && n < CAR_MAX ? n : 0].s; }
uint8_t *Damage_Log(int n) { return logs[n >= 0 && n < CAR_MAX ? n : 0]; }
int32_t Damage_Flag(const Car *c, int n) { return R(of(c), 0x4b0 + 4 * (uint32_t)n); }
void Damage_SetDents(int on, int n) { gw32(0x588970 + 4 * (uint32_t)n, (uint32_t)on); }

/* A slot's vertices: the floats at +0 (position) and +0xc (normal), as 16.16 (fistp, to nearest) */
static uint8_t *verts(const Dmg *d, int s)
{
    const Obj *o = d->obj[s];
    if (!o || !o->model || !d->mesh[s]) return NULL;
    return (uint8_t *)C3d_Ref(o->model, d->mesh[s] + 0x0c, o->model->verts);
}
static int32_t vget(const uint8_t *v, uint32_t o) { float f; memcpy(&f, v + o, 4); return (int32_t)nearbyint((double)f * 65536.0); }
static void vset(uint8_t *v, uint32_t o, int32_t x) { float f = (float)x * (float)(1.0 / 65536.0); memcpy(v + o, &f, 4); }

bool Damage_Dents(int n) { return dents(n >= 0 && n < CAR_MAX ? n : 0); }
Obj *Damage_PartObj(int n, int k) { return Parts_Obj(n, k); }
int Damage_Slots(int n) { return count(&dmg[n >= 0 && n < CAR_MAX ? n : 0]); }
int Damage_SlotVerts(int n, int s) { return s >= 0 && s < SLOTS ? nverts(&dmg[n >= 0 && n < CAR_MAX ? n : 0], s) : 0; }

bool Damage_RestVert(int n, int s, int v, int32_t p[3])
{
    const Dmg *d = &dmg[n >= 0 && n < CAR_MAX ? n : 0];
    if (s < 0 || s >= SLOTS || !d->rest[s]) return false;
    const uint8_t *r = d->rest[s] + 0x20 * (size_t)v;
    for (int a = 0; a < 3; a++) p[a] = (int32_t)rd32(r + 4 * a);
    return true;
}

bool Damage_Vert(int n, int s, int v, int32_t p[3])
{
    const Dmg *d = &dmg[n >= 0 && n < CAR_MAX ? n : 0];
    const uint8_t *x = s >= 0 && s < SLOTS ? verts(d, s) : NULL;
    if (!x) return false;
    x += (size_t)v * C3D_VERTEX;
    for (int a = 0; a < 3; a++) p[a] = vget(x, 4 * (uint32_t)a);
    return true;
}

/* the mesh uploaded again; 0x4b4100 marks it in the car's relighting table (0x6e0124, not ported) */
static void upload(const Dmg *d, int s)
{
    const Obj *o = d->obj[s];
    if (!o || !o->mesh) return;
    Mesh_UploadVerts(o->model, o->mesh);
    PENDING_NOTE(0x4b4100, "Relight_MarkMesh");
}

/* ---- the records and logs ---- */

/* Damage_Alloc 0x4667c0 (the port: the records are static); the rest vertices are freed as the race's
   clean-up 0x466680 does */
void Damage_Alloc(int n)
{
    (void)n;
    for (int i = 0; i < CAR_MAX; i++) {
        for (int s = 0; s < SLOTS; s++) free(rest_tab[i][s]), rest_tab[i][s] = NULL;
        rest_have[i] = false, rest_n[i] = 0;
    }
}

/* 0x4669b0 */
void Damage_Clear(int first, int n)
{
    for (int i = first; i < first + n && i < CAR_MAX; i++) memset(&dmg[i], 0, sizeof dmg[i]);
}

/* 0x466920: a log's next fields to "none", this stage's panels and flags cleared, then the carried
   log and panels taken as this stage's */
static void log_clear(uint8_t *l)
{
    l[0x20a] = 0, l[0x20b] = 0;
    for (int i = 0; i < ENTRIES; i++) l[0x112 + ENTRY * i] = 0xff;
    memset(l + 0x24c, 0, 0x22);
    for (uint32_t o = 0x270; o < 0x28c; o += 4) wr32(l + o, 0);
    memcpy(l, l + 0x106, 0x106);
    memcpy(l + 0x20c, l + 0x24c, 0x40);
}

/* 0x4668d0: every log, from the last, and the hit sounds' times (0x588a80-0x588a8c) */
void Damage_ResetLogs(void)
{
    for (int i = CAR_MAX - 1; i >= 0; i--) log_clear(logs[i]);
    for (uint32_t a = 0x588a80; a < 0x588a90; a += 4) gw32(a, 0);
}

bool Damage_SlotData(int n, int s, uint8_t **mesh, uint8_t **v, int *nv, uint8_t **rest)
{
    const Dmg *d = &dmg[n >= 0 && n < CAR_MAX ? n : 0];
    if (s < 0 || s >= SLOTS || !d->mesh[s]) return false;
    *mesh = d->mesh[s], *v = verts(d, s), *nv = nverts(d, s), *rest = d->rest[s];
    return true;
}

/* 0x466e90: a mesh object into the slot of its type (5-19); the count only for one with vertices.
   (The original writes outside the tables for other types; the port skips them.) */
static void add_slot(Dmg *d, Obj *o)
{
    int s = (int)(o->type & 0xff) - 5;
    if (!o->mesh || o->kind != 0 || s < 0 || s >= SLOTS) return;
    d->obj[s] = o, d->mesh[s] = o->mesh;
    int n = (int)rd32(o->mesh + 0x10);
    W(d, 0x420 + 4 * (uint32_t)s, n);
    if (0 < n) W(d, 0x45c, count(d) + 1);
}

/* 0x46afe0: the rest vertices of each slot, from the mesh as loaded: the vertex colour's red, green and
   blue less 0x80 are the limits (a dent stops there, 64ths of a unit), the dent direction their
   negation. The limits from the position's signs are written first and then overwritten, as in the
   original. */
static void save_rest(int n, Dmg *d)
{
    rest_have[n] = true;
    rest_n[n] = count(d);
    memset(rest_type[n], 0, sizeof rest_type[n]);
    for (int s = 0; s < count(d); s++) {
        int nv = nverts(d, s);
        rest_tab[n][s] = calloc((size_t)(nv > 0 ? nv : 1), 0x20);
        rest_type[n][s] = d->obj[s] ? (uint8_t)d->obj[s]->type : 0;
        const uint8_t *v = verts(d, s);
        for (int k = 0; v && k < nv; k++) {
            uint8_t *r = rest_tab[n][s] + 0x20 * k;
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

/* 0x46acb0: with dents on, each slot finds its rest vertices by type, the mesh is put back to them
   (not uploaded), and the slot's box centre (+0xb4) and half size (+0x168) and the car's extremes in x
   and z (+0x410 the largest, +0x414 the smallest x; +0x418, +0x41c z) are taken from them */
static void rest_shape(int n, Dmg *d)
{
    if (!dents(n)) return;
    if (!rest_have[n]) save_rest(n, d);
    for (int s = 0; s < count(d); s++) {
        int m = -1;
        for (int j = 0; j <= count(d) && m < 0; j++)
            if (j < SLOTS + 1 && d->obj[s] && rest_type[n][j] == (uint8_t)d->obj[s]->type) m = j;
        if (m < 0) continue;
        d->rest[s] = m < SLOTS ? rest_tab[n][m] : NULL;
        int32_t lo[3] = {0x640000, 0x640000, 0x640000}, hi[3] = {-0x640000, -0x640000, -0x640000};
        uint8_t *v = verts(d, s);
        for (int k = 0; v && d->rest[s] && k < nverts(d, s); k++) {
            const uint8_t *r = d->rest[s] + 0x20 * k;
            uint8_t *x = v + (size_t)k * C3D_VERTEX;
            for (uint32_t a = 0; a < 6; a++) vset(x, 4 * a, (int32_t)rd32(r + 4 * a));
            int32_t p[3] = {vget(x, 0), vget(x, 4), vget(x, 8)};
            for (int a = 0; a < 3; a++) {
                if (hi[a] < p[a]) hi[a] = p[a];
                if (p[a] < lo[a]) lo[a] = p[a];
            }
            if (p[0] < 0) { if (p[0] < R(d, 0x414)) W(d, 0x414, p[0]); }
            else if (R(d, 0x410) < p[0]) W(d, 0x410, p[0]);
            if (p[2] < 0) { if (p[2] < R(d, 0x41c)) W(d, 0x41c, p[2]); }
            else if (R(d, 0x418) < p[2]) W(d, 0x418, p[2]);
        }
        for (uint32_t a = 0; a < 3; a++) {
            int32_t ctr = hi[a] + fx_mul(lo[a] - hi[a], 0x8000);
            W(d, 0xb4 + 12 * (uint32_t)s + 4 * a, ctr);
            W(d, 0x168 + 12 * (uint32_t)s + 4 * a, hi[a] - ctr);
        }
    }
}

/* the slots' arrays from j + 1 down to j; the last is left as it was (duplicated) */
static bool shift_down(Dmg *d, int j)
{
    bool any = false;
    for (int i = j; i < SLOTS - 1; i++) {
        if (d->obj[i] || d->obj[i + 1]) any = true;
        d->mesh[i] = d->mesh[i + 1], d->obj[i] = d->obj[i + 1], d->rest[i] = d->rest[i + 1];
        W(d, 0x420 + 4 * (uint32_t)i, R(d, 0x420 + 4 * (uint32_t)i + 4));
        memcpy(d->s + 0xb4 + 12 * i, d->s + 0xb4 + 12 * (i + 1), 12);
        memcpy(d->s + 0x168 + 12 * i, d->s + 0x168 + 12 * (i + 1), 12);
    }
    return any;
}

/* the 34 panels' sensitivities (+0x2c8): 0.3, 0.4, 0.6 or 1.0 */
static const int32_t sens[34] = {
    0x4ccc, 0x4ccc, 0x9999, 0x9999, 0x4ccc, 0x4ccc, 0x4ccc, 0x4ccc, 0x9999, 0x9999, 0x4ccc, 0x4ccc,
    0x9999, 0x9999, 0x9999, 0x4ccc, 0x9999, 0x9999, 0x10000, 0x10000, 0x10000, 0x10000, 0x10000, 0x6666,
    0x9999, 0x4ccc, 0x10000, 0x10000, 0x10000, 0x10000, 0x10000, 0x10000, 0x10000, 0x10000};

void Damage_Setup(bool again, bool keep, const int16_t *cars, int n)
{
    Parts_Init();
    if (!again) Parts_ClearFlag();
    for (int i = n - 1; i >= 0; i--) {
        int k = cars[i];
        Car *c = Car_Get(k);
        Parts_Reset(c);
        Dmg *d = &dmg[k >= 0 && k < CAR_MAX ? k : 0];
        uint8_t *l = logs[k >= 0 && k < CAR_MAX ? k : 0];
        W(d, 0x45c, 0);
        Obj *root = c->second;
        if (!root) continue;
        for (uint32_t o = 0x410; o <= 0x41c; o += 4) W(d, o, 0);
        if ((uint8_t)root->type != 0x14) add_slot(d, root);
        for (Obj *a = root->child; a; a = a->next)
            for (Obj *b = a; b && (uint8_t)b->type != 0x14; b = b->child) add_slot(d, b);
        /* the empty slots squeezed out, a slot looked at again while something moved */
        for (int s = 0; s < SLOTS; s++)
            if (!d->obj[s] && s < SLOTS - 1 && shift_down(d, s)) s--;
        for (int s = 0; s < count(d); s++) Parts_Register(c, d->obj[s], s);
        static const uint8_t glass[6] = {4, 5, 1, 2, 8, 9};
        memcpy(d->s + 0x460, glass, 6);
        for (uint32_t o = 0x21c; o < 0x240; o += 4) W(d, o, 0);
        d->s[0x469] = 0;
        W(d, 0x46c, 0);
        for (uint32_t j = 0; j < 34; j++) W(d, 0x240 + 4 * j, 0), W(d, 0x350 + 4 * j, 0);
        for (uint32_t j = 0; j < 8; j++) W(d, 0x470 + 4 * j, 0), W(d, 0x490 + 4 * j, 0);
        for (uint32_t j = 0; j < 34; j++) W(d, 0x2c8 + 4 * j, sens[j]);
        if (!keep) wr32(l + 0x28c, 0);
        if (CI8(c, 0xb1a) < players()) {
            /* the player's damage carried between stages (0x52ebc0 + 0x148 per player, 0x407610) */
            uint32_t src = 0x52ebc0 + 0x148u * (uint32_t)(Gs_Byte537ef6() + CI8(c, 0xb1a));
            for (uint32_t j = 0; j < 0x106; j++) {
                if (!keep) l[0x106 + j] = gm8(src + j);
                l[j] = gm8(src + j);
            }
            for (uint32_t j = 0; j < 0x40; j++) {
                if (!keep) l[0x24c + j] = gm8(src + 0x108 + j);
                l[0x20c + j] = gm8(src + 0x108 + j);
            }
            if (l[0x104] == 0)
                for (int j = 0; j < ENTRIES; j++) {
                    if (!keep) l[0x112 + ENTRY * j] = 0xff;
                    l[0xc + ENTRY * j] = 0xff;
                }
        }
        rest_shape(CI8(c, 0xb1a), d);
        Damage_Reset(c);
    }
    if (again) Parts_ClearFlag();
}

void Damage_CarsLoaded(void)
{
    Damage_Alloc(g_all.n);
    Parts_Alloc(g_all.n);                  /* 0x480900, after Damage_Alloc */
    Parts_CinLinks(g_all.car, g_all.n);    /* 0x4809e0, after the cars */
    Parts_Empty();                         /* 0x480980 */
    Damage_Clear(0, g_all.n);
    Damage_ResetLogs();
    Damage_Setup(false, false, g_all.car, g_all.n);
    Particles_KillAll();   /* 0x4aff80, next in Cars_Load */
}

/* ---- the dents ---- */

/* 0x4675c0: the points the hit is pushed out to: the four bottom corners of the meshes' extremes at
   the box's floor (-+0x208), the same raised by the wheel radius (+0x774 front, +0x770 rear), and the
   four wheel points (+0x240) moved in by the weight split (+0x764/+0x768 in x, +0x76c in z) */
static void hull_points(const Car *c, const Dmg *d)
{
    int32_t y = -CI32(c, 0x208);
    int32_t xs[2] = {R(d, 0x410), R(d, 0x414)}, zs[2] = {R(d, 0x418), R(d, 0x41c)};
    for (int i = 0; i < 4; i++) hull[i][0] = xs[i >> 1], hull[i][1] = y, hull[i][2] = zs[i & 1];
    for (uint32_t i = 0; i < 4; i++) {
        hull[4 + i][0] = hull[i][0], hull[4 + i][2] = hull[i][2];
        hull[4 + i][1] = hull[i][1] + CI32(c, i < 2 ? 0x774 : 0x770);
        int32_t *p = hull[8 + i];
        for (uint32_t a = 0; a < 3; a++) p[a] = CI32(c, 0x240 + 12 * i + 4 * a);
        p[0] = p[0] < 1 ? CI32(c, 0x768) + p[0] : p[0] - CI32(c, 0x764);
        p[2] = p[2] < 1 ? CI32(c, 0x76c) + p[2] : p[2] - CI32(c, 0x76c);
    }
}

/* 0x508740: the vertex's move from its rest place, in 64ths, kept to the side and within the limit of
   each axis, then written as the vertex's position */
static void clamp_vertex(int32_t p[3], Dmg *d, int s, int k)
{
    const uint8_t *r = d->rest[s] + 0x20 * k;
    for (uint32_t a = 0; a < 3; a++) {
        int32_t base = (int32_t)rd32(r + 4 * a), lim = (int8_t)r[0x1b + a];
        int32_t m = (p[a] - base) >> 6;
        if ((0 < m && lim < 1) || (m < 0 && -1 < lim)) m = 0;
        if ((lim < m && 0 < m) || (m < lim && m < 0)) m = lim;
        p[a] = m * 0x40 + base;
    }
    uint8_t *v = verts(d, s) + (size_t)k * C3D_VERTEX;
    for (uint32_t a = 0; a < 3; a++) vset(v, 4 * a, p[a]);
}

/* the pseudo-random factor of the outer band: bits 7-9 of |a + b|, -1.0 to 0.875 in eighths with the
   lower half below -0.5 */
static int32_t jitter(int32_t a, int32_t b)
{
    int32_t m = a + b;
    if (m < 0) m = -m;
    uint32_t u = (uint32_t)m & 0x80000380u;
    if ((int32_t)u < 0) u = ((u - 1) | 0xfffffc00u) + 1;
    int32_t j = (int32_t)(u * 0x40);
    return j < 0x8000 ? j - 0x10000 : j;
}

/* the vertex moved to p (clamped), its normal moved by three times as much (0x467d8a, 0x46840f), as
   the original does */
static void move_vertex(Dmg *d, int s, int k, int32_t p[3], const int32_t was[3])
{
    clamp_vertex(p, d, s, k);
    uint8_t *v = verts(d, s) + (size_t)k * C3D_VERTEX;
    for (uint32_t a = 0; a < 3; a++) vset(v, 0xc + 4 * a, vget(v, 0xc + 4 * a) + fx_mul(p[a] - was[a], 0x30000));
}

static int32_t dot(const int32_t a[3], const int32_t b[3]) { return fx_mul(a[2], b[2]) + fx_mul(a[0], b[0]) + fx_mul(a[1], b[1]); }

/* 0x467700: a flat hit (kind 0). The hit point is pushed along the normal to the farthest of the hull
   points behind it (else the nearest in front); every vertex within r1 of the plane through it moves
   along the normal, towards the car's middle, by r1 - d^2 / r1; those out to r1 + r2 move along their
   dent direction by (d - r1) / r2 x r3 / 5 times the jitter. */
static void dent_flat(void)
{
    Dmg *d = of(car_);
    int32_t mh[3] = {fx_mul(hit[0], -0x10000), fx_mul(hit[1], -0x10000), fx_mul(hit[2], -0x10000)};
    bool flip = fx_mul(mh[2], nrm[2]) + fx_mul(mh[0], nrm[0]) + fx_mul(mh[1], nrm[1]) < 0;
    int32_t lo = 0, front = 0;
    for (int i = 0; i < 12; i++) {
        int32_t q[3] = {hull[i][0] - hit[0], hull[i][1] - hit[1], hull[i][2] - hit[2]};
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
    for (int s = 0; s < count(d); s++) {
        bool dirty = false;
        uint8_t *v = verts(d, s);
        for (int k = 0; v && d->rest[s] && k < nverts(d, s); k++) {
            const uint8_t *x = v + (size_t)k * C3D_VERTEX;
            int32_t p[3] = {vget(x, 0), vget(x, 4), vget(x, 8)};
            int32_t df[3] = {hit[0] - p[0], hit[1] - p[1], hit[2] - p[2]};
            int32_t e = fx_mul(nrm[2], df[2]) + fx_mul(nrm[0], df[0]) + fx_mul(nrm[1], df[1]);
            int32_t q = fx_mul(e, e);
            if (outer < q) continue;
            int32_t np[3];
            if (r1sq < q) {
                int32_t t = fx_mul(fx_mul(fx_mul((int32_t)Fx_Sqrt((uint32_t)q) - r1, i2), j3), jitter(df[2], df[0]));
                const uint8_t *r = d->rest[s] + 0x20 * k;
                for (int a = 0; a < 3; a++) np[a] = p[a] + fx_mul((int8_t)r[0x18 + a] << 9, t);
            } else {
                int32_t t = r1 - fx_mul(i1, q);
                for (int a = 0; a < 3; a++) np[a] = side ? p[a] - fx_mul(nrm[a], t) : p[a] + fx_mul(nrm[a], t);
            }
            move_vertex(d, s, k, np, p);
            dirty = true;
        }
        if (dirty) upload(d, s);
    }
}

/* 0x467e90: a round hit (kind 1 a cylinder of radius size_r, 2 an object's corner): the normal turned
   to face the car's middle, the point moved r1 along it; every vertex within the radius of the upright
   line through it moves along the normal by R - d^2 / R, those out to R + r2 along their dent direction
   as in the flat hit. */
static void dent_round(void)
{
    Dmg *d = of(car_);
    if (0 <= fx_mul(nrm[2], hit[2]) + fx_mul(nrm[0], hit[0]) + fx_mul(nrm[1], hit[1]))
        for (int a = 0; a < 3; a++) nrm[a] = fx_mul(nrm[a], -0x10000);
    for (int a = 0; a < 3; a++) hit[a] += fx_mul(nrm[a], r1);
    int32_t outer = fx_mul(r2 + size_r, r2 + size_r), rsq = fx_mul(size_r, size_r), i2 = inv(r2), iR = inv(size_r), j3 = fx_mul(r3, 0x3333);
    for (int s = 0; s < count(d); s++) {
        bool dirty = false;
        uint8_t *v = verts(d, s);
        for (int k = 0; v && d->rest[s] && k < nverts(d, s); k++) {
            const uint8_t *x = v + (size_t)k * C3D_VERTEX;
            int32_t p[3] = {vget(x, 0), vget(x, 4), vget(x, 8)};
            int32_t df[3] = {hit[0] - p[0], hit[1] - p[1], hit[2] - p[2]};
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
            int32_t np[3] = {p[0] + off[0], p[1] + off[1], p[2] + off[2]};
            move_vertex(d, s, k, np, p);
            dirty = true;
        }
        if (dirty) upload(d, s);
    }
}

/* ---- the hit log ---- */

/* 0x4688b0: an entry back into the hit: the point in 12.7ths, the normal and axis in 127ths, the
   strength in 255ths, the kind, a cylinder's radius in 25.5ths */
static void decode_hit(const uint8_t *e)
{
    for (int a = 0; a < 3; a++) {
        hit[a] = fx_mul((int8_t)e[9 + a] * 0x10000, 0x1428);
        nrm[a] = fx_mul((int8_t)e[3 + a] * 0x10000, 0x204);
        ax[a] = fx_mul((int8_t)e[6 + a] * 0x10000, 0x204);
    }
    strength = (int32_t)(((uint64_t)e[0] << 32) / 0xff0000);
    kind_ = e[1];
    if (e[1] == 1) size_r = fx_mul((int32_t)((uint32_t)e[2] << 16), 0xa0a);
}

/* 0x468520: the hit into this stage's log (+0x106) unless logging is off (+0x28c): a new entry while
   there are fewer than 20, else the weakest one if weaker than this hit, moved to the end of the list;
   otherwise into a scratch entry. Either way the hit is read back from the entry, so the dent is made
   from the quantised values. */
static void log_hit(void)
{
    uint8_t *l = logs[idx(car_)], *g = l + 0x106, *e = NULL;
    static uint8_t scratch[ENTRY];
    int32_t st = fx_mul(strength, 0xff0000) >> 16;
    if (0xff < st) st = 0xff;
    if (rd32(l + 0x28c) == 0) {
        uint8_t n = g[0x104];
        if (n < ENTRIES) {
            if (n != 0) g[ENTRY * n - 1] = n;
            e = g + ENTRY * g[0x104];
            g[0x104]++;
        } else {
            uint32_t weakest = 1000;
            uint8_t *p = g + ENTRY * g[0x105], *prev = NULL, *min = NULL, *min_prev = NULL, *last;
            for (;;) {
                if (*p < weakest) min_prev = prev, min = p, weakest = *p;
                if (p[0xc] == 0xff) break;
                prev = p;
                p = g + ENTRY * (int8_t)p[0xc];
            }
            last = p;
            if (min && weakest < (uint32_t)(st & 0xff)) {
                if (min != last) {
                    if (!min_prev) last[0xc] = g[0x105], g[0x105] = min[0xc];
                    else last[0xc] = min_prev[0xc], min_prev[0xc] = min[0xc];
                    min[0xc] = 0xff;
                }
                e = min;
            }
        }
    }
    if (!e) e = scratch;
    e[0] = (uint8_t)st;
    for (int a = 0; a < 3; a++) {
        e[9 + a] = (uint8_t)clamp127(fx_mul(hit[a], 0xcb2e7) >> 16);
        e[3 + a] = (uint8_t)clamp127(fx_mul(nrm[a], 0x7f0000) >> 16);
        e[6 + a] = (uint8_t)clamp127(fx_mul(ax[a], 0x7f0000) >> 16);
    }
    e[1] = kind_;
    if (kind_ == 1) {
        int32_t r = fx_mul(size_r, 0x197f67) >> 16;
        e[2] = (uint8_t)(0xff < r ? 0xff : r);
    }
    decode_hit(e);
}

/* ---- the panels ---- */

/* 0x468a80: the hit's place in the car's frame (+0x5dc, +0x5e4) on a 3 x 3 grid over the box (x by
   thirds from +2/3 of the half width, z by quarters/halves of the half length, each widened by half the
   strength); each cell it reaches gains 0.2 v, at most 100 */
static void zones(const Car *c, int32_t v)
{
    Dmg *d = of(c);
    int32_t h = fx_mul(v, 0x8000), xs = fx_mul(CI32(c, 0x204), 0xaac0), zs = fx_mul(CI32(c, 0x20c), 0xc000);
    int32_t zc = -zs;
    for (uint32_t r = 0; r < 3; r++) {
        int32_t zw = fx_mul(CI32(c, 0x20c), r == 1 ? 0x8000 : 0x4000) + h;
        int32_t xc = xs;
        for (uint32_t k = 0; k < 3; k++) {
            uint32_t o = 0x21c + 12 * r + 4 * k;
            int32_t dx = CI32(c, 0x5dc) - xc, dz = CI32(c, 0x5e4) - zc;
            if (dx < 0) dx = -dx;
            if (dz < 0) dz = -dz;
            if (dx <= fx_mul(CI32(c, 0x204), 0x553f) + h && dz <= zw) {
                int32_t z = R(d, o) + fx_mul(v, 0x3333);
                W(d, o, 0x640000 < z ? 0x640000 : z);
            }
            xc -= xs;
        }
        zc += zs;
    }
}

/* 0x469100: how far the meshes are from rest, the mean over every vertex of 5 x its distance (summed
   over the axes, at most 1.0); a slot that is a fallen-off part (1, 3) counts as vertices at 0 */
static int32_t crumple(const Car *c, const Dmg *d)
{
    if (!dents(idx(c))) return 0;
    int p1 = Parts_Slot(1, idx(c)), p3 = Parts_Slot(3, idx(c));
    int32_t sum = 0, n = 0;
    for (int s = 0; s < count(d); s++) {
        if ((s == p1 && Damage_Flag(c, 1)) || (s == p3 && Damage_Flag(c, 3))) { n += nverts(d, s); continue; }
        const uint8_t *v = verts(d, s);
        for (int k = 0; v && d->rest[s] && k < nverts(d, s); k++) {
            const uint8_t *x = v + (size_t)k * C3D_VERTEX, *r = d->rest[s] + 0x20 * k;
            int32_t t = 0;
            for (uint32_t a = 0; a < 3; a++) {
                int32_t m = vget(x, 4 * a) - (int32_t)rd32(r + 4 * a);
                t += m < 0 ? -m : m;
            }
            int32_t u = fx_mul(t, 0x50000);
            sum += 0x10000 < u ? 0x10000 : u;
            n++;
        }
    }
    int32_t div = (int32_t)((uint32_t)n << 16);
    return div ? (int32_t)(((int64_t)sum << 16) / div) : 0;   /* the original traps on none */
}

/* 0x468c10: with damage on (+0xb50) the 34 panels (+0x240) from the nine cells, times their
   sensitivity plus the carried part (+0x350), at most 1.0; then the handling factors from them: the
   engine (+0x404), the steering pulls (+0x3dc-+0x3e8, +0x3d8), the brakes (+0x3fc, +0x400), the wheels'
   drag (+0x3ec-+0x3f8), the differential (+0x7b4), the gear byte (+0x468), the crumple (+0x284, +0x408) */
static void panels(Car *c)
{
    Dmg *d = of(c);
    if (!CI32(c, 0xb50)) return;
    int32_t z[9];
    for (uint32_t i = 0; i < 9; i++) z[i] = R(d, 0x21c + 4 * i);
    static const int8_t from[34] = {6, 0, 8, 2, 6, 0, 6, 0, 8, 2, 6, 0, 8, 2, 3, 3, -1, -1,
                                    5, 3, 5, 5, 3, 3, 5, -1, 3, 5, 7, 1, 8, 2, 6, 0};
    for (uint32_t i = 0; i < 34; i++) if (from[i] >= 0) W(d, 0x240 + 4 * i, z[from[i]]);
    W(d, 0x280, fx_mul(z[4] + z[3] + z[5], 0x5553));
    W(d, 0x284, 0);
    W(d, 0x2a4, fx_mul(z[5] + z[3] + z[4], 0x5553));
    if (!CI32(c, 0xb7c)) W(d, 0x27c, 0);
    if (!CI32(c, 0xb80)) W(d, 0x280, 0);
    for (uint32_t i = 0; i < 34; i++) {
        uint32_t o = 0x240 + 4 * i;
        int32_t v = fx_mul(R(d, o), R(d, o + 0x88)) + R(d, o + 0x110);
        W(d, o, 0x10000 < v ? 0x10000 : v);
    }
    W(d, 0x404, 0x10000 - fx_mul(R(d, 0x2a0), 0x666) - fx_mul(R(d, 0x27c), 0x1333) - fx_mul(R(d, 0x2a4), 0x2666));
    W(d, 0x3dc, fx_mul(R(d, 0x258), -0x3333));
    W(d, 0x3e0, fx_mul(R(d, 0x25c), -0x3333));
    W(d, 0x3e4, fx_mul(R(d, 0x260), 0x1999));
    W(d, 0x3e8, fx_mul(R(d, 0x264), -0x199a));
    W(d, 0x3d8, fx_mul(fx_mul(R(d, 0x250) * 2, 0x8000), 0xa0000));
    W(d, 0x3fc, 0x10000 - fx_mul(fx_mul(R(d, 0x268) + R(d, 0x26c), 0x8000), 0x3333));
    W(d, 0x400, 0x10000 - fx_mul(fx_mul(R(d, 0x270) + R(d, 0x274), 0x8000), 0x3333));
    for (uint32_t i = 0; i < 4; i++) W(d, 0x3ec + 4 * i, fx_mul(R(d, 0x240 + 4 * i), 0xccc));
    int32_t diff = CI32(c, 0x7b8);
    if (diff != 0x10000 && diff != 0) {
        int32_t v = fx_mul(R(d, 0x280), 0x8000) + diff;
        CW32(c, 0x7b4, 0x10000 < v ? 0x10000 : v);
    }
    d->s[0x468] = (uint8_t)(((int64_t)R(d, 0x278) * 0xf0000) >> 32);
    int32_t k = crumple(c, d);
    W(d, 0x408, fx_mul(k, 0x4000));
    if (Damage_Flag(c, 3)) W(d, 0x408, R(d, 0x408) - 0x3333);
    W(d, 0x284, 0x10000 < k ? 0x10000 : k);
}

/* 0x4692b0: the first slot whose object is of the type */
static int slot_of_type(const Dmg *d, uint32_t type)
{
    for (int s = 0; s < count(d); s++)
        if (d->obj[s] && (d->obj[s]->type & 0xff) == type) return s;
    return -1;
}

static void frames(const Dmg *d, int s, uint32_t mask, uint32_t f0, uint32_t f1, bool both)
{
    if (s < 0 || !d->obj[s]) return;
    Mesh_SetFrame(d->obj[s]->model, d->mesh[s], mask, f0);
    if (both) Mesh_SetFrame1(d->obj[s]->model, d->mesh[s], mask, f1);
}

/* 0x4694a0: glass i's polygons to the frames of its state (0 whole, 1 cracked, 2 broken; the lights 4
   and 5 have no cracked look), for the 'A' and 'C' models: the windows on the type-7 body (and the
   rear's type 0xc), the lights on type 0xe */
static void glass_frames(const Car *c, int state, int i)
{
    const Dmg *d = of(c);
    char kind = Car_Slot(idx(c))->kind;
    if (kind != 'C' && kind != 'A') return;
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
    }
}

/* 0x4692f0: the eight glass panels (+0x2a8-+0x2c4): past their break limit (0x51bfac) one breaks,
   past the crack limit (0x51bfcc) it cracks; a player's car sounds it (the windows SCREEN1, the lights
   HLIGHT) and a broken one throws glass (0x49a910), unless quiet */
static void glass(Car *c, bool quiet)
{
    Dmg *d = of(c);
    char kind = Car_Slot(idx(c))->kind;
    if (kind != 'C' && kind != 'A') return;
    for (uint32_t i = 0; i < 8; i++) {
        uint8_t id = d->s[0x460 + i];
        int32_t broken = R(d, 0x490 + 4 * i), cracked = R(d, 0x470 + 4 * i), v = R(d, 0x2a8 + 4 * i);
        if (broken && cracked) continue;
        int snd = i < 4 ? 2 : 0;
        if ((int32_t)gm32(0x51bfac + 4 * i) < v && !broken) {
            glass_frames(c, 2, (int)i);
            W(d, 0x490 + 4 * i, 1), W(d, 0x470 + 4 * i, 1);
            if (!quiet) {
                Glass_Queue(d->s, c, id);   /* 0x49a910 */
                if (CI8(c, 0xb1a) < players()) Crash_Glass(CI8(c, 0xb1a), snd, CI8(c, 0xb1a));
            }
        } else if ((int32_t)gm32(0x51bfcc + 4 * i) < v && !cracked && !broken) {
            glass_frames(c, 1, (int)i);
            W(d, 0x470 + 4 * i, 1);
            if (!quiet && CI8(c, 0xb1a) < players()) Crash_Glass(CI8(c, 0xb1a), snd, CI8(c, 0xb1a));
        }
    }
}

/* ---- the hit ---- */

/* 0x588a88: per player the time of the last hit's sound, in the game state at its address (the third
   and fourth players' share 0x588a90-0x588a94 with the damage records' count, as in the original;
   Damage_ResetLogs clears the first two) */
static uint32_t last_hit(int p) { return 0x588a88 + 4 * (uint32_t)p; }

void Car_Damage(Car *c, const int32_t pt[3], const int32_t n[3], uint32_t size, uint8_t kind, bool replay)
{
    int32_t first = 0;
    if (!replay) {
        int32_t imp[3] = {CI32(c, 0x5c4), CI32(c, 0x5c8), CI32(c, 0x5cc)};
        int32_t v = (int32_t)Fx_Sqrt((uint32_t)fx_mul(imp[0], imp[0]) + (uint32_t)fx_mul(imp[1], imp[1]) + (uint32_t)fx_mul(imp[2], imp[2]));
        if (0x10000 < v) v = 0x10000;
        else if (v < 0) v = 0;
        int p = CI8(c, 0xb1a);
        if (p < players()) {
            if (CI32(c, 0xb74) == 0) Crash_Roll(p, v, false, p);
            else if (10 < Timer_Centis() - gm32(last_hit(p))) {
                Crash_Hit(p, v, p);
                gw32(last_hit(p), Timer_Centis());
            }
        }
        Camera_Shake(c, imp, true);   /* 0x4241d0 */
    }
    Dmg *d = of(c);
    if (count(d) == 0 || !dents(idx(c))) return;
    if (!replay) {
        int32_t imp[3] = {CI32(c, 0x5c4), CI32(c, 0x5c8), CI32(c, 0x5cc)};
        strength = (int32_t)Fx_Sqrt((uint32_t)fx_mul(imp[0], imp[0]) + (uint32_t)fx_mul(imp[1], imp[1]) + (uint32_t)fx_mul(imp[2], imp[2]));
        if (strength <= (int32_t)gm32(0x51bf94 + 4u * kind)) return;
        strength = fx_mul(strength, (int32_t)gm32(0x51bfa0 + 4u * kind));
        if (0x10000 < strength) strength = 0x10000;
        kind_ = kind;
        ax[0] = CI32(c, 0x04), ax[1] = CI32(c, 0x14), ax[2] = CI32(c, 0x24);   /* the orientation's second column */
        first = strength;
    }
    car_ = c;
    int32_t pos[3] = {CI32(c, 0x2d0), CI32(c, 0x2d4), CI32(c, 0x2d8)};
    if (kind_ == 0) {
        if (!replay) {
            Walls_RotT(c, nrm, n);
            int32_t e = fx_mul(n[0], pt[0] - pos[0]) + fx_mul(n[1], pt[1] - pos[1]) + fx_mul(n[2], pt[2] - pos[2]);
            int32_t q[3] = {fx_mul(n[0], e), fx_mul(n[1], e), fx_mul(n[2], e)};
            Walls_RotT(c, hit, q);
            log_hit();
        }
        r1 = fx_mul(strength, 0x5999), r2 = fx_mul(strength, 0x9999), r3 = fx_mul(strength, 0xb333);
        hull_points(c, d);
        dent_flat();
    } else {
        int32_t lim;
        if (kind_ == 1) {
            if ((int32_t)size < 0x4000) size = 0x4000;   /* (a replay passes 0: 0x4000, not the logged radius) */
            if (!replay) {
                int32_t q[3] = {pt[0] - pos[0], pt[1] - pos[1], pt[2] - pos[2]};
                size_r = (int32_t)size;
                Walls_RotT(c, hit, q);
                Walls_RotT(c, nrm, n);
                log_hit();
            }
            lim = (int32_t)size;
        } else {
            if (!replay) {
                int32_t dx = pos[0] - pt[0], dz = pos[2] - pt[2], u[3] = {0, 0, 0};
                uint32_t l = Fx_Sqrt((uint32_t)fx_mul(dx, dx) + (uint32_t)fx_mul(dz, dz));
                if (l) {
                    int32_t k = (int32_t)(0x100000000LL / (int32_t)l);
                    u[0] = fx_mul(dx, k), u[2] = fx_mul(dz, k);
                }
                int32_t q[3] = {pt[0] - pos[0], pt[1] - pos[1], pt[2] - pos[2]};
                Walls_RotT(c, hit, q);
                Walls_RotT(c, nrm, u);
                log_hit();
            }
            size_r = 0x4000;
            lim = 0x4000;
        }
        r1 = fx_mul(strength, 0x8000);
        if (lim < r1) r1 = lim;
        r2 = r3 = r1;
        hull_points(c, d);
        dent_round();
    }
    if (!replay) {
        zones(c, first);
        panels(c);
        glass(c, false);
    }
}

/* 0x4698a0: with dents on, every slot's vertices back to rest and uploaded with their colours, the
   cells, the counters (+0x469, +0x46c), the carried panels and the glass cleared, the glass made whole */
static void restore(Car *c)
{
    Dmg *d = of(c);
    if (!dents(idx(c))) return;
    for (int s = 0; s < count(d); s++) {
        uint8_t *v = verts(d, s);
        for (int k = 0; v && d->rest[s] && k < nverts(d, s); k++)
            for (uint32_t a = 0; a < 6; a++) vset(v + (size_t)k * C3D_VERTEX, 4 * a, (int32_t)rd32(d->rest[s] + 0x20 * k + 4 * a));
        if (d->obj[s] && d->obj[s]->mesh) {
            Mesh_RestoreColours(d->obj[s]->model, d->obj[s]->mesh);
            PENDING_NOTE(0x4b4100, "Relight_MarkMesh");
        }
    }
    for (uint32_t o = 0x21c; o < 0x240; o += 4) W(d, o, 0);
    d->s[0x469] = 0;
    W(d, 0x46c, 0);
    for (uint32_t j = 0; j < 34; j++) W(d, 0x350 + 4 * j, 0);
    for (int i = 0; i < 8; i++) {
        W(d, 0x490 + 4 * (uint32_t)i, 0), W(d, 0x470 + 4 * (uint32_t)i, 0);
        glass_frames(c, 0, i);
    }
    PENDING_NOTE(0x477c20, "Damage_Restore's 0x477c20 (car, 0, 0, 4)");
}

/* Damage_Reset 0x469690: the loose parts and the meshes back, the engine, brake and drag factors back
   to none, the carried hits replayed (each decoded, then Car_Damage as a replay, the car's contact
   and impulse kept), the carried panels (255ths) and part values taken, the panels and glass
   worked out again (quietly) */
void Damage_Reset(Car *c)
{
    Dmg *d = of(c);
    uint8_t *l = logs[idx(c)];
    Parts_Reset(c);
    restore(c);
    W(d, 0x404, 0x10000), W(d, 0x3fc, 0x10000), W(d, 0x400, 0x10000);
    for (uint32_t o = 0x3ec; o <= 0x3f8; o += 4) W(d, o, 0);
    W(d, 0x408, 0), W(d, 0x3d8, 0);
    d->s[0x468] = 0;
    uint8_t keep[0x24];
    memcpy(keep, c->s + 0x5c4, sizeof keep);   /* +0x5c4-+0x5e7: the impulse and the contact */
    if (l[0x104] != 0) {
        int i = l[0x105];
        for (int guard = 0; guard <= ENTRIES && i >= 0 && i < ENTRIES; guard++) {
            const uint8_t *e = l + ENTRY * i;
            decode_hit(e);
            Car_Damage(c, NULL, NULL, 0, 0, true);
            if (e[0xc] == 0xff) break;
            i = (int8_t)e[0xc];
        }
    }
    memcpy(c->s + 0x5c4, keep, 0xc);
    memcpy(c->s + 0x5dc, keep + 0x18, 0xc);
    for (uint32_t j = 0; j < 34; j++) W(d, 0x350 + 4 * j, (int32_t)(((uint64_t)l[0x20c + j] << 32) / 0xff0000));
    for (uint32_t j = 0; j < 3; j++) {
        W(d, 0x4c0 + 4 * j, (int32_t)rd32(l + 0x240 + 4 * j));
        Aerials_Reset(CI8(c, 0xb1a), (int)j, (int32_t)rd32(l + 0x240 + 4 * j));   /* 0x486630: a carried aerial stays down */
    }
    for (uint32_t j = 0; j < 4; j++) {
        W(d, 0x4b0 + 4 * j, (int32_t)rd32(l + 0x230 + 4 * j));
        Parts_Hide(c, (int)j, R(d, 0x4b0 + 4 * j));   /* 0x480ac0: a part carried as fallen off not drawn */
    }
    panels(c);
    glass(c, true);
}

/* Damage_PartOff 0x469bf0: the record's part flag k (+0x4b0 + 4k) set, the panels worked out again */
void Damage_PartOff(Car *c, int k)
{
    W(of(c), 0x4b0 + 4 * (uint32_t)(k & 3), 1);
    panels(c);
}

/* Damage_Finish 0x469a80: at the car's finish, once (the log's +0x28c then stops the logging), the
   panels as 255ths (clamped above only), the part values and the fallen-off flags as this stage's */
void Damage_Finish(int car)
{
    Car *c = Car_Get(car);
    const Dmg *d = of(c);
    uint8_t *l = logs[idx(c)];
    if (rd32(l + 0x28c) != 0) return;
    wr32(l + 0x28c, 1);
    for (uint32_t j = 0; j < 34; j++) {
        int32_t v = fx_mul(R(d, 0x240 + 4 * j), 0xff0000) >> 16;
        l[0x24c + j] = (uint8_t)(v > 0xff ? 0xff : v);
    }
    memcpy(l + 0x280, d->s + 0x4c0, 12);
    memcpy(l + 0x270, d->s + 0x4b0, 16);
}

/* Damage_Store 0x469b50: this stage's log (hits and panels) into the player's carried damage
   (0x52ebc0 + 0x148 per player from the race's first, 0x407610), for Damage_Setup of the next stage */
void Damage_Store(int car)
{
    Car *c = Car_Get(car);
    const uint8_t *l = logs[idx(c)];
    uint32_t dst = 0x52ebc0 + 0x148u * (uint32_t)(Gs_Byte537ef6() + CI8(c, 0xb1a));
    for (uint32_t j = 0; j < 0x106; j++) gw8(dst + j, l[0x106 + j]);
    for (uint32_t j = 0; j < 0x40; j++) gw8(dst + 0x108 + j, l[0x24c + j]);
}

/* 0x46b4e0, each step: an engine more than half broken (panel 23, +0x29c) cuts out. Its badness
   b = 0.8 * clamp(2 (p - 0.5), 0, 1); a countdown (+0x40c, in steps) runs the two phases (+0x4cc
   set while cutting out): running for (1 - b)(250 r + 50) steps, cutting out for 15 b r, r from rand
   (0 to 1 in 16.16). The second phase's added term multiplies by 0, as the original does. */
void Damage_Misfire(Car *c)
{
    Dmg *d = of(c);
    int32_t p = R(d, 0x29c);
    if (p <= 0x8000) return;
    int32_t t = fx_mul(p - 0x8000, 0x20000);
    t = t < 0 ? 0 : t > 0x10000 ? 0x10000 : t;
    int32_t b = fx_mul(t, 0xcccc);
    if (R(d, 0x40c) > 0) {
        W(d, 0x40c, R(d, 0x40c) - 0x10000);
        return;
    }
    /* fild, fmul by the float 1/32767 (0x5112f0, 0x38000100) and the double 65536 (0x5112e0), fistp */
    double f = (double)crt_rand() * (double)0x1.0002p-15f * 65536.0;
    int32_t r = (int32_t)nearbyint(f);
    if (R(d, 0x4cc) != 0) {
        W(d, 0x4cc, 0);
        W(d, 0x40c, fx_mul(0x10000 - b, fx_mul(r, 0xfa0000)));
        W(d, 0x40c, R(d, 0x40c) + fx_mul(0x10000 - b, 0x320000));
    } else {
        W(d, 0x4cc, 1);
        W(d, 0x40c, fx_mul(b, fx_mul(r, 0xf0000)));
        W(d, 0x40c, R(d, 0x40c) + fx_mul(b, 0));
    }
}

/* 0x469de0: a surface that sparks under a scrape: (off the wheels, or +0xb29 = 6) and one of the
   hard surfaces */
static bool scrape_surface(uint16_t s, int32_t on_wheels, uint8_t b29)
{
    if (on_wheels && b29 != 6) return false;
    switch (s) {
    case 1: case 6: case 7: case 8: case 9: case 10: case 11: case 16: case 24: case 25: case 27: case 28:
        return true;
    default:
        return false;
    }
}

static uint32_t sc_len(const int32_t v[3]) { return Fx_Sqrt((uint32_t)fx_mul(v[0], v[0]) + (uint32_t)fx_mul(v[1], v[1]) + (uint32_t)fx_mul(v[2], v[2])); }
static void sc_unit(int32_t v[3])
{
    uint32_t l = sc_len(v);
    if (!l) { v[0] = v[1] = v[2] = 0; return; }
    int32_t r = (int32_t)(0x100000000LL / (int32_t)l);
    for (int i = 0; i < 3; i++) v[i] = fx_mul(v[i], r);
}
static void sc_cross(int32_t o[3], const int32_t a[3], const int32_t b[3])
{
    int32_t x = fx_mul(a[1], b[2]) - fx_mul(a[2], b[1]), y = fx_mul(a[2], b[0]) - fx_mul(a[0], b[2]),
            z = fx_mul(a[0], b[1]) - fx_mul(a[1], b[0]);
    o[0] = x, o[1] = y, o[2] = z;
}
/* the sparks' frame: along the velocity's part across the normal n (backwards), then across */
static void scrape_rows(int32_t rows[9], const Car *c, const int32_t n[3])
{
    int32_t v[3];
    for (uint32_t i = 0; i < 3; i++) v[i] = fx_mul(CI32(c, 0x408 + 4 * i), -0x10000);
    int32_t d = fx_mul(n[2], v[2]) + fx_mul(n[0], v[0]) + fx_mul(n[1], v[1]);
    for (int i = 0; i < 3; i++) rows[i] = v[i] - fx_mul(n[i], d);
    sc_unit(rows);
}

/* 0x46a500: bottoming out. All four wheels' suspension (+0x9d8) past their stops (+0x988, at least
   0x51e) with one moving fast (+0x9c8 above 0xd91), on its wheels: on a hard surface under a wheel at
   a speed above 0.3, sparks from the middle of the box's floor (corners +0x270 and +0x294) and the
   underside cell (+0x22c) dented by a hundredth of the speed (at most 1.0), the panels again; then
   for a player's car the roll sound (at most every quarter second) and, as for any bottoming car,
   the force feedback's shake (0x4241d0) and the velocity down by 3%. All wheels at their stops but
   none fast: only the shake. */
static void bottom_out(Car *c)
{
    Dmg *d = of(c);
    bool all = true, fast = false, sparked = false;
    for (uint32_t i = 0; i < 4; i++) {
        int32_t t = CI32(c, 0x988 + 4 * i);
        if (t < 0x51e) t = 0x51e;
        if (-t <= CI32(c, 0x9d8 + 4 * i)) { all = false; break; }
    }
    if (all)
        for (uint32_t i = 0; i < 4; i++) {
            int32_t v = CI32(c, 0x9c8 + 4 * i);
            if ((v < 0 ? -v : v) > 0xd91) fast = true;
        }
    if (!CI32(c, 0xb74)) return;
    int32_t vel[3] = {CI32(c, 0x408), CI32(c, 0x40c), CI32(c, 0x410)};
    if (!fast) {
        if (all) Camera_Shake(c, vel, false);
        return;
    }
    bool hard = false;
    for (uint32_t i = 0; i < 4 && !hard; i++) hard = scrape_surface((uint16_t)CI16(c, 0xaae + 2 * i), 0, c->s[0xb29]);
    if (hard && CI32(c, 0x778) > 0x4ccc) {
        sparked = true;
        int32_t n[3] = {CI32(c, 0x48c), CI32(c, 0x490), CI32(c, 0x494)}, rows[9], x[3], y[3];
        scrape_rows(rows, c, n);
        sc_cross(x, rows, n);
        sc_unit(x);
        sc_cross(y, x, rows);
        sc_unit(y);
        for (int i = 0; i < 3; i++) rows[3 + i] = fx_mul(y[i], 0x8000), rows[6 + i] = x[i];
        int32_t p[3];
        for (uint32_t i = 0; i < 3; i++)
            p[i] = fx_mul(CI32(c, 0x270 + 4 * i) - CI32(c, 0x294 + 4 * i), 0x8000) + CI32(c, 0x294 + 4 * i) - CI32(c, 0x2d0 + 4 * i);
        Sparks_Add(CI32(c, 0x778), p, c, rows, 0x1e0000, 0);
        int32_t k = CI32(c, 0x778) > 0x10000 ? 0x10000 : CI32(c, 0x778);
        W(d, 0x22c, R(d, 0x22c) + fx_mul(k, 0x28f));
        panels(c);
    }
    int n = CI8(c, 0xb1a);
    if (n < players() && Timer_Centis() - gm32(0x588a80 + 4 * (uint32_t)n) > 0x19 && CI32(c, 0x778) > 0) {
        int32_t s = CI32(c, 0x778);
        Crash_Roll(n, s > 0x10000 ? 0x5c28 : fx_mul(s, 0x5c28), sparked, n);
        gw32(0x588a80 + 4 * (uint32_t)n, Timer_Centis());
    }
    Camera_Shake(c, vel, false);
    for (uint32_t i = 0; i < 3; i++) CW32(c, 0x408 + 4 * i, fx_mul(CI32(c, 0x408 + 4 * i), 0xf851));
}

/* Damage_Scrapes 0x469e40: per listed car (from the last), each of its eight contact points (the
   wheels 0-3, the body 4-7) touching (+0xb2c + i clear) a hard surface (+0xaae + 2i) above 0.1 of
   speed sparks: on its wheels a wheel point (its +0xb29 = 6) with the strength from its loads
   (+0x870 + 4i and +0x880 + 4i, at most 1.0) at its contact point (+0x3c0 + 12i, 0.35 lower); off
   them any point at the speed, from below the car's middle (half its up axis down); none at or
   below 0.3. The sparks' frame: along the velocity across the point's normal (+0x4a4 + 12i), then
   that cross the normal, then those crossed. Then the car's bottoming out (0x46a500). */
void Damage_Scrapes(const int16_t *list, int n)
{
    for (int j = n - 1; j >= 0; j--) {
        Car *c = Car_Get(list[j]);
        if (!c) continue;
        for (uint32_t i = 0; i < 8; i++) {
            if (c->s[0xb2c + i]) continue;
            if (!scrape_surface((uint16_t)CI16(c, 0xaae + 2 * i), CI32(c, 0xb74), c->s[0xb29])) continue;
            int32_t k = CI32(c, 0x778);
            if (k <= 0x1999) continue;
            bool wheel = i < 4 && CI32(c, 0xb74);
            if (wheel) {
                int32_t a = CI32(c, 0x870 + 4 * i), b = CI32(c, 0x880 + 4 * i);
                k = fx_mul((a < 0 ? -a : a) + (b < 0 ? -b : b), 0x10000);
                if (k > 0x10000) k = 0x10000;
                else if (k <= 0x4ccc) continue;
            } else if (k <= 0x4ccc)
                continue;
            int32_t nrm_[3] = {CI32(c, 0x4a4 + 12 * i), CI32(c, 0x4a8 + 12 * i), CI32(c, 0x4ac + 12 * i)};
            int32_t rows[9], x[3], y[3];
            scrape_rows(rows, c, nrm_);
            sc_cross(x, rows, nrm_);
            sc_unit(x);
            sc_cross(y, x, rows);
            sc_unit(y);
            for (int q = 0; q < 3; q++) rows[3 + q] = y[q], rows[6 + q] = x[q];
            int32_t m[12], p[3];
            for (uint32_t q = 0; q < 12; q++) m[q] = CI32(c, 4 * q);
            if (wheel) {
                int32_t w[3] = {CI32(c, 0x3c0 + 12 * i), CI32(c, 0x3c4 + 12 * i) - 0x5999, CI32(c, 0x3c8 + 12 * i)};
                for (int q = 0; q < 3; q++) p[q] = fx_mul(m[8 + q], w[2]) + fx_mul(m[4 + q], w[1]) + fx_mul(m[q], w[0]);
            } else
                p[0] = fx_mul(m[1], -0x8000), p[1] = fx_mul(m[5], -0x8000), p[2] = fx_mul(m[9], -0x8000);
            Sparks_Add(k, p, c, rows, wheel ? 0 : 0x90000, wheel ? 0 : 0x9999);
        }
        bottom_out(c);
    }
}
