/* Car_Tyres 0x4387a0 with 0x43af70 and 0x43a920, ported from the disassembly (the decompiler's
   output for this function overlaps its stack arrays). Per wheel: the brakes and the drive torque
   (+0x850) turn the wheel's spin (+0x860) against the ground speed along its heading; the force at
   the contact (+0x648) is the longitudinal and lateral slips' sum inside a friction circle, limited
   by the grip ellipse of the wheel's coefficients, with rolling resistance; steep changes of the
   ground's slope push the car back. Two quirks are kept: a wheel without a valid heading reuses the
   previous wheel's slips, and the grip limit compares the force against one axis's limit but takes
   the other's. */
#include "sim/tyres.h"
#include "fixmath.h"
#include "game/gstate.h"
#include <math.h>

static int32_t dot3(const int32_t a[3], const int32_t b[3]) { return fx_mul(a[0], b[0]) + fx_mul(a[1], b[1]) + fx_mul(a[2], b[2]); }
static int32_t len3(const int32_t v[3]) { return (int32_t)Fx_Sqrt((uint32_t)fx_mul(v[0], v[0]) + (uint32_t)fx_mul(v[1], v[1]) + (uint32_t)fx_mul(v[2], v[2])); }
static int32_t iabs(int32_t v) { return v < 0 ? -v : v; }
static void v3load(int32_t v[3], const Car *c, uint32_t o) { for (uint32_t k = 0; k < 3; k++) v[k] = CI32(c, o + 4 * k); }
static void v3store(Car *c, uint32_t o, const int32_t v[3]) { for (uint32_t k = 0; k < 3; k++) CW32(c, o + 4 * k, v[k]); }
static void gv3(int32_t v[3], uint32_t va) { for (uint32_t k = 0; k < 3; k++) v[k] = gms32(va + 4 * k); }
static void gw3(uint32_t va, const int32_t v[3]) { for (uint32_t k = 0; k < 3; k++) gw32(va + 4 * k, (uint32_t)v[k]); }
static int32_t fdiv(int32_t a, int32_t b) { return (int32_t)(((int64_t)a << 16) / b); }

/* v projected off the ground normal, made unit; false for a zero length */
static bool ground_dir(int32_t out[3], const int32_t v[3], const int32_t n[3])
{
    int32_t p = -dot3(v, n);
    for (int k = 0; k < 3; k++) out[k] = v[k] + fx_mul(n[k], p);
    int32_t l = len3(out);
    if (!l) { out[0] = out[1] = out[2] = 0; return false; }
    int32_t r = (int32_t)(0x100000000LL / l);
    for (int k = 0; k < 3; k++) out[k] = fx_mul(out[k], r);
    return true;
}

/* the inline arctangent (see tilt.c) */
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
    int16_t a = (int16_t)g_atan[(uint32_t)fdiv(n, d) >> 7];
    if (swap) a = (int16_t)(0x400 - a);
    if (sign) a = (int16_t)-a;
    return a;
}

/* the spin of a wheel stopped from reversing through zero: against the gear's direction when that
   axle is driven in gear, else against its previous sign */
static void spin_clamp(Car *c, uint32_t i, int32_t old)
{
    uint32_t o = 0x860 + 4 * i;
    int32_t w = CI32(c, o);
    int8_t g = CI8(c, 0xb1e);
    if (g && !CI32(c, 0xb84) && CI32(c, 0x7b4) != (i >= 2 ? 0x10000 : 0)) {
        int32_t r = CI32(c, 0x7bc + 4 * (uint32_t)g);
        if ((r > 0 && w < 0) || (r < 0 && w > 0)) CW32(c, o, 0);
    } else if ((old > 0 && w < 0) || (old < 0 && w > 0)) {
        CW32(c, o, 0);
    }
}

static void brake(Car *c, uint32_t i, int32_t bf, int32_t br, int32_t dmg)
{
    uint32_t t = 0x850 + 4 * i;
    int32_t b = i >= 2 ? br : bf, w = CI32(c, 0x860 + 4 * i);
    if (w > 0) CW32(c, t, CI32(c, t) - b - dmg);
    else if (w < 0) CW32(c, t, CI32(c, t) + b + dmg);
}

/* 0x43af70: an axle's two spins averaged when they differ by more than 0.1 */
static void axle_average(Car *c)
{
    for (uint32_t o = 0x860; o <= 0x868; o += 8) {
        int32_t d = iabs(CI32(c, o) - CI32(c, o + 4));
        if (d > 0x1999) {
            int32_t m = fx_mul(CI32(c, o + 4) + CI32(c, o), 0x8000);
            CW32(c, o + 4, m);
            CW32(c, o, m);
        }
    }
}

/* 0x43a920: at each contact whose ground normal turned by more than 10 degrees in the step towards
   the car's motion, a push back along the normal's change, growing with the angle */
static void slope_push(Car *c, int32_t m)
{
    int cnt = 4, hits = 0;
    if (CI32(c, 0xc00)) {
        cnt = 8;
        if (CI8(c, 0xb34) > 0) m = fdiv(CI32(c, 0x75c), 0x80000);
    }
    int32_t ang[8];
    bool flag[8];
    for (int i = 0; i < cnt; i++) {
        uint32_t u = (uint32_t)i;
        flag[i] = false;
        ang[i] = 0;
        bool use = CI32(c, 0xc00) ? !c->s[0xb2c + u] : CI32(c, 0xbac + 4 * u) != 0;
        if (!use) continue;
        int32_t n0[3], n[3], v[3];
        v3load(n0, c, 0x504 + 12 * u);
        v3load(n, c, 0x4a4 + 12 * u);
        int32_t d = dot3(n0, n);
        if (d >= 0x10000) { ang[i] = 0; continue; }
        int32_t ad = d < 0 ? -d : d;
        int16_t s;
        if (ad > 0x10000) s = (int16_t)g_asin[4095];
        else {
            int64_t k = llrint((double)ad * (1.0 / 65536.0) * -4095.0);
            s = (int16_t)g_asin[-k];
            if (d < 0) s = (int16_t)-s;
        }
        ang[i] = (0x400 - s) * 5760;
        if (ang[i] <= 0xa0000) continue;
        if (CI32(c, 0x4a8 + 12 * u) >= CI32(c, 0x508 + 12 * u)) continue;
        v3load(v, c, 0x42c + 12 * u);
        int32_t q = dot3(n0, v), t[3];
        for (int k = 0; k < 3; k++) t[k] = v[k] - fx_mul(n0[k], q);
        if (dot3(n, t) < 0) flag[i] = true, hits++;
    }
    if (hits <= 0) return;
    int32_t k = fdiv(0x140000, hits << 16);
    for (int i = 0; i < cnt; i++) {
        if (!flag[i]) continue;
        uint32_t u = (uint32_t)i;
        int32_t n[3], n0[3], v[3], t[3];
        v3load(n, c, 0x4a4 + 12 * u);
        v3load(n0, c, 0x504 + 12 * u);
        v3load(v, c, 0x42c + 12 * u);
        int32_t q = dot3(n0, n);
        for (int j = 0; j < 3; j++) t[j] = n[j] - fx_mul(n0[j], q);
        Vec_Unit(t);
        int32_t e = dot3(v, t);
        if (iabs(e) > 0x10000) {
            if (e < 0) for (int j = 0; j < 3; j++) t[j] = -t[j];
        } else {
            for (int j = 0; j < 3; j++) t[j] = fx_mul(t[j], e);
        }
        ang[i] += (int32_t)0xfff60000;
        int32_t f = fx_mul(ang[i], 0x51e);
        if (f > 0x10000) f = 0x10000;
        int32_t g = -fx_mul(f, fx_mul(m, k));
        for (uint32_t j = 0; j < 3; j++) CW32(c, 0x648 + 12 * u + 4 * j, CI32(c, 0x648 + 12 * u + 4 * j) + fx_mul(t[j], g));
    }
}

void Car_Tyres(Car *c)
{
    const uint8_t *dmg = Damage_Get(CI8(c, 0xb1a));
    int32_t bf, br;   /* the front and rear brake torques (local -0x9c, -0xa0) */
    if (!c->s[0x1d2] && !c->s[0x1d3] && c->s[0xb1e]) {
        if (CI32(c, 0x778) < 0x1999) {
            bf = fx_mul(0x1999, CI32(c, 0x830));
            br = fx_mul(0x1999, 0x10000 - CI32(c, 0x830));
        } else {
            bf = br = 0;
        }
    } else {
        bf = fx_mul(fx_mul(CI32(c, 0x838), DI32(dmg, 0x3fc)), CI32(c, 0x830));
        br = fx_mul(fx_mul(CI32(c, 0x838), DI32(dmg, 0x400)), 0x10000 - CI32(c, 0x830));
    }
    if (CI32(c, 0x1d8)) br += CI32(c, 0x848);
    if (!c->s[0xb28] || !CI32(c, 0xb74)) {
        gw32(0x53ca94, (uint32_t)fx_mul(gms32(0x519c8c), 0xa0000));
        for (int i = 3; i >= 0; i--) {
            uint32_t u = (uint32_t)i;
            brake(c, u, bf, br, DI32(dmg, 0x3ec + 4 * u));
            int32_t old = CI32(c, 0x860 + 4 * u);
            CW32(c, 0x860 + 4 * u, old + fx_mul(CI32(c, 0x850 + 4 * u), gms32(0x53ca94)));
            spin_clamp(c, u, old);
        }
        axle_average(c);
        return;
    }
    int32_t n[3], fwd[4][3], side[4][3], v[3];
    bool fok[4], sok[4];
    v3load(n, c, 0x48c);
    v3load(v, c, 0x3b4);
    fok[0] = fok[1] = ground_dir(fwd[0], v, n);
    for (int k = 0; k < 3; k++) fwd[1][k] = fwd[0][k];
    v3load(v, c, 0x3a8);
    sok[0] = sok[1] = ground_dir(side[0], v, n);
    for (int k = 0; k < 3; k++) side[1][k] = side[0][k];
    v3load(v, c, 0x3f0);
    fok[2] = fok[3] = ground_dir(fwd[2], v, n);
    for (int k = 0; k < 3; k++) fwd[3][k] = fwd[2][k];
    int32_t h[3], up[3], x[3];
    v3load(h, c, 0x3f0);
    v3load(up, c, 0x36c);
    x[0] = fx_mul(h[1], up[2]) - fx_mul(h[2], up[1]);
    x[1] = fx_mul(h[2], up[0]) - fx_mul(h[0], up[2]);
    x[2] = fx_mul(h[0], up[1]) - fx_mul(h[1], up[0]);
    gw3(0x53ca88, x);
    sok[2] = sok[3] = ground_dir(side[2], x, n);
    for (int k = 0; k < 3; k++) side[3][k] = side[2][k];
    v3store(c, 0x6b4, fwd[0]);
    v3store(c, 0x6c0, fwd[2]);
    v3store(c, 0x6cc, side[0]);
    v3store(c, 0x6d8, side[2]);
    gw32(0x53c9b4, (uint32_t)fx_mul(CI32(c, 0x8b4), gms32(0x519c90)));
    gw32(0x53ca4c, (uint32_t)fx_mul(gms32(0x519c90), 0xf000));
    gw32(0x53ca94, (uint32_t)fx_mul(gms32(0x519c8c), 0x11113));
    int32_t vf[4] = {0, 0, 0, 0}, e = 0;   /* e: local -0x30, carried between the loops */
    bool coast = !c->s[0x1d2] && !c->s[0x1d3];
    for (int i = 3; i >= 0; i--) {
        uint32_t u = (uint32_t)i, rec = 0x90 + 0x24 * u, t = 0x850 + 4 * u;
        if (CI32(c, 0xbac + 4 * u)) {
            int32_t db = DI32(dmg, 0x3ec + 4 * u);
            brake(c, u, bf, br, db);
            if (fok[i]) {
                int32_t wv[3];
                v3load(wv, c, 0x42c + 12 * u);
                vf[i] = dot3(fwd[i], wv);
                int32_t w = CI32(c, 0x860 + 4 * u), s = vf[i] * 4 - w;
                bool opp = (vf[i] > 0 && s < 0) || (vf[i] < 0 && s > 0);
                CW32(c, 0xbe8 + 4 * u, 0);
                int32_t f4 = vf[i] * 4;
                if (f4 > 0 ? (w > f4 || w < -0x1999) : (w < f4 || w > 0x1999)) CW32(c, 0xbe8 + 4 * u, 1);
                if (rec > 0xb4 && CI32(c, 0x1d8) && !opp) {
                    CW32(c, t, 0);
                    brake(c, u, bf, br, db);
                } else {
                    e = fx_mul(s, gms32(0x53ca4c)) * 4;
                    int32_t lim;
                    if (coast) lim = CI32(c, 0xa5c + 4 * u);
                    else {
                        int32_t a = fx_mul(iabs(s), 0x4000) - iabs(dot3(side[i], wv));
                        if (a < 0) a = 0;
                        a = fx_mul(a, 0xccc) + 0x51e;
                        if (CI32(c, rec)) a += fx_mul(0xe666, CI32(c, rec));
                        lim = fx_mul(CI32(c, 0xa5c + 4 * u), a);
                    }
                    if (e > lim) e = lim;
                    else if (e < -lim) e = -lim;
                    int32_t d = coast ? e / 4 : e / 8;
                    if (CI8(c, 0xb1f)) d = fx_mul(d, 0x1999);
                    CW32(c, t, CI32(c, t) + d);
                }
            }
        }
        int32_t old = CI32(c, 0x860 + 4 * u);
        CW32(c, 0x860 + 4 * u, old + fx_mul(CI32(c, t), gms32(0x53ca94)));
        spin_clamp(c, u, old);
    }
    axle_average(c);
    /* the forces */
    int32_t fy = 0, gx = 0, gy = 0;   /* local -0xa4, -0xa8, -0xb0 */
    int32_t ext[3];
    gv3(ext, 0x53c9d8);
    for (int i = 3; i >= 0; i--) {
        uint32_t u = (uint32_t)i;
        if (!CI32(c, 0xbac + 4 * u)) continue;
        int32_t wv[3];
        v3load(wv, c, 0x42c + 12 * u);
        if (fok[i]) {
            int32_t s = vf[i] * 4 - CI32(c, 0x860 + 4 * u);
            CW32(c, 0x870 + 4 * u, s);
            if (iabs(s) < 0x10000) {
                gx = -dot3(fwd[i], ext);
                int32_t q = fx_mul(vf[i], gms32(0x53c9b4));
                if ((gx > 0 && q > 0) || (gx < 0 && q < 0)) gx = iabs(gx) > iabs(q) ? gx - q : 0;
                int32_t r = CI32(c, 0x7bc + 4 * (uint32_t)CI8(c, 0xb1e));
                if (((r > 0 && gx < 0) || (r < 0 && gx > 0) || r == 0) && !CI32(c, 0x838)) gx = 0;
            }
            e = -fx_mul(fx_mul(s, 0x4000), gms32(0x53c9b4));
        }
        if (sok[i]) {
            int32_t vs = dot3(side[i], wv);
            CW32(c, 0x880 + 4 * u, vs);
            if (iabs(vs) < 0x10000) gy = -dot3(side[i], ext);
            fy = -fx_mul(vs, gms32(0x53c9b4));
        }
        e += gx;
        fy += gy;
        int32_t mag, sum[3], dir[3];
        int32_t ax = iabs(e), ay = iabs(fy);
        bool norm = (ax > 0x10000 && ax >= ay) || (ay > 0x10000 && ay >= ax);
        int32_t m = 0, r = 0x10000;
        if (norm) {
            m = ax > 0x10000 && ax >= ay ? ax : ay;
            r = fdiv(0x10000, m);
            gw32(0x53cae0, (uint32_t)m);
            gw32(0x53cc20, (uint32_t)r);
        }
        int32_t a[3], b[3], kx = norm ? fx_mul(e, r) : e, ky = norm ? fx_mul(fy, r) : fy;
        for (int k = 0; k < 3; k++) a[k] = fx_mul(fwd[i][k], kx), b[k] = fx_mul(side[i][k], ky), sum[k] = a[k] + b[k];
        gw3(0x53c9e8, a);
        gw3(0x53c9b8, b);
        gw3(0x53ca88, sum);
        int32_t l1 = len3(sum);
        for (int k = 0; k < 3; k++) dir[k] = l1 ? fx_mul(sum[k], (int32_t)(0x100000000LL / l1)) : 0;
        int32_t l2 = len3(sum);
        for (int k = 0; k < 3; k++) dir[k] = l2 ? fx_mul(sum[k], (int32_t)(0x100000000LL / l2)) : 0;
        mag = fx_mul(l2, l1);
        if (norm) mag = fx_mul(mag, m);
        e = ax;
        fy = ay;
        /* the grip ellipse */
        int32_t ex = CI32(c, 0x84 + 0x24 * u), ey = CI32(c, 0x8c + 0x24 * u), bon = CI32(c, 0x1a8 + 12 * u);
        int32_t kxx = ex + bon, kyy = ey + bon;
        if (i >= 2) kxx -= 0x4ccc, kyy -= 0x4ccc;
        int32_t lx, ly;
        if (e < 0x6666) {
            ly = fx_mul(CI32(c, 0xa5c + 4 * u), kyy);
            lx = fx_mul(CI32(c, 0xa4c + 4 * u), kxx);
        } else if (fy < 0x6666) {
            ly = CI32(c, 0xa5c + 4 * u);
            lx = CI32(c, 0xa4c + 4 * u);
        } else {
            int16_t a1 = atan2_units(fx_mul(e, kxx), fy);
            int32_t c1 = fx_mul(kxx, g_sin[(a1 + 0x400) & 0xfff]), s1 = g_sin[a1 & 0xfff];
            lx = fx_mul((int32_t)Fx_Sqrt((uint32_t)(fx_mul(c1, c1) + fx_mul(s1, s1))), CI32(c, 0xa4c + 4 * u));
            int16_t a2 = atan2_units(fx_mul(e, kyy), fy);
            int32_t c2 = fx_mul(kyy, g_sin[(a2 + 0x400) & 0xfff]), s2 = g_sin[a2 & 0xfff];
            ly = fx_mul((int32_t)Fx_Sqrt((uint32_t)(fx_mul(c2, c2) + fx_mul(s2, s2))), CI32(c, 0xa5c + 4 * u));
        }
        if (mag > lx) mag = ly;
        for (uint32_t k = 0; k < 3; k++) CW32(c, 0x648 + 12 * u + 4 * k, fx_mul(dir[k], mag));
        /* rolling resistance */
        if (CI32(c, 0x90 + 0x24 * u)) {
            int32_t d = dot3(wv, fwd[i]);
            if (d) {
                int32_t f = -fx_mul(d / 4, fx_mul(CI32(c, 0x8b4), CI32(c, 0x90 + 0x24 * u)));
                for (uint32_t k = 0; k < 3; k++) CW32(c, 0x648 + 12 * u + 4 * k, CI32(c, 0x648 + 12 * u + 4 * k) + fx_mul(fwd[i][k], f));
            }
        }
    }
    slope_push(c, gms32(0x53c9b4));
}
