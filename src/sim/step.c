/* The car part of the simulation step (see step.h). */
#include "sim/step.h"
#include "sim/controls.h"
#include "sim/integrate.h"
#include "sim/suspension.h"
#include "fixmath.h"
#include "game/gstate.h"
#include "pending.h"
#include "world/section.h"

int g_pending_passes;
#define PENDING_PASS(addr, name) do { PENDING_NOTE(addr, name); g_pending_passes++; } while (0)

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
static void wheel_load(Car *c)
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
    int16_t near[3] = {-1, -1, -1};
    int16_t cell = (int16_t)Section_Find(p);
    PENDING_NOTE(0x4b8910, "Section_Neighbours");
    CW16(c, 0xb00, (uint16_t)cell);
    for (uint32_t i = 0; i < 3; i++) CW16(c, 0xb02 + 2 * i, (uint16_t)near[i]);
    for (uint32_t k = 0; k < 3; k++) CW32(c, 0x2dc + 4 * k, CI32(c, 0x2d0 + 4 * k));
}

/* Sim_StepCars 0x433890, in its passes, each over every car (last to first). */
void Sim_StepCars(const int16_t *list, int n)
{
    g_pending_passes = 0;
    if (Gs_SelBits14() == 2 && n == 2 && Gs_SelBit24()) PENDING_PASS(0x433e80, "Sim_HeadToHeadBalance");
    for (int i = n - 1; i >= 0; i--) {
        Car *c = Car_Get(list[i]);
        g_car = c;
        gw32(0x53c9d8, 0);
        gw32(0x53c9dc, 0);
        gw32(0x53c9e0, 0);
        gw32(0x53c9d4, 0);
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
        if (!CI32(c, 0xbfc)) damage_latch(c);
        PENDING_PASS(0x4781d0, "Car_SurfaceGrip");
        clutch_counter(c);
        PENDING_PASS(0x46b4e0, "Damage_Step");
    }
    for (int i = n - 1; i >= 0; i--) {
        Car *c = Car_Get(list[i]);
        g_car = c;
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
        PENDING_NOTE(0x469680, "Damage_Get");   /* the damage record's counters +0x469/+0x46c */
        PENDING_PASS(0x434f50, "Car_Aero");
        for (uint32_t o = 0x5e8; o < 0x648; o += 4) CW32(c, o, 0);
        for (uint32_t o = 0x648; o < 0x6a8; o += 4) CW32(c, o, 0);
        CW32(c, 0x6a8, 0);
        int32_t g = fx_mul(gms32(0x53cadc), 0x8000);
        CW32(c, 0x6ac, -fx_mul(CI32(c, 0x75c), g));   /* gravity on the mass */
        CW32(c, 0x6b0, 0);
        wheel_load(c);
        Car_Suspension(c);
        tilt_factor(c);
        PENDING_PASS(0x4348c0, "Car_Drive");
        PENDING_PASS(0x434140, "Car_Tyres");
    }
    for (int i = n - 1; i >= 0; i--) { g_car = Car_Get(list[i]); PENDING_PASS(0x437dc0, "Car_Collide"); }
    for (int i = n - 1; i >= 0; i--) { g_car = Car_Get(list[i]); PENDING_PASS(0x438460, "Car_Engine"); }
    for (int i = n - 1; i >= 0; i--) { g_car = Car_Get(list[i]); Car_Integrate(g_car); }
    for (int i = n - 1; i >= 0; i--) {
        Car *c = Car_Get(list[i]);
        g_car = c;
        CW32(c, 0x7a8, CI32(c, 0x7a4));
        PENDING_PASS(0x4380c0, "Car_Gearbox");
        rev_limit(c);
        for (uint32_t o = 0x5c4; o < 0x5dc; o += 4) CW32(c, o, 0);
    }
    for (int i = n - 1; i >= 0; i--) car_cell(Car_Get(list[i]));
}
