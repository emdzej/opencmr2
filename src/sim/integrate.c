/* Car_Integrate 0x435470, the step's integration of one car's body, ported from the disassembly (the
   decompiler loses its position and angle steps). Forces are summed at the touching corners and the
   wheels, turned into the car's frame for the torques; velocity and spin are stepped (dt at 0x519c8c,
   1.0), and the position and the rows moved by v dt - dv dt / 2. The angle step is converted to
   4,096 units through a double, 4096 / (2 pi 65536) at 0x511380, rounded to nearest (FISTP). */
#include "sim/integrate.h"
#include "sim/tilt.h"
#include "fixmath.h"
#include "game/gstate.h"
#include "pending.h"
#include <math.h>

static void v3load(int32_t v[3], const Car *c, uint32_t o) { for (uint32_t k = 0; k < 3; k++) v[k] = CI32(c, o + 4 * k); }
static void v3scale(int32_t v[3], int32_t s) { for (int k = 0; k < 3; k++) v[k] = fx_mul(v[k], s); }
static int32_t iabs(int32_t v) { return v < 0 ? -v : v; }

/* r x arm, the original's component order */
static void cross(int32_t o[3], const int32_t r[3], const int32_t a[3])
{
    o[0] = fx_mul(r[1], a[2]) - fx_mul(r[2], a[1]);
    o[1] = fx_mul(r[2], a[0]) - fx_mul(r[0], a[2]);
    o[2] = fx_mul(r[0], a[1]) - fx_mul(r[1], a[0]);
}

static void to_body(const Car *c, int32_t out[3], const int32_t v[3])
{
    int32_t m[12];
    for (uint32_t i = 0; i < 12; i++) m[i] = CI32(c, 4 * i);   /* the orientation through +0x750 */
    for (int r = 0; r < 3; r++) out[r] = fx_mul(m[4 * r + 2], v[2]) + fx_mul(m[4 * r + 1], v[1]) + fx_mul(m[4 * r], v[0]);
}

/* spin damping against a torque of the other sign: (1 - min(5 |w|, 1)) times f */
static int32_t damp(int32_t t, int32_t w, int32_t f)
{
    if (!((t > 0 && w < 0) || (t < 0 && w > 0))) return t;
    int32_t s = fx_mul(iabs(w), 0x50000);
    if (s > 0x10000) s = 0x10000;
    return fx_mul(t, fx_mul(0x10000 - s, f));
}

static int16_t to_units(int32_t v) { return (int16_t)(int64_t)llrint((double)v * (4096.0 / (2.0 * M_PI * 65536.0))); }

void Car_Integrate(Car *c)
{
    PENDING_NOTE(0x436630, "Car_BodyContacts");
    Car_Tilt(c);
    if (!CI8(c, 0xb28) && CI32(c, 0xb74))   /* 0x437d70 */
        CW32(c, 0x5d8, CI32(c, 0x5d8) - fx_mul(CI32(c, 0x7a4) - CI32(c, 0x7a8), 0x1999));
    int32_t t[3] = {0, 0, 0}, f[3];
    v3load(f, c, 0x6a8);
    v3scale(f, Gs_Option(1) ? 0x6666 : 0xcccc);
    int32_t last[3] = {0, 0, 0};   /* the last vector added (local -0x18) */
    if (CI8(c, 0xb34) > 0) {
        int32_t k = fx_mul(CI32(c, 0x778), fx_mul(0x10000, 0x13333));
        if (k > 0x13333) k = 0x13333;
        k += 0x10000;
        for (int n = CI8(c, 0xb34) - 1; n >= 0; n--) {
            int j = CI8(c, 0xb36 + (uint32_t)n);
            uint32_t u = (uint32_t)j;
            int32_t a[3], b[3];
            v3load(a, c, 0x648 + 12 * u);
            v3load(b, c, 0x5e8 + 12 * u);
            if (j >= 4) {
                v3scale(a, k);
                for (int q = 0; q < 3; q++) last[q] = a[q] + b[q];
                int32_t raw[3];
                v3load(raw, c, 0x648 + 12 * u);
                for (int q = 0; q < 3; q++) f[q] += raw[q];
            } else if (!CI32(c, 0xc00)) {
                last[0] = last[1] = last[2] = 0;
            } else if (!CI32(c, 0xb74)) {
                v3scale(a, k);
                for (int q = 0; q < 3; q++) last[q] = a[q] + b[q];
            } else {
                for (int q = 0; q < 3; q++) last[q] = a[q] + b[q];
            }
            int32_t r[3], arm[3], o[3];
            to_body(c, r, last);
            v3load(arm, c, 0x210 + 12 * u);
            cross(o, r, arm);
            for (int q = 0; q < 3; q++) t[q] += o[q];
        }
    }
    if (CI32(c, 0xc00)) {
        t[0] = damp(t[0], CI32(c, 0x420), 0xf333);
        t[1] = damp(t[1], CI32(c, 0x424), 0xcccc);
        t[2] = damp(t[2], CI32(c, 0x428), 0xf333);
    }
    /* the wheels on the ground (+0xbac-+0xbb8): their forces at +0x648 */
    for (int j = 3; j >= 0; j--) {
        uint32_t u = (uint32_t)j;
        if (!CI32(c, 0xbac + 4 * u)) continue;
        v3load(last, c, 0x648 + 12 * u);
        for (int q = 0; q < 3; q++) f[q] += last[q];
        if (CI32(c, 0xc00)) continue;
        int32_t r[3], arm[3], o[3];
        to_body(c, r, last);
        v3load(arm, c, 0x210 + 12 * u);
        cross(o, r, arm);
        for (int q = 0; q < 3; q++) t[q] += o[q];
    }
    /* a hard enough side or yaw push breaks the car loose (+0xc00), the ground normal noted in the
       car's frame */
    if (!CI32(c, 0xc00) && fx_mul(iabs(CI32(c, 0x5d0)) + iabs(CI32(c, 0x5d8)), CI32(c, 0x75c)) > 0xbb80000) {
        CW32(c, 0xc00, 1);
        CW32(c, 0x96c, 0x10000);
        CW32(c, 0xc04, 1);
        for (uint32_t r = 0; r < 3; r++) {
            int32_t n[3], row[3];
            v3load(n, c, 0x48c);
            v3load(row, c, 0x360 + 12 * r);
            CW32(c, 0x91c + 4 * r, fx_mul(n[0], row[0]) + fx_mul(n[1], row[1]) + fx_mul(n[2], row[2]));
        }
    }
    /* drag: the speed times (the damage record's +0x408 plus 0x519c88) along the velocity */
    int32_t sp = iabs(CI32(c, 0x778));
    int32_t e = fx_mul(sp, DI32(Damage_Get(CI8(c, 0xb1a)), 0x408) + gms32(0x519c88));
    v3load(last, c, 0x408);
    v3scale(last, e);
    for (int q = 0; q < 3; q++) f[q] -= last[q];
    int32_t dv[3], dw[3];
    for (int q = 0; q < 3; q++) dv[q] = fx_mul(f[q], CI32(c, 0x760)) + CI32(c, 0x5c4 + 4 * (uint32_t)q);
    for (uint32_t q = 0; q < 3; q++) {
        int32_t inertia = CI32(c, 0x3fc + 4 * q);
        dw[q] = -fx_mul(t[q], inertia) - fx_mul(CI32(c, 0x5d0 + 4 * q), fx_mul(inertia, CI32(c, 0x75c)));
    }
    v3scale(dv, 0x3333);
    for (uint32_t k2 = 0; k2 < 3; k2++) CW32(c, 0x414 + 4 * k2, CI32(c, 0x408 + 4 * k2));
    int32_t dt = gms32(0x519c8c);
    v3scale(dv, dt);
    for (uint32_t q = 0; q < 3; q++) CW32(c, 0x408 + 4 * q, CI32(c, 0x408 + 4 * q) + dv[q]);
    int32_t v[3];
    v3load(v, c, 0x408);
    CW32(c, 0x778, (int32_t)Fx_Sqrt((uint32_t)fx_mul(v[0], v[0]) + (uint32_t)fx_mul(v[1], v[1]) + (uint32_t)fx_mul(v[2], v[2])));
    v3scale(dw, dt);
    for (uint32_t q = 0; q < 3; q++) CW32(c, 0x420 + 4 * q, CI32(c, 0x420 + 4 * q) + dw[q]);
    int32_t step[3], half = dt / 2;
    v3load(step, c, 0x408);
    v3scale(step, dt);
    v3scale(dv, half);
    for (int q = 0; q < 3; q++) step[q] -= dv[q];
    if (c->s[0xa9c] | c->s[0xa9d]) {
        for (uint32_t q = 0; q < 3; q++) {
            int32_t w = CI32(c, 0x420 + 4 * q);
            if (iabs(w) > 0x3333) CW32(c, 0x420 + 4 * q, w <= 0 ? -0x3333 : 0x3333);
        }
    }
    int16_t a[3];
    int32_t w[3];
    v3load(w, c, 0x420);
    if (!CI32(c, 0xc00)) {
        v3scale(w, dt);
        v3scale(dw, half);
        for (int q = 0; q < 3; q++) a[q] = to_units(w[q] - dw[q]);
    } else {
        for (int q = 0; q < 3; q++) a[q] = to_units(fx_mul(w[q], dt));
    }
    /* settling: small turns dropped when on the ground, the move dropped below 0.05 */
    if (CI8(c, 0xb34) > 0) {
        int32_t n[3], up[3];
        v3load(n, c, 0x48c);
        v3load(up, c, 0x36c);
        int32_t d = fx_mul(n[2], up[2]) + fx_mul(n[0], up[0]) + fx_mul(n[1], up[1]);
        if (iabs(a[0]) < 10 && iabs(d) > 0xcccc) a[0] = 0;
        if ((!c->s[0x1d0] && !c->s[0x1d1]) || iabs(CI32(c, 0x778)) < 0x28f || !CI32(c, 0xb74))
            if (iabs(a[1]) < 10) a[1] = 0;
        if (iabs(a[2]) < 10) a[2] = 0;
        if (CI32(c, 0x778) < 0xccc) step[0] = step[1] = step[2] = 0;
    }
    if (CI32(c, 0xbfc)) {
        if (CI32(c, 0xbdc)) {
            CW32(c, 0x428, 0);
            CW32(c, 0x420, 0);
            a[2] = 0;
            a[0] = 0;
        } else if (CI32(c, 0xbe0)) {
            CW32(c, 0x424, 0);
            CW32(c, 0x420, 0);
            a[1] = 0;
            a[0] = 0;
        }
    }
    for (uint32_t q = 0; q < 3; q++) CW32(c, 0x2d0 + 4 * q, CI32(c, 0x2d0 + 4 * q) + step[q]);
    CW32(c, 0xb60, 0);
    if (!a[0] && !a[1] && !a[2]) {
        if (CI32(c, 0x778) < 0x51e) CW32(c, 0xb60, 1);
    } else {
        int32_t rows[9];
        for (uint32_t i = 0; i < 9; i++) rows[i] = CI32(c, 0x360 + 4 * i);
        uint16_t ua[3] = {(uint16_t)a[0], (uint16_t)a[1], (uint16_t)a[2]};
        Rows_Turn3(rows, 3, ua);
        for (uint32_t i = 0; i < 9; i++) CW32(c, 0x360 + 4 * i, rows[i]);
    }
    for (uint32_t r = 0; r < 3; r++)
        for (uint32_t k2 = 0; k2 < 3; k2++) CW32(c, 0x10 * r + 4 * k2, CI32(c, 0x360 + 0xc * r + 4 * k2));
    for (uint32_t k2 = 0; k2 < 3; k2++) CW32(c, 0x30 + 4 * k2, CI32(c, 0x2d0 + 4 * k2));
    Car_BoxCorners(c);
}
