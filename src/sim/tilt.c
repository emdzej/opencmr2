/* Car_Tilt 0x4373c0 (named Car_WheelForces before it was read), ported from the disassembly: the push
   against a car tilted past 30 degrees, and the slide that breaks a car loose. */
#include "sim/tilt.h"
#include "fixmath.h"
#include "game/gstate.h"
#include <math.h>

static void v3load(int32_t v[3], const Car *c, uint32_t o) { for (uint32_t k = 0; k < 3; k++) v[k] = CI32(c, o + 4 * k); }
static int32_t dot3(const int32_t a[3], const int32_t b[3]) { return fx_mul(a[0], b[0]) + fx_mul(a[1], b[1]) + fx_mul(a[2], b[2]); }
static int32_t iabs(int32_t v) { return v < 0 ? -v : v; }

static void to_body(const Car *c, int32_t out[3], const int32_t v[3])
{
    int32_t m[12];
    for (uint32_t i = 0; i < 12; i++) m[i] = CI32(c, 4 * i);
    for (int r = 0; r < 3; r++) out[r] = fx_mul(m[4 * r + 2], v[2]) + fx_mul(m[4 * r + 1], v[1]) + fx_mul(m[4 * r], v[0]);
}

/* the ground normal in the body rows' terms (+0x91c-+0x924), noted when the car breaks loose */
static void note_normal(Car *c)
{
    int32_t n[3];
    v3load(n, c, 0x48c);
    for (uint32_t r = 0; r < 3; r++) {
        int32_t row[3];
        v3load(row, c, 0x360 + 12 * r);
        CW32(c, 0x91c + 4 * r, dot3(n, row));
    }
}

/* (a << 16) / b, the 64-bit divide the original uses for reciprocals */
static int32_t fdiv(int32_t a, int32_t b) { return b ? (int32_t)(((int64_t)a << 16) / b) : 0; }

/* The inline arctangent of y over x: 4,096 units, the 512-entry table by the smaller over the larger */
static int16_t atan2_units(int32_t y, int32_t x)
{
    if (!y) return 0;
    if (!x) return 0x400;
    int sign = 0;
    if (y < 0) y = -y, sign ^= 1;
    if (x < 0) x = -x, sign ^= 1;
    int swap = 0;
    int32_t n = y, d = x;
    if (y > x) n = x, d = y, swap = 1;
    uint32_t idx = (uint32_t)fdiv(n, d) >> 7;
    int16_t a = (int16_t)g_atan[idx];
    if (swap) a = (int16_t)(0x400 - a);
    if (sign) a = (int16_t)-a;
    return a;
}

void Car_Tilt(Car *c)
{
    int32_t inv = 0;   /* local -0x8 */
    if (!c->s[0xb28] && !c->s[0xb34]) return;
    int32_t y = CI32(c, 0x490);
    bool pushed = false;
    if (y < 0x10000) {
        bool neg = y < 0;
        int32_t ay = neg ? -y : y;
        int16_t s;
        if (ay > 0x10000) s = (int16_t)g_asin[4095];
        else {
            int64_t i = llrint((double)ay * (1.0 / 65536.0) * -4095.0);   /* 0x511310, 0x511330 */
            s = (int16_t)g_asin[-i];
            if (neg) s = (int16_t)-s;
        }
        if ((int16_t)(0x400 - s) > 0x155 && CI8(c, 0xb42) <= 0) {
            int32_t n[3], r1[3], f[3], r2[3], t[3];
            v3load(n, c, 0x48c);
            for (int q = 0; q < 3; q++) n[q] = fx_mul(n[q], 0x280000);
            to_body(c, r1, n);
            int32_t k = fx_mul(fdiv(0x10000, 0xc80000), 0x8000);
            k = fx_mul((int32_t)((uint32_t)(c->s[0xa9c] | c->s[0xa9d] << 8) << 16), k);
            v3load(f, c, 0x6a8);
            for (int q = 0; q < 3; q++) f[q] = fx_mul(f[q], k);
            to_body(c, r2, f);
            t[0] = fx_mul(r2[1], r1[2]) - fx_mul(r2[2], r1[1]);
            t[1] = fx_mul(r2[2], r1[0]) - fx_mul(r2[0], r1[2]);
            t[2] = fx_mul(r2[0], r1[1]) - fx_mul(r2[1], r1[0]);
            for (int q = 0; q < 3; q++) t[q] = fx_mul(t[q], CI32(c, 0x760));
            if (!CI32(c, 0xc00)) {
                CW32(c, 0xc00, 1);
                CW32(c, 0x96c, 0x10000);
                note_normal(c);
            }
            for (uint32_t q = 0; q < 3; q++) CW32(c, 0x5d0 + 4 * q, CI32(c, 0x5d0 + 4 * q) + t[q]);
            uint16_t a9c = (uint16_t)(c->s[0xa9c] | c->s[0xa9d] << 8);
            if (a9c < 200) CW16(c, 0xa9c, (uint16_t)(a9c + 1));
            pushed = true;
        }
    }
    if (!pushed) CW16(c, 0xa9c, 0);
    if (CI32(c, 0xc00)) return;
    /* the grip threshold: the mean of the four wheels' +0x1a0 bytes over 67.07 */
    int32_t sum = 0;
    for (uint32_t w = 0; w < 4; w++) sum += c->s[0x1a0 + 12 * w] << 16;
    int32_t k = fdiv(0x10000, 0x431168);
    int32_t thr = fx_mul(fx_mul(sum, 0x4000), k);
    /* the side row in the ground plane, unit, and the velocity along it */
    int32_t n[3], side[3], v[3];
    v3load(n, c, 0x48c);
    v3load(side, c, 0x378);
    int32_t d = dot3(n, side);
    for (int q = 0; q < 3; q++) side[q] -= fx_mul(n[q], d);
    Vec_Unit(side);
    v3load(v, c, 0x408);
    int32_t lat = dot3(v, side);
    int32_t r = 0;
    if (iabs(lat) > thr) {
        int32_t sum2 = 0;
        for (uint32_t w = 0; w < 4; w++) sum2 += c->s[0x1a1 + 12 * w] << 16;
        int32_t t2 = fx_mul(fx_mul(sum2, 0x4000), k);
        if (thr) inv = fdiv(0x10000, t2 - thr);
        r = fx_mul(lat > 0 ? lat - thr : lat + thr, inv);
        if (iabs(r) > 0x10000) r = r <= 0 ? -0x10000 : 0x10000;
    }
    r = fx_mul(r, 0x10000);
    /* the slide value follows it at 0.1 per step */
    int32_t rate = fx_mul(0x1999, gms32(0x519c8c)), e = r - CI32(c, 0x77c);
    if (iabs(e) < rate) CW32(c, 0x77c, r);
    else CW32(c, 0x77c, CI32(c, 0x77c) + (e > 0 ? rate : -rate));
    bool loose = iabs(CI32(c, 0x77c)) > fx_mul(0xcccc, 0x10000);
    CW16(c, 0xb18, (uint16_t)atan2_units(CI32(c, 0x77c), 0x20000));
    if (!loose) return;
    CW16(c, 0xb18, 0);
    CW32(c, 0xc00, 1);
    CW32(c, 0x96c, 0x10000);
    note_normal(c);
    CW32(c, 0x5d0, CI32(c, 0x5d0) + fx_mul(fx_mul(lat, gms32(0x519c90)), (int32_t)0xfff60000));
}
