/* Car_Revs 0x4380c0 (named Car_Gearbox before it was read), ported from the disassembly: the
   engine's revs (+0x7a4). In gear and not shifting they follow the driven wheels' spins through the
   ratio, the axles weighted by the differential split (both axles the front's with the handbrake on
   a front-driven car); out of gear the engine revs on its torque, or runs the starter's curve from the
   counter at +0xafe (with +0xb4c a second phase that sets it to 25,000); over the limiter the revs
   are held there, and more than 0.8 over stalls the drive with a brake torque (+0x7b0, +0xb78). */
#include "sim/gearbox.h"
#include "fixmath.h"
#include "game/gstate.h"
#include <math.h>

static int16_t afe(const Car *c) { return (int16_t)(c->s[0xafe] | c->s[0xaff] << 8); }

void Car_Revs(Car *c)
{
    int8_t g = CI8(c, 0xb1e);
    if (g && !CI32(c, 0xb84)) {
        int32_t r = CI32(c, 0x7bc + 4 * (uint32_t)g);
        int32_t fr = fx_mul(CI32(c, 0x860), r) + fx_mul(CI32(c, 0x864), r), rr;
        if (CI32(c, 0x1d8) && CI32(c, 0x7b4)) rr = fr;
        else rr = fx_mul(CI32(c, 0x868), r) + fx_mul(CI32(c, 0x86c), r);
        rr /= 2;
        int32_t f2 = fr / 2;
        int32_t t = fx_mul(f2 - rr, CI32(c, 0x7b4)) - CI32(c, 0x7a4) + rr;
        CW32(c, 0x7a4, CI32(c, 0x7a4) + fx_mul(t, 0x10000));
    } else if (!CI32(c, 0xb4c)) {
        int16_t a = afe(c);
        if (!a) {
            CW32(c, 0x7a4, CI32(c, 0x7a4) + fx_mul(CI32(c, 0x780), 0xa0000));
        } else if (a > 0) {
            int32_t k = 0x190000 - fx_mul(0x41, a << 16);
            k = fx_mul(fx_mul(k, 0xa3d), 0x8c0000);
            int16_t ang = (int16_t)X87_MulRound((double)k, 1.0 / 5760.0);
            int32_t s = fx_mul(g_sin[ang & 0xfff], 0x10000);
            CW32(c, 0x7a4, fx_mul(s, CI32(c, 0x794)));
            int16_t na = (int16_t)(a - (int16_t)(fx_mul(gms32(0x519c8c), 0x3e80000) >> 16));
            CW16(c, 0xafe, (uint16_t)(na < 0 ? 0 : na));
            if (a < 0x400) CW32(c, 0x79c, 0x10000);
        }
    } else if (afe(c) > 1000) {
        int16_t na = (int16_t)(afe(c) - (int16_t)(fx_mul(gms32(0x519c8c), 0x3e80000) >> 16));
        CW16(c, 0xafe, (uint16_t)(na < 0 ? 0 : na));
    } else {
        CW32(c, 0xb4c, 0);
        CW16(c, 0xafe, 25000);
    }
    int32_t rev = CI32(c, 0x7a4), lim = CI32(c, 0x794);
    if (rev < 0) {
        CW32(c, 0x7a4, 0);
        CW32(c, 0xb78, 0);
    } else if (rev <= lim) {
        CW32(c, 0xb78, 0);
    } else {
        int32_t over = rev - lim;
        if (over > 0xcccc) {
            int32_t f = fx_mul(over - 0xcccc, 0x10000);
            if (f > 0x10000) f = 0x10000;
            CW32(c, 0x7b0, fx_mul(f, 0x51eb));
            CW32(c, 0xb78, 1);
        } else {
            CW32(c, 0xb78, 0);
        }
        CW32(c, 0x7a4, lim);
    }
}
