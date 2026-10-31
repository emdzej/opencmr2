/* Car_CornerImpulse 0x438460, ported from the disassembly: for a car that is not upright (+0xb74
   clear), each touching corner's force (+0x648) along its velocity relative to the ground normal
   motion: the rest of the corner velocity (+0x42c + 12 i) off the normal, as a unit direction, times minus its size
   times the contact mass (+0x8b0), less the size of 0x53c9d8 below 1.0, limited to the corner's
   +0xa48. */
#include "sim/corner_impulse.h"
#include "fixmath.h"
#include "game/gstate.h"

static int32_t len3(const int32_t v[3]) { return (int32_t)Fx_Sqrt((uint32_t)fx_mul(v[0], v[0]) + (uint32_t)fx_mul(v[1], v[1]) + (uint32_t)fx_mul(v[2], v[2])); }

void Car_CornerImpulse(Car *c)
{
    if (CI32(c, 0xb74)) return;
    int32_t n[3] = {CI32(c, 0x48c), CI32(c, 0x490), CI32(c, 0x494)};
    for (int i = 7; i >= 0; i--) {
        uint32_t u = (uint32_t)i;
        if (c->s[0xb2c + u]) continue;
        uint32_t o = 0x480 - 12 * (7 - u);   /* this corner's velocity, 0x42c + 12 i */
        int32_t v[3] = {CI32(c, o), CI32(c, o + 4), CI32(c, o + 8)};
        int32_t p = fx_mul(v[0], n[0]) + fx_mul(v[1], n[1]) + fx_mul(v[2], n[2]), d[3];
        for (int k = 0; k < 3; k++) d[k] = v[k] - fx_mul(n[k], p);
        int32_t l = len3(d);
        if (l <= 0) continue;
        int32_t r = (int32_t)(0x100000000LL / l), dir[3];
        for (int k = 0; k < 3; k++) dir[k] = fx_mul(d[k], r);
        int32_t f = -fx_mul(l, CI32(c, 0x8b0));
        if (l < 0x10000) {
            int32_t g[3] = {gms32(0x53c9d8), gms32(0x53c9dc), gms32(0x53c9e0)};
            f -= len3(g);
        }
        uint32_t lim = 0xa28 - 4 * (7 - u) + 0x20;   /* 0xa2c + 4 i */
        int32_t af = f < 0 ? -f : f;
        if (af > CI32(c, lim)) f = f > 0 ? CI32(c, lim - 0x20) : -CI32(c, lim - 0x20);
        for (uint32_t k = 0; k < 3; k++) CW32(c, 0x648 + 12 * u + 4 * k, fx_mul(dir[k], f));
    }
}
