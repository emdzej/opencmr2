/* The controls pass of the step, 0x493520 (Car_Controls): the driver's inputs (+0x1d0 steer right,
   +0x1d1 left, +0x1d2 throttle, +0x1d3 brake, all 0-63; +0x1d4 the gear request; +0x1d8 the handbrake)
   into throttle (+0x79c), brake (+0x838), handbrake (+0x848), the gearbox and the steering angle
   (+0xb10, in 4,096 units, and +0xb14). Two conversions in it are floating point, as in the original:
   16.16 degrees times 1/5760 (0x511300) or -1/5760 (0x511308), rounded to nearest (x87 FISTP). */
#include "game/balls.h"
#include "sim/controls.h"
#include "fixmath.h"
#include "game/gstate.h"
#include "pending.h"
#include "world/tod.h"
#include <math.h>

static Car *c;         /* DAT_0059226c */
static uint8_t *dmg;   /* DAT_00592270 */

static int32_t steer_max(void) { return gms32(0x592160); }
static int32_t s164(void) { return gms32(0x592164); }

static int32_t deg_to_units(int32_t v, bool neg)
{
    double k = neg ? -(1.0 / 5760.0) : 1.0 / 5760.0;   /* the doubles at 0x511308, 0x511300 */
    return (int32_t)X87_MulRound((double)v, k);
}

static int32_t input(uint32_t o)   /* an input byte as a fraction, at most 1.0 */
{
    int32_t v = fx_mul(c->s[o] << 16, 0x410);
    return (uint32_t)v > 0x10000 ? 0x10000 : v;
}

/* 0x4932f0: the boost of option 5 (in the modes where the car table says so) and option 0's effect;
   the handbrake input cleared */
static void boost(void)
{
    if (!Gs_Option(5)) {
        if (Gs_Option(0)) {
            bool night = Gs_Mode() == 0 && (gm32(0x52f2ac) & 0x1f) == 8 && (gm32(0x52f2ac) & 0x3e0) == 0x160;
            if ((night || Gs_SelBit24() || Gs_SelBit27()) && CI32(c, 0xb9c) && CI32(c, 0x1d8))
                Balls_Fire(CI8(c, 0xb1a));   /* 0x47d5a0 */
            CW32(c, 0x1d8, 0);
        }
        return;
    }
    CW32(c, 0xa84, 0);
    /* the car's type in the tables at 0x537f3c and 0x537dd0 (pointers into the heap) */
    PENDING_NOTE(0x41b390, "Car_TypeTable");
}

/* 0x4946c0: the digital throttle ramp (+0x7a0, along a quarter sine of 90 degrees) */
static void throttle_ramp(void)
{
    if (!c->s[0x1d2] && (!CI32(c, 0xb94) || !c->s[0x1d3])) {
        int32_t r = CI32(c, 0x7a0) - fx_mul(CI32(c, 0x790), 0x20000);
        CW32(c, 0x7a0, r);
        if (r < 0) {
            CW32(c, 0x7a0, 0);
            CW32(c, 0x79c, 0);
        } else {
            uint16_t a = (uint16_t)deg_to_units(fx_mul(r, 0x5a0000), false);
            CW32(c, 0x79c, fx_mul(CI32(c, 0x788), g_sin[a & 0xfff]));
        }
        if (CI32(c, 0x79c) && !CI32(c, 0xb94)) CW8(c, 0x1d2, 0x3f);
        return;
    }
    CW32(c, 0x7a0, CI32(c, 0x7a0) + CI32(c, 0x790));
    if (CI32(c, 0x7a0) > 0x10000) {
        CW32(c, 0x7a0, 0x10000);
        CW32(c, 0x79c, CI32(c, 0x788));
        return;
    }
    uint16_t a = (uint16_t)deg_to_units(fx_mul(CI32(c, 0x7a0), 0x5a0000), false);
    CW32(c, 0x79c, fx_mul(CI32(c, 0x788), g_sin[a & 0xfff]));
}

/* a ramp: +ramp grows by +step to 1.0 along a quarter sine of out's full value */
static void ramp(bool on, uint32_t ramp_o, uint32_t step_o, uint32_t out_o, uint32_t full_o)
{
    if (!on) {
        CW32(c, ramp_o, 0);
        CW32(c, out_o, 0);
        return;
    }
    CW32(c, ramp_o, CI32(c, ramp_o) + CI32(c, step_o));
    if (CI32(c, ramp_o) > 0x10000) {
        CW32(c, ramp_o, 0x10000);
        CW32(c, out_o, CI32(c, full_o));
        return;
    }
    uint16_t a = (uint16_t)deg_to_units(fx_mul(CI32(c, ramp_o), 0x5a0000), false);
    CW32(c, out_o, fx_mul(CI32(c, full_o), g_sin[a & 0xfff]));
}

/* The best gear for the revs (+0x7a4 through the current ratio, then each ratio's inverse): the
   highest above, below 98% of the limiter (sixth always qualifies). */
static int best_gear(void)
{
    int32_t best_v = (int32_t)0xd8f00000, p = fx_mul(CI32(c, 0x7a4), CI32(c, 0x7dc + 4 * (uint32_t)CI8(c, 0xb1e)));
    int best = 0;
    for (uint32_t o = 0x7c0, k = 1; o < 0x7d8; o += 4, k++) {
        int32_t v = fx_mul(p, CI32(c, o));
        if (best_v < v && (v < fx_mul(CI32(c, 0x794), 0xfae1) || o == 0x7d4)) best = (int)k, best_v = v;
    }
    return best;
}

/* The damaged gearbox (damage +0x278 above 0.7) may skip a gear up, by a value taken from the corner
   position's bits. */
static bool gear_skip(void)
{
    int32_t u = DI32(dmg, 0x278);
    if (u <= 0xb333) return false;
    int32_t l = fx_mul(u - 0xb333, 0x3553f);
    l = l > 0x10000 ? 0x10000 : l < 0 ? 0 : l;
    int32_t a = CI32(c, 0x270), b = CI32(c, 0x278);
    a = a < 0 ? -a : a;
    b = b < 0 ? -b : b;
    int32_t d = a - b < 0 ? b - a : a - b;
    return (d % 0x401) * 0x40 < fx_mul(l, 0x4ccc);
}

/* 0x493890: a manual shift (1 up, 0xff down, 7 to reverse) */
static void manual_shift(void)
{
    uint8_t r = c->s[0x1d4];
    if (r == 1) {
        int8_t g = CI8(c, 0xb1e);
        if (g < 6) {
            CW8(c, 0xb20, (uint8_t)(g + 1));
            CW8(c, 0xb24, dmg[0x468]);
            if (gear_skip() && CI8(c, 0xb20) < 6) CW8(c, 0xb20, (uint8_t)(CI8(c, 0xb20) + 1));
            CW32(c, 0xb84, 1);
        }
    } else if (r == 7) {
        CW8(c, 0xb20, 0);
        CW32(c, 0xb84, 1);
        CW8(c, 0xb24, dmg[0x468]);
    } else if (r == 0xff) {
        int8_t g = CI8(c, 0xb1e);
        if (g < 7) {
            CW8(c, 0xb20, (uint8_t)(g - 1));
            CW32(c, 0xb84, 1);
            CW8(c, 0xb24, dmg[0x468]);
            if (CI8(c, 0xb20) < 0) CW8(c, 0xb20, 7);
        }
    }
}

/* 0x493a40: the manual box's hint (+0xb98) that the gear is at or below the best one */
static void shift_hint(void)
{
    int best = CI32(c, 0x7bc + 4 * (uint32_t)CI8(c, 0xb1e)) < 1 ? 7 : best_gear();
    CW32(c, 0xb98, CI8(c, 0xb1e) <= best ? 1 : 0);
}

static int32_t forward_speed(void)
{
    return fx_mul(CI32(c, 0x368), CI32(c, 0x410)) + fx_mul(CI32(c, 0x360), CI32(c, 0x408)) + fx_mul(CI32(c, 0x364), CI32(c, 0x40c));
}

/* 0x493b30: the automatic box, with a wheel on the ground: to the best gear unless it would change
   back within ten steps, or shift up at speed on the throttle; into reverse from first when stopped
   on the brake, and out again on the throttle */
static void auto_shift(void)
{
    if (!CI32(c, 0xb9c)) return;
    bool touching = false;
    for (uint32_t i = 0; i < 4; i++)
        if (!c->s[0xb2c + i]) { touching = true; break; }
    if (CI8(c, 0xb21) > 0) CW8(c, 0xb21, (uint8_t)(CI8(c, 0xb21) - 1));
    if (!CI32(c, 0xb94) && touching) {
        int best = best_gear(), g = CI8(c, 0xb1e);
        if (best != g) {
            int32_t sp = CI32(c, 0x870);
            if (sp < 0) sp = -sp;
            uint8_t dir = c->s[0xb22];
            if ((sp < 0x8000 || best < g || !c->s[0x1d2]) &&
                (!CI8(c, 0xb21) || ((dir != 2 || g <= best) && (dir != 1 || best <= g)))) {
                CW32(c, 0xb84, 1);
                CW8(c, 0xb22, CI8(c, 0xb1e) < best ? 2 : 1);
                CW8(c, 0xb21, 10);
                if (CI8(c, 0xb1e) < best && gear_skip() && best < 6) best++;
                CW8(c, 0xb20, (uint8_t)best);
                CW8(c, 0xb24, dmg[0x468]);
            }
        }
    }
    if (c->s[0x1d3] && CI8(c, 0xb1e) == 1 && !c->s[0x1d2] && CI32(c, 0x7a4) < 0x1999 && forward_speed() < 0x1999) {
        CW8(c, 0xb20, 7);
        CW32(c, 0xb84, 1);
        CW32(c, 0xb94, 1);
        CW8(c, 0xb24, dmg[0x468]);
        return;
    }
    if (c->s[0x1d2] && !c->s[0x1d3] && CI8(c, 0xb1e) == 7) {
        CW8(c, 0xb20, 1);
        CW32(c, 0xb84, 1);
        CW32(c, 0xb94, 0);
        CW8(c, 0xb24, dmg[0x468]);
    }
}

/* 0x4943d0: the steering lock (0x592160), lowered with the forward speed when +0xb1c bit 0 is set */
static void steer_lock(void)
{
    if (c->s[0xb1c] & 1) {
        int32_t v = forward_speed();
        if (v < 0) v = -v;
        int32_t u = fx_mul(v, CI32(c, 0x828));
        if (u > 0xb332) u = 0xb333;
        gw32(0x592160, (uint32_t)(fx_mul(0x10000 - u, (int16_t)(c->s[0xb16] | c->s[0xb17] << 8)) * 0x1680));
        return;
    }
    gw32(0x592160, (uint32_t)((int16_t)(c->s[0xb16] | c->s[0xb17] << 8) * 0x1680));
}

/* 0x494540: the digital steering input's build-up (+0x818, -1 to 1, reset on a change of side) */
static void steer_input(void)
{
    if (!c->s[0x1d0]) {
        int32_t v = 0;
        if (c->s[0x1d1]) {
            if (CI32(c, 0x818) > 0) CW32(c, 0x818, 0);
            v = (int32_t)0xffff0000;
            CW32(c, 0x818, CI32(c, 0x818) - 0x10000);
            if (-0x10001 < CI32(c, 0x818)) return;
        }
        CW32(c, 0x818, v);
        return;
    }
    if (CI32(c, 0x818) < 0) CW32(c, 0x818, 0);
    CW32(c, 0x818, CI32(c, 0x818) + 0x10000);
    if (CI32(c, 0x818) > 0x10000) CW32(c, 0x818, 0x10000);
}

/* 0x4945d0: the steering rate (0x592164) from +0x824, the constant 0x519c8c, the build-up, and with
   +0xb1c bit 2 the square of the distance from full lock */
static void steer_rate(void)
{
    steer_input();
    if (!c->s[0x1d1] && !c->s[0x1d0]) { gw32(0x592164, 0); return; }
    int32_t l = fx_mul(CI32(c, 0x824), gms32(0x519c8c));
    if (c->s[0xb1c] & 2) {
        int32_t a = CI32(c, 0x81c);
        if (a < 0) a = -a;
        l = fx_mul(l, fx_mul(a - 0x10000, a - 0x10000));
    }
    int32_t b = CI32(c, 0x818);
    if (b < 0) b = -b;
    int32_t u = fx_mul(l, b);
    if (c->s[0x1d1]) gw32(0x592164, (uint32_t)-u);
    else if (c->s[0x1d0]) gw32(0x592164, (uint32_t)u);
}

/* 0x494110: digital steering: the wheel position (+0x81c, -1 to 1) moved by the rate and a return
   (+0x820, against the lock's side), both scaled by the slip (+0x94, +0xb8) and +0x804; the angle
   (+0xb12) from it, and +0xb10 following it by a step that grows with the slip */
static void steer_digital(void)
{
    steer_lock();
    steer_rate();
    int32_t ret = CI32(c, 0x820);
    if (!CI32(c, 0x81c)) ret = 0;
    else if (fx_mul(steer_max(), CI32(c, 0x81c)) > 0) ret = -ret;
    int32_t w = 0x10000 - fx_mul(CI32(c, 0xb8) + CI32(c, 0x94), 0x8000);
    gw32(0x592164, (uint32_t)fx_mul(fx_mul(s164(), w), CI32(c, 0x804)));
    ret = fx_mul(fx_mul(ret, w), CI32(c, 0x804));
    bool right = c->s[0x1d0] != 0, left = c->s[0x1d1] != 0;
    int32_t p = CI32(c, 0x81c);
    enum { L223, L235, L2C1, DONE } at;
    if (!right) at = left ? L223 : L235;
    else at = ret < 1 ? L223 : L235;
    if (at == L223) {
        if (!left) at = L2C1;
        else if (ret < 0) at = L235;
        else {
            p += s164();
            if (p < -0x10000) p = -0x10000;
            else if (p >= 1) p = 0;
            at = DONE;
        }
    }
    if (at == L2C1) {
        if (right) {
            p += s164();
            if (p > 0x10000) p = 0x10000;
            else if (p < 0) p = 0;
        }
        at = DONE;
    }
    if (at == L235) {
        int32_t t = s164() + ret, ap = p < 0 ? -p : p, at2 = t < 0 ? -t : t;
        p = ap <= at2 ? 0 : p + t;
    }
    CW32(c, 0x81c, p);
    int16_t a = (int16_t)deg_to_units(fx_mul(steer_max(), p), true);
    CW16(c, 0xb12, (uint16_t)a);
    int16_t cur = (int16_t)(c->s[0xb10] | c->s[0xb11] << 8);
    int16_t d = (int16_t)(a - cur);
    int16_t r = (int16_t)((int16_t)(((int64_t)(0x10000 - w) * 0x22) >> 16) + 0x22);
    int ad = d < 0 ? -d : d;
    if (ad < r) CW16(c, 0xb10, (uint16_t)a);
    else CW16(c, 0xb10, (uint16_t)(d > 0 ? cur + r : cur - r));
}

/* 0x493ed0: analog steering: the target from left minus right against the lock; mode 2 takes it at
   once, else +0xb10 follows by the turn-in (+0x7fc) or return (+0x800) rate beyond 0x1f units */
static void steer_analog(void)
{
    int32_t in_rate = CI32(c, 0x7fc), out_rate = CI32(c, 0x800);
    int32_t s = input(0x1d1) - input(0x1d0);
    steer_lock();
    int16_t target;
    if (s < -0x10000) s = -0x10000;
    else if (s > 0x10000) s = 0x10000;
    target = s == 0 ? 0 : (int16_t)deg_to_units(fx_mul(steer_max(), s), false);
    if (CI32(c, 0xb88) == 2) {
        CW16(c, 0xb10, (uint16_t)target);
        CW16(c, 0xb12, (uint16_t)target);
        return;
    }
    int16_t cur = (int16_t)(c->s[0xb10] | c->s[0xb11] << 8);
    int16_t d = (int16_t)(cur - target);
    if (d > 0x800) d = (int16_t)(0x1000 - d);
    if (d > 0x1f || d < -0x1f) {
        int32_t rate;
        if (cur == 0) rate = in_rate;
        else if (cur < 1) rate = d < 1 ? out_rate : in_rate;
        else rate = d < 0 ? in_rate : out_rate;
        d = (int16_t)deg_to_units(fx_mul(d * 0x1680, rate), false);
    }
    CW16(c, 0xb10, (uint16_t)(cur - d));
    CW16(c, 0xb12, (uint16_t)(cur - d));
}

void Car_Controls(Car *car)
{
    c = car;
    dmg = Damage_Get(CI8(car, 0xb1a));
    boost();
    if (!CI32(c, 0xb8c)) throttle_ramp();
    else CW32(c, 0x79c, c->s[0x1d2] ? fx_mul(CI32(c, 0x788), input(0x1d2)) : 0);
    CW32(c, 0xb5c, CI32(c, 0x7bc + 4 * (uint32_t)CI8(c, 0xb1e)) < 0);
    CW32(c, 0xb54, 0);
    if (CI32(c, 0xb94) ? c->s[0x1d2] : c->s[0x1d3]) CW32(c, 0xb54, 1);
    if (!CI8(c, 0xb1f)) {
        if (!CI32(c, 0xb84)) {
            if (CI32(c, 0xb48) == 1 && c->s[0x1d4]) manual_shift();
            else if (CI32(c, 0xb48) == 2) auto_shift();
        } else if (CI8(c, 0xb24) < 1) {
            CW32(c, 0xb84, 0);
            CW8(c, 0xb1e, c->s[0xb20]);
        } else {
            CW8(c, 0xb24, (uint8_t)(CI8(c, 0xb24) - 1));
            CW8(c, 0xb1e, 0);
        }
    }
    if (!CI32(c, 0xb88)) steer_digital();
    else steer_analog();
    if (!CI32(c, 0xb90)) {
        ramp(c->s[0x1d3] && !CI32(c, 0xb94), 0x83c, 0x834, 0x838, 0x82c);
        if (CI32(c, 0xb8c) && CI32(c, 0xb94)) CW32(c, 0x79c, fx_mul(CI32(c, 0x788), input(0x1d3)));
    } else if (!c->s[0x1d3]) {
        CW32(c, 0x838, 0);
    } else {
        int32_t p = input(0x1d3);
        if (!CI32(c, 0xb94)) {
            int32_t u = fx_mul(CI32(c, 0x82c), p), d = u - CI32(c, 0x838), ad = d < 0 ? -d : d;
            if (ad < fx_mul(CI32(c, 0x82c), 0xccc)) CW32(c, 0x838, u);
            else CW32(c, 0x838, CI32(c, 0x838) + fx_mul(d, 0x23d7));
        } else {
            CW32(c, 0x79c, fx_mul(CI32(c, 0x788), p));
            CW32(c, 0x838, 0);
        }
    }
    ramp(CI32(c, 0x1d8) != 0, 0x84c, 0x840, 0x848, 0x844);   /* 0x494960: the handbrake */
    if (CI32(c, 0xb48) == 1) shift_hint();
    int32_t p = fx_mul((int16_t)(c->s[0xb10] | c->s[0xb11] << 8) * 0x1680, 0x20000);
    int32_t ap = p < 0 ? -p : p;
    if (steer_max() < ap) p = p < 1 ? -steer_max() : steer_max();
    CW16(c, 0xb14, (uint16_t)(int16_t)deg_to_units(p, false));
}
