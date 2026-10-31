/* Car_SteerBody 0x436630 (named Car_BodyContacts before it was read), ported from the disassembly:
   with a wheel down, the yaw rate damped, the forward row turned towards the front wheels' heading
   (+0xb10) by at most 0.012 a step, the side and up rows squared to it, a side force at the front
   wheels from the steering position (+0x81c, doubled on the handbrake), and the speed cut when the
   ground's normal changes under the car. */
#include "sim/steer_body.h"
#include "fixmath.h"
#include "game/gstate.h"

static void v3load(int32_t v[3], const Car *c, uint32_t o) { for (uint32_t k = 0; k < 3; k++) v[k] = CI32(c, o + 4 * k); }
static void v3store(Car *c, uint32_t o, const int32_t v[3]) { for (uint32_t k = 0; k < 3; k++) CW32(c, o + 4 * k, v[k]); }
static int32_t dot3(const int32_t a[3], const int32_t b[3]) { return fx_mul(a[0], b[0]) + fx_mul(a[1], b[1]) + fx_mul(a[2], b[2]); }
static int32_t len3(const int32_t v[3]) { return (int32_t)Fx_Sqrt((uint32_t)fx_mul(v[0], v[0]) + (uint32_t)fx_mul(v[1], v[1]) + (uint32_t)fx_mul(v[2], v[2])); }
static int32_t inv(int32_t v) { return (int32_t)(0x100000000LL / v); }

void Car_SteerBody(Car *c)
{
    if (!c->s[0xb28] || !CI32(c, 0xb74)) return;
    int32_t dt = gms32(0x519c8c), a = fx_mul(0x624, dt), lim = fx_mul(0x312, dt);
    int32_t w = fx_mul(CI32(c, 0x424), 0x3e80000);
    w -= fx_mul(w, a);
    CW32(c, 0x424, fx_mul(w, 0x41));
    int32_t dir[3] = {0, 0, 0}, fwd[3], v[3];
    v3load(fwd, c, 0x360);
    v3load(v, c, 0x408);
    int32_t sp = CI32(c, 0x778);
    if ((sp < 0 ? -sp : sp) >= 0x28f) {
        int16_t h = (int16_t)(c->s[0xb10] | c->s[0xb11] << 8);
        int32_t loc[3] = {g_sin[(uint16_t)(h + 0x400) & 0xfff], 0, -g_sin[(uint16_t)h & 0xfff]}, m[16];
        for (uint32_t i = 0; i < 12; i++) m[i] = CI32(c, 4 * i);
        for (int k = 0; k < 3; k++) dir[k] = fx_mul(m[8 + k], loc[2]) + fx_mul(m[4 + k], loc[1]) + fx_mul(m[k], loc[0]);
        int32_t s = fx_mul(CI32(c, 0x778), 0x2604);
        if (dot3(fwd, v) < 0) s = -s;
        for (int k = 0; k < 3; k++) dir[k] = fx_mul(dir[k], s);
    }
    int32_t t[3];
    for (int k = 0; k < 3; k++) t[k] = fwd[k] + dir[k];
    Vec_Unit(t);
    int32_t d[3];
    for (int k = 0; k < 3; k++) d[k] = t[k] - fwd[k];
    int32_t dl = len3(d);
    if (dl < lim) {
        v3store(c, 0x360, t);
    } else {
        int32_t r = inv(dl);
        for (int k = 0; k < 3; k++) t[k] = fwd[k] + fx_mul(fx_mul(d[k], r), lim);
        Vec_Unit(t);
        v3store(c, 0x360, t);
    }
    v3load(fwd, c, 0x360);
    int32_t side[3];
    v3load(side, c, 0x378);
    int32_t p = dot3(fwd, side);
    for (int k = 0; k < 3; k++) side[k] -= fx_mul(fwd[k], p);
    Vec_Unit(side);
    v3store(c, 0x378, side);
    int32_t up[3] = {fx_mul(side[1], fwd[2]) - fx_mul(side[2], fwd[1]), fx_mul(side[2], fwd[0]) - fx_mul(side[0], fwd[2]),
                     fx_mul(side[0], fwd[1]) - fx_mul(side[1], fwd[0])};
    Vec_Unit(up);
    v3store(c, 0x36c, up);
    /* the side force on the front pair */
    int32_t u = dot3(up, v), g[3];
    for (int k = 0; k < 3; k++) g[k] = v[k] - fx_mul(up[k], u);
    if (len3(g) > 0) {
        int32_t k = fx_mul(fx_mul(0x1e0000, CI32(c, 0x778)), CI32(c, 0x81c));
        if (CI32(c, 0x1d8)) k += fx_mul(k, 0x20000);
        for (uint32_t q = 0; q < 3; q++) {
            int32_t f = fx_mul(side[q], k);
            CW32(c, 0x648 + 4 * q, CI32(c, 0x648 + 4 * q) + f);
            CW32(c, 0x654 + 4 * q, CI32(c, 0x654 + 4 * q) + f);
        }
    }
    /* the speed cut when the ground normal turns against the horizontal direction of travel */
    int32_t hv[3] = {v[0], 0, v[2]}, hl = len3(hv);
    if (hl <= 0) return;
    int32_t r = inv(hl);
    for (int k = 0; k < 3; k++) hv[k] = fx_mul(hv[k], r);
    int32_t n[3], n0[3];
    v3load(n, c, 0x48c);
    v3load(n0, c, 0x498);
    int32_t e = dot3(n0, hv) - dot3(n, hv);
    if (e <= 0x41) return;
    int32_t f = fx_mul(e, CI32(c, 0x9c4));
    if (f > 0x10000) f = 0x10000;
    for (uint32_t q = 0; q < 3; q++) CW32(c, 0x408 + 4 * q, fx_mul(CI32(c, 0x408 + 4 * q), 0x10000 - f));
}
