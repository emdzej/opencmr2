/* Car_Suspension 0x43b100, ported from the disassembly, with Car_LoadSplit 0x43c640: two body-sway
   springs (+0x6e4 against +0x9b8, +0x6f0 against +0x9bc) driven by the step's change of velocity in
   the car's frame, a frame from each sway that tilts the box's four lower corners into the wheels'
   offsets (+0x998, +0x9a8, plus the damage record's +0x3dc), and the wheels' loads (+0x8b8). */
#include "sim/suspension.h"
#include "fixmath.h"
#include "game/gstate.h"
#include <math.h>

static void v3load(int32_t v[3], const Car *c, uint32_t o) { for (uint32_t k = 0; k < 3; k++) v[k] = CI32(c, o + 4 * k); }
static int32_t dot3(const int32_t a[3], const int32_t b[3]) { return fx_mul(a[0], b[0]) + fx_mul(a[1], b[1]) + fx_mul(a[2], b[2]); }
static int32_t len3(const int32_t v[3]) { return (int32_t)Fx_Sqrt((uint32_t)fx_mul(v[0], v[0]) + (uint32_t)fx_mul(v[1], v[1]) + (uint32_t)fx_mul(v[2], v[2])); }
static int32_t inv(int32_t v) { return (int32_t)(0x100000000LL / v); }
static int32_t iabs(int32_t v) { return v < 0 ? -v : v; }
static int32_t clamp170a(int32_t v) { return iabs(v) > 0x170a ? (v <= 0 ? -0x170a : 0x170a) : v; }

/* 0x4b9c20: v times the 3 x 3 of a 16-int matrix (row vector on the left) */
static void vmul(int32_t o[3], const int32_t v[3], const int32_t m[16])
{
    for (int k = 0; k < 3; k++) o[k] = fx_mul(m[8 + k], v[2]) + fx_mul(m[4 + k], v[1]) + fx_mul(m[k], v[0]);
}

/* One sway spring: the spring pull, plus the drive, times 1.2 dt; its x and z limited to `lim`. */
static void sway(Car *c, uint32_t o, uint32_t stiff, const int32_t drive[3], int32_t lim)
{
    int32_t s[3], spring[3] = {0, 0, 0};
    v3load(s, c, o);
    int32_t l = len3(s);
    if (l > 0) {
        int32_t k = -fx_mul(l, CI32(c, stiff)), r = inv(l);
        for (int q = 0; q < 3; q++) spring[q] = fx_mul(fx_mul(s[q], r), k);
    }
    int32_t a[3], dt = fx_mul(gms32(0x519c8c), 0x13333);
    for (int q = 0; q < 3; q++) a[q] = fx_mul(drive[q] + spring[q], dt);
    int32_t t[3] = {a[0], 0, a[2]}, tl = len3(t);
    if (tl > lim) {
        int32_t r = inv(tl);
        for (int q = 0; q < 3; q++) t[q] = fx_mul(fx_mul(t[q], r), lim);
        a[0] = t[0];
        a[2] = t[2];
    }
    for (uint32_t q = 0; q < 3; q++) CW32(c, o + 4 * q, CI32(c, o + 4 * q) + a[q]);
}

/* the frame of a sway: up from it (x and z within 0.09), forward square to it, side as their cross */
static void frame(const Car *c, uint32_t o, int32_t m[16])
{
    int32_t u[3] = {clamp170a(CI32(c, o)), 0x10000, clamp170a(CI32(c, o + 8))};
    Vec_Unit(u);
    int32_t x[3] = {0x10000, 0, 0}, p = dot3(u, x);
    for (int q = 0; q < 3; q++) x[q] -= fx_mul(u[q], p);
    Vec_Unit(x);
    int32_t z[3] = {fx_mul(x[1], u[2]) - fx_mul(x[2], u[1]), fx_mul(x[2], u[0]) - fx_mul(x[0], u[2]), fx_mul(x[0], u[1]) - fx_mul(x[1], u[0])};
    Vec_Unit(z);
    for (int q = 0; q < 16; q++) m[q] = 0;
    for (int q = 0; q < 3; q++) m[q] = x[q], m[4 + q] = u[q], m[8 + q] = z[q];
}

/* the four lower corners (+-half length, 0, +-half width) through the frame: their y, plus the damage */
static void offsets(Car *c, uint32_t o, const int32_t m[16])
{
    int32_t h = CI32(c, 0x204), w = CI32(c, 0x20c);
    const int32_t p[4][3] = {{h, 0, w}, {h, 0, -w}, {-h, 0, w}, {-h, 0, -w}};
    const uint8_t *d = Damage_Get(CI8(c, 0xb1a));
    for (uint32_t i = 0; i < 4; i++) {
        int32_t t[3];
        vmul(t, p[i], m);
        CW32(c, o + 4 * i, t[1] + DI32(d, 0x3dc + 4 * i));
    }
}

/* 0x4adb10: the rotation matrix for an angle about a unit axis (overwriting m) */
static void axis_matrix(int32_t m[16], const int32_t a[3], int32_t ang)
{
    int32_t s = g_sin[-ang & 0xfff], co = g_sin[(-ang + 0x400) & 0xfff], k = 0x10000 - co;
    int32_t xx = fx_mul(a[0], a[0]), yy = fx_mul(a[1], a[1]), zz = fx_mul(a[2], a[2]);
    int32_t xy = fx_mul(fx_mul(a[0], a[1]), k), xz = fx_mul(fx_mul(a[0], a[2]), k), yz = fx_mul(fx_mul(a[2], a[1]), k);
    m[0] = xx + fx_mul(co, 0x10000 - xx);
    m[1] = xy - fx_mul(a[2], s);
    m[2] = xz + fx_mul(a[1], s);
    m[3] = 0;
    m[4] = xy + fx_mul(a[2], s);
    m[5] = yy + fx_mul(co, 0x10000 - yy);
    m[7] = 0;
    m[6] = yz - fx_mul(a[0], s);
    m[8] = xz - fx_mul(a[1], s);
    m[9] = fx_mul(a[0], s) + yz;
    m[11] = m[12] = m[13] = m[14] = 0;
    m[10] = fx_mul(co, 0x10000 - zz) + zz;
    m[15] = 0x10000;
}

/* Car_LoadSplit 0x43c640 (all four wheels down): the offsets scaled to at most 0.25, each wheel's
   load a quarter of the mass less +0x8d8 times (4 x offset + +0x808); with the damage record's +0x4bc
   set, the loads drawn towards half the mass on the front pair with the speed */
static void load_split(Car *c)
{
    int32_t q = CI32(c, 0x75c) / 4, m = 0;
    for (uint32_t i = 0; i < 4; i++) {
        int32_t v = iabs(CI32(c, 0x998 + 4 * i));
        if (m < v) m = v;
    }
    if (m > 0x4000) {
        int32_t s = 0x40000000 / m;
        for (uint32_t i = 0; i < 4; i++) CW32(c, 0x998 + 4 * i, fx_mul(CI32(c, 0x998 + 4 * i), s));
    }
    for (uint32_t i = 0; i < 4; i++)
        CW32(c, 0x8b8 + 4 * i, q - fx_mul(CI32(c, 0x8d8), fx_mul(CI32(c, 0x998 + 4 * i), 0x40000) + CI32(c, 0x808 + 4 * i)));
    if (!DI32(Damage_Get(CI8(c, 0xb1a)), 0x4bc)) return;   /* 0x469bc0 (car, 3) */
    int32_t f = fx_mul(CI32(c, 0x778), 0x2000);
    if (f > 0x10000) f = 0x10000;
    int32_t half = CI32(c, 0x75c) / 2;
    for (uint32_t i = 0; i < 4; i++) {
        int32_t l = CI32(c, 0x8b8 + 4 * i);
        CW32(c, 0x8b8 + 4 * i, l + fx_mul(f, (i < 2 ? half : 0) - l));
    }
}

void Car_Suspension(Car *c)
{
    int32_t d[3] = {0, 0, 0}, w[3] = {0, 0, 0};
    if (c->s[0xb28] && CI32(c, 0xb74)) {
        int32_t dv[3], m[12];
        for (uint32_t q = 0; q < 3; q++) dv[q] = CI32(c, 0x414 + 4 * q) - CI32(c, 0x408 + 4 * q);
        for (uint32_t i = 0; i < 12; i++) m[i] = CI32(c, 4 * i);
        for (int r = 0; r < 3; r++) d[r] = fx_mul(m[4 * r + 2], dv[2]) + fx_mul(m[4 * r + 1], dv[1]) + fx_mul(m[4 * r], dv[0]);
        int32_t l = len3(d);
        d[1] = 0;
        if (!l || CI32(c, 0xb60)) d[0] = d[1] = d[2] = 0;
        for (int q = 0; q < 3; q++) w[q] = fx_mul(d[q], gms32(0x519c90));
    }
    int32_t ma[16], mb[16];
    sway(c, 0x6e4, 0x9b8, d, 0x312);
    frame(c, 0x6e4, ma);
    offsets(c, 0x998, ma);
    w[2] = fx_mul(w[2], 0x20000);
    sway(c, 0x6f0, 0x9bc, w, fx_mul(0x312, gms32(0x519c8c)));
    frame(c, 0x6f0, mb);
    /* at the rev limiter (+0xb1d) in a gear, the second frame twisted about z by 0.2 degrees a count */
    if (c->s[0xb1d] && CI32(c, 0x7bc + 4 * (uint32_t)CI8(c, 0xb1e))) {
        static const int32_t z[3] = {0, 0, 0x10000};
        int32_t a = fx_mul(CI8(c, 0xb1d) << 16, 0x3333);
        int16_t ang = (int16_t)(int64_t)llrint((double)a * (1.0 / 5760.0));
        int32_t rot[16], rows[3][3];
        for (int r = 0; r < 3; r++)
            for (int q = 0; q < 3; q++) rows[r][q] = mb[4 * r + q];
        axis_matrix(rot, z, ang);
        for (int r = 0; r < 3; r++) {
            int32_t t[3];
            vmul(t, rows[r], rot);
            Vec_Unit(t);
            for (int q = 0; q < 3; q++) rows[r][q] = t[q];
        }
        for (int r = 0; r < 3; r++)
            for (int q = 0; q < 3; q++) mb[4 * r + q] = rows[r][q];
    }
    offsets(c, 0x9a8, mb);
    if (c->s[0xb28] == 4) load_split(c);
    else for (uint32_t o = 0x8b8; o <= 0x8d4; o += 4) CW32(c, o, CI32(c, 0x8b4));
}
