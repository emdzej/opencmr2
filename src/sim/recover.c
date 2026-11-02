/* The recovery (see recover.h). */
#include "game/weather.h"
#include "sim/recover.h"
#include "audio/codriver.h"
#include "world/particles.h"
#include "sim/car_internal.h"
#include "sim/drive.h"
#include "sim/fade.h"
#include "sim/grip.h"
#include "sim/progress.h"
#include "fixmath.h"
#include "game/gstate.h"
#include "game/camera.h"
#include "game/mem.h"
#include "pending.h"

static int32_t iabs(int32_t v) { return v < 0 ? -v : v; }

/* 0x431d80: another car within reach (the two radii, +0x758) of centre-line point i, when the cars
   collide; point 0 counts as free on a stage (not a circuit). The distance is the largest component
   times the length of the vector scaled by it. */
static bool point_taken(const Car *c, int i)
{
    if (i == 0 && !Cat_Circuit()) return false;
    int n;
    const int16_t *list = Sim_Cars(&n);
    int32_t p[3];
    Cat_Point(i, p);
    for (int k = 0; k < n; k++) {
        const Car *o = Car_Get(list[k]);
        if (CI8(o, 0xb1a) == CI8(c, 0xb1a) || !Gs_CarsCollide()) continue;
        int32_t d[3] = {p[0] - CI32(o, 0x2d0), p[1] - CI32(o, 0x2d4), p[2] - CI32(o, 0x2d8)};
        int32_t ax = iabs(d[0]), ay = iabs(d[1]), az = iabs(d[2]), m;
        if (ay < ax && az < ax) m = ax;
        else if (ax < ay && az < ay) m = ay;
        else m = az;
        if (m) {
            int32_t r = (int32_t)(0x100000000LL / m);
            for (int q = 0; q < 3; q++) d[q] = fx_mul(d[q], r);
        }
        uint32_t l = Fx_Sqrt((uint32_t)fx_mul(d[0], d[0]) + (uint32_t)fx_mul(d[1], d[1]) + (uint32_t)fx_mul(d[2], d[2]));
        if (fx_mul((int32_t)l, m) < CI32(c, 0x758) + CI32(o, 0x758)) return true;
    }
    return false;
}

static void orient_from_rows(Car *c)
{
    for (uint32_t r = 0; r < 3; r++)
        for (uint32_t k = 0; k < 3; k++) CW32(c, 0x10 * r + 4 * k, CI32(c, 0x360 + 12 * r + 4 * k));
}

/* 0x431ff0: the car at pos (y 100 units under it, then lifted out of the ground) heading along dir
   on the ground's slope, and its motion, history, drivetrain, damage counters and loose state reset;
   first gear. */
static void reset_at(Car *c, const int32_t pos[3], const int32_t dir[3])
{
    int32_t f[3] = {dir[0], 0, dir[2]};
    uint32_t l = Fx_Sqrt((uint32_t)fx_mul(f[0], f[0]) + (uint32_t)fx_mul(f[2], f[2]));
    if (!l) f[0] = f[2] = 0;
    else {
        int32_t r = (int32_t)(0x100000000LL / (int32_t)l);
        f[0] = fx_mul(f[0], r), f[2] = fx_mul(f[2], r);
    }
    CW32(c, 0x360, f[0]), CW32(c, 0x364, 0), CW32(c, 0x368, f[2]);
    CW32(c, 0x36c, 0), CW32(c, 0x370, 0x10000), CW32(c, 0x374, 0);
    CW32(c, 0x378, f[2]), CW32(c, 0x37c, 0), CW32(c, 0x380, -f[0]);
    CW32(c, 0x2d0, pos[0]);
    CW32(c, 0x2d4, pos[1] - 0x640000);
    CW32(c, 0x2d8, pos[2]);
    orient_from_rows(c);
    Car_BoxCorners(c);
    Car_CornerGround(c, 8);
    Car_GroundNormal(c);
    /* up from the ground, forward square to it, side across */
    int32_t u[3], s[3];
    for (uint32_t k = 0; k < 3; k++) u[k] = CI32(c, 0x48c + 4 * k), CW32(c, 0x36c + 4 * k, u[k]);
    for (uint32_t k = 0; k < 3; k++) f[k] = CI32(c, 0x360 + 4 * k);
    int32_t p = fx_mul(f[0], u[0]) + fx_mul(f[1], u[1]) + fx_mul(f[2], u[2]);
    for (int k = 0; k < 3; k++) f[k] -= fx_mul(u[k], p);
    Vec_Unit(f);
    for (uint32_t k = 0; k < 3; k++) CW32(c, 0x360 + 4 * k, f[k]);
    s[0] = fx_mul(f[1], u[2]) - fx_mul(f[2], u[1]);
    s[1] = fx_mul(f[2], u[0]) - fx_mul(f[0], u[2]);
    s[2] = fx_mul(f[0], u[1]) - fx_mul(f[1], u[0]);
    Vec_Unit(s);
    for (uint32_t k = 0; k < 3; k++) CW32(c, 0x378 + 4 * k, s[k]);
    orient_from_rows(c);
    Car_BoxCorners(c);
    Car_CornerGround(c, 8);
    Car_Contacts(c);
    Car_LiftOut(c);
    Car_Contacts(c);
    Car_WheelLoad(c);
    for (uint32_t k = 0; k < 3; k++) CW32(c, 0x30 + 4 * k, CI32(c, 0x2d0 + 4 * k));
    for (uint32_t k = 0; k < 16; k++) CW32(c, 0x40 + 4 * k, CI32(c, 4 * k));   /* +0x754 from +0x750 */
    CW32(c, 0x91c, 0), CW32(c, 0x920, 0x10000), CW32(c, 0x924, 0);
    CW32(c, 0xba8, 0), CW32(c, 0xba4, 0), CW32(c, 0xba0, 0);
    for (uint32_t o = 0x408; o < 0x420; o += 4) CW32(c, o, 0);
    CW32(c, 0x778, 0);
    for (uint32_t o = 0x420; o < 0x42c; o += 4) CW32(c, o, 0);
    for (uint32_t k = 0; k < 9; k++) CW32(c, 0x384 + 4 * k, CI32(c, 0x360 + 4 * k));
    for (uint32_t k = 0; k < 3; k++) CW32(c, 0x498 + 4 * k, CI32(c, 0x48c + 4 * k));
    for (uint32_t o = 0x504; o < 0x564; o += 4) CW32(c, o, CI32(c, o - 0x60));
    for (uint32_t o = 0x5c4; o < 0x5dc; o += 4) CW32(c, o, 0);
    for (uint32_t k = 0; k < 3; k++) {
        int32_t v = CI32(c, 0x2d0 + 4 * k);
        CW32(c, 0x2dc + 4 * k, v), CW32(c, 0x2f4 + 4 * k, v), CW32(c, 0x2e8 + 4 * k, v);
    }
    /* the corners' history: +0x330 and +0x300 both from the first four box corners */
    for (uint32_t o = 0x330; o < 0x360; o += 0xc)
        for (uint32_t k = 0; k < 3; k++) {
            CW32(c, o + 4 * k, CI32(c, o - 0xc0 + 4 * k));
            CW32(c, o - 0x30 + 4 * k, CI32(c, o + 4 * k));
        }
    for (uint32_t k = 0; k < 3; k++) {
        CW32(c, 0x3a8 + 4 * k, CI32(c, 0x378 + 4 * k));
        CW32(c, 0x3f0 + 4 * k, CI32(c, 0x360 + 4 * k));
        CW32(c, 0x3b4 + 4 * k, CI32(c, 0x3f0 + 4 * k));
    }
    CW32(c, 0xc00, 0), CW32(c, 0xc10, 0), CW32(c, 0x96c, 0), CW32(c, 0xc04, 0);
    CW16(c, 0xa9c, 0);
    CW32(c, 0x95c, 0);
    CW16(c, 0xb18, 0);
    CW32(c, 0x77c, 0);
    c->s[0xb45] = 3;
    for (uint32_t o = 0x6e4; o < 0x6fc; o += 4) CW32(c, o, 0);
    for (uint32_t o = 0x860; o < 0x870; o += 4) CW32(c, o, 0);
    CW32(c, 0x79c, 0), CW32(c, 0x7a0, 0), CW32(c, 0x7a4, 0), CW32(c, 0x7ac, 0);
    CW32(c, 0x838, 0), CW32(c, 0x83c, 0), CW32(c, 0x848, 0), CW32(c, 0x84c, 0), CW32(c, 0x81c, 0);
    for (uint32_t o = 0xa9e; o < 0xaae; o += 2) CW16(c, o, 0);
    CW16(c, 0xb10, 0), CW16(c, 0xb12, 0);
    CW32(c, 0xbfc, 0), CW32(c, 0xbe4, 0), CW32(c, 0xbe0, 0), CW32(c, 0xbdc, 0);
    CW32(c, 0xb94, 0);
    c->s[0xb1e] = 1;
    CW32(c, 0xb78, 0);
    CW32(c, 0x960, 0), CW32(c, 0x964, 0), CW32(c, 0x968, 0);
    gw32(0x53acf0 + 4 * (uint32_t)CI8(c, 0xb1a), 1);   /* 0x42c870: the pose snaps, no blend */
    CW32(c, 0xc08, 1);
    CW32(c, 0x970, 0x320000);
    CW32(c, 0xc14, 1);
    Car_SurfaceGrip(c, Weather_CarWet(c));   /* Weather_CarValue 0x460c80 */
    CW32(c, 0xa74, CI32(c, 0xa78));   /* 0x4789b0 */
}

/* 0x431c50 */
static void recover(int ch)
{
    Car *c = Car_Get(ch & 0xff);
    g_car = c;
    int i = Car_CatIndex(c);
    const uint8_t *r = Cat_Record(i);
    int32_t pos[3], dir[3];
    if (!r) {
        for (uint32_t k = 0; k < 3; k++) pos[k] = CI32(c, 0x2d0 + 4 * k), dir[k] = CI32(c, 0x360 + 4 * k);
    } else {
        /* back along the line past points another car is on (round a circuit's start), and past
           points flagged 2 */
        while (point_taken(c, i)) {
            if (--i < 0) i = Cat_Circuit() ? i + Cat_FinishPoint() : 0;
        }
        while ((r[0x18] & 2) && 0 < i) r = Cat_Record(--i);   /* (r first the point the car was at) */
        Cat_Tangent(dir, i);
        Cat_Point(i, pos);
    }
    reset_at(c, pos, dir);
    Codriver_Rewind(ch & 0xff);   /* 0x417780 */
    Particles_KillAll();   /* 0x4aff80 */
    if ((uint32_t)(ch & 0xff) < Gs_SelBits14()) Camera_Replace(ch & 0xff);   /* 0x421d80 */
    CW32(c, 0xbf8, 0);
    CW32(c, 0xa7c, 0);
    CW32(c, 0xa80, 0);
}

void Car_RecoverCheck(Car *c)
{
    if (CI32(c, 0xbf8)) Fade_OutIn(CI8(c, 0xb1a), recover, 2, 1, 0, false);
}
