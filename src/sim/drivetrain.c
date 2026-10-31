/* Car_Drivetrain 0x437dc0: the engine torque (+0x780) and its split to the wheels (+0x850-+0x85c),
   then the tyres (0x4387a0) and the slip smoothing (0x437fd0). */
#include "sim/drivetrain.h"
#include "fixmath.h"
#include "sim/tyres.h"

/* 0x437fd0: per wheel on an unchanged surface (+0xabe = +0xaae), touching (+0xbac) and upright, the
   smoothed slips (+0x890, +0x8a0) follow the slips' sizes (+0x870, +0x880) up at the wheel's rate
   (+0x98 + 0x24 i) and drop at once; otherwise both 0 */
static void slip_smooth(Car *c)
{
    for (uint32_t i = 0; i < 4; i++) {
        uint32_t o = 0x870 + 4 * i, s = 0xaae + 2 * i;
        bool same = (c->s[s + 0x10] | c->s[s + 0x11] << 8) == (c->s[s] | c->s[s + 1] << 8);
        if (same && CI32(c, o + 0x33c) && CI32(c, 0xb74)) {
            for (uint32_t k = 0; k < 2; k++) {
                uint32_t src = o + 0x10 * k, dst = o + 0x20 + 0x10 * k;
                int32_t v = CI32(c, src);
                if (v < 0) v = -v;
                int32_t d = v - CI32(c, dst);
                if (d < 0) CW32(c, dst, v);
                else CW32(c, dst, CI32(c, dst) + fx_mul(d, CI32(c, 0x98 + 0x24 * i)));
            }
        } else {
            CW32(c, o + 0x30, 0);
            CW32(c, o + 0x20, 0);
        }
    }
}

/* The engine: below the limiter in gear, the throttle (+0x79c) times the damage record's +0x404, less
   the friction (+0x784) times the revs squared; during a shift (+0xb84) only the friction; at or
   over the limiter, or in neutral, nothing; a stalled engine (+0xb78) brakes by +0x7b0. In gear,
   the torque through the ratio (+0x7bc) split by the differential (+0x7b4): half of the share to each
   front wheel, half of the rest to each rear. */
void Car_Drivetrain(Car *c)
{
    int32_t rev = CI32(c, 0x7a4);
    if (!CI32(c, 0xb78)) {
        if (CI32(c, 0x794) <= rev) CW32(c, 0x780, 0);
        else if (CI8(c, 0xb1e) && CI32(c, 0xb84) == 1) CW32(c, 0x780, fx_mul(CI32(c, 0x784), fx_mul(rev, rev)));
        else {
            int32_t t = fx_mul(CI32(c, 0x79c), DI32(Damage_Get(CI8(c, 0xb1a)), 0x404));   /* 0x437f90 */
            CW32(c, 0x780, t - fx_mul(CI32(c, 0x784), fx_mul(rev, rev)));
        }
    } else {
        CW32(c, 0x780, CI8(c, 0xb1e) ? -CI32(c, 0x7b0) : 0);
    }
    if (CI8(c, 0xb1e) && !CI32(c, 0xb84)) {
        int32_t w = fx_mul(CI32(c, 0x780), CI32(c, 0x7bc + 4 * (uint32_t)CI8(c, 0xb1e)));
        int32_t f = fx_mul(w, CI32(c, 0x7b4)) / 2;
        CW32(c, 0x850, f);
        CW32(c, 0x854, f);
        CW32(c, 0x858, w / 2 - f);
        CW32(c, 0x85c, w / 2 - f);
    } else {
        for (uint32_t o = 0x850; o < 0x860; o += 4) CW32(c, o, 0);
    }
    Car_Tyres(c);
    slip_smooth(c);
}
