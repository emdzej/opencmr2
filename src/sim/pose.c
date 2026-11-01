/* The cars' drawn poses (see pose.h). */
#include "sim/pose.h"
#include "sim/sim.h"
#include "fixmath.h"
#include "game/gstate.h"
#include "game/mem.h"
#include "exe.h"
#include "pending.h"
#include <math.h>
#include <string.h>

/* A pose record, 0xfc bytes as ints */
enum { P_MAIN = 0, P_SECOND = 16, P_WHEEL = 32, P_NORMAL = 56, P_HEIGHT = 59, P_INTS = 63 };
static int32_t prev[CAR_MAX][P_INTS], cur[CAR_MAX][P_INTS], out[CAR_MAX][P_INTS];   /* 0x53a3a8, 0x53ad10, 0x53b560 */
static int32_t wheel_m[CAR_MAX][4][16];   /* 0x53bda0 */
static int32_t box[CAR_MAX][8][3];        /* 0x53c5a0: the car's box corners in the world */
static int16_t spin[CAR_MAX][4];          /* 0x53a230: the wheels' turn */
static uint32_t spinning[CAR_MAX][4];     /* 0x53ac48 */

static int32_t lerp(int32_t a, int32_t b, int32_t k) { return a + fx_mul(b - a, k); }
static void copy34(int32_t *d, const int32_t *s) { for (int r = 0; r < 4; r++) d[4 * r] = s[4 * r], d[4 * r + 1] = s[4 * r + 1], d[4 * r + 2] = s[4 * r + 2]; }

/* 0x4224e0 (as in camera.c): two frames blended, mode 1 (forward, side; up from them) */
static void cross(int32_t o[3], const int32_t a[3], const int32_t b[3])
{
    int32_t x = fx_mul(a[1], b[2]) - fx_mul(a[2], b[1]), y = fx_mul(a[2], b[0]) - fx_mul(a[0], b[2]),
            z = fx_mul(a[0], b[1]) - fx_mul(a[1], b[0]);
    o[0] = x, o[1] = y, o[2] = z;
}
static void blend1(int32_t *o, const int32_t *a, const int32_t *b, int32_t k)
{
    int32_t f[3], s[3], u[3], p[3];
    for (int i = 0; i < 3; i++) f[i] = lerp(a[i], b[i], k), s[i] = lerp(a[8 + i], b[8 + i], k), p[i] = lerp(a[12 + i], b[12 + i], k);
    Vec_Unit(s);
    cross(u, s, f);
    Vec_Unit(u);
    cross(f, u, s);
    memset(o, 0, 16 * sizeof *o);
    o[15] = 0x10000;
    for (int i = 0; i < 3; i++) o[i] = f[i], o[4 + i] = u[i], o[8 + i] = s[i], o[12 + i] = p[i];
}

const int32_t *Pose_Out(int n) { return out[n]; }                  /* 0x42ca70 */
const int32_t *Pose_WheelM(int n) { return wheel_m[n][0]; }        /* 0x42cab0 */
const int32_t *Pose_Box(int n) { return box[n][0]; }               /* 0x42cac0 */

/* Car_Shadow 0x487140: the eight corners of the box of half extents e (the car's +0x204) on rows m
   and the position p: with a, b, c the rows 0, 1, 2 times ex, ey, ez, the corners a-b+c, a-b-c,
   -a-b+c, -a-b-c, a+b+c, a+b-c, -a+b+c, -a+b-c */
static void car_box(int32_t o[8][3], const int32_t *m, const Car *c)
{
    static const int8_t sgn[8][3] = {{1, -1, 1}, {1, -1, -1}, {-1, -1, 1}, {-1, -1, -1}, {1, 1, 1}, {1, 1, -1}, {-1, 1, 1}, {-1, 1, -1}};
    int32_t e[3] = {CI32(c, 0x204), CI32(c, 0x208), CI32(c, 0x20c)};
    for (int k = 0; k < 3; k++) {
        int32_t a = fx_mul(m[k], e[0]), b = fx_mul(m[4 + k], e[1]), cc = fx_mul(m[8 + k], e[2]);
        for (int i = 0; i < 8; i++) o[i][k] = sgn[i][0] * a + sgn[i][1] * b + sgn[i][2] * cc + m[12 + k];
    }
}

void Pose_Snap(int car) { gw32(0x53acf0 + 4 * (uint32_t)car, 1); }   /* 0x42c870 */

/* 0x42cae0: the wheels drawn narrow (and with mode 1 the hubs turned): a car on surface 6 at +0xb29
   but for models 8, 10, 11, 13, not with option 6; a car of kind A only with mode 1 */
bool Pose_Narrow(const Car *c, int mode)
{
    uint8_t m = Car_Slot(CI8(c, 0xb1a))->model;
    if (Gs_Option(6) || c->s[0xb29] != 6 || m == 11 || m == 8 || m == 10 || m == 13) return false;
    return Car_Slot(CI8(c, 0xb1a))->kind != 'A' || mode != 0;
}

void Pose_Spin(const CarList *l)
{
    for (int k = l->n - 1; k >= 0; k--) {
        int n = l->car[k];
        const Car *c = Car_Get(n);
        for (uint32_t i = 0; i < 4; i++) {
            int32_t w = fx_mul(CI32(c, 0x860 + 4 * i), gms32(0x519c8c));
            if (!CI32(c, 0xb60) || c->s[0x1d2] || c->s[0x1d3] || 0 < CI32(c, 0x1d8))
                spin[n][i] = (int16_t)(spin[n][i] + (int16_t)llrint((double)w * -(4096.0 / (2.0 * M_PI * 65536.0))));
            spinning[n][i] = (w < 0 ? -w : w) > 0x8000;
        }
    }
}

void Pose_Capture(const CarList *l)
{
    for (int k = l->n - 1; k >= 0; k--) {
        int n = l->car[k];
        Car *c = Car_Get(n);
        memcpy(prev[n], cur[n], sizeof prev[n]);
        if (CI32(c, 0x958)) {
            CW32(c, 0x34, CI32(c, 0x34) + CI32(c, 0x958));
            CW32(c, 0x74, CI32(c, 0x74) + CI32(c, 0x958));
        }
        /* the body raised along its up row by the model's and class's height (0x5199c8, 0x519a00) */
        uint8_t model = Car_Slot(CI8(c, 0xb1a))->model, cls = c->s[0xb1b];
        int32_t h = model == cls ? (int32_t)exe_u32(0x5199c8 + 4u * cls)
                                 : (int32_t)exe_u32(0x519a00 + 4u * model) + (int32_t)exe_u32(0x5199c8 + 4u * cls);
        int32_t up[3] = {fx_mul(CI32(c, 0x10), h), fx_mul(CI32(c, 0x14), h), fx_mul(CI32(c, 0x18), h)};
        if (Gs_Option(6)) PENDING_NOTE(0x42af50, "Pose_Capture (option 6)");
        for (int i = 0; i < 16; i++) {
            if (i % 4 == 3) continue;
            cur[n][P_MAIN + i] = CI32(c, 4 * (uint32_t)i);
            cur[n][P_SECOND + i] = CI32(c, 0x40 + 4 * (uint32_t)i);
        }
        for (uint32_t q = 0; q < 3; q++) cur[n][P_NORMAL + q] = CI32(c, 0x48c + 4 * q);
        for (uint32_t q = 0; q < 4; q++) cur[n][P_HEIGHT + q] = CI32(c, 0x8dc + 4 * q);
        for (int q = 0; q < 3; q++) cur[n][P_MAIN + 12 + q] += up[q], cur[n][P_SECOND + 12 + q] += up[q];
    }
    for (int k = l->n - 1; k >= 0; k--) {
        int n = l->car[k];
        const Car *c = Car_Get(n);
        int16_t steer = (int16_t)(c->s[0xb14] | c->s[0xb15] << 8);
        const uint8_t *d = Damage_Get(n);
        for (uint32_t i = 0; i < 4; i++) {
            int32_t *w = &cur[n][P_WHEEL + 6 * i];
            if (CI32(c, 0xb70)) {
                for (uint32_t q = 0; q < 3; q++) w[q] = CI32(c, 0x3c0 + 12 * i + 4 * q);
                continue;
            }
            /* the angles kept times 5760 (back to units by 1/5760 when drawn): the damage's camber
               (+0x240, times 5), the front wheels' steer (+0xb14), the turn */
            int16_t camber = (int16_t)llrint((double)fx_mul(DI32(d, 0x240 + 4 * i), 0x50000) * (1.0 / 5760.0));
            w[3] = camber * 0x1680;
            w[4] = i < 2 ? steer * 0x1680 : 0;
            w[5] = spin[n][i] * 0x1680;
            int32_t f = DI32(d, 0x250 + 4 * i) + CI32(c, 0x978 + 4 * i);
            if (f > 0x10000) f = 0x10000;
            w[0] = CI32(c, 0x3c0 + 12 * i) + CI32(c, 0x6fc + 8 * i);
            w[1] = CI32(c, 0x3c4 + 12 * i) + CI32(c, 0x928 + 4 * i) + fx_mul(0x10000 - f, CI32(c, 0x938 + 4 * i)) + CI32(c, 0x700 + 8 * i);
            w[2] = CI32(c, 0x3c8 + 12 * i);
        }
    }
}

void Car_PoseInterp(void)
{
    for (int k = g_all.n - 1; k >= 0; k--) {
        int n = g_all.car[k];
        Car *c = Car_Get(n);
        int32_t f = CI32(c, 0xa90);
        if (gm32(0x53acf0 + 4 * (uint32_t)CI8(c, 0xb1a))) {   /* 0x42c7b0: no blend from the last */
            memcpy(prev[n], cur[n], sizeof prev[n]);
            gw32(0x53acf0 + 4 * (uint32_t)CI8(c, 0xb1a), 0);
        }
        blend1(&out[n][P_SECOND], &prev[n][P_SECOND], &cur[n][P_SECOND], f);
        if (c->second) copy34(c->second->local.m, &out[n][P_SECOND]), c->second->dirty = 1;
        blend1(&out[n][P_MAIN], &prev[n][P_MAIN], &cur[n][P_MAIN], f);
        if (c->main) copy34(c->main->local.m, &out[n][P_MAIN]), c->main->dirty = 1;
        for (int i = 0; i < 4; i++) {
            const int32_t *a = &prev[n][P_WHEEL + 6 * i], *b = &cur[n][P_WHEEL + 6 * i];
            int32_t *o = &out[n][P_WHEEL + 6 * i];
            for (int q = 0; q < 6; q++) o[q] = lerp(a[q], b[q], f);
            /* the right wheels (odd) mirrored: turned half a turn about up */
            uint16_t ang[3];
            const double u = 1.0 / 5760.0;
            if (i & 1) ang[0] = (uint16_t)llrint(-(double)o[3] * u), ang[1] = (uint16_t)llrint((double)(o[4] + 0xb40000) * u), ang[2] = (uint16_t)llrint(-(double)o[5] * u);
            else ang[0] = (uint16_t)llrint((double)o[3] * u), ang[1] = (uint16_t)llrint((double)o[4] * u), ang[2] = (uint16_t)llrint((double)o[5] * u);
            int32_t m[9] = {0x10000, 0, 0, 0, 0x10000, 0, 0, 0, 0x10000};
            Rows_Turn3(m, 3, ang);
            /* a fixed camber of 0x1c units, outwards on both sides */
            int32_t cs = g_sin[(i & 1) ? 0x41c : 0x3e4], sn = g_sin[(i & 1) ? 0x1c : 0xfe4];
            for (int r = 0; r < 3; r++) {
                int32_t y = m[3 * r + 1], z = m[3 * r + 2];
                y = fx_mul(y, cs) - fx_mul(z, sn);
                m[3 * r + 1] = y;
                m[3 * r + 2] = fx_mul(z, cs) + fx_mul(y, sn);
            }
            if (Pose_Narrow(c, 0))
                for (int q = 6; q < 9; q++) m[q] = fx_mul(m[q], 0x9999);
            if (Gs_Option(6))
                for (int q = 0; q < 9; q++) m[q] = fx_mul(m[q], 0x28000);
            int32_t *w = wheel_m[n][i];
            memset(w, 0, 16 * sizeof *w);
            for (int r = 0; r < 3; r++) w[4 * r] = m[3 * r], w[4 * r + 1] = m[3 * r + 1], w[4 * r + 2] = m[3 * r + 2];
            w[12] = o[0], w[13] = o[1], w[14] = o[2], w[15] = 0x10000;
            if (c->wheel[i]) copy34(c->wheel[i]->local.m, w), c->wheel[i]->dirty = 1;
            PENDING_NOTE(0x42bf70, "Car_PoseInterp (the hubs at +0x728)");
        }
        /* the normal: the step's (the original adds the whole difference), unit */
        int32_t nn[3];
        for (int q = 0; q < 3; q++) nn[q] = prev[n][P_NORMAL + q] + (cur[n][P_NORMAL + q] - prev[n][P_NORMAL + q]);
        Vec_Unit(nn);
        memcpy(&out[n][P_NORMAL], nn, sizeof nn);
        for (int q = 0; q < 4; q++) out[n][P_HEIGHT + q] = lerp(prev[n][P_HEIGHT + q], cur[n][P_HEIGHT + q], f);
        car_box(box[n], out[n], c);   /* Car_Shadow 0x487140 */
    }
    PENDING_NOTE(0x42be30, "Car_PoseInterp 0x42be30");
}

void Pose_Init(int n)
{
    for (int k = 0; k < n && k < CAR_MAX; k++) {
        const Car *c = Car_Get(k);
        if (c->second) copy34(&cur[k][P_SECOND], c->second->local.m);
        if (c->main) copy34(&cur[k][P_MAIN], c->main->local.m);
        memset(&cur[k][P_WHEEL], 0, 24 * sizeof(int32_t));
        cur[k][P_NORMAL] = 0, cur[k][P_NORMAL + 1] = 0x10000, cur[k][P_NORMAL + 2] = 0;
        memcpy(prev[k], cur[k], sizeof prev[k]);
        memcpy(out[k], cur[k], sizeof out[k]);
        car_box(box[k], out[k], c);   /* Car_Shadow 0x487140 */
        gw32(0x53acf0 + 4 * (uint32_t)k, 1);
    }
}

/* One plane from three box corners at their travel: a corner, its neighbour across the axle, and
   the centre line at the other axle's mean; the normal made unit and turned up */
static void axle_plane(int32_t n[3], const Car *c, uint32_t a, uint32_t b, uint32_t o, int32_t ha, int32_t hb, int32_t other)
{
    int32_t ax = CI32(c, 0x210 + 12 * a), az = CI32(c, 0x218 + 12 * a);
    int32_t u[3] = {CI32(c, 0x210 + 12 * b) - ax, hb - ha, CI32(c, 0x218 + 12 * b) - az};
    int32_t v[3] = {CI32(c, 0x210 + 12 * o) - ax, other - ha, -az};
    cross(n, u, v);
    Vec_Unit(n);
    if (n[1] < 0) n[0] = -n[0], n[1] = -n[1], n[2] = -n[2];
}

/* 0x432c20 (from the disassembly; the decompiler drops the two planes' sum): the drawn body (+0x40,
   the +0x754 matrix) at the car's rows and position, then tilted on its suspension: each wheel's
   travel (+0x988 + +0x9a8 + +0x9d8, at least -0.1, -0.35 with option 6), the normals of the front
   pair with the rear mean and of the rear pair with the front mean summed, made unit and turned into
   the world as the up row; forward made square to it, side across; raised by the mean travel along
   the car's up row */
void Car_BodyPose(Car *c)
{
    int32_t h[4];
    int32_t lo = Gs_Option(6) ? -0x5999 : -0x1999;
    for (uint32_t i = 0; i < 4; i++) {
        h[i] = CI32(c, 0x988 + 4 * i) + CI32(c, 0x9d8 + 4 * i) + CI32(c, 0x9a8 + 4 * i);
        if (h[i] < lo) h[i] = lo;
    }
    int32_t rear = fx_mul(0x8000, h[3] + h[2]), front = fx_mul(0x8000, h[1] + h[0]);
    int32_t n1[3], n2[3], n[3], nw[3], r0[3], r2[3];
    axle_plane(n1, c, 0, 1, 2, h[0], h[1], rear);
    axle_plane(n2, c, 2, 3, 0, h[2], h[3], front);
    for (int q = 0; q < 3; q++) n[q] = n1[q] + n2[q];
    Vec_Unit(n);
    /* 0x4b9c20: the row vector through the orientation's 3 x 3 */
    for (uint32_t q = 0; q < 3; q++)
        nw[q] = fx_mul(CI32(c, 0x20 + 4 * q), n[2]) + fx_mul(CI32(c, 0x10 + 4 * q), n[1]) + fx_mul(CI32(c, 4 * q), n[0]);
    for (uint32_t q = 0; q < 3; q++) r0[q] = CI32(c, 0x360 + 4 * q);
    int32_t d = fx_mul(nw[0], r0[0]) + fx_mul(nw[1], r0[1]) + fx_mul(nw[2], r0[2]);
    for (int q = 0; q < 3; q++) r0[q] -= fx_mul(nw[q], d);
    Vec_Unit(r0);
    cross(r2, r0, nw);
    Vec_Unit(r2);
    int32_t lift = fx_mul(0x8000, front + rear);
    for (uint32_t q = 0; q < 3; q++) {
        CW32(c, 0x40 + 4 * q, r0[q]);
        CW32(c, 0x50 + 4 * q, nw[q]);
        CW32(c, 0x60 + 4 * q, r2[q]);
        CW32(c, 0x70 + 4 * q, fx_mul(CI32(c, 0x36c + 4 * q), lift) + CI32(c, 0x2d0 + 4 * q));
    }
}

/* 0x429810 (in the view's draw, per player): each car's two shells under the drawn body, +0x748 (type
   0xf) 0.1 towards the camera and grown by 0.1 over the distance, +0x74c (type 0xe) 0.5 away and
   shrunk by 0.5 over it: their rows the rest rows scaled, their position the camera's direction in
   the body's frame (the rest position not kept). Beyond 128 units on an axis the direction is taken
   from the vector over 128. */
void Cars_ShellsToCamera(const Obj *camera)
{
    int32_t cam[3] = {camera->local.m[12], camera->local.m[13], camera->local.m[14]};
    for (int k = g_all.n - 1; k >= 0; k--) {
        Car *c = Car_Get(g_all.car[k]);
        if ((!c->part748 && !c->part74c) || !c->second) continue;
        const int32_t *bm = c->second->local.m;
        int32_t v[3] = {bm[12] - cam[0], bm[13] - cam[1], bm[14] - cam[2]};
        int32_t d;
        bool far = false;
        for (int q = 0; q < 3; q++) far |= (v[q] < 0 ? -v[q] : v[q]) > 0x800000;
        if (!far) {
            d = (int32_t)Fx_Sqrt((uint32_t)fx_mul(v[0], v[0]) + (uint32_t)fx_mul(v[1], v[1]) + (uint32_t)fx_mul(v[2], v[2]));
            if (!d) continue;
            int32_t r = (int32_t)(0x100000000LL / d);
            for (int q = 0; q < 3; q++) v[q] = fx_mul(v[q], r);
        } else {
            int32_t s[3];
            for (int q = 0; q < 3; q++) s[q] = v[q] / 128;   /* (rounded towards zero, as the original) */
            d = (int32_t)Fx_Sqrt((uint32_t)fx_mul(s[0], s[0]) + (uint32_t)fx_mul(s[1], s[1]) + (uint32_t)fx_mul(s[2], s[2]));
            if (!d) continue;
            int32_t r = (int32_t)(0x100000000LL / d);
            for (int q = 0; q < 3; q++) v[q] = fx_mul(s[q], r);
            d <<= 7;
        }
        int32_t dir[3];
        for (int r = 0; r < 3; r++) dir[r] = fx_mul(bm[4 * r + 2], v[2]) + fx_mul(bm[4 * r + 1], v[1]) + fx_mul(bm[4 * r], v[0]);
        struct { Obj *o; int32_t off, scale; } p[2] = {
            {c->part748, 0x1999, (int32_t)(((int64_t)0x1999 << 16) / d) + 0x10000},
            {c->part74c, -0x8000, 0x10000 - (int32_t)(((int64_t)0x8000 << 16) / d)},
        };
        for (int i = 0; i < 2; i++) {
            Obj *o = p[i].o;
            if (!o) continue;
            for (int q = 0; q < 3; q++) o->local.m[12 + q] = fx_mul(dir[q], p[i].off);
            for (int r = 0; r < 3; r++)
                for (int q = 0; q < 3; q++) o->local.m[4 * r + q] = fx_mul(o->rest.m[4 * r + q], p[i].scale);
            o->inherit = 0;
            for (Obj *u = o; u; u = u->parent) u->dirty = 1;
        }
    }
}
