/* The network game's cars (docs/re/network.md#cars): 0x424dc0-0x427580.

   A local car's state (Net_PackCar 0x424f20) goes into the 30 bytes at 0x539388, quantised:
     +0x00 u16  the race's step (0x41d2a0)
     +0x02 s16  x and +0x04 z of +0x2dc - +0x2e8, times 0.2 (16.16), in 32,765ths (0x7ffd, 0x8003 out of range)
     +0x06 u16  the product of the wheels' averaged sums (+0x1a4/+0x88 and +0x1a8/+0x8c), in 65,530ths
     +0x08 s16  x and +0x0a z of the place from its cell's centre (+0xb00), over 128, in 32,765ths
     +0x0c u16  the cell (+0xb00)
     +0x0e u8   the revs (+0x960) over 10, in 255ths; +0x0f s8 their change (+0x960 - +0x964), 0.2, in 127ths
     +0x10      the first row and +0x12 the third: the turn about y in 255ths of a turn, then the angle
                from y in 255ths of half a turn (the tables' arctangent and arcsine)
     +0x14 s8   x3 the spin (+0x420-+0x428) times 5 in 127ths
     +0x17      bit 0 upright (+0xb35), bit 1 braking (+0xb54), bits 2-7 the share of the segment (0x4209d0)
     +0x18      bits 0-9 the sector point (0x4582f0, at most 0x400)
     +0x1a      bits 0-6 the steering (+0xb10 / +0xb16, 0..126), bit 7 +0x79c, bit 8 loose (+0xc00), bit 9 a
                snap asked (+0xb45, counted down), bit 10 paused, bits 11-14 the lap (0x4582d0), bit 15 its sign
   The receiving game keeps the last state of each record (records.c, 0x409d50) and per car slot a
   predictor record of 0xec bytes at 0x5393d8:
     +0x00 the frame shown (a 4 x 4: rows, place +0x30), +0x40 the three rows, +0x64 the place, +0x70 the
     velocity, +0x7c the spin, +0x88 the drag (0x4263d0), +0x94 the last change of velocity (unit) and
     +0xa0 the place then, +0xac the drag's speed and +0xb0 its inverse, +0xb4 the revs shown, +0xb8 the
     throttle, +0xbc the revs and +0xc0 their change, +0xc4 the steering, +0xc6 the snap's count down,
     +0xc8 the frame predicted to and +0xca the state's frame, +0xcc upright, +0xd0 braking, +0xd4 loose,
     +0xd8 a snap, +0xdc a change of velocity seen, +0xe0 a state taken, +0xe4 paused, +0xe8 a jump.
   Each step the states taken are unpacked (0x425c40), each predictor run to the frame estimated for
   its record (0x426810: velocity, drag, spin and revs; the shown frame halfway to it), the car posed
   from it (0x426d80); after the collisions the car's velocity and spin go back into it (0x426fc0). */
#include "net/carsync.h"
#include "exe.h"
#include "fixmath.h"
#include "game/camera.h"
#include "game/mem.h"
#include "game/pause.h"
#include "game/race.h"
#include "game/sectors.h"
#include "net/race_msg.h"
#include "net/records.h"
#include "sim/pose.h"
#include "sim/progress.h"
#include "world/section.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

enum { PK = 0x539388, EST = 0x5393ac, PRED = 0x5393d8, PRED_SIZE = 0xec, TRI = 0x539b38, SPLITS_ON = 0x539cc8 };

static uint32_t pred(int slot) { return PRED + PRED_SIZE * (uint32_t)slot; }
static int32_t P32(uint32_t p, uint32_t o) { return gms32(p + o); }
static void W32(uint32_t p, uint32_t o, int32_t v) { gw32(p + o, (uint32_t)v); }
static bool on(void) { return gm8(SPLITS_ON) != 0; }

/* the float (and the double) constants of 0x511300-0x511390 */
static double kf(uint32_t va)
{
    uint32_t u = exe_u32(va);
    float f;
    memcpy(&f, &u, 4);
    return f;
}
static double kd(uint32_t va)
{
    uint64_t u = exe_u32(va) | (uint64_t)exe_u32(va + 4) << 32;
    double d;
    memcpy(&d, &u, 8);
    return d;
}

static void put16(uint32_t va, int v) { gw16(va, (uint16_t)v); }
static void bits8(uint32_t va, uint8_t mask, bool set) { gw8(va, set ? gm8(va) | mask : gm8(va) & (uint8_t)~mask); }

/* a 16.16 value times k (as a float), at least 1 0x7ffd, at most -1 0x8003, else in 32,765ths */
static int q16(int32_t v, double k)
{
    float f = (float)((double)v / 65536.0 * k);
    if (f >= kf(0x511350)) return 0x7ffd;
    if (f <= kf(0x51134c)) return 0x8003;
    return (int)(int16_t)llrint((double)f * kf(0x511348));
}
/* the same in 127ths of a byte (0x7f, 0x81) */
static uint8_t q8(int32_t v, double k)
{
    float f = (float)((double)v / 65536.0 * k);
    if (f >= kf(0x511350)) return 0x7f;
    if (f <= kf(0x51134c)) return 0x81;
    return (uint8_t)llrint((double)f * kf(0x51133c));
}

/* a row's two angles (0x425400-0x425623): its turn about y from the x and z magnitudes (the arctangent
   table, then the quadrant from the signs) and its angle from y (the arcsine table on |y|, from the
   pole; past the equator when y is not above it), each in 255ths, clamped to 0..255 */
static uint16_t row_angles(const int32_t r[3])
{
    uint32_t ax = (uint32_t)abs(r[0]), ay = (uint32_t)abs(r[1]), az = (uint32_t)abs(r[2]);
    int32_t yaw;
    if (ax == 0) yaw = 0;
    else {
        int16_t t;
        if (az == 0) t = 0;
        else if (ax == 0) t = 0x400;
        else {
            uint32_t num = az, den = ax;
            bool swap = (int32_t)az > (int32_t)ax;
            if (swap) num = ax, den = az;
            t = (int16_t)g_atan[(uint32_t)(((int64_t)(int32_t)num << 16) / (int32_t)den) >> 7];
            if (swap) t = (int16_t)(0x400 - t);
        }
        yaw = t * 0x1680;
    }
    int16_t s;
    if ((int32_t)ay > 0x10000) s = (int16_t)g_asin[4095];
    else s = (int16_t)g_asin[llrint((double)(int32_t)ay / 65536.0 * kd(0x511330)) * -1];
    int32_t pitch = (0x400 - s) * 0x1680;
    int32_t x = r[0], z = r[2];
    if (x >= 0 && z <= 0) yaw = 0x1680000 - yaw;
    else if (!(x > 0 && z > 0)) {   /* x below 0, or x 0 with z above */
        if (z >= 0) yaw = 0xb40000 - yaw;
        else if (x <= 0) yaw += 0xb40000;
    }
    if (r[1] <= 0) pitch = 0xb40000 - pitch;
    float fy = (float)((double)yaw / 65536.0 * kf(0x511328) * kf(0x511324));
    double fp = (double)pitch / 65536.0 * kf(0x511320) * kf(0x511324);
    if (fy < 0) fy = 0;
    else if (fy > 255) fy = 255;
    if (fp < 0) fp = 0;
    else if (fp > 255) fp = 255;
    return (uint16_t)((llrint(fp) & 0xff) << 8 | (llrint(fy) & 0xff));
}

/* Net_PackCar 0x424f20: car c's state into 0x539388 (the fields it does not set are kept) */
static void pack(Car *c)
{
    put16(PK, Race_Steps());   /* 0x41d2a0 */
    int32_t a = 0, b = 0;
    for (uint32_t i = 0; i < 4; i++) {
        a += CI32(c, 0x1a4 + 12 * i) + CI32(c, 0x88 + 0x24 * i);
        b += CI32(c, 0x1a8 + 12 * i) + CI32(c, 0x8c + 0x24 * i);
    }
    a = fx_mul(a, 0x4000), b = fx_mul(b, 0x4000);   /* (times 2^32 / 0x40000: a quarter) */
    put16(PK + 2, q16(CI32(c, 0x2dc) - CI32(c, 0x2e8), kf(0x511354)));
    put16(PK + 4, q16(CI32(c, 0x2e4) - CI32(c, 0x2f0), kf(0x511354)));
    double ab = (double)fx_mul(a, b) / 65536.0;
    put16(PK + 6, ab >= kf(0x511350) ? 0xfffa : (int)(uint16_t)llrint(ab * kf(0x511344)));
    gw8(PK + 0x14, q8(CI32(c, 0x420), kf(0x511340)));
    gw8(PK + 0x15, q8(CI32(c, 0x424), kf(0x511340)));
    gw8(PK + 0x16, q8(CI32(c, 0x428), kf(0x511340)));
    bits8(PK + 0x17, 1, c->s[0xb35] & 1);
    bits8(PK + 0x1b, 1, c->s[0xc00] & 1);
    int cell = CI16(c, 0xb00);
    put16(PK + 0xc, cell);
    const Section *sec = &g_sec[cell];
    put16(PK + 8, q16(CI32(c, 0x30) - sec->x, kf(0x511338)));
    put16(PK + 0xa, q16(CI32(c, 0x38) - sec->z, kf(0x511338)));
    int32_t r0[3] = {CI32(c, 0), CI32(c, 4), CI32(c, 8)}, r2[3] = {CI32(c, 0x20), CI32(c, 0x24), CI32(c, 0x28)};
    put16(PK + 0x10, row_angles(r0));   /* 0x4bac60 */
    put16(PK + 0x12, row_angles(r2));   /* 0x4baca0 */
    /* the steering's share, 0..126 */
    int32_t den = CI16(c, 0xb16) * 0x1680, q = 0;
    if (den) q = (int32_t)(((int64_t)(CI16(c, 0xb10) * 0x1680) << 16) / den);   /* (the original divides by zero) */
    q = fx_mul(q + 0x10000, 0x3f0000) + 0x1999;
    if (q > 0x7e8000) q = 0x7e8000;
    gw8(PK + 0x1a, (uint8_t)((gm8(PK + 0x1a) & 0x80) | ((q >> 16) & 0x7f)));
    bits8(PK + 0x1a, 0x80, CI32(c, 0x79c) != 0);
    bits8(PK + 0x17, 2, c->s[0xb54] & 1);
    float rv = (float)((double)CI32(c, 0x960) / 65536.0 * kf(0x511318));
    gw8(PK + 0xe, rv >= kf(0x511350) ? 0xff : rv <= 0 ? 0 : (uint8_t)llrint((double)rv * kf(0x511324)));
    gw8(PK + 0xf, q8(CI32(c, 0x960) - CI32(c, 0x964), kf(0x511354)));
    if (c->s[0xb45]) {
        bits8(PK + 0x1b, 2, true);
        c->s[0xb45]--;
    } else
        bits8(PK + 0x1b, 2, false);
    bits8(PK + 0x1b, 4, Pause_Active());   /* 0x404f20 */
    int slot = CI8(c, 0xb1a), pt = Sectors_CarPoint(slot);   /* 0x4582f0 */
    if (pt < 0) pt = 0;
    else if (pt > 0x400) pt = 0x400;
    put16(PK + 0x18, (gm16(PK + 0x18) & ~0x3ff) | (pt & 0x3ff));
    int lap = Sectors_Lap(slot);   /* 0x4582d0 */
    uint16_t w = gm16(PK + 0x1a);
    if (lap < 0) lap = -lap, w |= 0x8000;
    else w &= 0x7fff;
    if (lap > 15) lap = 15;
    put16(PK + 0x1a, (w & 0x87ff) | (lap & 0xf) << 11);
    int32_t f = fx_mul(Car_SegmentFrac(c), 0x400000) >> 16;   /* 0x4209d0 */
    if (f < 0) f = 0;
    else if (f > 0x3f) f = 0x3f;
    put16(PK + 0x16, (int)(f << 10 | (gm16(PK + 0x16) & 0x3ff)));
}

/* 0x427580: the send rate from the link's bytes and bits a second and the players (DAT_005393d4) */
void Net_SetRate(int bytes, int bits, int players)
{
    float per = (float)(players * 0xc0 + 0x230);
    float a = (float)bytes / per, b = (float)bits / ((float)players * per);
    if (a <= b) b = a;
    if (b > kf(0x511390)) b = (float)kf(0x511390);
    gw32(0x5393d4, 0x28);
    if (0 < kf(0x511350) / b * kf(0x51138c) - kf(0x511388)) gw32(0x5393d4, 0x29);
}

void Net_CarsReset(void)
{
    for (uint32_t i = 0; i < 7; i++) gw32(EST + 4 * i, 0);
    int32_t t = 0;
    for (uint32_t i = 0; i < 100; i++) t += (int32_t)i, gw32(TRI + 4 * i, (uint32_t)t);   /* i (i + 1) / 2 */
    Net_SetRate(20000, 1000000, 2);
    gw32(0x5393a8, 0xffffffffu);
    gw32(0x5393cc, 0xffffffffu);
    gw32(0x5393d0, 0xffffffffu);
}

/* the predictor's frame (+0x00) from its rows (+0x40) and place (+0x64): 0x4bace0-0x4bad20, 0x4bacc0 */
static void frame_from(uint32_t p, uint32_t m)
{
    for (uint32_t r = 0; r < 3; r++)
        for (uint32_t k = 0; k < 3; k++) W32(m, 0x10 * r + 4 * k, P32(p, 0x40 + 12 * r + 4 * k));
    for (uint32_t k = 0; k < 3; k++) W32(m, 0x30 + 4 * k, P32(p, 0x64 + 4 * k));
}

void Net_CarStart(Car *c)
{
    int slot = CI8(c, 0xb1a);
    uint32_t p = pred(slot);
    memset(gm(p), 0, PRED_SIZE);
    for (uint32_t k = 0; k < 3; k++) W32(p, 0x64 + 4 * k, CI32(c, 0x2d0 + 4 * k));
    for (uint32_t k = 0; k < 9; k++) W32(p, 0x40 + 4 * k, CI32(c, 0x360 + 4 * k));
    frame_from(p, p);
    if (slot > 0) {
        pack(c);
        uint32_t at = NetRec_SplitsAt(NetRec_BySlot((uint32_t)slot));   /* 0x40b020, 0x409e20 */
        memcpy(gm(at), gm(PK), 30);
        gw16(at, 0);
    }
}

void Net_SendCars(const CarList *l)
{
    if (!on()) return;
    for (int i = l->n - 1; i >= 0; i--) {
        Car *c = Car_Get(l->car[i]);
        if (CI32(c, 0xc1c)) continue;
        pack(c);
        if (on()) Net_SendSplits(gm(PK));   /* 0x4278f0 */
    }
}

/* the game's inline unit vector of 0x425c40 and 0x426fc0 (as Vec_Unit) on predictor words */
static void unit_at(uint32_t va)
{
    int32_t v[3] = {gms32(va), gms32(va + 4), gms32(va + 8)};
    Vec_Unit(v);
    for (uint32_t k = 0; k < 3; k++) gw32(va + 4 * k, (uint32_t)v[k]);
}

/* the x-z unit of (x, z), zero for a zero length */
static void unit2(int32_t x, int32_t z, int32_t *ux, int32_t *uz)
{
    int32_t len = (int32_t)Fx_Sqrt((uint32_t)fx_mul(x, x) + (uint32_t)fx_mul(z, z));
    *ux = *uz = 0;
    if (!len) return;
    int32_t k = (int32_t)(((int64_t)1 << 32) / len);
    *ux = fx_mul(x, k), *uz = fx_mul(z, k);
}

/* 0x4263d0: the drag along the car's way (its velocity when it is not upright, row 2's y under 0.1;
   else its forward row) on the speed there over +0xac, squared and kept in -1..1 */
static void drag(uint32_t p)
{
    W32(p, 0x88, 0), W32(p, 0x8c, 0), W32(p, 0x90, 0);
    int32_t dx, dz;
    if (P32(p, 0x50) < 0x1999) {
        int32_t vx = P32(p, 0x70), vz = P32(p, 0x78);
        if ((int32_t)Fx_Sqrt((uint32_t)fx_mul(vx, vx) + (uint32_t)fx_mul(vz, vz)) > 0) unit2(vx, vz, &dx, &dz);
        else dx = dz = 0;
    } else
        unit2(P32(p, 0x58), P32(p, 0x60), &dx, &dz);
    int32_t s = fx_mul(fx_mul(dz, P32(p, 0x78)) + fx_mul(dx, P32(p, 0x70)), P32(p, 0xb0));
    int32_t q = s < 0 ? -fx_mul(s, s) : fx_mul(s, s);
    if (abs(q) > 0x10000) q = q < 1 ? -0x10000 : 0x10000;
    int32_t k = -fx_mul(fx_mul(q, P32(p, 0xac)), 0x11eb);
    W32(p, 0x88, P32(p, 0x88) + fx_mul(dx, k));
    W32(p, 0x90, P32(p, 0x90) + fx_mul(dz, k));
}

/* 0x425c40: the state at 0x539388 into predictor p when it is newer (by 1 to 99 frames) and in a
   cell of the stage; *steer the steering share (-1..1). False when not taken. */
static bool unpack(uint32_t p, int32_t *steer)
{
    int dt = (int)gm16(PK) - (int)(uint16_t)P32(p, 0xca);
    if (!(dt > 0 && on() && dt < 100 && gm16(PK + 0xc) < g_nsec)) return false;
    W32(p, 0xe0, 1);
    gw16(p + 0xc8, gm16(PK)), gw16(p + 0xca, gm16(PK));
    double k7c = kf(0x51137c), u = 65536.0;
    W32(p, 0x70, (int32_t)llrint(gms16(PK + 2) * k7c * kf(0x511378) * u));
    W32(p, 0x74, 0);
    W32(p, 0x78, (int32_t)llrint(gms16(PK + 4) * k7c * kf(0x511378) * u));
    int32_t s = (int32_t)llrint(gm16(PK + 6) * kf(0x511374) * u);
    W32(p, 0xac, s);
    W32(p, 0xb0, s ? (int32_t)(((int64_t)1 << 32) / s) : 0);
    for (uint32_t k = 0; k < 3; k++) W32(p, 0x7c + 4 * k, (int32_t)llrint(gms8(PK + 0x14 + k) * kf(0x511370) * kf(0x51136c) * u));
    gw8(p + 0xcc, gm8(PK + 0x17) & 1);
    W32(p, 0xd4, (gm16(PK + 0x1a) & 0x100) >> 8);
    const Section *sec = &g_sec[gm16(PK + 0xc)];
    W32(p, 0x64, (int32_t)llrint(gms16(PK + 8) * k7c * kf(0x511368) * u) + sec->x);
    W32(p, 0x68, sec->y);
    W32(p, 0x6c, (int32_t)llrint(gms16(PK + 0xa) * k7c * kf(0x511368) * u) + sec->z);
    W32(p, 0xbc, (int32_t)llrint(gm8(PK + 0xe) * kf(0x511364) * kf(0x511360) * u));
    W32(p, 0xc0, (int32_t)llrint(gms8(PK + 0xf) * kf(0x511370) * kf(0x511378) * u));
    /* rows 0 and 2 from their angles: (sin b cos a, cos b, sin b sin a), unit; row 1 their cross */
    for (uint32_t i = 0; i < 2; i++) {
        float yaw = (float)(gm8(PK + 0x10 + 2 * i) * kf(0x51135c));
        float pitch = (float)(gm8(PK + 0x11 + 2 * i) * kf(0x511358));
        uint32_t a = (uint32_t)(uint16_t)X87_MulRound((double)llrint((double)yaw * u), kd(0x511300));
        uint32_t b = (uint32_t)(uint16_t)X87_MulRound((double)llrint((double)pitch * u), kd(0x511300));
        uint32_t r = p + 0x40 + 0x18 * i;
        gw32(r, (uint32_t)fx_mul(g_sin[b & 0xfff], g_sin[(a + 0x400) & 0xfff]));
        gw32(r + 4, (uint32_t)g_sin[(b + 0x400) & 0xfff]);
        gw32(r + 8, (uint32_t)fx_mul(g_sin[b & 0xfff], g_sin[a & 0xfff]));
        unit_at(r);
    }
    W32(p, 0x4c, fx_mul(P32(p, 0x5c), P32(p, 0x48)) - fx_mul(P32(p, 0x60), P32(p, 0x44)));
    W32(p, 0x50, fx_mul(P32(p, 0x60), P32(p, 0x40)) - fx_mul(P32(p, 0x58), P32(p, 0x48)));
    W32(p, 0x54, fx_mul(P32(p, 0x58), P32(p, 0x44)) - fx_mul(P32(p, 0x5c), P32(p, 0x40)));
    unit_at(p + 0x4c);
    uint8_t st = gm8(PK + 0x1a) & 0x7f;
    *steer = st == 0 ? -0x10000 : st == 0x7f ? 0x10000 : fx_mul(st << 16, 0x418) - 0x10000;
    W32(p, 0xb8, gm8(PK + 0x1a) & 0x80 ? 0x10000 : 0);
    W32(p, 0xd0, (gm16(PK + 0x16) & 0x200) >> 9);
    if (gm16(PK + 0x1a) & 0x200 && gm16(p + 0xc6) == 0) W32(p, 0xd8, 1), gw16(p + 0xc6, 100);
    W32(p, 0xe4, (gm16(PK + 0x1a) & 0x400) >> 10);
    drag(p);
    return true;
}

/* 0x425a90: each record's new state taken into its car's predictor, the frame estimate (0x5393ac)
   moved halfway to the state's (to it after a jump of 51 or more: +0xe8); without one, on by a frame.
   (The original logs record 0's estimate through a print that does nothing, 0x4083f0.) */
static void take(void)
{
    for (int i = 0; i < 7; i++) {
        uint32_t est = gm32(EST + 4 * (uint32_t)i);
        if (!NetRec_Used(i) || !NetRec_SplitsNew(i)) {   /* 0x409cb0, 0x409df0 */
            if (est) gw32(EST + 4 * (uint32_t)i, est + 1);
            continue;
        }
        NetRec_SplitsSeen(i);   /* 0x409e00 */
        uint32_t at = NetRec_SplitsAt(i);
        memcpy(gm(PK), gm(at), 30);
        int slot = (int)NetRec_Slot(i);   /* 0x40b010 */
        uint32_t p = pred(slot);
        Car *c = Car_Get(slot);
        int32_t steer;
        if (unpack(p, &steer)) {
            int32_t a = fx_mul(steer, CI16(c, 0xb16) * 0x1680);
            gw16(p + 0xc4, (uint16_t)X87_MulRound((double)a, kd(0x511300)));
            W32(p, 0xb8, fx_mul(CI32(c, 0x788), P32(p, 0xb8)));
        }
        uint32_t frame = gm16(at);
        int32_t d = (int32_t)(est - frame);
        if (abs(d) < 0x33) W32(p, 0xe8, 0);
        else est = frame, W32(p, 0xe8, 1);
        gw32(EST + 4 * (uint32_t)i, (frame + est) >> 1);
    }
}

/* 0x426b90: after a change of velocity (+0xdc), the part of the velocity into its direction (+0x94)
   kept to what reaches the place then (+0xa0) in dt */
static void bounce(uint32_t p, int32_t dt)
{
    if (!P32(p, 0xdc) || !dt) return;
    int32_t n[3], v = 0, d = 0;
    for (uint32_t k = 0; k < 3; k++) n[k] = fx_mul(P32(p, 0x94 + 4 * k), -0x10000);
    v = fx_mul(n[2], P32(p, 0x78)) + fx_mul(n[0], P32(p, 0x70)) + fx_mul(n[1], P32(p, 0x74));
    if (v <= 0) return;
    for (uint32_t k = 0; k < 3; k++) d += fx_mul(n[k], P32(p, 0xa0 + 4 * k) - P32(p, 0x64 + 4 * k));
    int32_t q = (int32_t)(((int64_t)(int32_t)(d + 0x10000) << 16) / dt);
    if (q <= 0) return;
    if (v < q) q = v;
    for (uint32_t k = 0; k < 3; k++) W32(p, 0x70 + 4 * k, P32(p, 0x70 + 4 * k) - fx_mul(n[k], v));
    W32(p, 0x74, P32(p, 0x74) + fx_mul(n[1], q));
    W32(p, 0x70, P32(p, 0x70) + fx_mul(n[0], q));
    W32(p, 0x78, P32(p, 0x78) + fx_mul(n[2], q));
}

/* 0x426810: predictor p run to frame est: the place by the velocity and the drag (the drag's sum of
   the steps from the table at 0x539b38), the rows turned by the spin, the revs; the frame shown snapped
   to it (+0xd8) or blended halfway */
static void predict(uint32_t p, uint32_t est)
{
    uint16_t cd = gm16(p + 0xc6);
    if (cd) gw16(p + 0xc6, (uint16_t)(cd - 1));
    int32_t dt;
    if (P32(p, 0xe8)) dt = 0, W32(p, 0xe8, 0);
    else dt = (int32_t)((est - gm16(p + 0xc8)) << 16);
    if (P32(p, 0xe0)) bounce(p, dt);
    for (uint32_t k = 0; k < 3; k++) W32(p, 0x64 + 4 * k, P32(p, 0x64 + 4 * k) + fx_mul(P32(p, 0x70 + 4 * k), dt));
    int32_t tri = gms32(TRI + 4 * (uint32_t)(abs(dt) >> 16)) << 16;
    if (dt < 0) tri = -tri;
    int32_t d[3];
    for (uint32_t k = 0; k < 3; k++) W32(p, 0x64 + 4 * k, P32(p, 0x64 + 4 * k) + fx_mul(P32(p, 0x88 + 4 * k), tri));
    for (uint32_t k = 0; k < 3; k++) W32(p, 0x70 + 4 * k, P32(p, 0x70 + 4 * k) + fx_mul(P32(p, 0x88 + 4 * k), dt));
    for (uint32_t k = 0; k < 3; k++) d[k] = fx_mul(P32(p, 0x7c + 4 * k), dt);
    uint16_t ang[3];
    for (int k = 0; k < 3; k++) ang[k] = (uint16_t)X87_MulRound((double)d[k], kd(0x511380));
    int32_t rows[9];
    for (uint32_t k = 0; k < 9; k++) rows[k] = P32(p, 0x40 + 4 * k);
    Rows_Turn3(rows, 3, ang);   /* 0x429f20 */
    for (uint32_t k = 0; k < 9; k++) W32(p, 0x40 + 4 * k, rows[k]);
    int32_t c0 = P32(p, 0xc0);
    W32(p, 0xbc, P32(p, 0xbc) + (fx_mul(c0, dt) - fx_mul(fx_mul(dt, dt), 0xc49)));
    W32(p, 0xc0, c0 - fx_mul(fx_mul(0x20000, dt), 0xc49));
    gw16(p + 0xc8, (uint16_t)est);
    if (P32(p, 0xd8)) {
        frame_from(p, p);
        W32(p, 0xe0, 0);
        W32(p, 0xb4, P32(p, 0xbc));
        return;
    }
    int32_t cur[16], to[16] = {0}, out[16];
    for (uint32_t k = 0; k < 16; k++) cur[k] = P32(p, 4 * k);
    for (uint32_t r = 0; r < 3; r++)
        for (uint32_t k = 0; k < 3; k++) to[4 * r + k] = P32(p, 0x40 + 12 * r + 4 * k);
    for (uint32_t k = 0; k < 3; k++) to[12 + k] = P32(p, 0x64 + 4 * k);
    to[15] = 0x10000;   /* (the original's stack word: Frame_Blend uses the 3 x 4) */
    Frame_Blend(out, cur, to, 0x8000, 0x8000, 0x8000, 0);   /* 0x4224e0 */
    for (uint32_t r = 0; r < 4; r++)
        for (uint32_t k = 0; k < 3; k++) W32(p, 16 * r + 4 * k, out[4 * r + k]);   /* 0x4bab80 */
    W32(p, 0xe0, 0);
    W32(p, 0xb4, P32(p, 0xb4) + fx_mul(P32(p, 0xbc) - P32(p, 0xb4), 0x8000));
}

/* 0x426d80: car c posed from predictor p (its height kept from the last step), the last steps' records
   moved on as a step does, the box corners again */
static void pose_car(Car *c, uint32_t p)
{
    CW32(c, 0xb54, P32(p, 0xd0));
    CW32(c, 0x7a4, 0);
    CW16(c, 0xb10, gm16(p + 0xc4));
    CW32(c, 0x79c, P32(p, 0xb8));
    for (uint32_t k = 0; k < 3; k++) CW32(c, 0x414 + 4 * k, CI32(c, 0x408 + 4 * k));
    c->s[0xb35] = gm8(p + 0xcc);
    CW32(c, 0xc00, P32(p, 0xd4));
    for (uint32_t k = 0; k < 3; k++) CW32(c, 0x408 + 4 * k, P32(p, 0x70 + 4 * k));
    for (uint32_t k = 0; k < 3; k++) CW32(c, 0x420 + 4 * k, P32(p, 0x7c + 4 * k));
    for (uint32_t r = 0; r < 4; r++)
        for (uint32_t k = 0; k < 3; k++) CW32(c, 16 * r + 4 * k, P32(p, 16 * r + 4 * k));   /* 0x4bab80 on +0x750 */
    CW32(c, 0x34, CI32(c, 0x2ec));
    for (uint32_t w = 0; w < 4; w++) {
        for (uint32_t k = 0; k < 3; k++) {
            CW32(c, 0x330 + 12 * w + 4 * k, CI32(c, 0x300 + 12 * w + 4 * k));
            CW32(c, 0x300 + 12 * w + 4 * k, CI32(c, 0x270 + 12 * w + 4 * k));
        }
        CW16(c, 0xabe + 2 * w, (uint16_t)CI16(c, 0xaae + 2 * w));
    }
    for (uint32_t w = 0; w < 8; w++) {
        for (uint32_t k = 0; k < 3; k++) CW32(c, 0x504 + 12 * w + 4 * k, CI32(c, 0x4a4 + 12 * w + 4 * k));
        CW32(c, 0xbbc + 4 * w, 0);
    }
    CW32(c, 0x7a8, CI32(c, 0x7a4));
    for (uint32_t k = 0; k < 3; k++) {
        CW32(c, 0x2f4 + 4 * k, CI32(c, 0x2e8 + 4 * k));
        CW32(c, 0x2e8 + 4 * k, CI32(c, 0x2d0 + 4 * k));
        CW32(c, 0x498 + 4 * k, CI32(c, 0x48c + 4 * k));
    }
    for (uint32_t k = 0; k < 9; k++) CW32(c, 0x384 + 4 * k, CI32(c, 0x360 + 4 * k));
    for (uint32_t k = 0; k < 3; k++) {
        CW32(c, 0x2d0 + 4 * k, CI32(c, 0x30 + 4 * k));   /* 0x4bac40 */
        CW32(c, 0x360 + 4 * k, CI32(c, 4 * k));          /* 0x4bac60 */
        CW32(c, 0x36c + 4 * k, CI32(c, 0x10 + 4 * k));   /* 0x4bac80 */
        CW32(c, 0x378 + 4 * k, CI32(c, 0x20 + 4 * k));   /* 0x4baca0 */
    }
    Car_BoxCorners(c);   /* 0x43eef0 */
    for (uint32_t k = 0; k < 6; k++) CW32(c, 0x5c4 + 4 * k, 0);
    CW32(c, 0xc20, 1);
}

void Sim_SimpleCars(const CarList *l)
{
    if (on()) take();
    NetRec_Places(Sectors_CarPoint(0), Car_SegmentFrac(Car_Get(0)) * 100 >> 16, Sectors_Lap(0));   /* 0x40a580 */
    if (!on()) return;
    for (int i = l->n - 1; i >= 0; i--) {
        int k = l->car[i];
        if (CI32(Car_Get(k), 0xc1c)) predict(pred(k), gm32(EST + 4 * (uint32_t)NetRec_BySlot((uint32_t)k)));
    }
    for (int i = l->n - 1; i >= 0; i--) {
        int k = l->car[i];
        Car *c = Car_Get(k);
        if (CI32(c, 0xc1c)) pose_car(c, pred(k));
    }
}

void Net_CarsAfter(const CarList *l)
{
    int32_t step = gms32(0x519c8c);
    for (int i = l->n - 1; i >= 0; i--) {
        int k = l->car[i];
        Car *c = Car_Get(k);
        uint32_t p = pred(k);
        if (!CI32(c, 0xc20)) continue;
        for (uint32_t q = 0; q < 3; q++) {
            W32(p, 0x30 + 4 * q, CI32(c, 0x2d0 + 4 * q));
            W32(p, 4 * q, CI32(c, 0x360 + 4 * q));
            W32(p, 0x10 + 4 * q, CI32(c, 0x36c + 4 * q));
            W32(p, 0x20 + 4 * q, CI32(c, 0x378 + 4 * q));
        }
        if (P32(p, 0xd8)) {
            Pose_Snap(CI8(c, 0xb1a));   /* 0x42c870 */
            CW32(c, 0xbf8, 1);
            W32(p, 0xd8, 0);
        }
        int32_t old[3];
        for (uint32_t q = 0; q < 3; q++) {
            old[q] = P32(p, 0x70 + 4 * q);
            W32(p, 0x70 + 4 * q, CI32(c, 0x408 + 4 * q) + fx_mul(fx_mul(CI32(c, 0x5c4 + 4 * q), 0x3333), step));
            W32(p, 0x7c + 4 * q, CI32(c, 0x420 + 4 * q) + fx_mul(CI32(c, 0x5d0 + 4 * q), -fx_mul(CI32(c, 0x3fc + 4 * q), CI32(c, 0x75c))));
        }
        W32(p, 0xd4, CI32(c, 0xc00));
        CW32(c, 0x960, P32(p, 0xb4));
        drag(p);
        int32_t s = CI32(c, 0x96c) - fx_mul(step, 0x1eb8);
        CW32(c, 0x96c, s < 0 ? 0 : s);
        int32_t up = fx_mul(CI32(c, 0x494), CI32(c, 0x374)) + fx_mul(CI32(c, 0x48c), CI32(c, 0x36c)) + fx_mul(CI32(c, 0x490), CI32(c, 0x370));
        if (CI32(c, 0x96c) < 1 && up > 0xfc28 && abs(CI32(c, 0x420)) < 0x28f && abs(CI32(c, 0x428)) < 0x28f) {
            CW32(c, 0xc00, 0), CW32(c, 0x96c, 0);
            W32(p, 0xd4, 0);
        }
        if (old[0] == P32(p, 0x70) && old[2] == P32(p, 0x78)) {
            W32(p, 0xdc, 0);
            continue;
        }
        W32(p, 0xdc, 1);
        for (uint32_t q = 0; q < 3; q++) W32(p, 0x94 + 4 * q, P32(p, 0x70 + 4 * q) - old[q]);
        unit_at(p + 0x94);
        int32_t at[3], dv[3], t = 0;
        for (uint32_t q = 0; q < 3; q++) at[q] = P32(p, 0x30 + 4 * q), dv[q] = P32(p, 0x64 + 4 * q) - at[q];
        t = fx_mul(P32(p, 0x9c), dv[2]) + fx_mul(P32(p, 0x94), dv[0]) + fx_mul(P32(p, 0x98), dv[1]);
        for (uint32_t q = 0; q < 3; q++) W32(p, 0x64 + 4 * q, dv[q] - fx_mul(P32(p, 0x94 + 4 * q), t) + at[q]);
        for (uint32_t q = 0; q < 3; q++) W32(p, 0xa0 + 4 * q, at[q]);
    }
}
