/* The surface sounds (see surface.h). */
#include "audio/surface.h"
#include "audio/engine.h"
#include "audio/snd.h"
#include "crt.h"
#include "exe.h"
#include "fixmath.h"
#include "game/camera.h"
#include "game/common.h"
#include "game/effects.h"
#include "game/mem.h"
#include "game/paths.h"
#include "game/race.h"
#include "game/weather.h"
#include "sim/car.h"
#include <stdio.h>

/* A sound set (0x518ca8, 0x1c bytes, 31 of them): the counts of its inside and outside loops and
   skids (+0-+3), the set a surface type uses (+4, read by type), its class (+8: 0 none, 1-4), the
   first ids of its inside and outside loops (+0xc, +0x10) and skids (+0x14, +0x18), set by the
   loader */
#define SET(s) (0x518ca8u + 0x1cu * (uint32_t)(s))
static int set_of(int type) { return (int8_t)gm8(SET(type) + 4); }
static int count(int s, int k) { return (int8_t)gm8(SET(s) + (uint32_t)k); }
static int32_t first(int s, int k) { return gms32(SET(s) + 0xc + 4u * (uint32_t)k); }

/* A car's record (0x5377a8 + 0xb4 n): the wheels' surface types (+0, short, -1 none) and when each
   changed (+8); the surface taken (+0x18 the new, +0x1a the last); ten voices (+0x1c: 0-3 the
   skids, 4 the loop, 5 the next loop, 6 the skid, 7 the next skid, 8 and 9 the rain), their sounds
   (+0x44), when each changes to another of its set (+0x6c) and whether it was started inside (+0x94,
   bytes); the classes (+0xa0 the new, +0xa4 the last), the transition (+0xa8: new + 5 old, 0x19
   none) and when it began (+0xac); the rolling loop's state (+0xb0) */
#define REC(c) (0x5377a8u + 0xb4u * (uint32_t)(c))
#define WHEEL(c, i) (REC(c) + 2u * (uint32_t)(i))
#define WHEEL_T(c, i) (REC(c) + 8u + 4u * (uint32_t)(i))
#define NEWTYPE(c) (REC(c) + 0x18u)
#define CURTYPE(c) (REC(c) + 0x1au)
#define VOICE(c, k) (REC(c) + 0x1cu + 4u * (uint32_t)(k))
#define SOUND(c, k) (REC(c) + 0x44u + 4u * (uint32_t)(k))
#define NEXT(c, k) (REC(c) + 0x6cu + 4u * (uint32_t)(k))
#define STARTED_IN(c, k) (REC(c) + 0x94u + (uint32_t)(k))
#define NEWCLASS(c) (REC(c) + 0xa0u)
#define CURCLASS(c) (REC(c) + 0xa4u)
#define TRANS(c) (REC(c) + 0xa8u)
#define TRANS_T(c) (REC(c) + 0xacu)
#define ROLLING(c) (REC(c) + 0xb0u)
#define NONE 0x19

/* per car: outside the car (0x5375f4, bytes), the levels (0x537588 the loop, 0x537620 the next, 0x537668
   the skid, 0x537640 the next skid) and the last ones (0x5375fc, 0x5375b0, 0x537568, 0x537788), the
   largest slip (0x537544), the wheels' slips (0x537d48) and side slips (0x5374c4, four a car) */
#define OUTSIDE(c) (0x5375f4u + (uint32_t)(c))
#define LV_LOOP(c) (0x537588u + 4u * (uint32_t)(c))
#define LV_NEXT(c) (0x537620u + 4u * (uint32_t)(c))
#define LV_SKID(c) (0x537668u + 4u * (uint32_t)(c))
#define LV_NSKID(c) (0x537640u + 4u * (uint32_t)(c))
#define WAS_LOOP(c) (0x5375fcu + 4u * (uint32_t)(c))
#define WAS_NEXT(c) (0x5375b0u + 4u * (uint32_t)(c))
#define WAS_SKID(c) (0x537568u + 4u * (uint32_t)(c))
#define WAS_NSKID(c) (0x537788u + 4u * (uint32_t)(c))
#define SLIPMAX(c) (0x537544u + 4u * (uint32_t)(c))
#define SLIP(c, w) (0x537d48u + 0x10u * (uint32_t)(c) + 4u * (uint32_t)(w))
#define SIDE(c, w) (0x5374c4u + 0x10u * (uint32_t)(c) + 4u * (uint32_t)(w))
enum { USED = 0x5375d4, NUSED = 0x5375d0, LOADED = 0x5375a8, FIRST = 0x5375ac, RAIN_IN = 0x537564, RAIN_OUT = 0x537dc8,
       MASTER = 0x537664, SIDEMAX = 0x53761c, ROLLFREQ = 0x5374c0 };

static bool no_rain_sounds(void) { return gm8(0x539ed8) != 0; }   /* 0x427aa0 (the other cars' mode) */
static int outside(int car) { return gm8(OUTSIDE(car)); }
static int32_t now(int car) { return Race_StageTime(car); }   /* Timer_Get 0x4781c0 */

/* 0x418d30: voice k of the car playing a sound looped at 22,050 Hz from an offset, its time to
   change set by rand (half a second plus up to a quarter) */
static void play(int car, int32_t id, int k, int32_t vol, uint32_t start)
{
    gw32(VOICE(car, k), (uint32_t)Voice_Play((int)id, vol, 0x5622, start, true, false));
    int32_t t = now(car);
    gw32(NEXT(car, k), (uint32_t)(crt_rand() % 0x19 + 0x32 + t));
    gw8(STARTED_IN(car, k), (uint8_t)outside(car));
    gw32(SOUND(car, k), (uint32_t)id);
}

/* 0x418dd0: voice k stopped (its sound forgotten when asked) */
static void stop(int car, int k, bool forget)
{
    if (forget) gw32(SOUND(car, k), 0xffffffff);
    Voice_Stop(gms32(VOICE(car, k)));
    gw32(VOICE(car, k), 0xffffffff);
}

/* 0x418e20: voice src moved to dst */
static void move(int car, int dst, int src)
{
    gw32(SOUND(car, dst), gm32(SOUND(car, src)));
    gw32(VOICE(car, dst), gm32(VOICE(car, src)));
    gw32(VOICE(car, src), 0xffffffff), gw32(SOUND(car, src), 0xffffffff);
}

/* 0x418e70: the speed over 120 (km/h), 0 to 1.0 */
static int32_t speed_share(int car)
{
    int32_t s = (int32_t)(((int64_t)CI32(Car_Get(car), 0x778) * 0x431168) >> 16) >> 16;
    s = s < 0 ? 0 : s > 0x78 ? 0x78 : s;
    return (int32_t)(((int64_t)s << 32) / 0x780000);
}

/* 0x419b50: one of n by rand, not `not` (the original divides by zero for a set without any; the
   port answers 0) */
static int variant(int not, int n)
{
    if (n == 1) return 0;
    if (n <= 0) return 0;
    int r = crt_rand();
    while (r % n == not) r = crt_rand();
    return r % n;
}

void Surface_Mark(const uint8_t *srf, uint32_t n)
{
    for (uint32_t i = 0; i < 0x1f; i++) gw8(USED + i, 0);
    gw32(NUSED, n);
    for (uint32_t i = 0; i < n; i++) gw8(USED + (uint32_t)(int8_t)gm8(SET(gm8(0x51df58 + srf[i])) + 4), 1);   /* Surface_Map 0x478a10 */
}

void Surface_Reset(void)
{
    gw32(0x537660, 0);
    for (int c = 0; c < 8; c++) {
        gw32(WAS_NEXT(c), 0), gw32(WAS_LOOP(c), 0), gw32(WAS_NSKID(c), 0), gw32(WAS_SKID(c), 0);
        for (int k = 0; k < 10; k++) gw32(VOICE(c, k), 0xffffffff), gw32(SOUND(c, k), 0xffffffff);
        gw32(CURCLASS(c), 0), gw32(NEWCLASS(c), 0);
        gw32(REC(c), 0xffffffff), gw32(REC(c) + 4, 0xffffffff);
        gw16(CURTYPE(c), 0xffff), gw16(NEWTYPE(c), 0xffff);
        gw32(TRANS(c), NONE);
    }
}

void Surface_LoadSounds(void)
{
    const char *dir = Path_Sounds();   /* 0x40ed30 */
    char p[160];
    gw8(LOADED, 0);
    gw32(FIRST, (uint32_t)Snd_Count());
    static const uint32_t fmt[4] = {0x519090, 0x519074, 0x519058, 0x51903c};   /* i, o, ski, sko */
    for (int s = 0; s < 0x1f; s++) {
        if (!gm8(USED + (uint32_t)s)) continue;
        int32_t id = (int32_t)gm8(LOADED) + gms32(FIRST);
        for (int k = 0; k < 4; k++) {
            gw32(SET(s) + 0xc + 4u * (uint32_t)k, (uint32_t)id);
            id += count(s, k);
        }
        for (int k = 0; k < 4; k++)
            for (int i = 0; i < count(s, k); i++) {
                snprintf(p, sizeof p, exe_str(fmt[k]), dir, s, i);
                Snd_LoadNamed(p, Bfl_Surface());   /* 0x4552e0 */
                gw8(LOADED, (uint8_t)(gm8(LOADED) + 1));
            }
    }
    gw32(RAIN_IN, (uint32_t)Snd_Count());
    snprintf(p, sizeof p, exe_str(0x519024), dir);
    Snd_LoadNamed(p, Bfl_Surface());
    gw8(LOADED, (uint8_t)(gm8(LOADED) + 1));
    gw32(RAIN_OUT, (uint32_t)Snd_Count());
    snprintf(p, sizeof p, exe_str(0x51900c), dir);
    Snd_LoadNamed(p, Bfl_Surface());
    gw8(LOADED, (uint8_t)(gm8(LOADED) + 1));
    /* (0x455020 frees a cache the port does not keep) */
    Surface_Reset();
}

void Surface_Volume(int32_t v) { gw32(MASTER, (uint32_t)fx_mul(v, 0x10000)); }

void Surface_Silence(void)
{
    for (int c = 0; c < 8; c++)
        for (int k = 0; k < 10; k++)
            if (Voice_Playing(gms32(VOICE(c, k)))) Voice_SetVolume(gms32(VOICE(c, k)), 0);
}

/* the new surface's set's loops and skids, by the camera (inside or out) */
static void loop_v(int car, int s, int k) { play(car, variant(-1, count(s, outside(car))) + first(s, outside(car)), k, 0, 0); }
static void skid_v(int car, int s, int k) { play(car, variant(-1, count(s, outside(car) + 2)) + first(s, outside(car) + 2), k, 0, 0); }
static void loop_first(int car, int s, int k) { play(car, first(s, outside(car)), k, 0, 0); }
static void skids3(int car, int s)
{
    if (!no_rain_sounds()) skid_v(car, s, 1), skid_v(car, s, 2), skid_v(car, s, 3);
}
static void stop123(int car)
{
    if (!no_rain_sounds()) stop(car, 1, true), stop(car, 2, true), stop(car, 3, true);
}

/* 0x419200: a transition's start (new class + 5 old): the next loop (5) and skid (7) of the new set,
   the four wheels' skids (0-3, the last three not for the other cars) or the loop itself (4) from
   no surface; the old ones stopped where the new class has none */
static void trans_start(int car)
{
    int16_t t = (int16_t)gm16(NEWTYPE(car));
    int s = t != -1 ? set_of(t) : -1;
    bool S = s != -1 && t != -1;
    bool other = no_rain_sounds();
    switch (gms32(TRANS(car))) {
    case 1: if (S) loop_v(car, s, 5); break;
    case 2: if (S) loop_v(car, s, 5), skid_v(car, s, 7); break;
    case 3:
        if (!S) return;
        loop_first(car, s, 4);
        skid_v(car, s, 0), skids3(car, s);
        break;
    case 4: if (S) loop_v(car, s, 5), skid_v(car, s, 0), skids3(car, s); break;
    case 5: stop(car, 4, true); break;
    case 6:
        if (!S) break;
        if (other) stop(car, 4, true);
        loop_v(car, s, 5);
        break;
    case 7:
        if (!S) break;
        if (other) stop(car, 4, true);
        loop_v(car, s, 5), skid_v(car, s, 7);
        break;
    case 8:
        stop(car, 4, true);
        if (!S) return;
        skids3(car, s);
        loop_first(car, s, 4);
        skid_v(car, s, 0);
        break;
    case 9:
        stop(car, 4, true);
        if (!S) break;
        skids3(car, s);
        loop_v(car, s, 5);
        skid_v(car, s, 0);
        break;
    case 10: stop(car, 4, true), stop(car, 6, true); break;
    case 0xb:
        if (!S) return;
        if (other) stop(car, 4, true), stop(car, 6, true);
        loop_v(car, s, 5);
        break;
    case 0xc:
        if (!S) return;
        if (other) stop(car, 4, true), stop(car, 6, true);
        loop_v(car, s, 5), skid_v(car, s, 7);
        break;
    case 0xd:
        stop(car, 4, true), stop(car, 6, true);
        if (!S) return;
        loop_first(car, s, 4);
        skid_v(car, s, 0), skids3(car, s);
        break;
    case 0xe:
        stop(car, 4, true), stop(car, 6, true);
        if (!S) return;
        loop_v(car, s, 5);
        skid_v(car, s, 0), skids3(car, s);
        break;
    case 0xf: case 0x14: stop(car, 4, true), stop(car, 0, true), stop123(car); break;
    case 0x10:
        stop(car, 4, true), stop(car, 0, true), stop123(car);
        if (S) loop_v(car, s, 5);
        break;
    case 0x11:
        stop(car, 4, true), stop(car, 0, true), stop123(car);
        if (S) loop_v(car, s, 5), skid_v(car, s, 7);
        break;
    case 0x13:
        stop(car, 4, true);
        if (S) loop_v(car, s, 5);
        break;
    case 0x15:
        stop(car, 0, true);
        if (!other) stop(car, 1, true), stop(car, 2, true), stop(car, 3, true);
        else stop(car, 4, true);
        if (S) loop_v(car, s, 5);
        break;
    case 0x16:
        stop(car, 4, true), stop(car, 0, true), stop123(car);
        if (S) loop_v(car, s, 5), skid_v(car, s, 7);
        break;
    case 0x17:
        stop(car, 4, true);
        if (S) loop_first(car, s, 4);
        break;
    case 0x18:
        stop(car, 4, true);
        if (S) loop_v(car, s, 5);
        break;
    default: break;
    }
}

/* 0x419b90: a transition's end: the next loop and skid become the loop and skid (the old ones
   stopped first where they still play), and the levels carried over */
static void trans_end(int car)
{
    bool other = no_rain_sounds(), loops = false, skids = false;
    switch (gms32(TRANS(car))) {
    case 1: case 4: case 9: case 0xe: case 0x10: case 0x13: case 0x18: loops = true; break;
    case 2: case 0x11: case 0x16: loops = skids = true; break;
    case 6: case 0x15:
        if (!other) stop(car, 4, true);
        loops = true;
        break;
    case 7:
        if (!other) stop(car, 4, true);
        loops = skids = true;
        break;
    case 0xb:
        if (!other) stop(car, 4, true), stop(car, 6, true);
        loops = true;
        break;
    case 0xc:
        if (!other) stop(car, 4, true), stop(car, 6, true);
        loops = skids = true;
        break;
    default: break;
    }
    if (loops) move(car, 4, 5);
    if (skids) move(car, 6, 7);
    gw32(WAS_LOOP(car), gm32(WAS_NEXT(car)));
    gw32(WAS_SKID(car), gm32(WAS_NSKID(car)));
    gw32(WAS_NEXT(car), 0), gw32(WAS_NSKID(car), 0);
}

/* 0x41a340: the wheels' surfaces (none off the ground, past type 30 or of type 16) and when each
   changed; the car's is the one of three or four wheels, or of two that are not two pairs, else the
   one that changed last; a new set class or type ends the transition running and starts one (its
   fade half a second), which ends by itself after that */
static void wheels(int car)
{
    const Car *c = Car_Get(car);
    for (int i = 0; i < 4; i++) {
        int16_t t = CI16(c, 0xaae + 2 * i);
        if (!CI32(c, 0xbac + 4 * i) || 0x1e < t || t < 0 || t == 0x10) t = -1;
        if ((int16_t)gm16(WHEEL(car, i)) != t) gw32(WHEEL_T(car, i), (uint32_t)now(car));
        gw16(WHEEL(car, i), (uint16_t)t);
    }
    int n[4], best = 0, most = 0;
    bool pairs = true;
    for (int i = 0; i < 4; i++) {
        int16_t t = (int16_t)gm16(WHEEL(car, i));
        n[i] = 0;
        for (int j = 0; j < 4; j++)
            if ((int16_t)gm16(WHEEL(car, j)) == t) n[i]++;
        if (t == -1 && n[i] != 4) n[i] = 0;
        if (most < n[i]) best = i, most = n[i];
        if (n[i] != 2) pairs = false;
    }
    int type = 0;
    bool found = false;
    for (int i = 0; i < 4 && !found; i++) {
        if (2 < n[i] || (n[i] == 2 && !pairs)) type = (int16_t)gm16(WHEEL(car, best)), found = true;
    }
    if (!found) {
        uint32_t least = 0xffffffff;
        for (int i = 0; i < 4; i++)
            if ((uint32_t)(now(car) - gms32(WHEEL_T(car, i))) < least)
                type = (int16_t)gm16(WHEEL(car, i)), least = (uint32_t)(now(car) - gms32(WHEEL_T(car, i)));
    }
    int32_t cls = type == -1 ? 0 : gms32(SET(set_of(type)) + 8);
    if (gms32(CURCLASS(car)) != cls || (int16_t)gm16(CURTYPE(car)) != type) {
        if (gms32(TRANS(car)) != NONE) trans_end(car);
        gw16(NEWTYPE(car), (uint16_t)type);
        gw32(NEWCLASS(car), (uint32_t)cls);
        gw32(TRANS_T(car), (uint32_t)now(car));
        gw32(TRANS(car), (uint32_t)(gms32(NEWCLASS(car)) + gms32(CURCLASS(car)) * 5));
        trans_start(car);
        gw32(CURCLASS(car), gm32(NEWCLASS(car)));
        gw16(CURTYPE(car), gm16(NEWTYPE(car)));
    }
    if (gms32(TRANS(car)) != NONE && 0x32 < (uint32_t)(now(car) - gms32(TRANS_T(car)))) {
        trans_end(car);
        gw32(TRANS(car), NONE);
    }
}

/* 0x41ae80: on type 25 (a surface rolled on) the loop is the set's first, started again (from
   0x3542) when the throttle closes and anew when it opens */
static void rolling(int car)
{
    if ((int16_t)gm16(NEWTYPE(car)) != NONE) return;
    const Car *c = Car_Get(car);
    int32_t base = gms32(0x518f70 + 4u * (uint32_t)outside(car));
    if (CI32(c, 0x79c) < 1) {
        if (!gm8(ROLLING(car))) return;
        if (Voice_Playing(gms32(VOICE(car, 4)))) Voice_Stop(gms32(VOICE(car, 4))), gw32(VOICE(car, 4), 0xffffffff);
        play(car, base + 1, 4, 0, 0x3542);
        gw8(ROLLING(car), 0);
    } else if (!gm8(ROLLING(car))) {
        if (Voice_Playing(gms32(VOICE(car, 4)))) Voice_Stop(gms32(VOICE(car, 4))), gw32(VOICE(car, 4), 0xffffffff);
        play(car, base, 4, 0, 0);
        gw8(ROLLING(car), 1);
    }
}

/* a level stepped from the last by at most up (and down 0xccc) */
static void limit(uint32_t at, uint32_t was, int32_t up)
{
    int32_t v = gms32(at), w = gms32(was);
    if (v - w > up) gw32(at, (uint32_t)(w + up));
    else if (v - w < -0xccc) gw32(at, (uint32_t)(w - 0xccc));
}

static void set_vol(int car, int player, int k, int32_t lvl)
{
    if (Voice_Playing(gms32(VOICE(car, k))))
        Voice_SetVolume(gms32(VOICE(car, k)), fx_mul(Engine_DistanceFade(car, player), fx_mul(gms32(MASTER), lvl)));
}

/* 0x41a5c0: the levels. The loops by the speed share, faded across the transition, plus the largest
   slip; the skids by the largest slip the same way; each stepped from the last. On type 25 the
   loops follow the side slips, or (the car slow or its +0x1d2 clear, not rolling) all at 2.0 while
   the four +0x860 are under 10 and it moves with +0x1d8 or +0x1d3 set, the rolling loop pitched up
   to 22,050 Hz at 50; the wheels' skids by their slips (the other cars: one voice for the sum).
   (The original reads the first wheel's +0x860 four times for the grip share.) */
static void levels(int car, int player)
{
    int32_t spd = speed_share(car);
    const Car *c = Car_Get(car);
    uint32_t sum = 0;
    for (int i = 0; i < 4; i++) {
        int32_t a = CI32(c, 0x860);
        if (a < 0) a = -a;
        int32_t v = (int32_t)(((int64_t)a << 16) / 0xa0000);
        if (0x10000 < v) v = 0x10000;
        sum += (uint32_t)(0x10000 - v);
    }
    int32_t grip = (int32_t)(((int64_t)(int32_t)sum << 16) / 0x40000);
    int32_t kmh = (int32_t)(((int64_t)CI32(c, 0x778) * 0x431168) >> 16) >> 16;
    gw32(SIDEMAX, 0), gw32(SLIPMAX(car), 0);
    for (int w = 0; w < 4; w++) {
        gw32(SLIP(car, w), (uint32_t)Wheel_Slip(car, w));
        /* 0x465e40: the side slip |+0x870| less 0.15, 0 to 1.0 */
        int32_t s = CI32(c, 0x870 + 4 * w);
        s = (s < 0 ? -s : s) - 0x2666;
        s = s < 1 ? 0 : s > 0xffff ? 0x10000 : s;
        gw32(SIDE(car, w), (uint32_t)s);
        if (gms32(SLIPMAX(car)) < gms32(SLIP(car, w))) gw32(SLIPMAX(car), gm32(SLIP(car, w)));
        if (gms32(SIDEMAX) < s) gw32(SIDEMAX, (uint32_t)s);
    }
    int32_t in = 0x10000, out = 0;
    if (gms32(TRANS(car)) != NONE) {
        uint32_t t = (uint32_t)(now(car) - gms32(TRANS_T(car)));
        in = (int32_t)(((int64_t)(int32_t)t << 16) / 0x320000);
        out = 0x10000 - in;
    }
    int32_t mx = gms32(SLIPMAX(car));
    gw32(LV_LOOP(car), (uint32_t)fx_mul(in, spd));
    gw32(LV_NEXT(car), (uint32_t)fx_mul(out, spd));
    gw32(LV_SKID(car), (uint32_t)fx_mul(in, mx));
    gw32(LV_NSKID(car), (uint32_t)fx_mul(out, mx));
    gw32(LV_NEXT(car), (uint32_t)(gms32(LV_NEXT(car)) + mx));
    gw32(LV_LOOP(car), (uint32_t)(gms32(LV_LOOP(car)) + mx));
    limit(LV_SKID(car), WAS_SKID(car), 0xc000);
    limit(LV_NSKID(car), WAS_NSKID(car), 0xc000);
    limit(LV_LOOP(car), WAS_LOOP(car), 0xccc);
    limit(LV_NEXT(car), WAS_NEXT(car), 0xccc);
    int32_t side = fx_mul(gms32(SIDEMAX), 0x20000);
    if (0x10000 < side) side = 0x10000;
    gw32(SIDEMAX, (uint32_t)side);
    if ((int16_t)gm16(NEWTYPE(car)) == NONE) {
        uint32_t lv[4] = {LV_LOOP(car), LV_NEXT(car), LV_SKID(car), LV_NSKID(car)};
        if (!CI8(c, 0x1d2) || 0x31 < kmh) {
            if (!gm8(ROLLING(car))) {
                int32_t hi = (int32_t)(((int64_t)CI32(c, 0x778) * 0x431168) >> 32);
                if (CI32(c, 0x860) < 0xa0000 && CI32(c, 0x864) < 0xa0000 && CI32(c, 0x868) < 0xa0000 && CI32(c, 0x86c) < 0xa0000 &&
                    (CI32(c, 0x1d8) != 0 || CI8(c, 0x1d3)) && 0x50000 < (int32_t)((uint32_t)hi << 16)) {
                    for (int i = 0; i < 4; i++) gw32(lv[i], 0x20000), gw32(SLIP(car, i), (uint32_t)(0x10000 - grip));
                } else
                    for (int i = 0; i < 4; i++) gw32(lv[i], 0);
                int32_t k2 = (int32_t)(((int64_t)CI32(c, 0x778) * 0x431168) >> 16) >> 16;
                gw32(ROLLFREQ, (uint32_t)(k2 < 0x32 && -1 < k2 ? (k2 - 0x32) * 0x2b11 / 0x32 + 0x5622 : 0x5622));
                Voice_SetFrequency(gms32(VOICE(car, 4)), gm32(ROLLFREQ));
            } else
                for (int i = 0; i < 4; i++) gw32(lv[i], 0);
        } else {
            for (int i = 0; i < 4; i++) gw32(lv[i], (uint32_t)side);
            for (int w = 0; w < 4; w++) gw32(SLIP(car, w), (uint32_t)fx_mul(gms32(SLIP(car, w)), 0x10000 - side));
        }
    }
    if (0x10000 < gms32(LV_LOOP(car))) gw32(LV_LOOP(car), 0x10000);
    if (0x10000 < gms32(LV_NEXT(car))) gw32(LV_NEXT(car), 0x10000);
    set_vol(car, player, 4, gms32(LV_LOOP(car)));
    set_vol(car, player, 5, gms32(LV_NEXT(car)));
    set_vol(car, player, 6, gms32(LV_SKID(car)));
    set_vol(car, player, 7, gms32(LV_NSKID(car)));
    if (!no_rain_sounds()) {
        for (int w = 0; w < 4; w++) set_vol(car, player, w, gms32(SLIP(car, w)));
    } else {
        int32_t all = 0;
        for (int w = 0; w < 4; w++) all += gms32(SLIP(car, w));
        if (0x10000 < all) all = 0x10000;
        set_vol(car, player, 0, all);
    }
    gw32(WAS_SKID(car), gm32(LV_SKID(car)));
    gw32(WAS_NSKID(car), gm32(LV_NSKID(car)));
    gw32(WAS_LOOP(car), gm32(LV_LOOP(car)));
    gw32(WAS_NEXT(car), gm32(LV_NEXT(car)));
}

/* 0x41a0a0: the rain by its level (1.2 times the weather's, rain only): inside on the roof (voice 8,
   quieter and higher with the speed to 100), outside on the body (voice 9, by the speed share) */
static void rain(int car, int player)
{
    int32_t spd = speed_share(car);
    const Car *c = Car_Get(car);
    int32_t kmh = (int32_t)(((int64_t)CI32(c, 0x778) * 0x431168) >> 16) >> 16;
    int32_t lvl = Weather_Kind(car) == 1 ? fx_mul(Weather_Level(car), 0x13333) : 0;   /* 0x460bf0, 0x460c10 */
    if (0x10000 < lvl) lvl = 0x10000;
    int32_t fade = Engine_DistanceFade(car, player), m = gms32(MASTER);
    if (!outside(car)) {
        if (Voice_Playing(gms32(VOICE(car, 9)))) stop(car, 9, true);
        int32_t k = kmh < 0 ? 0 : kmh < 0x65 ? 0x10000 - fx_mul((int32_t)(((int64_t)kmh << 32) / 0x640000), 0x8000) : 0x8000;
        int32_t v = fx_mul(lvl, k);
        int f = kmh < 0 ? 0 : kmh < 0x65 ? kmh * 11000 / 100 : 11000;
        if (!Voice_Playing(gms32(VOICE(car, 8)))) play(car, gms32(RAIN_IN), 8, fx_mul(fade, fx_mul(m, v)), 0);
        Voice_SetVolume(gms32(VOICE(car, 8)), fx_mul(fade, fx_mul(m, v)));
        Voice_SetFrequency(gms32(VOICE(car, 8)), (uint32_t)(f + 0x5622));
        return;
    }
    if (Voice_Playing(gms32(VOICE(car, 8)))) stop(car, 8, true);
    if (!Voice_Playing(gms32(VOICE(car, 9)))) play(car, gms32(RAIN_OUT), 9, fx_mul(fade, fx_mul(m, lvl)), 0);
    Voice_SetVolume(gms32(VOICE(car, 9)), fx_mul(fade, fx_mul(m, fx_mul(lvl, spd))));
}

/* 0x419ed0: the loops and skids started on the other side of the camera (inside, outside) begun
   again from the other side's sounds (the loop and skid only when no transition runs, the loop not
   on type 25) */
static void view_swap(int car)
{
    int16_t t = (int16_t)gm16(NEWTYPE(car));
    if (t == -1) return;
    int s = set_of(t), o = outside(car);
    for (int k = 4; k < 8; k++) {
        int32_t h = gms32(VOICE(car, k));
        if (h == -1 || !Voice_Playing(h) || gm8(STARTED_IN(car, k)) == o) continue;
        bool skid = k >= 6;
        if ((k == 4 && !(gms32(TRANS(car)) == NONE && t != NONE)) || (k == 6 && gms32(TRANS(car)) != NONE)) continue;
        int32_t v = Voice_Volume(h);
        stop(car, k, true);
        int j = skid ? o + 2 : o;
        play(car, variant(-1, count(s, j)) + first(s, j), k, v, 0);
    }
}

/* 0x419cd0: a loop or skid with more than one of its set changes to another (rand) when its time
   comes (the loop and skid only when no transition runs; the loop not on type 25) */
static void variants(int car)
{
    int16_t t = (int16_t)gm16(NEWTYPE(car));
    if (t == -1) return;
    int s = set_of(t), o = outside(car);
    for (int k = 0; k < 10; k++) {
        int32_t h = gms32(VOICE(car, k));
        if ((k == 4 && t == NONE) || h == -1 || !Voice_Playing(h) || (uint32_t)now(car) <= gm32(NEXT(car, k))) continue;
        if (k < 4 || k > 7) continue;
        int j = k >= 6 ? o + 2 : o;
        if (count(s, j) <= 1) continue;
        if ((k == 4 || k == 6) && gms32(TRANS(car)) != NONE) continue;
        int32_t v = Voice_Volume(h);
        stop(car, k, false);
        play(car, variant(gms32(SOUND(car, k)), count(s, j)) + first(s, j), k, v, 0);
    }
}

void Surface_Step(int car, int player)
{
    int k = Camera_Kind(player);
    gw8(OUTSIDE(car), k == 1 || k == 2 || k == 3 ? 0 : 1);
    wheels(car);    /* 0x41a340 */
    rolling(car);   /* 0x41ae80 */
    levels(car, player);   /* 0x41a5c0 */
    if (!no_rain_sounds()) rain(car, player);   /* 0x41a0a0 */
    view_swap(car);   /* 0x419ed0 */
    variants(car);    /* 0x419cd0 */
}
