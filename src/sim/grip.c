/* Car_SurfaceGrip 0x4781d0: each corner's grip coefficients (+0x80-+0xa0 + 0x24 i) from its surface
   (+0xaae + 2 i), each blended towards the surface's wet twin (0x51e8e8) by the weather value; the
   wheels' coefficients softened by their smoothed slips (+0x890, +0x8a0); the wheels' tyre grip
   (+0x1a4 + 12 i) by the set-up's tyre choice (+0xb29), their two surface bytes (+0x1a0), and the
   surface's roughness (+0xa78, followed by +0xa74 at 0.2 a step). The tables are the exe's. */
#include "sim/grip.h"
#include "fixmath.h"
#include "game/gstate.h"

static int32_t lerp(uint32_t tab, uint32_t stride, int s, int w, int32_t t)
{
    int32_t a = gms32(tab + stride * (uint32_t)s), b = gms32(tab + stride * (uint32_t)w);
    return a + fx_mul(b - a, t);
}

static int32_t fdiv(int32_t a, int32_t b) { return b ? (int32_t)(((int64_t)a << 16) / b) : 0; }

void Car_SurfaceGrip(Car *c, int32_t wet)
{
    for (int i = 7; i >= 0; i--) {
        uint32_t u = (uint32_t)i, r = 0x84 + 0x24 * u;
        int s = (int16_t)(c->s[0xaae + 2 * u] | c->s[0xaaf + 2 * u] << 8), w = gm8(0x51e8e8 + (uint32_t)s);
        int32_t a0 = lerp(0x51dfb8, 8, s, w, wet), a1 = lerp(0x51dfbc, 8, s, w, wet);
        int32_t b0 = lerp(0x51e138, 8, s, w, wet), b1 = lerp(0x51e13c, 8, s, w, wet);
        int32_t cc = lerp(0x51e2b8, 4, s, w, wet), d = lerp(0x51e4f8, 4, s, w, wet), e = lerp(0x51e5b8, 4, s, w, wet);
        int32_t f = lerp(0x51e738, 4, s, w, wet), g0 = lerp(0x51e378, 8, s, w, wet), g1 = lerp(0x51e37c, 8, s, w, wet);
        int32_t h = lerp(0x51e678, 4, s, w, wet);
        if (i < 4) {
            int32_t sl = CI32(c, 0x890 + 4 * u);
            if (!sl) {
                CW32(c, r + 4, b0);
                CW32(c, r - 4, a0);
                CW32(c, r, a1);
                CW32(c, r + 8, b1);
            } else {
                if (sl > 0x10000) sl = 0x10000;
                int32_t g = fx_mul(sl, g0);
                CW32(c, r - 4, g + a0);
                CW32(c, r + 4, g + b0);
                CW32(c, r, fdiv(fx_mul(a0, a1), CI32(c, r - 4)));
                CW32(c, r + 8, fdiv(fx_mul(b0, b1), CI32(c, r + 4)));
            }
            int32_t la = CI32(c, 0x8a0 + 4 * u);
            if (la) {
                if (la > 0x10000) la = 0x10000;
                int32_t g = fx_mul(la, g1);
                CW32(c, r, CI32(c, r) + g);
                CW32(c, r + 8, CI32(c, r + 8) + g);
            }
        } else {
            CW32(c, r, a1);
            CW32(c, r - 4, a0);
            CW32(c, r + 4, b0);
            CW32(c, r + 8, b1);
        }
        CW32(c, r + 0x14, cc);
        CW32(c, r + 0x10, f);
        CW32(c, r + 0x1c, e);
        CW32(c, r + 0x18, d);
        CW32(c, r + 0xc, h);
    }
    int s13 = 0, s24 = 0;
    for (int i = 3; i >= 0; i--) {
        uint32_t u = (uint32_t)i;
        int s = (int16_t)(c->s[0xaae + 2 * u] | c->s[0xaaf + 2 * u] << 8), w = gm8(0x51e8e8 + (uint32_t)s);
        c->s[0x1a0 + 12 * u] = gm8(0x51e7f8 + 2 * (uint32_t)s);
        c->s[0x1a1 + 12 * u] = gm8(0x51e7f9 + 2 * (uint32_t)s);
        uint32_t tyre = c->s[0xb29] * 9u;
        int32_t q = fx_mul(gms32(0x51de5c + 4 * (gm8(0x51e918 + (uint32_t)s) + tyre)), 0x51e);
        int32_t qw = fx_mul(gms32(0x51de5c + 4 * (gm8(0x51e918 + (uint32_t)w) + tyre)), 0x51e);
        CW32(c, 0x1a8 + 12 * u, 0);
        CW32(c, 0x1a4 + 12 * u, q + fx_mul(qw - q, wet));
        s13 += gm8(0x51e858 + (uint32_t)s);
        s24 += gm8(0x51e858 + (uint32_t)w);
    }
    int32_t base = (int32_t)((uint32_t)(int16_t)s13 & 0xfffffffcu) * 0x4000;
    int32_t v = fx_mul(base + fx_mul(((int16_t)s24 >> 2) * 0x10000 - base, wet), 0x28f);
    CW32(c, 0xa78, v);
    int32_t d = v - CI32(c, 0xa74), ad = d < 0 ? -d : d;
    if (ad < 0x3334) CW32(c, 0xa74, v);
    else CW32(c, 0xa74, CI32(c, 0xa74) + (d > 0 ? 0x3333 : -0x3333));
}
