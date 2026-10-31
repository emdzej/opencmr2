/* Car_WheelHeading 0x434140, ported from the disassembly: the heading row (+0x3f0) kept square to the
   body's up row and eased towards its forward row (faster on the throttle), and the front wheels'
   frame (+0x3b4 forward, +0x3a8 side) as the heading turned about up by the steering angle (+0xb10),
   which a damaged rack (damage +0x3d8) makes wobble by the position's bits. */
#include "sim/wheel_heading.h"
#include "fixmath.h"
#include "game/gstate.h"
#include <math.h>

static void v3load(int32_t v[3], const Car *c, uint32_t o) { for (uint32_t k = 0; k < 3; k++) v[k] = CI32(c, o + 4 * k); }
static void v3store(Car *c, uint32_t o, const int32_t v[3]) { for (uint32_t k = 0; k < 3; k++) CW32(c, o + 4 * k, v[k]); }
static int32_t dot3(const int32_t a[3], const int32_t b[3]) { return fx_mul(a[0], b[0]) + fx_mul(a[1], b[1]) + fx_mul(a[2], b[2]); }
static int32_t len3(const int32_t v[3]) { return (int32_t)Fx_Sqrt((uint32_t)fx_mul(v[0], v[0]) + (uint32_t)fx_mul(v[1], v[1]) + (uint32_t)fx_mul(v[2], v[2])); }
static int32_t iabs(int32_t v) { return v < 0 ? -v : v; }

void Car_WheelHeading(Car *c)
{
    int16_t ang = 0;
    if (iabs(CI32(c, 0x778)) >= 0x28f) {
        ang = (int16_t)(c->s[0xb10] | c->s[0xb11] << 8);
        int32_t dmg = DI32(Damage_Get(CI8(c, 0xb1a)), 0x3d8);
        if (dmg > 0) {
            int32_t x = iabs(CI32(c, 0x2d0)), z = iabs(CI32(c, 0x2d8)), d = x - z < 0 ? z - x : x - z;
            int32_t e = fx_mul(0x20000, (d % 0x401 - 0x200) << 6);
            if (CI32(c, 0x778) < 0x10000) e = fx_mul(e, CI32(c, 0x778));
            e = fx_mul(e, dmg);
            ang = (int16_t)(ang + (int16_t)(int64_t)llrint((double)e * (1.0 / 5760.0)));
        }
    }
    int32_t fwd[3], up[3], h[3];
    v3load(fwd, c, 0x360);
    v3load(up, c, 0x36c);
    v3load(h, c, 0x3f0);
    int32_t p = dot3(up, h);
    for (int k = 0; k < 3; k++) h[k] -= fx_mul(up[k], p);
    Vec_Unit(h);
    int32_t d[3];
    for (int k = 0; k < 3; k++) d[k] = fwd[k] - h[k];
    int32_t s = fx_mul(len3(d), 0x13333);
    int32_t k2 = fx_mul(fx_mul(0xcccd, CI32(c, 0x79c)) + 0x3333, gms32(0x519c8c));
    s = fx_mul(s, k2);
    if (s >= 0x10000) {
        for (int k = 0; k < 3; k++) h[k] = fwd[k];
    } else {
        for (int k = 0; k < 3; k++) h[k] += fx_mul(d[k], s);
        Vec_Unit(h);
    }
    v3store(c, 0x3f0, h);
    int32_t w[3];
    if (!ang) {
        for (int k = 0; k < 3; k++) w[k] = h[k];
    } else {
        int32_t m[16];
        int32_t s2 = g_sin[-ang & 0xfff], co = g_sin[(-ang + 0x400) & 0xfff], kk = 0x10000 - co;
        int32_t xx = fx_mul(up[0], up[0]), yy = fx_mul(up[1], up[1]), zz = fx_mul(up[2], up[2]);
        int32_t xy = fx_mul(fx_mul(up[0], up[1]), kk), xz = fx_mul(fx_mul(up[0], up[2]), kk), yz = fx_mul(fx_mul(up[2], up[1]), kk);
        m[0] = xx + fx_mul(co, 0x10000 - xx); m[1] = xy - fx_mul(up[2], s2); m[2] = xz + fx_mul(up[1], s2);
        m[4] = xy + fx_mul(up[2], s2); m[5] = yy + fx_mul(co, 0x10000 - yy); m[6] = yz - fx_mul(up[0], s2);
        m[8] = xz - fx_mul(up[1], s2); m[9] = fx_mul(up[0], s2) + yz; m[10] = fx_mul(co, 0x10000 - zz) + zz;
        for (int k = 0; k < 3; k++) w[k] = fx_mul(m[8 + k], h[2]) + fx_mul(m[4 + k], h[1]) + fx_mul(m[k], h[0]);
        Vec_Unit(w);
    }
    v3store(c, 0x3b4, w);
    int32_t sd[3] = {fx_mul(w[1], up[2]) - fx_mul(w[2], up[1]), fx_mul(w[2], up[0]) - fx_mul(w[0], up[2]),
                     fx_mul(w[0], up[1]) - fx_mul(w[1], up[0])};
    v3store(c, 0x3a8, sd);
}
