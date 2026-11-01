/* The cars' shadows (see shadow.h). */
#include "game/shadow.h"
#include "exe.h"
#include "fixmath.h"
#include "game/car_load.h"
#include "game/gstate.h"
#include "game/race.h"
#include "render/d3d7.h"
#include "render/polyq.h"
#include "render/tex.h"
#include "sim/pose.h"
#include "world/hpc.h"
#include "world/relight.h"
#include "world/stage.h"
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/* A car's record (0x2a4 bytes): the points are world positions, 16.16 */
typedef struct {
    int32_t wheel[4][4][3];   /* +0x000: each wheel's patch, ends A, B (one side), C, D (the other) */
    int32_t pt[18][3];        /* +0x0c0: the body's quad (0-3), then the outline (4 on) */
    int32_t front[4][3];      /* +0x198: each wheel patch's front middle (for the skid marks) */
    int32_t rear[4][3];       /* +0x1c8: its rear middle */
    int32_t pt_y[18];         /* +0x1f8: the points' last ground heights */
    int32_t wheel_y[4];       /* +0x240: the wheels' */
    int32_t scale;            /* +0x250: the outline's inner ring, 0x9999 (0x4984b0, not ported) */
    int32_t wheel_a[4];       /* +0x254: each wheel patch's strength */
    int16_t wheel_tri[4];     /* +0x264: the ground triangles, kept between calls */
    int16_t pt_tri[18];       /* +0x26c */
    int16_t corner;           /* +0x290: the quad corner the outline starts from */
    int16_t count;            /* +0x292: the points in use, 4 or the profile's + 5 */
    int32_t box_only;         /* +0x294: the quad from the box's corners, no outline */
    int32_t wheels;           /* +0x298: the wheel patches */
    int32_t first_shift;      /* +0x29c: the outline from point 5 (its extra point at 4) */
    int32_t forward;          /* +0x2a0: the outline in profile order */
} Rec;
_Static_assert(sizeof(Rec) == 0x2a4, "the shadow record");
_Static_assert(offsetof(Rec, front) == 0x198 && offsetof(Rec, scale) == 0x250 && offsetof(Rec, corner) == 0x290, "the shadow record");

static Rec *recs;   /* 0x592734 */
static int count;   /* 0x592738 */
/* the car's .cin section 1 (0x494bb0): its point count, the damage range (first, count) of the
   outline's points, the profile's side offset and the wheel patch's half length, then the profile's
   (along, across) pairs */
static const uint8_t *prof[CAR_MAX];   /* 0x592278, 0x5922bc, 0x592344, 0x59229c, 0x5922fc */
/* 0x5201c8: the sun's horizontal direction (x, z, unit and reversed) and the outline's length */
static int32_t sun_x, sun_z, sun_len;
static bool sun_set;

static int32_t ri32(const uint8_t *p) { return (int32_t)((uint32_t)p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24); }
static int32_t pair(int car, int i) { return ri32(prof[car] + 0xc + 4 * i); }
static int32_t dot(const int32_t a[3], const int32_t b[3]) { return fx_mul(a[2], b[2]) + fx_mul(a[0], b[0]) + fx_mul(a[1], b[1]); }
static int32_t iabs(int32_t v) { return v < 0 ? -v : v; }
/* Vec_Mul3 0x4b9c20: a row vector times a matrix's 3 x 3 */
static void mul3(int32_t o[3], const int32_t v[3], const int32_t *m)
{
    int32_t x = v[0], y = v[1], z = v[2];
    for (int k = 0; k < 3; k++) o[k] = fx_mul(x, m[k]) + fx_mul(y, m[4 + k]) + fx_mul(z, m[8 + k]);
}

void Shadow_Alloc(int n)
{
    /* (the original allocates again on each load without freeing; the port frees the old block) */
    free(recs);
    recs = calloc(n > 0 ? (size_t)n : 1, sizeof *recs);
    count = n;
    if (!sun_set) {
        sun_x = (int32_t)exe_u32(0x5201c8), sun_z = (int32_t)exe_u32(0x5201d0), sun_len = (int32_t)exe_u32(0x5201d4);
        sun_set = true;
    }
}

void Shadow_Free(void)
{
    free(recs);
    recs = NULL;
    count = 0;
}

uint8_t *Shadow_Get(int car) { return recs ? (uint8_t *)(recs + car) : NULL; }

void Shadow_SunDirection(const int32_t d[3])
{
    sun_x = d[0], sun_z = d[2];
    uint32_t l = Fx_Sqrt((uint32_t)fx_mul(sun_x, sun_x) + (uint32_t)fx_mul(sun_z, sun_z));
    if ((int32_t)l > 0) {
        int32_t r = (int32_t)(0x100000000LL / -(int64_t)l);
        sun_x = fx_mul(sun_x, r), sun_z = fx_mul(sun_z, r);
    }
    sun_len = fx_mul(0x10000 - d[1], 0x20000);
    sun_set = true;
}

void Shadow_Light(int car, int32_t v)
{
    if (!recs || car < 0 || car >= count) return;
    recs[car].scale = v;
    uint8_t a = (uint8_t)(((int64_t)v * 0x4b0000) >> 32);
    /* 0x4ae140 stores (0, 0, 0, a) at 0x520f94, which nothing reads */
    D3d_SetRenderState(D3DRS_TEXTUREFACTOR, (uint32_t)(uint8_t)(a + 100) << 24);   /* 0x4a3df0 */
}

void Shadow_Place(void)
{
    uint32_t m = Gs_Mode();
    for (int i = 0; i < count; i++) {
        Rec *r = &recs[i];
        if (m == 5 || m == 6 || m == 7) {
            r->box_only = 1, r->wheels = 1;
            if (i == 0) {
                r->box_only = 0;
                if (Gs_SelBits14() == 1) r->wheels = 1;   /* (already set) */
            }
        } else r->box_only = 0, r->wheels = 1;
        r->scale = 0x9999;
        for (int w = 0; w < 4; w++) r->wheel_a[w] = 0x10000;
    }
    for (int i = 0; i < count && i < CAR_MAX; i++) prof[i] = Car_CinSection(i, 1);
}

/* the frame's pose, as 0x496e00 leaves it in globals for its helpers */
static const int32_t *P;             /* 0x5922ec: the pose record (main matrix, normal +0xe0, heights +0xec) */
static const int32_t *WM;            /* 0x592488 */
static const int32_t *BOX;           /* 0x592298 */
static int32_t R0[3], R1[3], R2[3], POS[3], GND[3];   /* 0x592320, 0x59232c, 0x592338, 0x5922f0, 0x5922e0 */
static int32_t AX[3], SIDE[3], EXT_F, EXT_S;          /* 0x592468, 0x592474, 0x592480, 0x592484 */

/* 0x497db0: each wheel still on, a patch of the wheel's length and 0.35 wide on the ground under it
   (laid on the ground's plane), fading as the wheel rises and as the car tilts off the ground */
static void wheels(const Car *c, Rec *r)
{
    int n = CI8(c, 0xb1a);
    bool air = CI32(c, 0xb64) != 0;
    int32_t tilt = 0x10000;
    if (!air) {
        int32_t t = dot(&P[56], R1) - 0xcccc;   /* (the normal first, as the original) */
        if (t < 0) t = 0;
        t = fx_mul(t, 0x50000);
        if (t <= 0x10000) tilt = t;
    }
    int32_t down[3];
    for (int k = 0; k < 3; k++) down[k] = fx_mul(R1[k], -0x5999);
    if (Gs_Option(6))
        for (int k = 0; k < 3; k++) down[k] = fx_mul(down[k], 0x28000);
    for (int w = 0; w < 4; w++) {
        if (!c->wheel[w] || c->wheel[w]->parent == &g_world) continue;
        const int32_t *m = WM + 16 * w;
        int32_t o[3], ctr[3], nrm[3] = {0, 0x10000, 0};
        mul3(ctr, &m[12], P);
        for (int k = 0; k < 3; k++) ctr[k] += POS[k] + down[k];
        int32_t a = 0x10000;
        if (!air) {
            /* Car_PointGround 0x4932b0: the ground, its surface (unused here) */
            uint16_t surf;
            int32_t g = Hpc_Ground(ctr, nrm, &r->wheel_tri[w], &surf, r->wheel_y[w]);
            r->wheel_y[w] = g;
            int32_t h = ctr[1] - g;
            ctr[1] = g;
            if (h >= 1) {
                int32_t f = fx_mul(h, 0x40000);
                a = 0x10000 - (f > 0x10000 ? 0x10000 : f);
            }
        }
        r->wheel_a[w] = fx_mul(tilt, a);
        int32_t f[3], s[3];
        mul3(f, &m[8], P);
        s[0] = fx_mul(fx_mul(R1[1], f[2]) - fx_mul(R1[2], f[1]), 0x5999);
        s[1] = fx_mul(fx_mul(R1[2], f[0]) - fx_mul(R1[0], f[2]), 0x5999);
        s[2] = fx_mul(fx_mul(R1[0], f[1]) - fx_mul(R1[1], f[0]), 0x5999);
        int32_t len = ri32(prof[n] + 8);
        for (int k = 0; k < 3; k++) f[k] = fx_mul(f[k], len);
        if (Pose_Narrow(c, 1))
            for (int k = 0; k < 3; k++) f[k] = fx_mul(f[k], 0x9999);
        int32_t(*p)[3] = r->wheel[w];
        for (int k = 0; k < 3; k++) {
            p[0][k] = ctr[k] + s[k] + f[k], p[1][k] = ctr[k] + s[k] - f[k];
            p[2][k] = ctr[k] - s[k] + f[k], p[3][k] = ctr[k] - s[k] - f[k];
        }
        if (!air)
            for (int i = 0; i < 4; i++) {
                for (int k = 0; k < 3; k++) o[k] = p[i][k] - ctr[k];
                int32_t d = dot(nrm, o);
                for (int k = 0; k < 3; k++) p[i][k] = o[k] - fx_mul(nrm[k], d) + ctr[k];
            }
    }
}

/* 0x4962c0: the body's quad. On the ground: the car's forward row laid on the ground (its up row when
   the forward one is nearly upright), the side across it, and the box's reach along both; in the air
   the car's own rows and the corners at +0x270. A car with box_only takes its box's corners at the
   pose's heights, pulled in or out by the model's two scales when it drives another class. */
static void quad(const Car *c, Rec *r)
{
    int n = CI8(c, 0xb1a);
    uint8_t model = Car_Slot(n)->model;
    EXT_F = EXT_S = 0;
    if (!r->box_only || CI32(c, 0xc00)) {
        if (!CI32(c, 0xb64)) {
            const int32_t *nrm = &P[56];
            int32_t d = dot(R0, nrm);
            const int32_t *row = iabs(d) < 0xfd71 ? R0 : R1;
            if (row == R1) d = dot(R1, nrm);
            for (int k = 0; k < 3; k++) AX[k] = row[k] - fx_mul(nrm[k], d);
            Vec_Unit(AX);
            SIDE[0] = fx_mul(AX[1], nrm[2]) - fx_mul(AX[2], nrm[1]);
            SIDE[1] = fx_mul(AX[2], nrm[0]) - fx_mul(AX[0], nrm[2]);
            SIDE[2] = fx_mul(AX[0], nrm[1]) - fx_mul(AX[1], nrm[0]);
            for (int i = 0; i < 8; i++) {
                int32_t v[3] = {BOX[3 * i] - POS[0], BOX[3 * i + 1] - POS[1], BOX[3 * i + 2] - POS[2]};
                int32_t f = dot(AX, v), s = dot(SIDE, v);
                if (f > 0 && EXT_F < f) EXT_F = f;
                if (s > 0 && EXT_S < s) EXT_S = s;
            }
            for (int k = 0; k < 3; k++) {
                int32_t x = fx_mul(AX[k], EXT_F), z = fx_mul(SIDE[k], EXT_S);
                r->pt[1][k] = z + x, r->pt[0][k] = x - z;
                r->pt[2][k] = z - x, r->pt[3][k] = -x - z;
                for (int i = 0; i < 4; i++) r->pt[i][k] += GND[k];
            }
            return;
        }
        for (int k = 0; k < 3; k++) {
            AX[k] = CI32(c, 0x360 + 4 * (uint32_t)k), SIDE[k] = CI32(c, 0x378 + 4 * (uint32_t)k);
        }
        EXT_F = CI32(c, 0x204), EXT_S = CI32(c, 0x20c);
        int32_t ey = CI32(c, 0x208);
        for (uint32_t k = 0; k < 3; k++) {
            int32_t off = GND[k] - CI32(c, 0x2d0 + 4 * k) + fx_mul(CI32(c, 0x36c + 4 * k), ey);
            for (uint32_t i = 0; i < 4; i++) r->pt[i][k] = CI32(c, 0x270 + 12 * i + 4 * k) + off;
        }
        for (int k = 0; k < 3; k++) {
            int32_t t = r->pt[2][k];
            r->pt[2][k] = r->pt[3][k], r->pt[3][k] = t;
        }
        return;
    }
    static const int map[4] = {1, 0, 2, 3};   /* the quad's corners from the box's */
    for (int i = 0; i < 4; i++) memcpy(r->pt[i], &BOX[3 * map[i]], sizeof r->pt[i]);
    if (!CI32(c, 0xb64)) r->pt[1][1] = P[59], r->pt[0][1] = P[60], r->pt[2][1] = P[61], r->pt[3][1] = P[62];
    if (model == c->s[0xb1b]) return;
    /* the pairs (bytes at 0x5201c0) and the per-model scales (0x520150 the first two, 0x520188) */
    for (int j = 0; j < 4; j++) {
        int a = (int)exe_u16(0x5201c0 + 2u * (uint32_t)j) & 0xff, b = exe_u16(0x5201c0 + 2u * (uint32_t)j) >> 8;
        int32_t s = (int32_t)exe_u32((j < 2 ? 0x520150u : 0x520188u) + 4u * model);
        for (int k = 0; k < 3; k++) {
            int32_t h = fx_mul(r->pt[a][k] - r->pt[b][k], 0x8000), mid = r->pt[b][k] + h;
            h = fx_mul(h, s);
            r->pt[a][k] = h + mid, r->pt[b][k] = mid - h;
        }
    }
}

/* the angle of the 16.16 cosine d as 0x496e00 takes it, in degrees 16.16 from the side (as glow.c) */
static int32_t side_angle(int32_t d)
{
    int32_t ad = iabs(d);
    int16_t s;
    if (ad <= 0x10000) {
        int64_t k = llrint((double)ad * (1.0 / 65536.0) * -4095.0);
        s = (int16_t)g_asin[-k];
        if (d < 0) s = (int16_t)-s;
    } else s = (int16_t)g_asin[4095];
    return (0x400 - s) * 0x1680;
}

void Shadow_Build(const Car *c)
{
    int n = CI8(c, 0xb1a);
    if (!recs || n < 0 || n >= count || n >= CAR_MAX || !prof[n]) return;
    Rec *r = &recs[n];
    P = Pose_Out(n), WM = Pose_WheelM(n), BOX = Pose_Box(n);
    for (int k = 0; k < 3; k++) R0[k] = P[k], R1[k] = P[4 + k], R2[k] = P[8 + k], POS[k] = P[12 + k];
    const int32_t *nrm = &P[56];
    /* the ground point: the position moved along the normal to the first box corner's plane (its
       height the pose's first) */
    int32_t dist;
    if (!CI32(c, 0xb64)) {
        int32_t v[3] = {BOX[0] - POS[0], P[59] - POS[1], BOX[2] - POS[2]};
        dist = dot(nrm, v);
    } else dist = fx_mul(P[59] - POS[1], nrm[1]);
    for (int k = 0; k < 3; k++) GND[k] = fx_mul(nrm[k], dist) + POS[k];
    if (r->wheels) wheels(c, r);
    quad(c, r);
    const uint8_t *pf = prof[n];
    int N = pf[0], first = 0;
    int32_t a14 = 0;
    if (r->box_only || (g_render_opts & 0x20)) r->count = 4;
    else {
        /* the outline: the profile's points along the car's length, pushed sideways and stretched
           away from the sun */
        int32_t p0 = pair(n, 0);
        int32_t k24 = p0 ? (int32_t)(((int64_t)EXT_F << 16) / p0) : 0;
        int32_t hx = 0, hz = 0;
        uint32_t l = Fx_Sqrt((uint32_t)fx_mul(AX[0], AX[0]) + (uint32_t)fx_mul(AX[2], AX[2]));
        if (l) {
            int32_t q = (int32_t)(0x100000000LL / (int32_t)l);
            hx = fx_mul(AX[0], q), hz = fx_mul(AX[2], q);
        }
        int32_t cs = fx_mul(hz, sun_z) + fx_mul(hx, sun_x);
        int32_t t = dot(R1, nrm) - 0xcccc, up;
        if (t < 0) up = 0;
        else {
            up = fx_mul(t, 0x4ffec);
            if (up > 0x10000) up = 0x10000;
        }
        int32_t a18 = fx_mul(cs, sun_len);
        a14 = fx_mul(fx_mul(hx, sun_z) + fx_mul(-hz, sun_x), sun_len);
        int32_t a34 = fx_mul(a14, up);
        int32_t W = ri32(pf + 4);
        int32_t a28 = fx_mul((a18 < 0 ? pair(n, N * 2 - 1) : pair(n, 1)) + W, a18);
        int32_t a38 = (a14 < 0 ? -EXT_S : EXT_S) + fx_mul(W, a14);
        int shift;
        if (a18 < 0) {
            if (a14 < 0) shift = 1, r->corner = 1;
            else shift = 0, r->corner = 0;
        } else if (a14 < 0) shift = 0, r->corner = 2;
        else shift = 1, r->corner = 3;
        r->first_shift = shift;
        r->forward = a14 >= 0;
        int32_t keep_f = 0, keep_s = 0;
        for (int i = 0; i < N; i++) {
            int32_t a = pair(n, 2 * i), s = fx_mul(pair(n, 2 * i + 1), a34) + a38;
            if (i != 0 && i != N - 1) {
                int32_t d = a18 < 0 ? pair(n, 2 * i + 2) - a : pair(n, 2 * i - 2) - a;
                a += fx_mul(d, iabs(cs));
            }
            int32_t f = fx_mul(a + a28, k24);
            if (a18 < 0 ? i == N - 1 : i == 0) keep_f = f, keep_s = s;
            int idx = (a14 < 0 ? N - i + 3 : i + 4) + shift;
            if ((i == 0 && a14 >= 0) || (i == N - 1 && a14 < 0)) first = idx;
            for (int k = 0; k < 3; k++) r->pt[idx][k] = fx_mul(AX[k], f) + fx_mul(SIDE[k], s) + GND[k];
        }
        /* one more at the kept end, across the car's whole width */
        int32_t w2 = fx_mul(EXT_S, 0x20000);
        keep_s += a14 < 0 ? w2 : -w2;
        int idx = shift ? 4 : N + 4;
        for (int k = 0; k < 3; k++) r->pt[idx][k] = fx_mul(SIDE[k], keep_s) + fx_mul(AX[k], keep_f) + GND[k];
        r->count = (int16_t)(N + 5);
    }
    if (r->box_only) return;
    if (!(g_render_opts & 0x20)) {
        /* the outline relaxed: each point at least 1.5 degrees on from the one before, around the
           ground point, keeping its distance */
        int32_t acc = 0;
        for (int i = a14 < 0 ? N - 1 : 0; i < N && i >= 0; i += a14 < 0 ? -1 : 1) {
            int idx = (i + (int16_t)first) % r->count;
            int32_t v[3] = {r->pt[idx][0] - GND[0], r->pt[idx][1] - GND[1], r->pt[idx][2] - GND[2]};
            int32_t len = (int32_t)Fx_Sqrt((uint32_t)fx_mul(v[0], v[0]) + (uint32_t)fx_mul(v[1], v[1]) + (uint32_t)fx_mul(v[2], v[2]));
            if (len > 0) {
                int32_t q = (int32_t)(0x100000000LL / len);
                for (int k = 0; k < 3; k++) v[k] = fx_mul(v[k], q);
            }
            int32_t ang = side_angle(fx_mul(AX[2], v[2]) + fx_mul(AX[0], v[0]) + fx_mul(AX[1], v[1]));
            if (a14 < 0 ? i != N - 1 : i != 0) {
                acc += 0x18000;
                if (ang < acc) {
                    uint16_t sa = (uint16_t)llrint((double)(a14 < 0 ? -acc : acc) * (1.0 / 5760.0));
                    int32_t sn = g_sin[sa & 0xfff];
                    uint16_t ca = (uint16_t)llrint((double)acc * (-1.0 / 5760.0));
                    int32_t cn = g_sin[(uint16_t)(0x400 - ca) & 0xfff];
                    int32_t d[3];
                    for (int k = 0; k < 3; k++) d[k] = fx_mul(AX[k], cn) + fx_mul(SIDE[k], sn);
                    Vec_Unit(d);
                    for (int k = 0; k < 3; k++) r->pt[idx][k] = GND[k] + fx_mul(d[k], len);
                    ang = acc;
                }
            }
            acc = ang;
        }
    }
    /* every point dropped onto the ground, at most half a unit a frame */
    for (int i = 0; i < r->count; i++) {
        int32_t gn[3];
        uint16_t surf;
        int32_t h = Hpc_Ground(r->pt[i], gn, &r->pt_tri[i], &surf, r->pt_y[i]);
        r->pt_y[i] = h;
        int32_t d = h - r->pt[i][1];
        if (iabs(d) > 0x8000) d = d < 1 ? -0x8000 : 0x8000;
        r->pt[i][1] += d;
    }
}

/* 0x495f50: the points (and the wheels' when on) a unit toward the camera when farther than 2 */
static void toward(const Obj *cam, Rec *r)
{
    const int32_t *e = &cam->local.m[12];
    int32_t(*pts[2])[3] = {r->pt, r->wheel[0]};
    int nn[2] = {r->count, r->wheels ? 16 : 0};
    for (int s = 0; s < 2; s++)
        for (int i = 0; i < nn[s]; i++) {
            int32_t *p = pts[s][i];
            int32_t d[3] = {e[0] - p[0], e[1] - p[1], e[2] - p[2]};
            uint32_t l = Fx_Sqrt((uint32_t)fx_mul(d[0], d[0]) + (uint32_t)fx_mul(d[1], d[1]) + (uint32_t)fx_mul(d[2], d[2]));
            if ((int32_t)l <= 0x20000 || !l) continue;
            int32_t q = (int32_t)(0x100000000LL / (int32_t)l);
            for (int k = 0; k < 3; k++) p[k] += fx_mul(fx_mul(d[k], q), 0x10000);
        }
}

/* 0x494d40: outline point i among the damage range (those a damaged car leaves out) */
static bool damaged(int n, const Rec *r, int i)
{
    if (r->first_shift) i--;
    if (!r->forward) i = prof[n][0] - i - 1;
    i -= prof[n][1];
    return i >= 0 && i < prof[n][2];
}

static uint8_t rgb[3];
static void vert(PolyVert *v, const int32_t p[3], uint8_t a)
{
    v->x = p[0], v->y = p[1], v->z = p[2];
    v->rgba[0] = rgb[0], v->rgba[1] = rgb[1], v->rgba[2] = rgb[2], v->rgba[3] = a;
    v->u = v->v = 0;
}
static void tri(const int32_t *p0, uint8_t a0, const int32_t *p1, uint8_t a1, const int32_t *p2, uint8_t a2)
{
    PolyVert a, b, c;
    vert(&a, p0, a0), vert(&b, p1, a1), vert(&c, p2, a2);
    Poly_Add(&a, &b, &c, NULL, 10);
}
static void mid(int32_t o[3], const int32_t a[3], const int32_t b[3])
{
    for (int k = 0; k < 3; k++) o[k] = fx_mul(a[k] - b[k], 0x8000) + b[k];
}

void Shadow_Draw(const Car *c, int view)
{
    int n = CI8(c, 0xb1a);
    if (!recs || n < 0 || n >= count || n >= CAR_MAX || !prof[n]) return;
    Rec *live = &recs[n];
    int L = prof[n][0];
    bool whole = true;
    if (ri32(Damage_Get(n) + 0x4bc)) whole = false, L -= prof[n][2];
    /* 0x4b4a80: the shadow colour (R, G, B) and strength */
    rgb[0] = (uint8_t)g_shadow_rgb, rgb[1] = (uint8_t)(g_shadow_rgb >> 8), rgb[2] = (uint8_t)(g_shadow_rgb >> 16);
    int32_t strength = g_shadow_alpha;
    Rec r = *live;   /* 0x592490 */
    const Obj *cam = Race_Camera(view);
    if (cam) toward(cam, &r);
    if (r.wheels)
        for (int w = 0; w < 4; w++) {
            if (!c->wheel[w] || c->wheel[w]->parent == &g_world || !r.wheel_a[w]) continue;
            /* the ends' middles for the skid marks, from the record as built */
            mid(live->front[w], live->wheel[w][0], live->wheel[w][2]);
            mid(live->rear[w], live->wheel[w][1], live->wheel[w][3]);
            int32_t(*p)[3] = r.wheel[w], f[3], b[3];
            mid(f, p[0], p[2]);
            mid(b, p[1], p[3]);
            uint8_t a = (uint8_t)(((int64_t)r.wheel_a[w] * 0xe60000) >> 32);
            tri(p[0], 0, p[1], 0, f, a);
            tri(f, a, p[1], 0, b, a);
            tri(f, a, b, a, p[2], 0);
            tri(b, a, p[3], 0, p[2], 0);
        }
    int32_t ctr[3];
    mid(ctr, r.pt[0], r.pt[2]);
    if (!(g_render_opts & 0x20) && !r.box_only) {
        /* the outline: two quad corners, the outline's points (a damaged car's range left out), the
           other two corners; an inner ring toward the centre, solid, fading to the outer */
        int32_t o[24][3], in[24][3];
        int m = 2;
        memcpy(o[0], r.pt[r.corner], sizeof o[0]);
        memcpy(o[1], r.pt[(r.corner + 1) & 3], sizeof o[1]);
        for (int i = 0; i < prof[n][0] + 1; i++)
            if ((whole || !damaged(n, &r, i)) && m < 22) memcpy(o[m++], r.pt[4 + i], sizeof o[0]);
        if (L + 4 < 24 && L >= 0) {
            memcpy(o[L + 3], r.pt[(r.corner + 3) & 3], sizeof o[0]);
            memcpy(o[L + 4], r.pt[(r.corner + 2) & 3], sizeof o[0]);
        }
        int K = (int16_t)(L + 4);
        if (K < 0 || K > 23) K = 0;
        int32_t sc = 0xfd70 - fx_mul(0x10000 - r.scale, 0x23d7);
        for (int j = 0; j <= K; j++)
            for (int k = 0; k < 3; k++) in[j][k] = ctr[k] + fx_mul(o[j][k] - ctr[k], sc);
        uint8_t a = (uint8_t)(((int64_t)strength * CI32(c, 0xa70)) >> 32);
        for (int j = 0; j < K; j++) {
            int i = (j + 1) % K;   /* (the last point, K, never drawn: the original's) */
            tri(in[j], a, ctr, a, in[i], a);
            tri(o[j], 0, in[j], a, o[i], 0);
            tri(o[i], 0, in[j], a, in[i], a);
        }
    }
    /* the body's quad, its corners cut: solid inside, fading to the edge; a car on the ground darker
       the lower its lowest corner (a box-only car fully) */
    int32_t o[8][3], in[8][3];
    for (int k = 1; k <= 4; k++) {
        const int32_t *a = r.pt[k - 1], *b = r.pt[k & 3];
        for (int q = 0; q < 3; q++) {
            int32_t d = b[q] - a[q];
            o[2 * k - 1][q] = a[q] + fx_mul(d, 0xccc);
            o[(2 * k) & 7][q] = a[q] + fx_mul(d, 0xf333);
        }
    }
    for (int j = 0; j < 8; j++)
        for (int k = 0; k < 3; k++) in[j][k] = ctr[k] + fx_mul(o[j][k] - ctr[k], 0x9999);
    uint8_t a = 0xff;
    if (!r.box_only) {
        int32_t low = CI32(c, 0x274) - CI32(c, 0x8dc);
        for (uint32_t j = 1; j < 8; j++) {
            int32_t h = CI32(c, 0x274 + 12 * j) - CI32(c, 0x8dc + 4 * j);
            if (h < low) low = h;
        }
        int32_t k = 0x10000;
        if (low >= 1) {
            int32_t f = fx_mul(low, 0x20000);
            k = 0x10000 - (f > 0x10000 ? 0x10000 : f);
        }
        a = (uint8_t)(((int64_t)k * 0xff0000) >> 32);
    }
    for (int j = 0; j < 8; j++) {
        int i = (j + 1) & 7;
        tri(in[i], a, in[j], a, ctr, a);
        tri(o[j], 0, in[j], a, o[i], 0);
        tri(in[j], a, in[i], a, o[i], 0);
    }
}
