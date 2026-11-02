/* The race times and the HUD (see hud.h). */
#include "game/hud.h"
#include "game/profile.h"
#include "crt.h"
#include "text.h"
#include "world/tod.h"
#include "world/stage.h"
#include "game/paths.h"
#include "exe.h"
#include "game/pause.h"
#include "game/opponents.h"
#include <string.h>
#include "game/sectors.h"
#include "game/gstate.h"
#include "game/mem.h"
#include "game/race.h"
#include "game/car_load.h"
#include "font.h"
#include "render/d3d7.h"
#include "render/twod.h"
#include "sim/sim.h"
#include "sim/progress.h"
#include "fixmath.h"
#include "audio/snd.h"
#include "audio/codriver.h"
#include "game/common.h"
#include "pending.h"
#include <stdio.h>
#include <stdlib.h>

static int32_t shown, clock_;          /* DAT_0053d1b0, DAT_0053d1b4 */
static bool capped;                    /* DAT_0053d1d8 */
static int32_t finish[CAR_MAX];        /* 0x53d1b8 */
static uint8_t done[CAR_MAX];          /* 0x53d1da */
static int32_t hud_time[4];            /* 0x536e0c + 0x48 n */
static int32_t splits[4][9];           /* 0x53e190: the players' times at each split */
static bool hud_ready;                 /* DAT_00536bfc: the reference splits taken */
static int32_t ref_split[12];          /* 0x536e94: the leader's (or record's) time at each split */
static int split_idx[4];               /* 0x536e00 + 0x48 n: the last split passed */
static int hud_place[4];               /* 0x536dfc + 0x48 n */
static int rows[4][3];                 /* 0x536c94: the standings' three places */
static int32_t my_split[4][12];        /* 0x536e10 + 0x48 n */
static uint8_t show_for[4];            /* DAT_00536c20 */
static int32_t last_time[4], ref_time[4];   /* 0x536e04, 0x536e08 + 0x48 n: the time at the last split, the reference */

void Times_Clear(void) { shown = clock_ = 0; }

/* The split bar's tables stay at the original's addresses, as its layout reads past their ends (the
   thirteenth box is the first colour): per player (0xa0 apart) 12 boxes (x, y, w, h), 12 colours,
   the passed part of the current one and the rest */
#define BAR_BOX(p, i) (0x536cb8u + 0xa0u * (uint32_t)(p) + 8u * (uint32_t)(i))
#define BAR_COL(p, i) (0x536d18u + 0xa0u * (uint32_t)(p) + 4u * (uint32_t)(i))
#define BAR_DONE(p) (0x536d48u + 0xa0u * (uint32_t)(p))
#define BAR_REST(p) (0x536d50u + 0xa0u * (uint32_t)(p))
#define SPLIT_PT(i) (0x536ff0u + 8u * (uint32_t)(i))   /* each split's centre-line point, then its distance */
#define NSPLITS 0x536c90u

static void box_at(uint32_t a, int16_t r[4]) { for (int i = 0; i < 4; i++) r[i] = gms16(a + 2u * (uint32_t)i); }

/* 0x411880: the screen split between two players */
static bool split_screen(void)
{
    if (Gs_SelBits14() > 1 && !Gs_ModeBit17() && Gs_Mode() != 4) return true;
    if (Gs_Mode() == 4) PENDING_NOTE(0x4070f0, "Split_Screen (the network game's)");
    return false;
}

/* 0x411550: the splits shown (no more than 12 unless one lap), each one's point and its distance
   along the centre line (the sum of the steps' lengths over 100); the ones past the last at the
   finish. Returns the stage's length the same way. */
static int32_t split_distances(void)
{
    int32_t n;
    if ((gm32(0x52f2ac) >> 0x10 & 0xf) == 1) n = Gs_SelBit25() ? Sectors_Count() + 1 : Sectors_Count();   /* Gs_Laps 0x406990 */
    else n = Sectors_Count() > 0xc ? 0xc : Sectors_Count();
    gw32(NSPLITS, (uint32_t)n);
    int32_t d = 0, at = 0;
    for (int i = 0; i < Cat_FinishPoint(); i++) {
        if (i > 0) {
            int32_t a[3], b[3];
            Cat_Point(i - 1, a);
            Cat_Point(i, b);
            int64_t x = b[0] - a[0], y = b[1] - a[1], z = b[2] - a[2];
            at = d + (int32_t)Fx_Sqrt((uint32_t)((x * x) >> 16) + (uint32_t)((y * y) >> 16) + (uint32_t)((z * z) >> 16)) / 100;
        }
        for (int k = 0; k < n; k++)
            if (Sectors_Point(k) >> 16 == i) gw32(SPLIT_PT(k), (uint32_t)i), gw32(SPLIT_PT(k) + 4, (uint32_t)at);
        d = at;
    }
    for (int k = Gs_SelBit25() ? n - 1 : n; k < 0xc; k++) gw32(SPLIT_PT(k) + 4, (uint32_t)d), gw32(SPLIT_PT(k), (uint32_t)(Cat_FinishPoint() - 1));
    return d;
}

/* 0x411b20: the split bar's boxes, the stage's length across the top of the screen from the left
   edge (further left in modes 8, 9, 11, another place on a split screen) to 95%, each split's box
   from its distance; a box's colour green or red when the player was faster or slower over it than
   the reference (in mode 4 than the other player) */
static void bar_layout(void)
{
    int32_t total = split_distances();
    int w, h;
    D3d_ScreenSize(&w, &h);
    uint32_t m = Gs_Mode();
    bool wide = m == 8 || m == 9 || m == 11;
    int left;
    if (wide) left = (w * 0xc00) >> 16;
    else if (split_screen() && !(gm32(0x52afb4) >> 0x12 & 1)) left = ((w * 0xc934) >> 16) - 10;   /* 0x405dc0 */
    else left = (w * 0xab9b) >> 16;
    if (total == 0) total = 1;
    int32_t n = gms32(NSPLITS);
    if (n == 0) return;
    for (int p = 0; p < 2; p++) {
        for (int i = 0; i < 0xc; i++) {
            int16_t ox = 0, oy = 0;
            if (split_screen()) {
                if (!(gm32(0x52afb4) >> 0x12 & 1)) { if (p == 0) ox = (int16_t)-(w / 2); }   /* (the first player's moved off to the left) */
                else if (p == 1) oy = (int16_t)(h / 2);
            }
            int32_t f = (int32_t)(((int64_t)gms32(SPLIT_PT(i) + 4) << 16) / total);
            int16_t x = (int16_t)((int16_t)(((int64_t)f * (int64_t)((((w * 0xf400) >> 16) - left) * 0x10000)) >> 32) + left + ox);
            int16_t y = (int16_t)((wide ? (int16_t)((uint32_t)(h * 0x554) >> 16) : (int16_t)((uint32_t)(h << 12) >> 16)) + oy);
            if (i == n) x = (int16_t)((int16_t)((uint32_t)(w * 0xf400) >> 16) + ox);
            gw16(BAR_BOX(p, i), (uint16_t)x);
            gw16(BAR_BOX(p, i) + 2, (uint16_t)y);
            gw16(BAR_BOX(p, i) + 6, (uint16_t)((int16_t)((uint32_t)(h * 0xaac) >> 16) + 1));
            /* the default colour, its alpha the byte before it */
            gw8(BAR_COL(p, i), gm8(0x5170a8)), gw8(BAR_COL(p, i) + 1, gm8(0x5170a9));
            gw8(BAR_COL(p, i) + 2, gm8(0x5170aa)), gw8(BAR_COL(p, i) + 3, gm8(0x5170a7));
            int32_t mine = my_split[p][i], prev = i ? my_split[p][i - 1] : hud_time[p];
            if (m == 4) {
                if (mine) {
                    int32_t other = my_split[(p + 1) % 2][i];
                    gw32(BAR_COL(p, i) - 4, gm32(!other || mine < other ? 0x51709c : 0x5170a0));
                }
            } else if (mine) {
                int32_t rp = i ? ref_split[i - 1] : gms32(0x536e90);
                gw32(BAR_COL(p, i) - 4, gm32(mine - prev < ref_split[i] - rp ? 0x51709c : 0x5170a0));
            }
        }
        for (int i = 0; i < 0xc; i++) gw16(BAR_BOX(p, i) + 4, (uint16_t)(gms16(BAR_BOX(p, i + 1)) - gms16(BAR_BOX(p, i))));
    }
}

/* the messages' state, at the original's addresses (Hud_Draw reads them): per player a countdown
   (bytes at 0x536c08) and five flags (0x536c00 a lap record, 0x536c88, 0x536e88 a best without a
   profile, 0x536c28 the best so far, 0x536ec4 a lap done); the best time 0x53706c and its driver's
   name 0x536ed0 */
enum { MSG_COUNT = 0x536c08, MSG_REC = 0x536c00, MSG_88 = 0x536c88, MSG_NOPROF = 0x536e88, MSG_BEST = 0x536c28,
       MSG_LAP = 0x536ec4, MSG_TIME = 0x53706c, MSG_NAME = 0x536ed0 };

static void msg_clear(int p)
{
    const uint32_t o = 4 * (uint32_t)p;
    gw32(MSG_REC + o, 0), gw32(MSG_88 + o, 0), gw32(MSG_NOPROF + o, 0), gw32(MSG_BEST + o, 0), gw32(MSG_LAP + o, 0);
}

/* 0x415e30: the map: a mark every hundred points (0x536c24, at least one), the centre line's extent
   giving the scale (0x53705c: 0.64 over the longer side, 16.16) and the middle (0x536bf0, 0x536bf8;
   0x536bf4 zero); 0x536fe0, the rows' alternative, cleared (nothing sets it) */
static void map_init(void)
{
    int32_t x1 = -0x7d000000, x0 = 0x7d000000, z1 = -0x7d000000, z0 = 0x7d000000;
    gw32(0x536fe0, 0);
    uint32_t n = (uint32_t)Cat_FinishPoint();
    gw32(0x536c24, n < 100 ? 1 : n / 100 + 1);
    for (int i = 0; i < Cat_FinishPoint(); i++) {
        int32_t q[3];
        Cat_Point(i, q);
        if (x1 < q[0]) x1 = q[0];
        if (q[0] < x0) x0 = q[0];
        if (z1 < q[2]) z1 = q[2];
        if (q[2] < z0) z0 = q[2];
    }
    int64_t span = x1 - x0 < z1 - z0 ? (int64_t)z1 - z0 : (int64_t)x1 - x0;
    gw32(0x53705c, span ? (uint32_t)(int32_t)(0xa3d70000LL / span) : 0);   /* (the original traps on 0) */
    gw32(0x536bf4, 0);
    gw32(0x536bf0, (uint32_t)((x0 + x1) / 2));
    gw32(0x536bf8, (uint32_t)((z0 + z1) / 2));
}

/* 0x415870: on circuits (selection bit 24 or 25) the three counters 0x536c30 run down and the rows
   are the first three with the car's place (0x536fe0 is never set, so the other way, by the car's
   position 0x448330, never runs); out of modes 5 and 6 a split passed shows the split for 75 steps */
static void arcade_rows(int p)
{
    if (Gs_SelBit24() || Gs_SelBit25()) {
        for (uint32_t a = 0x536c30; a < 0x536c3c; a += 4)
            if (0 < gms32(a)) gw32(a, (uint32_t)(gms32(a) - 1));
        if (!gm32(0x536fe0)) {
            for (int i = 0; i < 3; i++) rows[p][i] = i;
            hud_place[p] = Times_Place(p);
        } else {
            int k = (int8_t)gm8(0x53d9ea + 0x88u * (uint32_t)p);   /* 0x448330 */
            rows[p][0] = rows[p][1] = rows[p][2] = -1;
            hud_place[p] = k;
            if (k != -1) {
                int b = k < 1 ? k : k < 5 ? k - 1 : k - 2;
                for (int i = 0; i < 3; i++) rows[p][i] = b + i;
            }
        }
    }
    uint32_t m = Gs_Mode();
    if (m != 5 && m != 6 && Sectors_Split(p)) show_for[p] = 0x4b;
}

/* Hud_Init 0x411450 (Stage_Load with 0, the line with 1, the restart with 0x53810c): the clock's
   cap, the reference splits forgotten (0x4111a0), the bar laid out; the map in the other modes */
void Hud_Init(int again)
{
    gw32(0x537068, Gs_SelBit25() || Gs_SelBit24() ? 24000 : 48000);
    /* 0x4111a0 */
    memset(ref_split, 0, sizeof ref_split);
    hud_ready = false;
    gw32(0x536c40, 0), gw32(0x536c3c, 0), gw32(0x537050, 0);
    gw32(0x536c30, 0), gw32(0x536c34, 0), gw32(0x536c38, 0);
    memset(show_for, 0, sizeof show_for);
    for (int i = 0; i < 4; i++) {
        rows[i][0] = rows[i][1] = rows[i][2] = -1;
        last_time[i] = -1, split_idx[i] = 0, hud_time[i] = 0, hud_place[i] = 0, ref_time[i] = 0;
        memset(my_split[i], 0, sizeof my_split[i]);
    }
    bar_layout();
    for (int i = 0; i < 2; i++) msg_clear(i);
    gw32(0x536c08, 0), gw32(0x536cac, 0), gw32(0x536c04, 0), gw32(0x537050, 0);
    if (!again) gw32(0x53706c, 0), gw8(MSG_NAME, 0);   /* (sprintf of the empty string 0x52ea5c) */
    uint32_t m = Gs_Mode();
    if (m == 5 || m == 6 || m == 7 || m == 4 || m == 11 || m == 12 || Gs_SelBit25()) map_init();   /* 0x415e30 */
}

int32_t Times_Race(void) { return shown; }
int32_t Times_Finish(int car) { return car == -1 ? -1 : finish[car]; }
int32_t Times_Split(int p, int k) { return p >= 0 && p < 4 && k >= 0 && k < 9 ? splits[p][k] : 0; }

/* 0x448de0: the clock 4 a step; the time shown off by -1 to 2 (rand() & 3, less one); at an hour the
   clock stops at 59:59.99 (unless 0x53d1d9) */
static void tick(void)
{
    if (!gm8(0x53d1d9) && 0x57e3b < clock_) {
        capped = true;
        clock_ = 360000;
        shown = 359999;
        return;
    }
    clock_ += 4;
    shown = clock_ + (crt_rand() % 4 - 1);   /* (C's remainder of a non-negative value, as the original's mask; msvcrt's rand 0x4c6676, the one sequence) */
    if (gm32(0x52f2b0) >> 0xb & 1) {
        /* the lap game's clock 0x53d1a0: its low half 4 a step, the high (the shown) with the jitter */
        uint16_t lo = (uint16_t)(gm16(0x53d1a0) + 4);
        gw16(0x53d1a0, lo), gw16(0x53d1a2, (uint16_t)(lo + (shown - clock_)));
    }
}

/* The arcade's records stay at the original's addresses (the results and the front end read them):
   per car 0x88 bytes at 0x53d968: +0 the time it last moved, +4 its times at the splits (selection
   bit 25), +0x28 the last lap's splits from its start, +0x4c the lap times by lap, +0x7c the lap's
   start, +0x80 the car that passed the split next (0xff for the last), +0x81 the place, +0x82 the
   place at the split, +0x83 passed at the split this step, +0x84, +0x85 the records' flags; per car
   12 laps of 5 split times at 0x53d1e8; per lap and split (9 a lap) the count through at 0x53ddb0
   and the cars in order at 0x53de1c (8 each); the standings at 0x53dda8; the finish order at
   0x53e17c, each car's at 0x53e184, their count 0x53e18c. */
#define AR(k, o) (0x53d968u + 0x88u * (uint32_t)(k) + (o))
#define LAPSP(k, lap, s) (0x53d1e8u + 4u * (uint32_t)(((lap) + (k) * 12) * 5 + (s)))
#define THROUGH(key) (0x53ddb0u + (uint32_t)(key))
#define ORDER_AT(key, i) (0x53de1cu + 8u * (uint32_t)(key) + (uint32_t)(i))
#define STANDING(i) (0x53dda8u + (uint32_t)(i))

int Times_Place(int car) { return (int8_t)gm8(AR(car, 0x81)); }    /* 0x4481c0 */
int Times_Standing(int i) { return (int8_t)gm8(STANDING(i)); }     /* 0x4481e0 */
static bool circuit(void) { return Gs_SelBit24() || Gs_SelBit25(); }
static int32_t cap(int32_t t) { return t > 359999 ? 359999 : t; }

/* 0x447f70: the clock and the finishes cleared; round a circuit the records: the places from the
   grid (the back car last; in a championship of more than two by its order 0x40cec0), the tables;
   0x53d1d9 (no clock cap) with selection bits 24 and 28 */
void Times_Reset(void)
{
    int n = Cars_Racing();
    clock_ = 0;
    if (circuit()) {
        gw8(0x53e18c, 0);
        for (uint32_t a = 0; a < 0x1b * 4; a++) gw8(THROUGH(a), 0);
        memset(splits, 0, sizeof splits);
        gw8(0x53e18d, 0), gw8(0x53e18e, 0), gw8(0x53e18f, 0);
        for (int i = 0; i < n; i++)
            for (int k = 0; k < 12 * 9; k++) gw8(ORDER_AT(k, i), 0xff);
        for (int i = 0; i < n; i++) {
            int place = n - i - 1;
            if (Gs_Mode() == 5 && n > 2) place = n - (int8_t)gm8(0x533620 + (uint32_t)i) - 1;
            gw8(AR(i, 0x81), (uint8_t)place);
            gw8(AR(i, 0x82), 0xff);
            gw32(AR(i, 0), 0);
            gw8(STANDING(place), (uint8_t)i);
            gw32(AR(i, 0x7c), 0);
            if (Gs_SelBit24())
                for (int k = 0; k < 0x3c; k++) gw32(0x53d1e8u + 0xf0u * (uint32_t)i + 4u * (uint32_t)k, 0);
            for (int k = 0; k < 9; k++) gw32(AR(i, 4 + 4 * k), 0);
            gw8(AR(i, 0x80), 0xff);
            gw8(AR(i, 0x83), 0);
        }
        if (gm32(0x52f2b0) >> 0xb & 1) gw32(0x53d1a4, 0), gw8(0x53d1a7, 0), gw8(0x53d1a6, 0);
    }
    for (int i = 0; i < n && i < CAR_MAX; i++) done[i] = 0;
    gw8(0x53d1d9, Gs_SelBit24() && Gs_SelBit28());
    capped = false;
}

/* 0x448cf0 */
static void finished(int k)
{
    done[k] = 1;
    finish[k] = shown;
    if (!circuit()) return;
    uint8_t f = gm8(0x53e18c);
    gw8(0x53e17c + f, (uint8_t)k);
    gw8(0x53e184 + (uint32_t)k, f);
    gw8(0x53e18c, (uint8_t)(f + 1));
}

/* 0x4487a0: at a split, a player's time (in a rally only that); round a circuit the car's time in
   the lap's table (bit 25: by split), a player's (or bit 27's two) from the lap's start, and its
   place through the split, the car before it told it was passed */
static void at_split(int k)
{
    int idx = Sectors_SplitIndex(k);
    if (!circuit()) {
        if (k < (int)Gs_SelBits14() && idx >= 0 && idx < 9) splits[k][idx] = shown;
        return;
    }
    int n = Cars_Racing(), lap = Sectors_LastLap(k), last = Sectors_Last(k);
    if (!Gs_SelBit24()) gw32(AR(k, 4 + 4 * idx), (uint32_t)shown);
    else gw32(LAPSP(k, lap, idx), (uint32_t)shown);
    if (k < (int)Gs_SelBits14() || Gs_SelBit27()) {
        gw8(AR(k, 0x84), 0);
        if (k < 4 && idx >= 0 && idx < 9) splits[k][idx] = Gs_SelBit24() ? shown - (int32_t)gm32(LAPSP(k, lap, 0)) : shown;
    }
    if (Gs_SelBit27()) lap = 0, last = idx;
    int key = last + lap * 9;
    int pos = (int8_t)gm8(THROUGH(key));
    gw8(THROUGH(key), (uint8_t)(pos + 1));
    gw8(AR(k, 0x82), (uint8_t)pos);
    gw8(ORDER_AT(key, pos), (uint8_t)k);
    if (pos > 0) {
        int b = (int8_t)gm8(ORDER_AT(key, pos - 1));
        gw8(AR(b, 0x80), (uint8_t)k);
        gw8(AR(b, 0x83), 1);
    }
    if (pos == n - 1) gw8(AR(k, 0x80), 0xff);
}

/* 0x448920: a lap done (the line crossed at split 0): its time (from the table, or with bit 28 or
   in mode 12 from +0x7c, capped), the lap's splits; the players' records (0x4cf530-0x4cfe80) and
   time trial's (0x40a3e0) pending; 0x52f2b0 bit 11's duels; the next lap's start */
static void lap_done(int k)
{
    if (!Sectors_LapFirst(k)) return;
    int lap = Sectors_LastLap(k);
    uint32_t lt = AR(k, 0x4c + 4 * lap);
    if (!Gs_SelBit27()) {
        gw32(LAPSP(k, lap, 0), (uint32_t)shown);
        if (!Gs_SelBit28() && Gs_Mode() != 12) {
            int32_t from = (int32_t)gm32(LAPSP(k, lap - 1, 0));
            gw32(lt, (uint32_t)((int32_t)gm32(LAPSP(k, lap, 0)) - from));
            for (int j = 0; j < 5; j++) gw32(AR(k, 0x28 + 4 * j), (uint32_t)((int32_t)gm32(LAPSP(k, lap - 1, j)) - from));
        } else {
            int32_t from = (int32_t)gm32(AR(k, 0x7c));
            gw32(lt, (uint32_t)cap(shown - from));
            gw32(AR(k, 0x28), 0);
            for (int j = 0; j < 4; j++) gw32(AR(k, 0x2c + 4 * j), (uint32_t)cap((int32_t)gm32(LAPSP(k, lap, 1 + j)) - from));
        }
        if (Gs_Mode() == 12) PENDING_NOTE(0x40a3e0, "the time trial's lap records");
    }
    if (k < (int)Gs_SelBits14() && !capped) PENDING_NOTE(0x4cf530, "the arcade's lap records");
    uint32_t w = gm32(0x52f2b0);
    if (w >> 0xb & 1) {
        if ((w & 7) == 2) {
            if (Times_Place(k) == 0) gw8(0x53d1a4 + (uint32_t)k, (uint8_t)(gm8(0x53d1a4 + (uint32_t)k) + 1));
            if ((int8_t)gm8(0x53d1a4 + (uint32_t)k) == (int)(w >> 7 & 0xf)) {
                gw8(0x53d1a7, 1);
                finished(k);
                finished(1 - k);
            }
        }
        if ((w & 7) == 1) {
            if (!gm8(0x53d1a6)) {
                if (Times_Place(k) == 0) gw8(0x53d1a6, 1), gw32(0x53d1a0, 0), gw8(0x53d1a8, (uint8_t)k);
            } else
                gw8(0x53d1a6, 0);
        }
    }
    gw32(AR(k, 0x7c), (uint32_t)shown);
}

/* 0x4486a0: 1 when car a is ahead of b (more points), -1 when behind or level and moved no later */
static int ahead(int a, int b)
{
    int sa = Sectors_Steps(a), sb = Sectors_Steps(b);
    if (sa == sb) return (int32_t)gm32(AR(b, 0)) <= (int32_t)gm32(AR(a, 0)) ? -1 : 0;
    return sa < sb ? -1 : sb < sa;
}

/* 0x448d50: one pass of swaps of neighbours in the standings */
static void standings_sort(int n)
{
    for (int i = 1; i < n; i++) {
        int a = (int8_t)gm8(STANDING(i)), b = (int8_t)gm8(STANDING(i - 1));
        if (ahead(a, b) != 1) continue;
        gw8(AR(b, 0x81), (uint8_t)(gm8(AR(b, 0x81)) + 1));
        gw8(STANDING(i), (uint8_t)b);
        gw8(STANDING(i - 1), (uint8_t)a);
        gw8(AR(a, 0x81), (uint8_t)(gm8(AR(a, 0x81)) - 1));
    }
}

/* 0x448bf0: 0x52f2b0's kind 1 duel: the other car more than its seconds behind the leader */
static void duel(int k)
{
    uint32_t w = gm32(0x52f2b0);
    if ((w & 7) == 1 && gm8(0x53d1a6) && k != (int8_t)gm8(0x53d1a8) && (int)(w >> 3 & 0xf) * 100 < (int)gm16(0x53d1a2)) {
        gw8(0x53d1a7, 1);
        finished((int8_t)gm8(0x53d1a8));
        finished(1 - (int8_t)gm8(0x53d1a8));
    }
}

/* 0x448120: the clock; round a circuit the passed flags cleared, the cars that moved timed, the
   standings sorted; per car not done (0x448730): the split, the lap and the finish */
void Times_Step(void)
{
    int n = Cars_Racing();
    tick();
    if (circuit()) {
        for (int i = 0; i < n; i++) gw8(AR(i, 0x83), 0);
        for (int i = 0; i < n; i++)
            if (!done[i] && Sectors_Moved(i)) gw32(AR(i, 0), (uint32_t)shown);   /* 0x448780 */
        standings_sort(n);
    }
    for (int k = 0; k < n && k < CAR_MAX; k++) {
        if (done[k]) continue;
        if (Sectors_Moved(k)) {
            if (Sectors_Split(k)) at_split(k);
            if (Sectors_Lapped(k)) lap_done(k);
            if (Race_Finished(k)) finished(k);
        }
        if (gm32(0x52f2b0) >> 0xb & 1) duel(k);
    }
}

void Hud_LoadSounds(void)
{
    /* the first three's ids kept: up 0x536ecc, down 0x537060, blip 0x536cb0 */
    static const uint32_t names[5] = {0x517d30, 0x517d24, 0x517d18, 0x517d0c, 0x517d00}, ids[3] = {0x536ecc, 0x537060, 0x536cb0};
    for (int i = 0; i < 5; i++) {
        if (i < 3) gw32(ids[i], (uint32_t)Snd_Count());
        Snd_LoadNamed(exe_str(names[i]), Bfl_CommonArc());
    }
}

/* 0x411ab0: at a split, "up" at 22,050 Hz when ahead of the reference, else "down" at 11,025, at
   half the speech volume; not with 0x52ea52 set, nor for a car whose type record (0x537f3c, 0x41f3d0)
   has +4 set (that table is not ported; a player's car is taken as clear) */
static void split_beep(bool ahead)
{
    if (Gs_Byte52ea52()) return;
    int32_t v = Codriver_Level() / 2;
    if (ahead) Voice_Play((int)gm32(0x536ecc), v, 0x5622, 0, false, false);
    else Voice_Play((int)gm32(0x537060), v, 0x2b11, 0, false, false);
}

/* 0x4118b0: the lap game's sounds (0x52f2b0 bit 11; called with the selection's bit 26). Kind 1 (a
   race of laps against the clock 0x53d1a0): on the lap's first step "cross" (0x536cb0 + 1) for the
   leader, else the split beep "up", and the seconds left (bits 3-6 less the clock's) kept at
   0x536fec; once a car leads (0x53d1a6) for the other cars a blip each second down, pitched up over
   the last six. Kind 2: on the lap's first step a new count (0x53d1a4) shows message 0x536c88 for
   125 steps with "cross". When the game is over (0x53d1a7), once (0x537050), "timeout" (+2) in kind
   1, else "cross". */
static void lap_sounds(int p)
{
    uint32_t w = gm32(0x52f2b0), k = w & 7;
    int32_t lvl = Codriver_Level();   /* 0x418570 */
    int blip = (int)gm32(0x536cb0);
    if (gm8(0x53d1a7)) {
        if (!gm32(0x537050)) {
            gw32(0x537050, 1);
            Voice_Play(k == 1 ? blip + 2 : blip + 1, lvl, 0xac44, 0, false, false);
        }
        return;
    }
    if (k == 1) {
        if (Sectors_LapFirst(p)) {
            if (Times_Place(p) == 0) Voice_Play(blip + 1, lvl, 0xac44, 0, false, false);
            else split_beep(true);   /* 0x411ab0 */
            gw32(0x536fec, (uint32_t)((int)(w >> 3 & 0xf) - (int16_t)gm16(0x53d1a2) / 100));   /* 0x4082d0, 0x448c70 */
        }
        if (gm8(0x53d1a6) && (int8_t)gm8(0x53d1a8) != p) {   /* 0x448c90, 0x448c80 */
            int left = (int)(w >> 3 & 0xf) - (int16_t)gm16(0x53d1a2) / 100;
            if (left < gms32(0x536fec)) {
                uint32_t f = left < 6 ? (uint32_t)((6 - left) * 0xac44 / 0x30 + 0xac44) : 0xac44;
                gw32(0x536fec, (uint32_t)left);
                if (left != 0) Voice_SetFrequency(Voice_Play(blip, lvl, f, 0, false, false), f);
            }
        }
    } else if (k == 2 && Sectors_LapFirst(p)) {
        int8_t c = (int8_t)gm8(0x53d1a4 + (uint32_t)p);   /* 0x448c60 */
        if ((int8_t)gm8(0x536cac + (uint32_t)p) != c) {
            gw8(0x536cac + (uint32_t)p, (uint8_t)c);
            gw32(MSG_88 + 4u * (uint32_t)p, 1);
            gw8(MSG_COUNT + (uint32_t)p, 0x7d);
            Voice_Play(blip + 1, lvl, 0xac44, 0, false, false);
        }
    }
}

/* 0x413330's rally part: at a split its index, the time there, the place at it */
static void rally_split(int p)
{
    if (!Sectors_Split(p)) return;
    int idx = Sectors_Last(p);
    if (!Sectors_LapFirst(p) && idx != 0 && idx < 12) {
        my_split[p][idx] = hud_time[p];
        ref_time[p] = ref_split[idx];
        last_time[p] = hud_time[p];
    }
    /* the box just finished green or red (unless 0x536c40 is the cap) and the faster or slower beep */
    if (gm32(0x536c40) != gm32(0x537068) && p < 2) {
        int32_t mine = idx < 12 ? my_split[p][idx] : 0, prev = idx == 0 ? hud_time[p] : my_split[p][idx - 1 < 12 ? idx - 1 : 11];
        int32_t rp = idx == 0 ? gms32(0x536e90) : ref_split[idx - 1];
        gw32(BAR_COL(p, idx) - 4, gm32(mine - prev < ref_split[idx < 12 ? idx : 11] - rp ? 0x51709c : 0x5170a0));
        if (idx != 0) split_beep(mine < ref_time[p]);   /* 0x411ab0 */
    }
    split_idx[p] = idx;
    uint32_t m = Gs_Mode();
    if (m != 5 && m != 6 && idx != 0) show_for[p] = 0x4b, gw8(0x5170e3 + 4u * (uint32_t)p, 0xff);
    if (m != 5 && m != 6 && m != 7 && m != 4) hud_place[p] = Opp_Loaded() ? Opp_PlaceAt(Race_Base() + p, idx) : 0xf;
}

/* 0x414720: the three places around the player's at its last split */
static void standings_rows(int p)
{
    int s = split_idx[p];
    if (s == 0) return;
    int pl = 0xf;
    if (Opp_Loaded()) {
        pl = Opp_PlaceAt(Race_Base() + p, s);
        if (pl < 1) {
            rows[p][0] = pl, rows[p][1] = pl + 1, rows[p][2] = pl + 2;
            return;
        }
    }
    if (Opp_Entries(s) - 1 <= pl) rows[p][0] = pl - 2, rows[p][1] = pl - 1, rows[p][2] = pl;
    else rows[p][0] = pl - 1, rows[p][1] = pl, rows[p][2] = pl + 1;
}

/* 0x408400: a player's profile name ("" without one) */
static const char *driver_name(int n)
{
    uint32_t k = gm32(0x531350 + 0x30u * (uint32_t)n) >> 0x12 & 0xf;
    return k == 0xf ? "" : (const char *)gm(0x52fa18 + k * 0x650);
}

/* the name into 0x536ed0 (the original passes it to sprintf as the format) */
static void set_name(const char *n)
{
    char *d = (char *)gm(MSG_NAME);
    size_t i = 0;
    for (; n[i] && i < 0x1f; i++) d[i] = n[i];
    d[i] = 0;
}

/* 0x415a60: a lap just finished (0x458290): its time (0x4481f0, the lap of the last split); the flags
   cleared, then (not networked, and unless 0x52afbc bit 11 with selection bit 24) the lap record
   flag (0x448cd0) and, faster than the record to beat (0x536c3c) without a profile, the no-profile
   flag; a best of the race (or in the time trial, mode 12 with 0x531ce4, its record 0x531c98) gets
   the driver's name; the lap flag; each shown for 125 steps */
static void lap_message(int p)
{
    if (!Sectors_LapFirst(p)) return;
    int32_t t = gms32(0x53d9b4 + 4u * (uint32_t)(Sectors_LastLap(p) + p * 0x22));
    msg_clear(p);
    const uint32_t o = 4 * (uint32_t)p;
    if ((!(gm32(0x52afbc) >> 0xb & 1) || !Gs_SelBit24()) && !Gs_ModeBit19()) {
        if (gm8(0x53d9ec + 0x88u * (uint32_t)p)) gw32(MSG_REC + o, 1), gw8(MSG_COUNT + (uint32_t)p, 0x7d);
        if ((gms32(0x536c3c) == 0 || t < gms32(0x536c3c)) && !Player_HasProfile(Gs_Byte537ef6() + p))
            gw32(MSG_NOPROF + o, 1), gw8(MSG_COUNT + (uint32_t)p, 0x7d);
    }
    bool named = false;
    if (!Gs_ModeBit19()) {
        if (gms32(MSG_TIME) == 0 || t < gms32(MSG_TIME)) {
            gw32(MSG_TIME, (uint32_t)t);
            set_name(driver_name(Gs_Byte537ef6() + p));
            named = true;
        }
    } else if (Gs_Mode() == 12 && gm32(0x531ce4)) {
        gw32(MSG_TIME, gm32(Gs_Mode() == 12 ? 0x531c98 : 0x531c94));   /* 0x40a420(0) */
        set_name(driver_name(0));
        named = true;
    }
    if (named) gw32(MSG_BEST + o, 1), gw8(MSG_COUNT + (uint32_t)p, 0x7d);
    gw32(MSG_LAP + o, 1);
    gw8(MSG_COUNT + (uint32_t)p, 0x7d);
}

/* 0x415990: the messages' countdown (the flags cleared when it runs out); off the circuits (selection
   bit 24 clear, and bit 25 only out of mode 3 or networked) the best finish among the players gets
   its driver's name; on them the laps (0x415a60) */
static void messages(int p)
{
    uint32_t c = MSG_COUNT + (uint32_t)p;
    if (gm8(c)) gw8(c, (uint8_t)(gm8(c) - 1));
    if (!gm8(c)) msg_clear(p);
    if (!Gs_SelBit24() && (!Gs_SelBit25() || Gs_Mode() != 3 || Gs_ModeBit19())) {
        if (Race_Finished(p) && (gms32(MSG_TIME) == 0 || hud_time[p] < gms32(MSG_TIME))) {
            gw32(MSG_TIME, (uint32_t)hud_time[p]);
            set_name(driver_name(Gs_Byte537ef6() + p));
        }
    } else
        lap_message(p);
}

void Hud_Update(int p, bool on_line)
{
    static int lw, lh;
    static uint8_t lb;   /* DAT_00536edc, DAT_0051711c, DAT_00537080 */
    int w, h;
    D3d_ScreenSize(&w, &h);
    uint8_t b = (uint8_t)(gm32(0x52afb4) >> 0x12 & 1);
    if (w != lw || h != lh || b != lb) {
        bar_layout();   /* 0x411b20 */
        lw = w, lh = h, lb = b;
    }
    /* 0x411f70's first frame: the reference splits, the leader's (0x455d40(0, i)) */
    if (!hud_ready && Opp_Loaded() && Gs_Mode() != 3 && Gs_Mode() != 7) {
        hud_ready = true;
        for (int i = 0; i < 12; i++) ref_split[i] = Opp_TimeAt(0, i);
    }
    if (on_line) hud_time[p] = 0;
    else hud_time[p] = Race_Finished(p) ? Times_Finish(p) : Times_Race();
    messages(p);   /* 0x415990 */
    /* the passed part of the current split's box, by the furthest point reached */
    if (p < 2) {
        int idx = split_idx[p];
        int32_t start = gms32(SPLIT_PT(idx)), len = gms32(SPLIT_PT(idx + 1)) - start;
        int16_t r[4];
        box_at(BAR_BOX(p, idx), r);
        int16_t done_w = len ? (int16_t)(((Sectors_Most(p) & 0xffff) - start) * r[2] / len) : 0;
        int16_t a[4] = {r[0], r[1], done_w, r[3]}, z[4] = {(int16_t)(r[0] + done_w), r[1], (int16_t)(r[2] - done_w), r[3]};
        for (int i = 0; i < 4; i++) gw16(BAR_DONE(p) + 2u * (uint32_t)i, (uint16_t)a[i]), gw16(BAR_REST(p) + 2u * (uint32_t)i, (uint16_t)z[i]);
    }
    rally_split(p);   /* 0x413160 */
    if (Gs_SelBit26() && (gm32(0x52f2b0) >> 0xb & 1)) lap_sounds(p);   /* 0x4118b0 */
    uint32_t m = Gs_Mode();
    if (m == 5 || m == 6 || m == 7 || Gs_SelBit25()) arcade_rows(p);   /* 0x415870 */
    else standings_rows(p);   /* 0x414720 */
    if (show_for[p]) show_for[p]--;
}

static const uint8_t *colour(uint32_t a)
{
    static uint8_t c[8][4];
    static int n;
    uint8_t *r = c[n++ & 7];
    for (int i = 0; i < 4; i++) r[i] = gm8(a + (uint32_t)i);
    return r;
}

/* 0x415750: the name at place e of split s (a player's if one is there, else the driver's, short
   when asked), in the font's case */
static void draw_time(int32_t t, int font, int32_t fx, int32_t fy, const uint8_t *box_rgba, uint32_t flags, const char *prefix, bool box);

static void place_name(char *b, size_t n, int e, int s, bool shrt)
{
    b[0] = 0;
    int drv = Opp_DriverAt(e, s);
    if (drv == -1) return;
    const char *name = NULL;
    for (int i = 0; i < (int)(gm32(0x52afb4) >> 13 & 0xf); i++)
        if (Opp_PlaceAt(i, s) == e) {
            uint32_t pr = gm32(0x531350 + 0x30u * (uint32_t)i) >> 0x12 & 0xf;
            name = pr == 0xf ? "" : (const char *)gm(0x52fa18 + pr * 0x650);
        }
    if (!name) name = shrt ? exe_str(0x5167e0 + 4u * (uint32_t)(drv < 0 || drv > 15 ? 0 : drv)) : exe_str(0x5166b4 + 0x14u * (uint32_t)(drv < 0 || drv > 15 ? 0 : drv));
    snprintf(b, n, "%s", name);
    Text_ToFontCase(b);
}

/* 0x414ed0 for one view: three boxes (the player's lit), each with the place, the name and the time
   at the last split */
static void standings(int p)
{
    if (rows[p][0] == -1 && !Pause_Active()) return;
    int w, h;
    D3d_ScreenSize(&w, &h);
    int ox = 0, oy = 0;   /* 0x464b10: the view's corner */
    int step = ((h * 0xccc) >> 16) - 2, ty = ((h * 0x1b32) >> 16) - 2 + oy;
    int16_t r[4] = {(int16_t)(((w * 0xc00) >> 16) + ox), (int16_t)(((h << 0xc) >> 16) + oy), (int16_t)((w * 0x47ae) >> 16), (int16_t)step};
    for (int k = 0; k < 3; k++, r[1] = (int16_t)(r[1] + step), ty += step) {
        int e = rows[p][k];
        Rect_Fill(r, colour(e == hud_place[p] ? 0x5170d8 : 0x5170d4), 2);
        if (e == -1) continue;
        char b[64];
        snprintf(b, sizeof b, exe_str(0x517df0), e + 1);
        Font_Print(0, b, ((w * 0xf95) >> 16) + ox, ty, colour(0x5170d0), 0x22);
        place_name(b, sizeof b, e, split_idx[p], false);
        if (Font_TextWidth(0, b) > w * 0x59 / 0x280) place_name(b, sizeof b, e, split_idx[p], true);
        int32_t t = 3000;
        if (!Opp_Loaded()) snprintf(b, sizeof b, "%s", exe_str(0x517de0));
        else t = Opp_TimeAt(e, split_idx[p]);
        Font_Print(0, b, ((w * 0x132a) >> 16) + ox, ty, colour(0x5170d0), 0x21);
        draw_time(t, 5, (int32_t)(((int64_t)ox << 16) / w) + 0x51e4, (int32_t)(((int64_t)(ty - 1) << 16) / h), colour(0x5170d0), 0x24, NULL, false);
    }
}

/* 0x408cb0: a player's profile record block (NULL without a profile; the original then reads at a
   small address, the port reads zero) */
static uint32_t profile_of(int n)
{
    uint32_t w = gm32(0x531350 + 0x30u * (uint32_t)n);
    return (w & 0x3c0000) == 0x3c0000 ? 0 : 0x52fa8c + (w >> 0x12 & 0xf) * 0x650;
}

/* 0x4147f0 for one view: the records panel of the other modes. The record to beat and its holder
   (0x52b044's table: mode 3 by rally and stage, the others by the circuit 0x52f2ac bits 10-11 and
   12-13), the player's own best (its profile), the race's best (0x53706c, 0x536ed0) and the last lap:
   four rows in modes 5-7 (two with 0x52afbc bit 11), three in the others, two in modes 10 and 12
   (whose rows are the time trial's: its driver and time 0x5320a8, the ghost 0x531cf0 and 0x531f80);
   with a profile the own best is left out and the rows below move up. A row whose message is on
   (0x536c00, 0x536e88, 0x536c28, 0x536ec4) flashes white in the message's first six steps. */
static void records(int p)
{
    int w, h;
    D3d_ScreenSize(&w, &h);
    int ox = 0, oy = 0;   /* 0x464b10: the view's corner */
    uint32_t m = Gs_Mode(), mine = 0, rec = 0;
    char holder[32] = "";
    uint32_t pr = profile_of(Gs_Byte537ef6() + p);
    if (m == 3) {
        uint32_t k = Tod_Rally() * 0xb + Tod_Stage();
        if (pr) mine = gm32(pr + 0x154 + (Tod_Stage() + Tod_Rally() * 0xc) * 8);
        rec = gm32(0x52b044 + 0x658 + k * 8) >> 7 & 0xffff;
        snprintf(holder, sizeof holder, "%s", (const char *)gm(0x52b044 + 0x654 + k * 8));
    } else {
        uint32_t k = (gm32(0x52f2ac) >> 10 & 3) * 3 + (gm32(0x52f2ac) >> 0xc & 3);
        if (pr) mine = gm32(pr + 0x4c0 + k * 8);
        rec = gm32(0x52b044 + 0x1214 + k * 8) >> 7 & 0xffff;
        snprintf(holder, sizeof holder, "%s", (const char *)gm(0x52b044 + 0x1210 + k * 8));
    }
    int step = (h * 0xccc) >> 16, n = 3;
    if (m == 5 || m == 6 || m == 7) n = gm32(0x52afbc) >> 0xb & 1 ? 2 : 4;
    if (m == 10 || m == 12) n = 2;
    bool trial = m == 10 || m == 12, lapsonly = gm32(0x52afbc) >> 0xb & 1;
    int16_t by = (int16_t)(((h << 0xc) >> 16) + oy);
    int flash = (int8_t)gm8(0x536c08 + (uint32_t)p) - 0x77;
    if (flash < 0) flash = 0;
    for (int k = 0, row_y = 0; k < n; k++, row_y += step) {
        int kind = k, ty = row_y;
        if (!trial) {
            if (lapsonly) kind = k + 2;
            else if (Player_HasProfile(Gs_Byte537ef6() + p)) {
                if (k == 1) continue;
                if (k > 1) ty = row_y - step;
            }
        }
        char b[96];
        int32_t t = 0;
        uint32_t flag = 0;
        bool on;
        switch (kind) {
        case 0:
            if (trial) snprintf(b, sizeof b, "%s", driver_name(0)), t = gms32(0x5320a8);
            else snprintf(b, sizeof b, exe_str(0x517dd8), Text_Get(0x69), holder), t = (int32_t)rec;
            flag = 0x536c00;
            break;
        case 1:
            if (trial) {
                snprintf(b, sizeof b, exe_str(0x517dd8), Text_Get(0x84), (const char *)gm(0x531cf0));
                if (m == 10) t = gms32(0x531f80 + 4u * (Gs_SelBit25() ? 2 : 8));
                else t = gms32(0x531c98);   /* 0x40a420(0) in mode 12 */
                flag = 0x536c28;
            } else snprintf(b, sizeof b, "%s", driver_name(Gs_Byte537ef6() + p)), t = (int32_t)mine, flag = 0x536e88;
            break;
        case 2:
            snprintf(b, sizeof b, exe_str(0x517dd8), Text_Get(0x84), (const char *)gm(0x536ed0));
            t = gms32(0x53706c), flag = 0x536c28;
            break;
        default:
            snprintf(b, sizeof b, "%s", Text_Get(0xa5));
            t = gms32(0x53d9b4 + 4u * (uint32_t)(Sectors_LastLap(p) + p * 0x22));
            flag = 0x536ec4;
            break;
        }
        on = gm32(flag + 4u * (uint32_t)p) && flash;
        uint8_t c[4];
        memcpy(c, colour(0x5170d4), 4);
        if (on && flash < 4) {
            for (int i = 0; i < 3; i++) c[i] = (uint8_t)(c[i] + (0xff - c[i]) * flash / 3);
            c[3] = (uint8_t)(c[3] + ((int)gm8(0x5170f7) - c[3]) * flash / 3);
        } else if (on)
            c[0] = c[1] = c[2] = 0xff, c[3] = gm8(0x5170f7);
        int16_t r[4] = {(int16_t)(((w * 0xc00) >> 16) + ox), by, (int16_t)((w * 0x47ae) >> 16), (int16_t)step};
        Rect_Fill(r, c, 2);
        int y = oy + 1 + ty + ((h * 0x1b32) >> 16);
        Font_Print(0, b, ((w * 0xf95) >> 16) + ox, y, colour(0x5170d0), 0x21);
        draw_time(t ? t : -1, 5, (int32_t)(((int64_t)ox << 16) / w) + 0x51e4, (int32_t)(((int64_t)(y - 1) << 16) / h), colour(0x5170d0), 0x24, NULL, false);
        by = (int16_t)(by + step);
    }
}

/* 0x411280's textures: the network's blobs (0x537070, 0x537074), the map's dot (0x537078) and the
   map (0x53707c) */
static Tex *t_net, *t_netr, *t_blob, *t_map;

static Tex *common_tex(uint32_t name)
{
    char b[260];
    snprintf(b, sizeof b, "%s%s", exe_str(0x516a50), exe_str(name));   /* "%s%s" 0x517d98 of 0x40ed50 */
    uint8_t found;
    return Bfl_FindTexture(Bfl_Com()   /* 0x4552a0 */, b, &found, false, 0);
}

/* 0x411280 (Stage_Load, after the archives): the blobs in modes 8 and 9; in modes 4-7, 11, 12 or a
   circuit (selection bit 25) the map's dot and the map from the track's textures: in mode 4 or on a
   circuit the stage's name (0x538340, 0x41f560) with its last two letters (the detail, hi or lo)
   turned into .DDS, else Tracks\chall\cha<nn>.tga by the location and its stage */
void Hud_LoadMap(void)
{
    uint32_t m = Gs_Mode();
    t_net = t_netr = t_blob = t_map = NULL;
    if (m == 8 || m == 9) t_net = common_tex(0x517da0), t_netr = common_tex(0x517d74);
    if (m != 5 && m != 6 && m != 7 && m != 4 && m != 11 && m != 12 && !Gs_SelBit25()) return;
    t_blob = common_tex(0x517d50);
    char b[260];
    const char *detail = exe_str((gm32(0x52afd0) >> 0x1b & 3) == 0 ? 0x519468 : 0x519464);
    if (m == 4 || Gs_SelBit25()) {
        if (m == 5 || m == 6 || m == 7 || m == 11 || m == 12) {
            uint32_t sel = gm32(0x52f2ac);
            int idx = (int)(sel >> 10 & 3) * 3 + (int)(sel >> 12 & 3);
            if (idx > 7) idx = 7;
            snprintf(b, sizeof b, "%s\\chall\\cha%02d%s", Path_Tracks(), idx + 1, detail);
        } else {
            uint32_t r = Tod_Rally();
            snprintf(b, sizeof b, "%s\\%s\\%s%02u%s", Path_Tracks(), exe_str(exe_u32(0x5192f8 + 4 * r)),
                     exe_str(exe_u32(0x51931c + 4 * r)), Tod_Stage() + 1, detail);
        }
        size_t n = strlen(b);
        snprintf(b + (n >= 2 ? n - 2 : 0), 8, ".DDS");
    } else
        snprintf(b, sizeof b, exe_str(0x517d38), Path_Tracks(),
                 (int)(gm32(0x52f2ac) >> 10 & 3) * 3 + (int)(gm32(0x52f2ac) >> 12 & 3) + 1);
    uint8_t found;
    t_map = Bfl_FindTexture(g_track_tex.loaded ? &g_track_tex : NULL, b, &found, false, 0);   /* 0x41f500 */
}

/* 0x40b0a0: a ghost's colour by its car (0x531cc0's eight, the colours 0x5168b0), else three of rand
   into 0x5168d0 (its alpha 0xff) */
static const uint8_t *ghost_colour(uint32_t car)
{
    static uint8_t c[4];
    for (uint32_t i = 0; i < 8; i++)
        if (gm32(0x531cc0 + 4 * i) == car) return colour(0x5168b0 + 4 * i);
    memcpy(c, colour(0x5168d0), 4);
    c[0] = (uint8_t)crt_rand(), c[1] = (uint8_t)crt_rand(), c[2] = (uint8_t)crt_rand();
    return c;
}

/* the map's 16.16 screen position of point i: its offset from the middle over the scale (0x53705c),
   a fraction about one half of the map's box (x, y, w, h) */
static void map_at(int i, const int16_t box[4], int32_t *x, int32_t *y)
{
    int32_t q[3];
    Cat_Point(i, q);
    int32_t s = gms32(0x53705c);
    int32_t fx = (int32_t)(((int64_t)(q[0] - gms32(0x536bf0)) * s) >> 16) + 0x8000;
    int32_t fy = (int32_t)(((int64_t)-(q[2] - gms32(0x536bf8)) * s) >> 16) + 0x8000;
    *x = (int32_t)(((int64_t)fx * ((int32_t)box[2] << 16)) >> 16) + ((int32_t)box[0] << 16);
    *y = (int32_t)(((int64_t)fy * ((int32_t)box[3] << 16)) >> 16) + ((int32_t)box[1] << 16);
}

/* 0x415f50 for one view: the map at the bottom left (its texture's size at 640 by 480, not drawn on a
   split screen with 0x52afb4 bit 18), a dot on it for each ghost (modes 11, 12, or a circuit in
   0x52afb4 bit 19 outside mode 10: 0x531bb8's eight, -2 the own run, each at its car's point 0x531bbc,
   10/640 wide) or else for each car (0x5170fc's colours, 12/640, the last car first), and on a split
   screen each player's number by its car */
static void map(int p)
{
    (void)p;
    int w, h;
    D3d_ScreenSize(&w, &h);
    int ox = 0;   /* 0x464b10: the view's corner */
    static const uint8_t white[4] = {0xff, 0xff, 0xff, 0xff};
    Rect16 src = {0, 0, 0, 0}, dot = {0, 0, 0, 0}, dst = {0, 0, 0, 0};
    int16_t box[4] = {0, 0, 0, 0};   /* (the original leaves the box unset without a map) */
    if (t_map) {
        Rect16 ms = {0, 0, (int16_t)(t_map->width - 1), (int16_t)(t_map->height - 1)};
        src = ms;
        if (t_blob) dot.w = (int16_t)(t_blob->width - 1), dot.h = (int16_t)(t_blob->height - 1);
        if (split_screen() && Gs_ModeBit18()) return;
        box[0] = (int16_t)(((w * 0x731) >> 16) + ox);
        box[2] = (int16_t)(src.w * w / 640);
        box[3] = (int16_t)(src.h * h / 480);
        box[1] = (int16_t)(((h * 0xea3d) >> 16) - box[3]);
        Rect16 d = {box[0], box[1], box[2], box[3]};
        Sprite_Add(&src, &d, t_map, 2, 0, NULL, NULL, white, 8);
    }
    uint32_t m = Gs_Mode();
    if (m == 11 || m == 12 || (Gs_ModeBit19() && Gs_SelBit25() && m != 10)) {
        for (uint32_t i = 0; i < 8; i++) {
            int32_t k = gms32(0x531bb8 + 0x1c * i);   /* 0x40a700 */
            if (k == -1) continue;
            int pt;
            const uint8_t *c;
            if (k == -2) pt = Sectors_CarPoint(0), c = ghost_colour(gm32(0x5a1ea0));   /* 0x4a1a00 */
            else {
                uint32_t car = gm32(0x531778 + 0x80u * (uint32_t)k);   /* 0x409d20 */
                pt = 0;   /* 0x40a720: the record of that car */
                for (uint32_t j = 0; j < 8; j++)
                    if (gm32(0x531bc8 + 0x1c * j) == car) { pt = gms32(0x531bbc + 0x1c * j); break; }
                c = ghost_colour(car);
            }
            int32_t x, y;
            map_at(Sectors_After(pt), box, &x, &y);
            dst.x = (int16_t)((x >> 16) - w * 5 / 640), dst.y = (int16_t)((y >> 16) - h * 5 / 480);
            dst.w = (int16_t)(w * 10 / 640), dst.h = (int16_t)(h * 10 / 480);
            Sprite_Add(&dot, &dst, t_blob, 2, 0, NULL, NULL, c, 8);
        }
    } else {
        for (int i = Race_Cars() - 1; i >= 0; i--) {
            int32_t x, y;
            map_at(Car_CatIndex(Car_Get(i)), box, &x, &y);
            dst.x = (int16_t)((x >> 16) - w * 6 / 640), dst.y = (int16_t)((y >> 16) - h * 6 / 480);
            dst.w = (int16_t)(w * 12 / 640), dst.h = (int16_t)(h * 12 / 480);
            Sprite_Add(&dot, &dst, t_blob, 2, 0, NULL, NULL, colour(0x5170fc + 4u * (uint32_t)i), 8);
        }
    }
    for (int i = 0; i < (int)Gs_SelBits14(); i++) {
        char b[8];
        snprintf(b, sizeof b, exe_str(0x517e1c), i + 1);
        int32_t x, y;
        map_at(Sectors_After(Sectors_CarPoint(i)), box, &x, &y);
        Font_Print(0, b, x >> 16, (y >> 16) + 7, colour(0x5170f4), 0x12);
    }
}

/* 0x415bd0: a time as <prefix>mm:ss.cc (or --:--.--) in a font at a fraction of the screen, in white,
   on a box of its own colour when asked */
static void draw_time(int32_t t, int font, int32_t fx, int32_t fy, const uint8_t *box_rgba, uint32_t flags, const char *prefix, bool box)
{
    char s[48];
    if (!prefix) prefix = (const char *)gm(0x537084);
    if (t == -1) snprintf(s, sizeof s, exe_str(0x517df4), prefix);
    else snprintf(s, sizeof s, exe_str(0x517e00), prefix, (t / 6000) % 100, (t % 6000) / 100, (t % 6000) % 100);
    int w, h;
    D3d_ScreenSize(&w, &h);
    if (box) {
        int16_t r[4] = {(int16_t)(((w * fx) >> 16) - w / 160 - 2), (int16_t)(((h * fy) >> 16) - h / 240 - 2),
                        (int16_t)(4 + w / 80 + Font_TextWidth(font, s)), (int16_t)(4 + h / 320 + Font_TextHeight(font, s))};
        Rect_Fill(r, box_rgba, 2);
    }
    Font_Print(font, s, (int)(((int64_t)w * fx) >> 16), (int)(((int64_t)h * fy) >> 16), colour(0x5170f4), flags);
}

/* 0x4137e0's rally part: for 75 frames after a split, the difference to the reference time there
   (the leader's), "-" and green when faster, "+" and red when slower, on its box under the
   standings */
static void split_delta(int p)
{
    int32_t last = last_time[p], ref = ref_time[p];
    if (last == -1 || !show_for[p] || (split_idx[p] == 0 && Gs_Mode() != 12)) return;
    int w, h;
    D3d_ScreenSize(&w, &h);
    int x = (w * 0xc00) >> 16, y = ((h * 0xd668) >> 16) + ((h * 0xccc) >> 16) - 1;
    int32_t fx = (int32_t)(((int64_t)(x + 4) << 16) / w), fy = (int32_t)(((int64_t)(y + 1) << 16) / h);
    if (ref < last) draw_time(last - ref, 4, fx, fy, colour(0x5170a0), 9, exe_str(0x517e14), true);
    else draw_time(ref - last, 4, fx, fy, colour(0x51709c), 9, exe_str(0x517e18), true);
}

/* 0x413e00: a network player's blob on the bar (k 0 the own one, the colour of the player's car
   0x5a1ea0; else player k's, 0x40b020's slot of id k in 0x531780): its centre at the fraction
   0x536c4c of split 0x536c48's box, the top blob at the box's top, the other (0x537074) at its bottom,
   cut to the bar's first and last boxes. The source's corner is the texture's +0x11c (the port's
   zero). */
static void net_blob(int p, uint32_t k)
{
    int32_t s = gms32(0x536c48 + 8 * k);
    int16_t b[4], first[4], last[4];
    box_at(BAR_BOX(p, s), b), box_at(BAR_BOX(p, 0), first), box_at(BAR_BOX(p, gms32(NSPLITS) - 1), last);
    Rect16 src = {0, 0, t_net->width, t_net->height};
    Rect16 dst = {(int16_t)((int16_t)(((int64_t)((int32_t)b[2] << 16) * gms32(0x536c4c + 8 * k)) >> 32) - t_net->width / 2 + b[0]),
                  b[1], t_net->width, t_net->height};
    if (first[0] - dst.x > 0) {
        int16_t d = (int16_t)(first[0] - dst.x);
        src.x = (int16_t)(src.x + d), dst.x = (int16_t)(dst.x + d), dst.w = (int16_t)(dst.w - d), src.w = (int16_t)(src.w - d);
    }
    int over = dst.w - last[0] - last[2] + dst.x;
    if (over > 0) dst.w = (int16_t)(dst.w - over), src.w = (int16_t)(src.w - over);
    uint32_t car = gm32(0x5a1ea0);   /* 0x4a1a00 */
    if (k) {
        uint32_t slot = 0;   /* 0x40b020 */
        for (uint32_t i = 0; i < 7; i++)
            if (gm8(0x53177c + 0x80 * i) & 0x80 && gm32(0x531780 + 0x80 * i) == k) { slot = i; break; }
        car = gm32(0x531778 + 0x80 * slot);   /* 0x409d20 */
    }
    Sprite_Add(&src, &dst, t_net, 2, 0, NULL, NULL, ghost_colour(car), 8);
    dst.y = (int16_t)(b[3] - t_netr->height + b[1]);
    Sprite_Add(&src, &dst, t_netr, 2, 0, NULL, NULL, ghost_colour(car), 8);
}

/* 0x4143f0: the split bar, each split's box in its colour, the current one's passed part opaque,
   1-pixel marks at the inner splits (not when the selection's bit 28 is set, unless 0x536c40 is
   neither 0 nor the cap). 0x414550, the arcade's (modes 8, 9, 11), is the same bar for any player
   (it reads 0x405dc0 on a split screen and drops it) with, when the network's blobs are loaded, a
   blob for each of the slots 6 to 0 in the game (0x531778 + 0x80 n, bit 7 of +4) by its id (+8),
   then the own one. */
static void split_bar(int p, bool arcade)
{
    uint32_t c40 = gm32(0x536c40);
    if (Gs_SelBit28() && (c40 == gm32(0x537068) || c40 == 0)) return;
    int32_t n = gms32(NSPLITS);
    int16_t r[4];
    for (int i = 0; i <= n; i++) {
        if (i == split_idx[p]) {
            gw32(0x5170cc, 0xff000000u | (gm32(BAR_COL(p, i)) & 0xffffff));
            box_at(BAR_DONE(p), r);
            Rect_Fill(r, colour(0x5170cc), 2);
            box_at(BAR_REST(p), r);
        } else box_at(BAR_BOX(p, i), r);
        Rect_Fill(r, colour(BAR_COL(p, i)), 2);
        if (i > 0 && i < n - 1) {
            box_at(BAR_BOX(p, i), r);
            r[2] = 1;
            Rect_Fill(r, colour(0x5170ac), 2);
        }
    }
    if (arcade && t_net && t_netr) {
        for (int i = 6; i >= 0; i--)
            if (gm32(0x53177c + 0x80u * (uint32_t)i) >> 7 & 1) net_blob(p, gm32(0x531780 + 0x80u * (uint32_t)i));   /* 0x409cb0, 0x40b010 */
        net_blob(p, 0);
    }
}

/* 0x413fe0 for one player: a box (0x5170bc) and the time in lcd_640 (slot 4) at its top right */
static void time_panel(int p)
{
    int w, h;
    D3d_ScreenSize(&w, &h);
    uint32_t m = Gs_Mode();
    if (m == 8 || m == 9 || m == 11) split_bar(p, true);   /* 0x414550 */
    else if (p < 2) split_bar(p, false);   /* 0x4143f0 */
    int16_t r[4] = {(int16_t)((w * 0xab9b) >> 16), (int16_t)((h * 0x1aac) >> 16), (int16_t)(((w * 0x4865) >> 16) + 1),
                    (int16_t)((h * 0x1554) >> 16)};
    if (gm32(0x543098)) PENDING_NOTE(0x459390, "Hud_TimePanel (laps)");
    Rect_Fill(r, colour(0x5170bc), 2);
    /* the viewport's corner (0, 0 for one player) over the screen, plus the offsets */
    draw_time(hud_time[p], 4, 0xaf30, 0x2e66, colour(0x5170f4), 0x21, NULL, false);
}

/* 0x409cd0: the name of game slot k's player, from the network's players (0x5a1884, 0xd0 apart, the
   one whose id +0x64 matches the slot's 0x531778; NULL unless its +0x68 is set) */
static const char *net_name(int k)
{
    uint32_t id = gm32(0x531778 + 0x80u * (uint32_t)k);
    for (uint32_t i = 0; i < 7; i++)   /* 0x4a19c0: to 0x5a1e98 */
        if (gm32(0x5a18e8 + 0xd0 * i) == id) return gm32(0x5a18ec + 0xd0 * i) ? (const char *)gm(0x5a1884 + 0xd0 * i) : NULL;   /* 0x4a1b60 */
    return NULL;
}

/* 0x415480 for one view: the standings of modes 8, 9 and 11, a box for each of 0x531bb8's eight
   entries in use (-2 the own one, lit, with the player's name), each with its place, the name and a
   square at the right in the car's colour (0x40b0a0) */
static void ghost_standings(int p)
{
    (void)p;
    int w, h;
    D3d_ScreenSize(&w, &h);
    int ox = 0, oy = 0;   /* 0x464b10: the view's corner */
    int16_t r[4] = {(int16_t)(((w * 0xc00) >> 16) + ox), (int16_t)(((h << 0xc) >> 16) + oy), (int16_t)((w * 0x47ae) >> 16),
                    (int16_t)(((h * 0xccc) >> 16) - 2)};
    int ty = ((h * 0x1b32) >> 16) - 2 + oy;
    for (int i = 0; i < 8; i++) {
        int32_t k = gms32(0x531bb8 + 0x1cu * (uint32_t)i);   /* 0x40a700 */
        if (k == -1) continue;
        char b[256];
        uint32_t car;
        if (k == -2) {
            snprintf(b, sizeof b, "%s", driver_name(0));
            Rect_Fill(r, colour(0x5170d8), 2);
            car = gm32(0x5a1ea0);   /* 0x4a1a00 */
        } else {
            const char *n = net_name(k);
            snprintf(b, sizeof b, "%s", n ? n : (const char *)gm(0x52ea5c));
            Rect_Fill(r, colour(0x5170d4), 2);
            car = gm32(0x531778 + 0x80u * (uint32_t)k);   /* 0x409d20 */
        }
        const uint8_t *c = ghost_colour(car);
        int16_t sw = (int16_t)(w * 10 / 640), sh = (int16_t)(h * 10 / 480);
        int16_t sq[4] = {(int16_t)(r[2] - w * 10 / 640 + r[0] - sw / 2), (int16_t)(r[3] / 2 + r[1] - sh / 2), sw, sh};
        Rect_Fill(sq, c, 2);
        char pl[8];
        snprintf(pl, sizeof pl, exe_str(0x517df0), i + 1);
        Font_Print(0, pl, ((w * 0xf95) >> 16) + ox, ty, colour(0x5170d0), 0x22);
        Font_Print(0, b, ((w * 0x132a) >> 16) + ox, ty, colour(0x5170d0), 0x21);
        r[1] = (int16_t)(r[1] + r[3]), ty += r[3];
    }
}

/* the player's display flags (Profile_HudFlags 0x408a00: mode 4's at 0x52f3ed, else the profile's
   +0x574; the original reads through NULL without a profile, the port reads zero) */
static uint8_t hud_flags(int n)
{
    if (Gs_Mode() == 4) return gm8(0x52f3ed + 0xc4u * (uint32_t)n);
    uint32_t t = gm32(0x531350 + 0x30u * (uint32_t)n) >> 0x12 & 0xf;
    return t != 0xf ? gm8(0x52ff90 + t * 0x650) : 0;
}

static bool msg_lock;   /* 0x537190: one message a view */

void Hud_MessageReset(void) { msg_lock = false; }   /* 0x417e60 */

/* 0x417e70: a message in the big font (slot 2), centred in the view at a quarter of the screen's
   height (lower by 0x10 with display flags 1 of 3; on a split screen at 0.1375 of the height
   further down or, one above the other, up from the half's line), with a shadow when asked, or at
   (x, y) given; only the view's first message is drawn */
static void message(const char *text, const uint8_t *rgba, int p, bool shadow, int x, int y)
{
    if (msg_lock) return;
    msg_lock = true;
    int w, h;
    D3d_ScreenSize(&w, &h);
    int16_t v[4] = {0, 0, (int16_t)w, (int16_t)h};   /* View_Rect 0x464b10 */
    bool split = split_screen();
    int off = 0;
    if (split) {
        if (!Gs_ModeBit18()) off = (h * 0x2333) >> 16;
        else off = (p == 1 ? h / 2 : 0) - ((h * 0x2333) >> 16);
    }
    int ty = ((h << 0xe) >> 16) + off;
    if (!split && (hud_flags(Gs_Byte537ef6() + p) & 3) == 1) ty += 0x10;
    int tx = v[2] / 2 + v[0];
    uint32_t fl = 0x12;
    if (x != -1) fl = 9, tx = x, ty = y;
    if (shadow) Font_Print(2, text, tx + 1, h / 0x60 + 1 + ty, colour(0x517e28), fl);
    Font_Print(2, text, tx, h / 0x60 + ty, rgba, fl);
}

/* 0x412970: with the lap game's clock (0x52f2b0 bit 11) done (0x53d1a7), in its kinds 1 and 2 the
   winner's message (text 0xbe) for the car at the head (0x53e184) */
static void game_over(int p)
{
    if (!gm8(0x53d1a7)) return;
    uint32_t k = gm32(0x52f2b0) & 7;
    if ((k == 1 || k == 2) && (int8_t)gm8(0x53e184 + (uint32_t)p) == 0) message(Text_Get(0xbe), colour(0x5170f4), p, false, -1, -1);
}

/* 0x4129d0 for one view: the arcade's time panel, the box (taller by a line or two for the laps,
   0x543098, by the language's slot) with the time at its left; under it "Lap n" (modes 7 and 12,
   or the lap game's kind 1) or "Lap n / laps" (not with the selection's bit 27), or in the lap
   game's kind 2 text 0xbc with the car's count (0x53d1a4) of 0x52f2b0's bits 7-10; at the right,
   outside modes 7 and 12 (and stage 10 of mode 10), text 0x99 and the place in slot 6 (with ghosts
   the own run's slot, the text left when there is none) */
static void arcade_time(int p)
{
    int w, h;
    D3d_ScreenSize(&w, &h);
    int m8 = w * 3 / 0x280, m20 = w * 0x2c / 0x280, dy = h * 3 / 0x1e0;
    int ox = 0, oy = 0;
    if (split_screen()) {
        if (!Gs_ModeBit18()) { if (p == 0) ox = -(w / 2); }
        else if (p == 1) oy = h / 2;
    }
    int16_t r[4] = {(int16_t)(((w * 0xab9b) >> 16) + ox), (int16_t)(((h << 0xc) >> 16) + oy), (int16_t)(((w * 0x4865) >> 16) + 1),
                    (int16_t)(h * 0x35 / 0x1e0)};
    uint32_t lang = gm32(0x52afb4) & 7;   /* 0x4057f0 */
    int extra = (lang == 1 || lang == 3 || lang == 2 ? 2 : 1) * (h * 0x18 / 0x1e0);
    if (gm32(0x543098)) r[3] = (int16_t)(r[3] + extra);   /* 0x459390 */
    Rect_Fill(r, colour(0x5170bc), 2);
    if (gm32(0x543098)) r[3] = (int16_t)(r[3] - extra);
    draw_time(hud_time[p], 4, (int32_t)(((int64_t)(r[0] + 0x10 + m8) << 16) / w), (int32_t)(((int64_t)(r[3] + r[1] - dy + 1) << 16) / h),
              colour(0x5170c4), 0x21, NULL, false);
    int lap = Sectors_Lap(p), laps = (int)(gm32(0x52f2ac) >> 0x10 & 0xf);   /* 0x4582d0, Gs_Laps 0x406990 */
    int ys = r[1] + dy - h * 2 / 0x1e0, yb = r[1] + dy, x = r[0] + m8;
    uint32_t m = Gs_Mode(), kind = gm32(0x52f2b0) >> 0xb & 1 ? gm32(0x52f2b0) & 7 : 0;
    char b[64];
#define PUT(font, y) (Font_Print(font, b, x, y, colour(0x5170c4), 9), x += Font_TextWidth(font, b))
    if (kind == 2) {
        snprintf(b, sizeof b, exe_str(0x517dd4), Text_Get(0xbc)), PUT(0, ys);
        snprintf(b, sizeof b, exe_str(0x517dd0), (int8_t)gm8(0x53d1a4 + (uint32_t)p)), PUT(5, yb);   /* 0x448c60 */
        snprintf(b, sizeof b, "%s", exe_str(0x517dcc)), PUT(0, ys);
        snprintf(b, sizeof b, exe_str(0x517dd0), (int)(gm32(0x52f2b0) >> 7 & 0xf)), PUT(5, yb);   /* 0x4082c0 */
    } else if (m == 7 || m == 12 || kind == 1) {
        if (lap < 0) lap = 0;
        snprintf(b, sizeof b, exe_str(0x517dd4), Text_Get(0x2f)), PUT(0, ys);
        snprintf(b, sizeof b, exe_str(0x517dd0), (lap + 1) % 100), PUT(5, yb);
    } else if (!Gs_SelBit27()) {
        if (lap < 0) lap = 0;
        if (lap < laps) lap++;
        snprintf(b, sizeof b, exe_str(0x517dd4), Text_Get(0x2f)), PUT(0, ys);
        snprintf(b, sizeof b, exe_str(0x517dd0), lap % 100), PUT(5, yb);
        snprintf(b, sizeof b, "%s", exe_str(0x517dcc)), PUT(0, ys);
        snprintf(b, sizeof b, exe_str(0x517dd0), laps), PUT(5, yb);
    }
#undef PUT
    if (m == 7 || m == 12 || (Tod_Stage() == 10 && m == 10)) return;
    snprintf(b, sizeof b, "%s", Text_Get(0x99));
    Font_Print(0, b, r[0] - m20 + r[2], ys, colour(0x5170c4), 0xc);
    if (m == 11 || (Gs_ModeBit19() && Gs_SelBit25())) {
        for (int i = 0; i < 8; i++)
            if (gms32(0x531bb8 + 0x1cu * (uint32_t)i) == -2) snprintf(b, sizeof b, exe_str(0x517dd0), i + 1);   /* 0x40a700 */
    } else
        snprintf(b, sizeof b, exe_str(0x517dd0), Times_Place(p) + 1);
    Font_Print(6, b, r[0] - m8 + r[2], yb, colour(0x5170c4), 0xc);
}

/* 0x4125f0 for one view: the debug line (0x52afd0 bit 29: the car's point of the finish's, the race's
   seconds), the lap game's or the circuits' end message, the messages, then by the display flags: 4
   the records (modes 3, 5-7, 10, 12; not on a split screen beside each other) or the standings
   (modes 0-2; modes 8, 9, 11 their own; neither with the selection's bit 27 unless 0x52afb4 bit 19),
   8 the time panel (the arcade's in modes 5-7, 11, 12 or on a circuit), 0x20 the map (the same
   modes) and the split delta */
void Hud_Draw(int p)
{
    int w, h;
    D3d_ScreenSize(&w, &h);
    int16_t v[4] = {0, 0, (int16_t)w, (int16_t)h};   /* View_Rect 0x464b10 */
    static const uint8_t white[4] = {0xff, 0xff, 0xff, 0xff};
    if (gm32(0x52afd0) >> 0x1d & 1) {   /* 0x405d60 */
        char b[64];
        snprintf(b, sizeof b, exe_str(0x517dc0), Car_CatIndex(Car_Get(p)), Cat_FinishPoint(), Times_Race() / 100);   /* 0x4ab600 */
        Font_Print(0, b, v[0] + 10, v[3] + v[1] - 10, white, 0x21);
    }
    if (gm32(0x52f2b0) >> 0xb & 1) game_over(p);   /* 0x4082e0 */
    else if (split_screen() && (Gs_SelBit24() || Gs_SelBit25()) && Race_Finished(p) && (int8_t)gm8(0x53e184 + (uint32_t)p) == 0)
        message(Text_Get(0xbe), colour(0x5170f4), p, false, -1, -1);
    static const uint32_t msgs[4][2] = {{MSG_88, 0xbd}, {MSG_REC, 0x30}, {MSG_NOPROF, 0x6a}, {MSG_BEST, 0x83}};
    for (int i = 0; i < 4; i++)
        if (gm32(msgs[i][0] + 4u * (uint32_t)p)) message(Text_Get(msgs[i][1]), colour(0x5170e8), p, true, -1, -1);
    uint8_t flags = hud_flags(Gs_Byte537ef6() + p);
    uint32_t m = Gs_Mode();
    bool arcade = m == 5 || m == 6 || m == 7 || m == 11 || m == 12 || Gs_SelBit25();
    if (flags & 4) {
        if (m == 7 || m == 5 || m == 6 || m == 3 || m == 10 || m == 12) {
            if (!split_screen() || Gs_ModeBit18()) records(p);   /* 0x4147f0 */
        } else if (!Gs_SelBit27() || Gs_ModeBit19()) {
            if (m == 2 || m == 0 || m == 1) standings(p);   /* 0x414ed0 */
            else if (m == 8 || m == 9 || m == 11) ghost_standings(p);   /* 0x415480 */
        }   /* (else 0x415bc0, empty) */
    }
    if (flags & 8) {
        if (arcade) arcade_time(p);   /* 0x4129d0 */
        else time_panel(p);           /* 0x413fe0 */
    }
    if (flags & 0x20) {
        if (arcade) map(p);   /* 0x415f50 */
        split_delta(p);       /* 0x4137e0 */
    }
}
