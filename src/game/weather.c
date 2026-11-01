/* The rally's weather (see weather.h). */
#include "game/weather.h"
#include "crt.h"
#include "fixmath.h"
#include "game/gstate.h"
#include "game/mem.h"
#include "game/timer.h"
#include "world/tod.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

enum { W = 0x52f100, MAIN = 0x52f1a0, RAIN = 0x52f1f0, RAIN2 = 0x52f240, LIGHT = 0x52f294 };

static uint32_t rally(void) { return gm32(0x52f2ac) & 0x1f; }
/* 0x4077d0: the stage's start (0) or end (1) time, hhmm */
static uint32_t stage_time(uint32_t r, uint32_t s, int which) { return gm8(0x516188 + (s + r * 12) * 2 + (uint32_t)which) * 100u; }
static int32_t fdiv(int32_t a, int32_t b) { return (int32_t)(((int64_t)a << 16) / b); }
/* rand() as a 16.16 fraction: rand * (float)(1/32767) * 65536.0, rounded */
static int32_t rand16(void) { return (int32_t)llrint((double)crt_rand() * (double)3.0518509447574615e-05f * 65536.0); }
static uint32_t table(uint32_t c) { return 0x51627c + (c + rally() * 3) * 0x28; }

/* 0x40dbe0: one weather for every stage */
static void fixed(uint32_t type)
{
    for (uint32_t i = 0; i < 0x14; i++) {
        gw32(W + 8 * i, type);   /* covers both words of every stage */
        gw32(W + 4 + 8 * i, type);
    }
}

/* 0x40d6c0: for each stage of block c the type most common within 1-20 percent either side of its
   share (the end's: the odd entries) */
static void spread(uint32_t c, uint32_t tbl, const int8_t *pct)
{
    int32_t out[4];
    for (int j = 0; j < 4; j++) {
        int32_t cnt[9] = {0};
        int p = pct[2 * j + 1];
        int lo = p - crt_rand() % 0x14 - 1, hi = crt_rand() % 0x14 + 1 + p;
        if (lo < 0) lo = 0;
        else if (hi > 100) hi = 100;
        int n = 0;
        if (lo < hi) {
            n = hi - lo;
            for (int i = lo; i < hi; i++) cnt[gm32(tbl + 4u * (uint32_t)(i / 10))]++;
        }
        int best = -1, bv = -100;
        for (int t = 0; t < 9; t++) {
            cnt[t] = fx_mul(fdiv(cnt[t] << 16, n << 16), 0x640000) >> 16;
            if (bv < cnt[t]) best = t, bv = cnt[t];
        }
        out[j] = best;
    }
    for (int j = 0; j < 4; j++) gw32(MAIN + c * 16 + 4u * (uint32_t)j, c == 2 && j > 1 ? 0 : (uint32_t)out[j]);
}

/* 0x40d9e0: the light of block c's stages, between the rally's dark and bright levels by the weather,
   by the time of day: dark before 5:01 and from 21:00, rising to 12:00, bright to 16:00, falling */
static void light(uint32_t c)
{
    uint32_t r = rally();
    for (uint32_t j = 0; j < 4; j++) {
        uint32_t s = c * 4 + j;
        int32_t a = gms32(W + 8 * s), b = gms32(W + 4 + 8 * s);
        if (a > 5) a -= 3;
        if (b > 5) a -= 3;   /* the second type's shift taken off the first, as the original does */
        int32_t f = fdiv(a + b, 10);
        if (f < 0) f = 0;
        else if (f > 0x10000) f = 0x10000;
        int32_t hi = gms32(0x516974 + 4 * r) + fx_mul(gms32(0x516998 + 4 * r), 0x10000 - f);
        int32_t lo = gms32(0x5169bc + 4 * r) + fx_mul(gms32(0x5169e0 + 4 * r), 0x10000 - f);
        uint32_t t = stage_time(r, s, 0);
        int32_t v;
        if (t < 0x1f5 || t > 0x833) v = lo >> 16;
        else if (t < 0x4b0) v = (fx_mul(hi - lo, fdiv((int32_t)(t / 100 - 5), 7)) + lo) >> 16;
        else if (t < 0x641) v = hi >> 16;
        else v = (fx_mul(lo - hi, fdiv((int32_t)(t / 100 - 0x10), 5)) + hi) >> 16;
        gw8(LIGHT + s, (uint8_t)v);
    }
}

/* 0x40d820: rain in the rallies that have it on wet stages (rally 6 stages 3, 6 and 8, rally 7 the
   first and third blocks), by day: a fifth of the time heavy (0x52f1f0), and whether any of the
   spread is above 80 (0x52f240) */
static void rain(void)
{
    for (uint32_t s = 0; s < 11; s++) {
        gw32(RAIN + 4 * s, 0);
        gw32(RAIN2 + 4 * s, 0);
        if (gm32(W + 8 * s) != 2 || gm32(W + 4 + 8 * s) != 2) continue;
        uint32_t r = Tod_Rally();
        bool wet = (r == 6 && (s == 2 || s == 5 || s == 7)) || (r == 7 && (s / 4 == 0 || s / 4 == 2));
        if (!wet || stage_time(rally(), s, 0) >= 0x834) continue;
        int v = fx_mul(rand16(), 0x640000) >> 16;
        if (v > 0x50) gw32(RAIN + 4 * s, 1);
        int lo = v - crt_rand() % 0x14 - 1, hi = crt_rand() % 0x14 + 1 + v;
        if (lo < 0) lo = 0;
        else if (hi > 100) hi = 100;
        int32_t c[2] = {0, 0}, n = 0;
        if (lo < hi) {
            n = hi - lo;
            for (int i = lo; i < hi; i++) c[i < 0x51 ? 1 : 0]++;
        }
        for (int k = 0; k < 2; k++) c[k] = fx_mul(fdiv(c[k] << 16, n << 16), 0x640000) >> 16;
        gw32(RAIN2 + 4 * s, c[0] != 0);
    }
}

void Weather_Choose(void)
{
    uint32_t m = Gs_Mode();
    if ((m == 2 || m == 9) && gm32(0x52f290)) {
        /* a weather chosen on the menus ("here" on the console) */
        fixed(gm32(0x52f0fc));
        for (uint32_t i = 0; i < 0x14; i++) gw32(MAIN + 4 * i, gm32(0x52f0fc)), gw32(RAIN + 4 * i, 0), gw32(RAIN2 + 4 * i, 0);
        light(0), light(1), light(2);
        return;
    }
    crt_srand(Time_Get());   /* srand(timeGetTime()) */
    bool same = Gs_SelBits14() > 1 && !Gs_ModeBit17();
    int32_t prev = same;
    for (uint32_t c = 0; c < 3; c++) {
        int32_t v[8];
        int8_t pct = 0, pc[8];
        int32_t dur = 0;
        uint32_t tbl = table(c);
        for (int k = 0; k < 8; k++) {
            if (!Gs_SelBit28()) {
                if (dur < 1) {
                    int32_t f = rand16(), u = fx_mul(f, 0xa0000);
                    int32_t type = gms32(tbl + 4u * (uint32_t)(u >> 16));
                    pct = (int8_t)(((int64_t)u * 0xa0000) >> 32);
                    if (k > 0) {
                        bool shifted = false;
                        int32_t a = prev, b = type;
                        if (a > 5) a -= 3, shifted = true;
                        if (b > 5) b -= 3, shifted = true;
                        int32_t d = b - a, lim = gm8(0x516968 + (uint32_t)prev);
                        if (lim < (d < 0 ? -d : d)) d = d < 1 ? -lim : lim;
                        int32_t t = a + d;
                        if (t > 2 && shifted) t += 3;
                        if (type != t) {
                            int idx = -1;
                            for (int i = 0; i < 10; i++)
                                if (t <= gms32(tbl + 4u * (uint32_t)i)) { idx = i; break; }
                            pct = (int8_t)(crt_rand() % 10 + idx * 10);
                            type = t;
                        }
                    }
                    dur = (crt_rand() % (gm8(0x51695c + (uint32_t)type) + 1) + gm8(0x5338f0 + (uint32_t)type)) * 2;
                    if (k) dur++;
                    prev = type;
                } else
                    dur--;
                if (c == 2 && k > 3) v[k] = 0, pc[k] = 0;
                else v[k] = prev, pc[k] = pct;
            } else {
                pc[k] = 0;
                v[k] = gms32(tbl);
            }
        }
        for (uint32_t j = 0; j < 4; j++) {
            gw32(W + c * 0x20 + 8 * j, (uint32_t)v[2 * j]);
            gw32(W + 4 + c * 0x20 + 8 * j, (uint32_t)(same ? v[2 * j] : v[2 * j + 1]));
        }
        spread(c, tbl, pc);
        light(c);
    }
    for (uint32_t s = 0; s < 11; s++)
        if (stage_time(rally(), s, 0) > 0x833) {
            gw32(W + 8 * s, 2);
            gw32(W + 4 + 8 * s, 2);
            gw32(MAIN + 4 * s, 2);
        }
    rain();
}
