/* Sim_CarsPost 0x42cd00 (Sim_Step's pass after the car step) with Car_RoadFollow 0x42ce60 and its
   helpers, ported from the disassembly and the decompiler output where it is whole. In road-following
   mode (+0xc00 clear) the body is re-aligned each step to the ground under its corners: the up row
   eased half way to the ground normal, the forward and side rows squared to it; the car lifted out of
   the ground by the deepest corner; the velocity into the ground removed and the pitch and roll
   spins cut to 12%. A car leaving the ground gets a turn from the change of the normal instead, and
   a slope of more than 45 degrees under it, or a turn of the ground, breaks it loose. */
#include "sim/road_follow.h"
#include "fixmath.h"
#include "game/gstate.h"
#include "pending.h"
#include "sim/car_internal.h"
#include "sim/free_body.h"
#include "sim/recover.h"
#include <math.h>

static void v3load(int32_t v[3], const Car *c, uint32_t o) { for (uint32_t k = 0; k < 3; k++) v[k] = CI32(c, o + 4 * k); }
static void v3store(Car *c, uint32_t o, const int32_t v[3]) { for (uint32_t k = 0; k < 3; k++) CW32(c, o + 4 * k, v[k]); }
static int32_t dot3(const int32_t a[3], const int32_t b[3]) { return fx_mul(a[0], b[0]) + fx_mul(a[1], b[1]) + fx_mul(a[2], b[2]); }
static int32_t len3(const int32_t v[3]) { return (int32_t)Fx_Sqrt((uint32_t)fx_mul(v[0], v[0]) + (uint32_t)fx_mul(v[1], v[1]) + (uint32_t)fx_mul(v[2], v[2])); }
static int32_t iabs(int32_t v) { return v < 0 ? -v : v; }
static void cross(int32_t o[3], const int32_t a[3], const int32_t b[3])
{
    o[0] = fx_mul(a[1], b[2]) - fx_mul(a[2], b[1]);
    o[1] = fx_mul(a[2], b[0]) - fx_mul(a[0], b[2]);
    o[2] = fx_mul(a[0], b[1]) - fx_mul(a[1], b[0]);
}
static void to_body(const Car *c, int32_t out[3], const int32_t v[3])
{
    int32_t m[12];
    for (uint32_t i = 0; i < 12; i++) m[i] = CI32(c, 4 * i);
    for (int r = 0; r < 3; r++) out[r] = fx_mul(m[4 * r + 2], v[2]) + fx_mul(m[4 * r + 1], v[1]) + fx_mul(m[4 * r], v[0]);
}

/* 0x4336f0: each wheel's turn angle (+0xb08) advanced by its spin (as a double, -1/2pi in 4,096
   units at 0x511398; at most 0x28b a step), and a bent wheel's wobble (+0x6fc, +0x700 from the damage
   record's +0x240) */
static void wheel_angles(Car *c)
{
    const uint8_t *d = Damage_Get(CI8(c, 0xb1a));
    for (uint32_t i = 0; i < 4; i++) {
        int32_t w = CI32(c, 0x860 + 4 * i);
        uint32_t ao = 0xb08 + 2 * i;
        int16_t a = (int16_t)(c->s[ao] | c->s[ao + 1] << 8);
        if (iabs(w) <= 0x10000) a = (int16_t)(a + (int16_t)(int64_t)llrint((double)w * (-4096.0 / (2.0 * M_PI * 65536.0))));
        else a = (int16_t)(a + (w < 1 ? 0x28b : -0x28b));
        CW16(c, ao, (uint16_t)a);
        int32_t b = DI32(d, 0x240 + 4 * i);
        if (b < 1) {
            CW32(c, 0x700 + 8 * i, 0);
            CW32(c, 0x6fc + 8 * i, 0);
        } else {
            CW32(c, 0x6fc + 8 * i, fx_mul(g_sin[(uint16_t)a & 0xfff], fx_mul(b, 0xa3d)));
            CW32(c, 0x700 + 8 * i, fx_mul(-g_sin[(uint16_t)(a + 0x400) & 0xfff], fx_mul(b, 0xa3d)));
        }
    }
}

/* 0x42f8c0: the car lifted by the deepest touching corner below its ground */
void Car_LiftOut(Car *c)
{
    int32_t best = 0;
    bool any = false;
    for (int i = 7; i >= 0; i--) {
        uint32_t u = (uint32_t)i;
        int32_t d = CI32(c, 0x8dc + 4 * u) - CI32(c, 0x274 + 12 * u);
        if (!c->s[0xb2c + u] && d > 0 && best < d) best = d, any = true;
    }
    int32_t l = any ? best : 0;
    CW32(c, 0x2d4, CI32(c, 0x2d4) + l);
    for (uint32_t i = 0; i < 8; i++) CW32(c, 0x274 + 12 * i, CI32(c, 0x274 + 12 * i) + l);
}

/* 0x42e8e0: each wheel's spring (+0x9d8 travel, +0x9c8 rate) against its stiffness (+0x9e8) and
   damping (+0x9f8), kicked by the corner's vertical acceleration in the car's frame near the ground;
   the travel at most 0.2 down */
static void springs(Car *c)
{
    for (uint32_t i = 0; i < 4; i++) {
        uint32_t o = 0x9c8 + 4 * i, j = 12 * i;
        if (CI32(c, 0xb74)) {
            int32_t y = CI32(c, j + 0x274), d = iabs(CI32(c, 0x8dc + 4 * i) - y);
            int32_t p2[3] = {CI32(c, j + 0x330), CI32(c, j + 0x334), CI32(c, j + 0x338)};
            if (d < 0x4ccc && (p2[0] || p2[1] || p2[2])) {
                int32_t a[3] = {(CI32(c, j + 0x270) - CI32(c, j + 0x300)) - (CI32(c, j + 0x300) - p2[0]),
                                (y - CI32(c, j + 0x304)) - (CI32(c, j + 0x304) - p2[1]),
                                (CI32(c, j + 0x278) - CI32(c, j + 0x308)) - (CI32(c, j + 0x308) - p2[2])}, b[3];
                to_body(c, b, a);
                int32_t u = fx_mul(b[1], fx_mul(gms32(0x519c90), 0x9c28));
                if (u > 0) CW32(c, o, CI32(c, o) - u);
            }
        }
        int32_t f = -(fx_mul(CI32(c, o + 0x20), CI32(c, o + 0x10)) + fx_mul(CI32(c, o + 0x30), CI32(c, o)));
        CW32(c, o, CI32(c, o) + fx_mul(gms32(0x519c8c), f));
        CW32(c, o + 0x10, CI32(c, o + 0x10) + fx_mul(gms32(0x519c8c), CI32(c, o)));
        if (CI32(c, o + 0x10) < -0x3333) {
            CW32(c, o + 0x10, (int32_t)0xffffcccd);
            if (CI32(c, o) < 0) CW32(c, o, 0);
        }
    }
}

/* the bump noise from the position's bits */
static int32_t pos_bits(const Car *c, uint32_t j, int32_t mod)
{
    int32_t a = iabs(CI32(c, 0x270 + j)), b = iabs(CI32(c, 0x278 + j));
    return (a - b < 0 ? b - a : a - b) % mod;
}

/* 0x42e450: the wheels' compression along the ground normal into their spring offsets (+0x808,
   centred, at most 1.0, times +0x9c0) and their ground penetration (+0x928, at least +0xa08); in
   road-following mode the bump noise of the surface (+0x938, +0x948 from the corner coefficients
   +0x9c, +0xa0) */
void Car_Travel(Car *c)
{
    int32_t n[3], up[3];
    v3load(n, c, 0x48c);
    v3load(up, c, 0x36c);
    int32_t d = dot3(n, up);
    if (d < 0xcccd) {
        for (uint32_t i = 0; i < 4; i++) CW32(c, 0x928 + 4 * i, 0), CW32(c, 0x808 + 4 * i, 0);
    } else {
        int32_t r = (int32_t)(0x100000000LL / d), arr[4], sum = 0, m = 0;
        for (uint32_t i = 0; i < 4; i++) {
            int32_t g = CI32(c, 0x8dc + 4 * i), y = CI32(c, 0x274 + 12 * i);
            arr[i] = fx_mul(-fx_mul(y - g, n[1]), r);
            sum += arr[i];
            int32_t e = (g - y) - CI32(c, 0x958);
            CW32(c, 0x928 + 4 * i, e < 0 ? (e < CI32(c, 0xa08) ? CI32(c, 0xa08) : e) : 0);
        }
        for (uint32_t i = 0; i < 4; i++) {
            CW32(c, 0x808 + 4 * i, fx_mul(arr[i] - sum / 4, CI32(c, 0x9c0)));
            int32_t v = iabs(CI32(c, 0x808 + 4 * i));
            if (m < v) m = v;
        }
        if (m > 0x10000)
            for (uint32_t i = 0; i < 4; i++) CW32(c, 0x808 + 4 * i, fx_mul(CI32(c, 0x808 + 4 * i), (int32_t)(0x100000000LL / m)));
    }
    if (!CI32(c, 0xc00)) {
        for (uint32_t i = 0; i < 4; i++) {
            uint32_t o = 0x948 + 4 * i, rec = 0x24 * i;
            if (CI32(c, rec + 0x9c) < 1 || c->s[0xb2c + i] || !CI32(c, 0xb74)) {
                CW32(c, o - 0x10, 0);
                CW32(c, o, 0);
                continue;
            }
            int32_t cv[3];
            v3load(cv, c, 0x42c + 12 * i);
            int32_t sp = CI32(c, 0x778) - dot3(n, cv);
            if (sp > 0x10000) sp = 0x10000;
            else if (sp < 0xccc) sp = 0;
            if (CI32(c, o) < 1) {
                if (!CI32(c, o - 0x10)) {
                    CW32(c, o - 0x10, fx_mul(fx_mul(pos_bits(c, 12 * i, 0x401) << 6, sp), CI32(c, rec + 0x9c)));
                } else {
                    CW32(c, o - 0x10, 0);
                    CW32(c, o, fx_mul(fx_mul(pos_bits(c, 12 * i, 0x201) << 7, 0x320000), CI32(c, rec + 0xa0)));
                }
            } else {
                CW32(c, o, CI32(c, o) - sp);
                if (CI32(c, o) < 0) CW32(c, o, 0);
            }
        }
    } else {
        for (uint32_t i = 0; i < 4; i++) CW32(c, 0x938 + 4 * i, 0), CW32(c, 0x948 + 4 * i, 0);
    }
    springs(c);
}

static void orient_from_rows(Car *c)
{
    for (uint32_t r = 0; r < 3; r++)
        for (uint32_t k = 0; k < 3; k++) CW32(c, 0x10 * r + 4 * k, CI32(c, 0x360 + 0xc * r + 4 * k));
}

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

void Car_RoadFollow(Car *c)
{
    for (uint32_t k = 0; k < 3; k++) CW32(c, 0x498 + 4 * k, CI32(c, 0x48c + 4 * k));
    for (uint32_t o = 0x504; o < 0x564; o += 4) CW32(c, o, CI32(c, o - 0x60));
    CW32(c, 0xba8, 0);
    CW32(c, 0xba4, 0);
    CW32(c, 0xba0, 0);
    Car_CornerGround(c, 4);
    uint8_t eased = 0;
    if (c->s[0xb35] == 1) Car_Contacts(c);
    if (!c->s[0xb35]) {
        int32_t n0[3], v[3];
        v3load(n0, c, 0x48c);
        Car_GroundNormal(c);
        int32_t n[3];
        v3load(n, c, 0x48c);
        v3load(v, c, 0x408);
        int32_t vn = dot3(v, n);
        int32_t k = CI32(c, 0x778);
        if (k > 0x1570a) k = 0x1570a;
        k = fx_mul(k, 0x8000);
        if (k > 0x10000) k = 0x10000;
        k = fx_mul(0x10000 - k, 0x1999);
        if (vn > k) {
            /* leaving the ground: a turn from the normal's change, the old normal kept */
            int32_t d[3] = {n[0] - n0[0], n[1] - n0[1], n[2] - n0[2]};
            if (len3(d) > 0) {
                Vec_Unit(d);
                int32_t r[3], t[3];
                static const int32_t yax[3] = {0, 0x10000, 0};
                to_body(c, r, d);
                cross(t, r, yax);
                CW32(c, 0x5d0, CI32(c, 0x5d0) + fx_mul(t[0], 0x14ccc));
                CW32(c, 0x5d8, CI32(c, 0x5d8) + fx_mul(t[2], 0xe666));
            }
            v3store(c, 0x48c, n0);
        } else {
            int32_t up[3];
            v3load(up, c, 0x36c);
            int32_t dd = dot3(n, up);
            if (dd <= 0xb333) {
                CW32(c, 0xc00, 1);
                CW32(c, 0x96c, 0x10000);
                CW32(c, 0xc04, 1);
            } else if (CI32(c, 0xb60) && dd >= 0xfd70) {
                for (uint32_t i = 0; i < 9; i++) CW32(c, 0x360 + 4 * i, CI32(c, 0x384 + 4 * i));
            } else {
                if (!c->s[0xb2a]) {
                    int32_t u[3];
                    for (int q = 0; q < 3; q++) u[q] = up[q] + fx_mul(n[q] - up[q], 0x8000);
                    Vec_Unit(u);
                    v3store(c, 0x36c, u);
                } else {
                    v3store(c, 0x36c, n);
                }
                eased = 1;
                int32_t f[3], u[3], s[3];
                v3load(f, c, 0x360);
                v3load(u, c, 0x36c);
                int32_t p = dot3(f, u);
                for (int q = 0; q < 3; q++) f[q] -= fx_mul(u[q], p);
                Vec_Unit(f);
                v3store(c, 0x360, f);
                cross(s, f, u);
                Vec_Unit(s);
                v3store(c, 0x378, s);
            }
            note_normal(c);
            orient_from_rows(c);
            for (uint32_t q = 0; q < 3; q++) CW32(c, 0x30 + 4 * q, CI32(c, 0x2d0 + 4 * q));
            Car_BoxCorners(c);
            Car_CornerGround(c, CI32(c, 0xc00) ? 8 : 4);
        }
        Car_LiftOut(c);
        Car_Contacts(c);
        if (!c->s[0xb35]) {
            int32_t k2 = fx_mul(0x1eb8, gms32(0x519c8c));
            if (vn < 0) {
                int32_t nn[3];
                v3load(nn, c, 0x48c);
                for (uint32_t q = 0; q < 3; q++) CW32(c, 0x408 + 4 * q, CI32(c, 0x408 + 4 * q) - fx_mul(nn[q], vn));
            }
            CW32(c, 0x420, fx_mul(CI32(c, 0x420), k2));
            CW32(c, 0x428, fx_mul(CI32(c, 0x428), k2));
        }
    }
    Car_Travel(c);
    c->s[0xb2a] = eased;
    /* the orientation: the rows, turned about forward by the slide angle (+0xb18) */
    int16_t sl = (int16_t)(c->s[0xb18] | c->s[0xb19] << 8);
    for (uint32_t k = 0; k < 3; k++) CW32(c, 4 * k, CI32(c, 0x360 + 4 * k));
    if (sl) {
        int32_t f[3], m[16], t[3], r[3];
        v3load(f, c, 0x360);
        Mat_AxisAngleRows(m, f, sl);
        for (uint32_t row = 1; row < 3; row++) {
            v3load(r, c, 0x360 + 12 * row);
            for (int k = 0; k < 3; k++) t[k] = fx_mul(m[8 + k], r[2]) + fx_mul(m[4 + k], r[1]) + fx_mul(m[k], r[0]);
            Vec_Unit(t);
            for (uint32_t k = 0; k < 3; k++) CW32(c, 0x10 * row + 4 * k, t[k]);
        }
    } else {
        for (uint32_t row = 1; row < 3; row++)
            for (uint32_t k = 0; k < 3; k++) CW32(c, 0x10 * row + 4 * k, CI32(c, 0x360 + 12 * row + 4 * k));
    }
    for (uint32_t k = 0; k < 3; k++) CW32(c, 0x30 + 4 * k, CI32(c, 0x2d0 + 4 * k));
    /* the clearance (+0x958): the smallest of the touching wheels' heights over their ground, at
       most 0.1 and not below 0, negated */
    CW32(c, 0x958, 0x1999);
    for (uint32_t i = 0; i < 4; i++) {
        if (c->s[0xb2c + i]) continue;
        int32_t d = CI32(c, 0x274 + 12 * i) - CI32(c, 0x8dc + 4 * i);
        if (d < CI32(c, 0x958)) CW32(c, 0x958, d <= 0 ? 0 : d);
    }
    CW32(c, 0x958, -CI32(c, 0x958));
}

void Sim_CarsPost(const int16_t *list, int n)
{
    for (int i = n - 1; i >= 0; i--) {
        Car *c = Car_Get(list[i]);
        g_car = c;
        wheel_angles(c);
        for (uint32_t k = 0; k < 4; k++) CW16(c, 0xabe + 2 * k, (uint16_t)(c->s[0xaae + 2 * k] | c->s[0xaaf + 2 * k] << 8));
        if (!CI32(c, 0xc00)) Car_RoadFollow(c);
        else Car_FreeBody(c);
        PENDING_NOTE(0x432c20, "Car_PostBody");
        Car_RecoverCheck(c);
        if (fx_mul(gms32(0x519c90), 0xc0000) < CI32(c, 0xa80)) CW32(c, 0xbf8, 1);
        CW32(c, 0x960, 0x3e80000);
        for (uint32_t k = 0; k < 8; k++) {
            int32_t d = CI32(c, 0x274 + 12 * k) - CI32(c, 0x8dc + 4 * k);
            if (d < CI32(c, 0x960)) CW32(c, 0x960, d);
        }
        CW32(c, 0x960, CI32(c, 0x960) + CI32(c, 0x958));
    }
}
