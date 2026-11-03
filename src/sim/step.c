/* The car part of the simulation step (see step.h). */
#include "game/weather.h"
#include "sim/step.h"
#include "sim/car_internal.h"
#include "sim/controls.h"
#include "sim/integrate.h"
#include "sim/suspension.h"
#include "sim/wheel_heading.h"
#include "sim/ground_contact.h"
#include "sim/corner_impulse.h"
#include "sim/drivetrain.h"
#include "sim/gearbox.h"
#include "sim/grip.h"
#include "sim/pose.h"
#include "sim/aerials.h"
#include "sim/damage.h"
#include "sim/sim.h"
#include "fixmath.h"
#include "game/gstate.h"
#include "game/mem.h"
#include "game/sectors.h"
#include "world/section.h"

int g_pending_passes;

/* 0x43b090: the drag-like coefficients from the constant at 0x519c8c */
static void coefficients(Car *c)
{
    int32_t k = gms32(0x519c8c);
    CW32(c, 0x790, fx_mul(k, 0xccc));
    CW32(c, 0x834, fx_mul(k, 0xccc));
    CW32(c, 0x840, fx_mul(k, 0x1999));
    CW32(c, 0x820, fx_mul(k, 0x3333));
}

/* 0x43b020: the first of three damage events (+0xbdc..+0xbe4) latches +0xbfc and sets its counter */
static void damage_latch(Car *c)
{
    for (uint32_t i = 0; i < 3; i++)
        if (CI32(c, 0xbdc + 4 * i)) {
            CW32(c, 0xbfc, 1);
            CW8(c, 0xb25 + i, 200);
            return;
        }
}

/* 0x4340f0: the counter at +0xb1f runs down while the engine speed (+0x778, times 4) stays 0.1 below
   the drive speed (+0x860, or +0x868 without a differential split), and clears when it catches up */
static void clutch_counter(Car *c)
{
    if (!CI8(c, 0xb1f)) return;
    int32_t v = CI32(c, 0x7b4) ? CI32(c, 0x868) : CI32(c, 0x860);
    if (-0x1999 < CI32(c, 0x778) * 4 - v) CW8(c, 0xb1f, 0);
    else CW8(c, 0xb1f, (uint8_t)(CI8(c, 0xb1f) - 1));
}

/* 0x434070: the step's tilt factor (0x53c9d4) from the ground normal's y: 0.9 at 0.94 and above, else
   (y - 0.7) times 4.17, at least 0, times 0.9 */
static void tilt_factor(Car *c)
{
    int32_t y = CI32(c, 0x490);
    if (y >= 0xf0a3) { gw32(0x53c9d4, 0xe666); return; }
    int32_t f = fx_mul(y - 0xb333, 0x42ab5);
    if (f < 0) f = 0;
    gw32(0x53c9d4, (uint32_t)fx_mul(f, 0xe666));
}

/* 0x433fd0: the revs limit (+0x7a4) held at the limiter (+0x794 less the counter +0xb1d times 0.2)
   with a three-step hold */
static void rev_limit(Car *c)
{
    int32_t lim = CI32(c, 0x794) - fx_mul(CI8(c, 0xb1d) << 16, 0x3333);
    if (-0x290 < CI32(c, 0x7a4) - lim) {
        CW32(c, 0x7a4, lim);
        if (!CI8(c, 0xb1d)) CW8(c, 0xb1d, 3);
    }
    if (CI8(c, 0xb1d)) CW8(c, 0xb1d, (uint8_t)(CI8(c, 0xb1d) - 1));
    CW32(c, 0x7ac, CI32(c, 0x7a4));
}

/* 0x432b30: the wheels within 0.8 of their ground (+0xbac..: flag), the count (+0xb28) and the mass
   per touching wheel (+0x8b4) */
void Car_WheelLoad(Car *c)
{
    CW8(c, 0xb28, 0);
    for (uint32_t i = 0; i < 4; i++) {
        int32_t d = CI32(c, 0x8dc + 4 * i) - CI32(c, 0x274 + 12 * i);
        if (d < 0) d = -d;
        if (d < 0xcccc) {
            CW32(c, 0xbac + 4 * i, 1);
            CW8(c, 0xb28, (uint8_t)(c->s[0xb28] + 1));
        } else {
            CW32(c, 0xbac + 4 * i, 0);
        }
    }
    uint8_t n = c->s[0xb28];
    int32_t m = CI32(c, 0x75c);
    CW32(c, 0x8b4, n == 0 ? 0 : n == 4 ? m / 4 : n == 2 ? m / 2 : n == 1 ? m : m / n);
}

/* 0x4b9da0: v times the 3 x 3 of a 16-int matrix (rows at 0, 4, 8: the transposed product) */
static void vec_rot_t(int32_t out[3], const int32_t v[3], const int32_t *m)
{
    for (int r = 0; r < 3; r++) out[r] = fx_mul(m[4 * r + 2], v[2]) + fx_mul(m[4 * r + 1], v[1]) + fx_mul(m[4 * r], v[0]);
}

/* The ground normal in the car's frame (+0x48c through the orientation at +0x00): upright enough
   (y at least 0x107, |x| and |z| within 0.58 and 1.0 of y) keeps +0xb74. */
static void upright(Car *c)
{
    CW32(c, 0xb74, 1);
    int32_t n[3] = {CI32(c, 0x48c), CI32(c, 0x490), CI32(c, 0x494)}, m[12], v[3];
    for (int i = 0; i < 12; i++) m[i] = CI32(c, 4 * (uint32_t)i);
    vec_rot_t(v, n, m);
    int32_t ay = v[1] < 0 ? -v[1] : v[1], ax = v[0] < 0 ? -v[0] : v[0], az = v[2] < 0 ? -v[2] : v[2];
    if (v[1] < 0x107 || fx_mul(ay, 0x93cd) < ax || fx_mul(ay, 0x10000) < az) CW32(c, 0xb74, 0);
}

/* 0x4b8910 for the position: the cell (+0xb00) and up to three neighbours (+0xb02-+0xb06) */
static void car_cell(Car *c)
{
    int32_t p[3] = {CI32(c, 0x2d0), CI32(c, 0x2d4), CI32(c, 0x2d8)};
    int16_t near[3];
    int16_t cell = (int16_t)Section_Find(p);
    Section_Neighbours(p, near);
    CW16(c, 0xb00, (uint16_t)cell);
    for (uint32_t i = 0; i < 3; i++) CW16(c, 0xb02 + 2 * i, (uint16_t)near[i]);
    for (uint32_t k = 0; k < 3; k++) CW32(c, 0x2dc + 4 * k, CI32(c, 0x2d0 + 4 * k));
}

/* Car_CornerVelocities 0x434f50: the eight box corners' velocities (+0x42c + 12 i): the spin
   (+0x420) across the half sizes, through the orientation rows, plus the body's velocity; the upper
   four are the lower four's opposites. */
static void corner_velocities(Car *c)
{
    int32_t wx = CI32(c, 0x420), wy = CI32(c, 0x424), wz = CI32(c, 0x428);
    int32_t hl = CI32(c, 0x204), hh = CI32(c, 0x208), hw = CI32(c, 0x20c);
    int32_t u6 = fx_mul(wy, hw), u7 = fx_mul(wz, hh), u8 = fx_mul(wz, hl), u9 = fx_mul(wx, hw), u10 = fx_mul(wx, hh), u11 = fx_mul(wy, hl);
    for (uint32_t k = 0; k < 3; k++) {
        int32_t m0 = CI32(c, 4 * k), m1 = CI32(c, 0x10 + 4 * k), m2 = CI32(c, 0x20 + 4 * k);
        int32_t a = fx_mul(m0, u6), b = fx_mul(m0, u7), cc = fx_mul(m1, u8), d = fx_mul(m1, u9), e = fx_mul(m2, u10), f = fx_mul(m2, u11);
        int32_t v[4] = {(((cc - f) - e) - d) + b + a, (((d - f) - e) - a) + cc + b, (((f - e) - d) - cc) + b + a, (((f - e) - cc) - a) + d + b};
        for (uint32_t i = 0; i < 4; i++) {
            CW32(c, 0x42c + 12 * i + 4 * k, v[i]);
            CW32(c, 0x42c + 12 * (7 - i) + 4 * k, -v[i]);
        }
    }
    for (uint32_t i = 0; i < 8; i++)
        for (uint32_t k = 0; k < 3; k++) CW32(c, 0x42c + 12 * i + 4 * k, CI32(c, 0x42c + 12 * i + 4 * k) + CI32(c, 0x408 + 4 * k));
}

/* the differential tests' probes before the passes (tools/emu/sim_diff.c) */
#define PROBE(id) do { if (g_sim_probe) g_sim_probe(id, &me, 1); } while (0)

/* Sim_HeadToHeadBalance 0x433e80: two players alone round a circuit (selection bit 24) with the
   catch-up option (0x52afbc bit 11, 0x406440): from 3 points of the course apart (Sectors_Steps of each
   car's +0xb1a) to 15, the leader's final drive (+0x788, from its base +0x78c) is lowered by up to
   0.05 and the other's raised as much */
static void head_to_head(const int16_t *list, int n)
{
    if (Gs_SelBits14() != 2 || n != 2 || !Gs_SelBit24() || !(gm32(0x52afbc) >> 0xb & 1)) return;
    Car *a = Car_Get(list[0]), *b = Car_Get(list[1]);
    int d = Sectors_Steps(CI8(a, 0xb1a)) - Sectors_Steps(CI8(b, 0xb1a));
    int32_t g = d * 0x10000, k;
    if (g < 0) g = -g;
    if (g <= 0x30000) k = 0;
    else if (g < 0xf0000) {
        k = fx_mul(g - 0x30000, 0x1555);
        k = k < 0 ? 0 : k > 0x10000 ? 0x10000 : k;
    } else
        k = 0x10000;
    int32_t u = fx_mul(k, 0xccc);
    Car *lead = d >= 0 ? a : b, *back = d >= 0 ? b : a;
    CW32(lead, 0x788, CI32(lead, 0x78c) - u);
    CW32(back, 0x788, CI32(back, 0x78c) + u);
}

/* Sim_StepCars 0x433890, in its passes, each over every car (last to first). */
void Sim_StepCars(const int16_t *list, int n)
{
    g_pending_passes = 0;
    head_to_head(list, n);   /* 0x433e80 */
    for (int i = n - 1; i >= 0; i--) {
        Car *c = Car_Get(list[i]);
        g_car = c;
        gw32(0x53c9d8, 0);
        gw32(0x53c9dc, 0);
        gw32(0x53c9e0, 0);
        gw32(0x53c9d4, 0);
        const int16_t me = list[i];
        PROBE(110);
        coefficients(c);
        /* the corner history: the last two steps' corners */
        for (uint32_t o = 0x300; o < 0x330; o += 0xc)
            for (uint32_t k = 0; k < 3; k++) {
                CW32(c, o + 0x30 + 4 * k, CI32(c, o + 4 * k));
                CW32(c, o + 4 * k, CI32(c, o - 0x90 + 4 * k));
            }
        for (uint32_t k = 0; k < 3; k++) {
            CW32(c, 0x2f4 + 4 * k, CI32(c, 0x2e8 + 4 * k));
            CW32(c, 0x2e8 + 4 * k, CI32(c, 0x2d0 + 4 * k));
        }
        for (uint32_t k = 0; k < 9; k++) CW32(c, 0x384 + 4 * k, CI32(c, 0x360 + 4 * k));
        CW32(c, 0x968, CI32(c, 0x964));
        CW32(c, 0x964, CI32(c, 0x960));
        if (!CI32(c, 0xbfc)) { PROBE(111); damage_latch(c); }
        PROBE(112);
        Car_SurfaceGrip(c, Weather_CarWet(c));   /* Weather_CarValue 0x460c80 */
        PROBE(113);
        clutch_counter(c);
        PROBE(114);
        Damage_Misfire(c);   /* 0x46b4e0 */
    }
    for (int i = n - 1; i >= 0; i--) {
        Car *c = Car_Get(list[i]);
        g_car = c;
        const int16_t me = list[i];
        PROBE(115);
        Car_Controls(c);
        for (uint32_t o = 0x88c; o > 0x87f; o -= 4) {
            CW32(c, o - 0x10, 0);
            CW32(c, o, 0);
        }
    }
    for (int i = n - 1; i >= 0; i--) upright(Car_Get(list[i]));
    for (int i = n - 1; i >= 0; i--) {
        Car *c = Car_Get(list[i]);
        g_car = c;
        /* the damage record's chip count (+0x469) down by one each step without a scrape (+0x46c) */
        uint8_t *d = Damage_Get(CI8(c, 0xb1a));
        if (!(d[0x46c] | d[0x46d] | d[0x46e] | d[0x46f]) && d[0x469]) d[0x469]--;
        d[0x46c] = d[0x46d] = d[0x46e] = d[0x46f] = 0;
        const int16_t me = list[i];
        PROBE(116);
        corner_velocities(c);
        for (uint32_t o = 0x5e8; o < 0x648; o += 4) CW32(c, o, 0);
        for (uint32_t o = 0x648; o < 0x6a8; o += 4) CW32(c, o, 0);
        CW32(c, 0x6a8, 0);
        int32_t g = fx_mul(gms32(0x53cadc), 0x8000);
        CW32(c, 0x6ac, -fx_mul(CI32(c, 0x75c), g));   /* gravity on the mass */
        CW32(c, 0x6b0, 0);
        PROBE(117);
        Car_WheelLoad(c);
        PROBE(118);
        Car_Suspension(c);
        PROBE(119);
        tilt_factor(c);
        PROBE(120);
        Car_GroundContact(c);
        PROBE(121);
        Car_WheelHeading(c);
    }
    if (g_sim_probe) g_sim_probe(102, list, n);
    for (int i = n - 1; i >= 0; i--) { g_car = Car_Get(list[i]); Car_Drivetrain(g_car); }
    if (g_sim_probe) g_sim_probe(103, list, n);
    for (int i = n - 1; i >= 0; i--) { g_car = Car_Get(list[i]); Car_CornerImpulse(g_car); }
    if (g_sim_probe) g_sim_probe(104, list, n);
    for (int i = n - 1; i >= 0; i--) { g_car = Car_Get(list[i]); Car_Integrate(g_car); }
    if (g_sim_probe) g_sim_probe(105, list, n);
    for (int i = n - 1; i >= 0; i--) {
        Car *c = Car_Get(list[i]);
        g_car = c;
        CW32(c, 0x7a8, CI32(c, 0x7a4));
        Car_Revs(c);
        rev_limit(c);
        for (uint32_t o = 0x5c4; o < 0x5dc; o += 4) CW32(c, o, 0);
    }
    for (int i = n - 1; i >= 0; i--) car_cell(Car_Get(list[i]));
}

/* 0x4458d0: the body lifted out of (or dropped onto) the ground by its corners: the largest gap of a
   corner below its ground (+0x8dc.. less +0x274..), else the smallest of those above it, none when
   all sit on it; the body's height and the eight corners' moved by it */
static int32_t corner_lift(const Car *c)
{
    int32_t up = 0, down = -0x3e80000;
    bool any_up = false, any_down = false;
    for (int k = 7; k >= 0; k--) {
        int32_t d = CI32(c, 0x8dc + 4 * (uint32_t)k) - CI32(c, 0x274 + 12 * (uint32_t)k);
        if (d >= 0 && up < d) any_up = true, up = d;
        if (d < 1 && down < d) any_down = true, down = d;
    }
    return any_up ? up : any_down ? down : 0;
}

static void lift_by(Car *c, int32_t d)
{
    CW32(c, 0x2d4, CI32(c, 0x2d4) + d);
    for (uint32_t k = 0; k < 8; k++) CW32(c, 0x274 + 12 * k, CI32(c, 0x274 + 12 * k) + d);
}

/* the wheels' and the body's last steps moved on (as Net_PoseCar's): with `snap` every record from the
   present (+0x270, +0x2d0), else from the one before */
static void shift_records(Car *c, bool snap)
{
    for (uint32_t w = 0; w < 4; w++) {
        for (uint32_t k = 0; k < 3; k++) {
            uint32_t o = 12 * w + 4 * k;
            CW32(c, 0x330 + o, CI32(c, (snap ? 0x270 : 0x300) + o));
            CW32(c, 0x300 + o, CI32(c, 0x270 + o));
        }
        CW16(c, 0xabe + 2 * w, (uint16_t)CI16(c, 0xaae + 2 * w));
    }
    for (uint32_t w = 0; w < 8; w++) {
        for (uint32_t k = 0; k < 3; k++) CW32(c, 0x504 + 12 * w + 4 * k, CI32(c, 0x4a4 + 12 * w + 4 * k));
        CW32(c, 0xbbc + 4 * w, 0);
    }
    CW32(c, 0x7a8, CI32(c, 0x7a4));
    for (uint32_t k = 0; k < 3; k++) {
        CW32(c, 0x2f4 + 4 * k, CI32(c, (snap ? 0x2d0 : 0x2e8) + 4 * k));
        CW32(c, 0x2e8 + 4 * k, CI32(c, 0x2d0 + 4 * k));
        CW32(c, 0x498 + 4 * k, CI32(c, 0x48c + 4 * k));
    }
    for (uint32_t k = 0; k < 9; k++) CW32(c, 0x384 + 4 * k, CI32(c, 0x360 + 4 * k));
}

/* 0x444c10: the step of the cars moved from outside (the network game's others, +0xc18), last to
   first: without a new pose this step (+0xc20) their records moved on and the pose taken from the
   frame; the engine speed the velocity's length, the corners' velocities the body's, the steering
   held, the drive speed the velocity along the car times 4, the gear the highest whose revs are
   within the limit; the corners on the ground, the contacts and the wheels' load. With a new pose: on
   the ground (not loose, +0xc00) the car set upright on the ground's normal (row 0 square to it, row
   2 their cross), else as it is; lifted onto the ground by its corners plus +0x960 (at least 0), and
   the frame put at the place. After a snap (+0xbf8) every record from the present and the corner
   impulses (+0x6e4) cleared. Then upright or not (+0xb74), the wheels' spin cleared, the grip, the
   travel, the suspension, the body's pose and the cell. Last, the aerials (0x484e00). */
void Sim_SimpleStep(const CarList *l)
{
    for (int i = l->n - 1; i >= 0; i--) {
        Car *c = Car_Get(l->car[i]);
        int32_t extra = CI32(c, 0x960) < 0 ? 0 : CI32(c, 0x960);
        if (!CI32(c, 0xc20)) {
            shift_records(c, false);
            for (uint32_t k = 0; k < 3; k++) {
                CW32(c, 0x2d0 + 4 * k, CI32(c, 0x30 + 4 * k));   /* 0x4bac40 */
                CW32(c, 0x360 + 4 * k, CI32(c, 4 * k));          /* 0x4bac60 */
                CW32(c, 0x36c + 4 * k, CI32(c, 0x10 + 4 * k));   /* 0x4bac80 */
                CW32(c, 0x378 + 4 * k, CI32(c, 0x20 + 4 * k));   /* 0x4baca0 */
            }
            Car_BoxCorners(c);
        }
        int32_t v[3] = {CI32(c, 0x408), CI32(c, 0x40c), CI32(c, 0x410)};
        CW32(c, 0x778, Fx_Sqrt((uint32_t)fx_mul(v[0], v[0]) + (uint32_t)fx_mul(v[1], v[1]) + (uint32_t)fx_mul(v[2], v[2])));
        for (uint32_t o = 0x42c; o < 0x48c; o += 0xc)
            for (uint32_t k = 0; k < 3; k++) CW32(c, o + 4 * k, v[k]);
        CW16(c, 0xb14, (uint16_t)CI16(c, 0xb10));
        CW16(c, 0xb12, (uint16_t)CI16(c, 0xb14));
        CW32(c, 0x958, 0);
        CW32(c, 0xb5c, 0);
        int32_t along = (fx_mul(CI32(c, 0x368), v[2]) + fx_mul(CI32(c, 0x360), v[0]) + fx_mul(CI32(c, 0x364), v[1])) * 4;
        for (uint32_t k = 0; k < 4; k++) CW32(c, 0x860 + 4 * k, along);
        CW32(c, 0x7a4, 0);
        CW8(c, 0xb1e, 0);
        for (uint32_t g = 0; g < 8; g++) {
            int32_t t = fx_mul(CI32(c, 0x860), CI32(c, 0x7bc + 4 * g));
            if (CI32(c, 0x7a4) < t && t <= CI32(c, 0x794)) CW32(c, 0x7a4, t), CW8(c, 0xb1e, (uint8_t)g);
        }
        CW32(c, 0x7ac, CI32(c, 0x7a4));
        CW32(c, 0xb60, 0);
        Car_CornerGround(c, 8);
        Car_Contacts(c);
        Car_WheelLoad(c);
        Car_GroundNormal(c);
        if (CI32(c, 0xc20)) {
            if (!CI32(c, 0xc00)) {
                int32_t up[3] = {CI32(c, 0x48c), CI32(c, 0x490), CI32(c, 0x494)};
                for (uint32_t k = 0; k < 3; k++) CW32(c, 0x36c + 4 * k, up[k]);
                int32_t f[3] = {CI32(c, 0x360), CI32(c, 0x364), CI32(c, 0x368)};
                int32_t dot = fx_mul(f[2], up[2]) + fx_mul(f[0], up[0]) + fx_mul(f[1], up[1]);
                for (int k = 0; k < 3; k++) f[k] -= fx_mul(up[k], dot);
                Vec_Unit(f);
                for (uint32_t k = 0; k < 3; k++) CW32(c, 0x360 + 4 * k, f[k]);
                int32_t s[3] = {fx_mul(f[1], up[2]) - fx_mul(f[2], up[1]), fx_mul(f[2], up[0]) - fx_mul(f[0], up[2]),
                                fx_mul(f[0], up[1]) - fx_mul(f[1], up[0])};
                Vec_Unit(s);
                for (uint32_t k = 0; k < 3; k++) CW32(c, 0x378 + 4 * k, s[k]);
                for (uint32_t k = 0; k < 3; k++) {   /* 0x4bace0, 0x4bad00, 0x4bad20 */
                    CW32(c, 4 * k, f[k]);
                    CW32(c, 0x10 + 4 * k, up[k]);
                    CW32(c, 0x20 + 4 * k, s[k]);
                }
                Car_BoxCorners(c);
                Car_CornerGround(c, 8);
                lift_by(c, corner_lift(c));
                lift_by(c, extra);
                Car_Contacts(c);
                Car_WheelLoad(c);
                Car_GroundNormal(c);
            } else {
                lift_by(c, corner_lift(c));
                lift_by(c, extra);
                Car_Contacts(c);
                Car_WheelLoad(c);
            }
            for (uint32_t k = 0; k < 3; k++) CW32(c, 0x30 + 4 * k, CI32(c, 0x2d0 + 4 * k));   /* 0x4bacc0 */
        }
        if (CI32(c, 0xbf8)) {
            shift_records(c, true);
            for (uint32_t k = 0; k < 6; k++) CW32(c, 0x6e4 + 4 * k, 0);
        }
        CW32(c, 0xbf8, 0);
        CW32(c, 0xc20, 0);
        upright(c);
        for (uint32_t k = 0; k < 8; k++) CW32(c, 0x890 + 4 * k, 0);
        Car_SurfaceGrip(c, 0);
        Car_Travel(c);
        CW8(c, 0xb1d, 0);
        Car_Suspension(c);
        Car_BodyPose(c);
        int32_t p[3] = {CI32(c, 0x2d0), CI32(c, 0x2d4), CI32(c, 0x2d8)};
        int16_t near[3];
        CW16(c, 0xb00, (uint16_t)(int16_t)Section_Find(p));
        Section_Neighbours(p, near);
        for (uint32_t k = 0; k < 3; k++) CW16(c, 0xb02 + 2 * k, (uint16_t)near[k]);
    }
    Aerials_Step(l);   /* 0x484e00 */
}
