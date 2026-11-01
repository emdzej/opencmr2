/* The computer drivers' times (see opponents.h), ported from 0x455300-0x456960 (0x456710 from the
   disassembly) and 0x40d520. */
#include "game/opponents.h"
#include "crt.h"
#include "exe.h"
#include "game/hud.h"
#include "game/race.h"
#include "fixmath.h"
#include "game/gstate.h"
#include "game/mem.h"
#include "game/sectors.h"
#include "game/timer.h"
#include "pending.h"
#include "sim/progress.h"
#include "world/stage.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

enum { N = 16 };
static uint8_t tm[0x550];          /* DAT_0054241c: the difficulty's .tm (1,360 bytes) */
static bool loaded;                /* DAT_00542604 */
static uint32_t seed;              /* DAT_00541f88 */
static int computers;              /* DAT_00541f98: 16 less the players */
static int8_t player_slot[N];      /* DAT_00541f9c: the players' places among the 16 (15 down) */
static int8_t order[16][N];        /* DAT_00541fac: by split, the drivers fastest first */
static int8_t place[16][N];        /* DAT_0054204c: by split, each driver's place */
static int8_t by_time[16][N];      /* DAT_005420ec */
static int8_t entries[16];         /* DAT_0054218c */
static int32_t times[16][N];       /* DAT_00542198: by split, the cumulative times (16.16 s) */
static int8_t racing_n[4];         /* DAT_005425e8: the players whose splits go in, by race */
static int8_t racing[4][2];        /* DAT_005425f0: their slots */
static bool racer_done[4];         /* DAT_00542418 */

static int32_t tmi(uint32_t off) { return (int32_t)((uint32_t)tm[off] | tm[off + 1] << 8 | tm[off + 2] << 16 | (uint32_t)tm[off + 3] << 24); }
static int32_t tmb(int i, int k) { return (int8_t)tm[i * 5 + k]; }
static int32_t fx(int64_t v) { return (int32_t)(v >> 16); }
static int32_t fdiv(int32_t a, int32_t b) { return b ? (int32_t)(((int64_t)a << 16) / b) : 0; }
static int32_t to16(double v) { return (int32_t)llrint(v * 65536.0); }
/* the double at 0x5112e8 (-65536): the scale of a driver's random range */
static double opp_scale(void)
{
    const uint8_t *p = exe_data(0x5112e8, 8);
    double d = 0;
    if (p) memcpy(&d, p, 8);
    return d;
}

/* 0x455300: <stage>.tm0, tm1 or tm2 by the difficulty (a challenge's level less one), when the
   selection has computer drivers (bit 21) */
void Stage_LoadTm(const char *stage)
{
    loaded = false;
    if (!(gm32(0x52f2ac) >> 0x15 & 1)) return;
    int d = Gs_Mode() == 4 ? (int)(gm32(0x52afb4) >> 23 & 0xf) : (int)Gs_Mode2();
    if (Gs_Mode() == 4 && d) d--;
    char p[32];
    snprintf(p, sizeof p, "%s.tm%d", stage, d);
    uint32_t sz = 0;
    const uint8_t *f = Bfl_FindFile(&g_stage_bfl, p, NULL, &sz, false);
    if (!f) return;
    memset(tm, 0, sizeof tm);
    memcpy(tm, f, sz < sizeof tm ? sz : sizeof tm);
    loaded = true;
}

/* 0x455e60 */
static void clear(void)
{
    memset(player_slot, 0, sizeof player_slot);
    computers = N;
    for (int s = 0; s < 16; s++)
        for (int i = 0; i < N; i++) order[s][i] = place[s][i] = by_time[s][i] = (int8_t)i, times[s][i] = 0;
}

/* 0x4556a0: the players at the bottom of the 16 */
static void seat_players(int n)
{
    computers = N - n;
    for (int i = 0; i < n; i++) player_slot[i] = (int8_t)(0xf - i);
    for (int s = 1; s <= Sectors_Count(); s++) entries[s] = (int8_t)computers;
}

/* The stage record of 0x455470's stack (0xb0 bytes, here 44 words): [0] 0, [1..10] the share of the
   stage's run within the weather's change up to each split, [11..20] / [21..30] / [31..39] each split's
   shares before, within and after the change, [40], [41] where the change starts and ends (cat points,
   16.16), [42], [43] the wetness there. (The after-shares' tenth would be [40].) */
enum { AT_A = 40, AT_B = 41, WET_A = 42, WET_B = 43 };

/* 0x456330 */
static void shares(int32_t *P)
{
    int n = Sectors_Shown(), ia = 0, ib = 0;
    for (int k = 1; k <= n; k++) {
        if (Sectors_Point(k - 1) <= P[AT_A] && P[AT_A] < Sectors_Point(k)) ia = k;
        if (Sectors_Point(k - 1) < P[AT_B] && P[AT_B] <= Sectors_Point(k)) ib = k;
    }
    int32_t run = 0, a = 0, b = 0;
    P[0] = 0;
    for (int k = 1; k <= n && k <= 9; k++) {
        int32_t len = Sectors_Point(k) - Sectors_Point(k - 1);
        if (k < ia) a = len;
        if (k == ia) a = P[AT_A] - Sectors_Point(k - 1);
        if (k > ia) a = 0;
        if (k < ib) b = 0;
        if (k == ib) b = Sectors_Point(k) - P[AT_B];
        if (k > ib) b = len;
        P[11 + k - 1] = fdiv(a, len);
        P[31 + k - 1] = fdiv(b, len);
        P[21 + k - 1] = 0x10000 - P[11 + k - 1] - P[31 + k - 1];
        run += len - b - a;
        P[1 + k - 1] = P[AT_B] - P[AT_A] == 0 ? 0 : fdiv(run, P[AT_B] - P[AT_A]);
    }
}

/* 0x4564d0: each driver's time for each split, the dry (.tm +0x90) and wet (+0x310) times by the
   split's shares and the wetness */
static void split_times(const int32_t *P, int32_t *out)
{
    int32_t wa = P[WET_A], wb = P[WET_B];
    for (int k = 0; k < Sectors_Shown() && k < 9; k++)
        for (int j = 0; j < N; j++) {
            int32_t dry = tmi(0x90 + 0x40u * (uint32_t)k + 4u * (uint32_t)j), wet = tmi(0x310 + 0x40u * (uint32_t)k + 4u * (uint32_t)j);
            int32_t before = P[11 + k], mid = P[21 + k], after = P[31 + k];
            int32_t u = wa + fx((int64_t)(wb - wa) * P[k + 1]) + wa + fx((int64_t)(wb - wa) * P[k]);
            int32_t m = fdiv(u, 0x20000);
            out[k * N + j] = fx((int64_t)(0x10000 - m) * fx((int64_t)mid * dry)) + fx((int64_t)m * fx((int64_t)mid * wet)) +
                             fx((int64_t)fx((int64_t)after * wb) * wet) + fx((int64_t)fx((int64_t)after * (0x10000 - wb)) * dry) +
                             fx((int64_t)fx((int64_t)before * wa) * wet) + fx((int64_t)fx((int64_t)before * (0x10000 - wa)) * dry);
        }
}

/* 0x456710: each driver's split times shaken by a random share, the most it may drift from its
   total by the split's limit (three times the difficulty's at the first split down to it at the
   last; a time trial half the difficulty's, unbounded) */
static void shake(int32_t *t)
{
    int n = Sectors_Shown();
    uint32_t d = Gs_Mode2();
    int32_t amp = d == 0 ? 0x19999 : d == 1 ? 0x10000 : 0xa8f5, top, lim[12] = {0};
    if (!Gs_SelBit25()) {
        top = fx((int64_t)amp * 0x30000);
        lim[n] = amp;
        lim[1] = top;
        if (n > 2) {
            int32_t span = to16(n - 1);
            for (int k = 2; k < n; k++) lim[k] = top - fx((int64_t)to16(k - 1) * fdiv(top - amp, span));
        }
    } else
        top = fx((int64_t)amp * 0x8000);
    for (int j = 0; j < N; j++) {
        int32_t sum = 0, run = 0;
        for (int k = 0; k < n; k++) {
            int32_t v = t[k * N + j];
            sum += v, run += v;
            int32_t range = fx((int64_t)fx((int64_t)top * fdiv(v, 0x640000)) * 0x20000);
            int32_t x = fdiv(to16(crt_rand()), 0x7fff0000);
            int32_t r = fx((int64_t)x * range) - fdiv(range, 0x20000);
            if (!Gs_SelBit25()) {
                int32_t m = fx((int64_t)fdiv(lim[k + 1], 0x640000) * sum);
                int32_t over = r - m - sum + run;
                if (over > 0) r -= over;
                int32_t under = m - sum + r + run;
                if (under < 0) r -= under;
            }
            t[k * N + j] += r;
            run += r;
        }
    }
}

/* 0x40d520: idx sorted by v (start from 0..n-1 when init), down (0) or up (1), by exchanges */
static void sort_by(const int32_t *v, int8_t *idx, int up, int n, bool init)
{
    if (init)
        for (int i = 0; i < n; i++) idx[i] = (int8_t)i;
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++) {
            int8_t a = idx[i], b = idx[j];
            if ((up == 0 && v[a] < v[b]) || (up == 1 && v[b] < v[a])) idx[i] = b, idx[j] = a;
        }
}

/* 0x455f00 with 0x456110: each driver's rating: its base, its dry and wet adjustments by the stage's
   shares before, within and after the weather's change, and a random part; the final order by it */
static void ratings(const int32_t *P)
{
    int32_t r[N];
    int32_t total = to16(Cat_FinishPoint());
    int32_t wa = P[WET_A], wb = P[WET_B];
    int32_t a = fdiv(P[AT_A], total), b = fdiv(total - P[AT_B], total), mid = 0x10000 - b - a;
    int32_t m = fdiv((0x10000 - wb) + (0x10000 - wa), 0x20000);
    for (int i = 0; i < N; i++) {
        int32_t c3 = to16(tmb(i, 3)), c4 = to16(tmb(i, 4));
        r[i] = fx((int64_t)mid * fx((int64_t)(0x10000 - m) * c4)) + fx((int64_t)mid * fx((int64_t)m * c3)) +
               fx((int64_t)b * fx((int64_t)wb * c4)) + fx((int64_t)b * fx((int64_t)(0x10000 - wb) * c3)) +
               fx((int64_t)a * fx((int64_t)wa * c4)) + fx((int64_t)a * fx((int64_t)(0x10000 - wa) * c3)) + to16(tmb(i, 0));
    }
    for (int i = 0; i < N; i++) {
        int32_t c2 = to16(tmb(i, 2)), c1 = (int32_t)llrint(tmb(i, 1) * opp_scale());
        int32_t x = fdiv(to16(crt_rand()), 0x7fff0000);
        r[i] += fx((int64_t)x * (-c1 - c2)) + c2;
    }
    sort_by(r, order[Sectors_Shown()], 0, computers, true);
}

/* 0x4561e0: the split times, shaken, summed, and each split's order by time */
static void split_order(int32_t *P)
{
    static int32_t t[16 * 10];
    memset(t, 0, sizeof t);
    shares(P);
    split_times(P, t);
    shake(t);
    int n = Sectors_Shown();
    memset(times, 0, sizeof times);   /* 0x456960 */
    for (int k = 0; k < n; k++)
        for (int j = 0; j < N; j++) times[k + 1][j] = times[k][j] + t[k * N + j];
    for (int s = 0; s <= n; s++) sort_by(times[s], by_time[s], 1, computers, true);
}

/* 0x456250: the drivers' identities through the splits: the final row by rating, each earlier row the
   same drivers by their time ranks there */
static void identities(void)
{
    int n = Sectors_Shown();
    int8_t who[N] = {0};
    for (int i = 0; i < computers; i++) {
        int8_t v = order[n][i];
        who[by_time[n][i]] = v;
        place[n][v] = (int8_t)i;
    }
    for (int i = computers; i < N; i++) order[n][i] = place[n][i] = (int8_t)i;
    for (int r = 1; r < n; r++) {
        for (int i = 0; i < computers; i++) {
            int8_t v = who[by_time[r][i]];
            order[r][i] = v;
            place[r][v] = (int8_t)i;
        }
        for (int i = computers; i < N; i++) order[r][i] = place[r][i] = (int8_t)i;
    }
}

/* 0x455470 */
void Opp_Times(bool new_seed)
{
    if (!loaded) return;
    clear();
    int32_t P[44] = {0};
    /* 0x45e6b0: the weather's change on the stage (from 0x461a70, not ported: dry throughout) */
    PENDING_NOTE(0x45e6b0, "Opp_Times (the weather's change along the stage)");
    if ((gm32(0x52f2ac) & 0x1f) == 3) P[WET_A] = P[WET_B] = 0;   /* Sweden: never wet */
    if (new_seed) seed = Timer_Now();
    crt_srand(seed);
    seat_players((int)(gm32(0x52afb4) >> 13 & 0xf));
    ratings(P);
    split_order(P);
    identities();
    if (Gs_SelBit27()) {
        PENDING_NOTE(0x4556f0, "Opp_Times (the championship's standings)");
        return;
    }
    /* 0x455620: the players' slots for the splits (a race, or the alternating players one each) */
    int n = (int)(gm32(0x52afb4) >> 13 & 0xf), kind = Race_Kind();
    memset(racing_n, 0, sizeof racing_n);
    if (kind >= 0 && kind < 2) {
        racing_n[0] = (int8_t)n;
        for (int i = 0; i < n && i < 2; i++) racing[0][i] = player_slot[i];
    } else if (kind == 4)
        for (int i = 0; i < n && i < 4; i++) racing_n[i] = 1, racing[i][0] = player_slot[i];
    memset(racer_done, 0, sizeof racer_done);   /* 0x455610 */
}

/* 0x455590: the players' times at their splits into the tables, until they finish (0x4569c0,
   0x456a00, 0x456a30) */
void Opp_Step(int race)
{
    if (!loaded) return;
    if (Gs_SelBit27()) {
        PENDING_NOTE(0x455590, "Opp_Step (the championship)");
        return;
    }
    for (int i = 0; i < racing_n[race]; i++) {
        if (racer_done[i]) continue;
        if (Sectors_Split(i) && Gs_Mode() != 4) Opp_Insert(racing[race][i], Times_Race(), Sectors_SplitIndex(i));
        if (Sectors_Finished(i)) racer_done[i] = true;
    }
}

int Opp_Computers(void) { return computers; }
int Opp_Place(int player) { return place[Sectors_Shown()][player_slot[player]]; }        /* 0x455ca0 */
int Opp_Order(int i) { return order[Sectors_Shown()][i]; }                                 /* 0x455cf0 */
int Opp_PlayerSlot(int player) { return player_slot[player]; }                            /* 0x455de0 */
/* 0x455d70 with 0x40d480: the i-th time of the final split in 1/100 s (-1 out of range) */
int32_t Opp_Time(int i)
{
    if (i < 0 || i >= N) return -1;
    int s = Sectors_Shown();
    return (int32_t)(((int64_t)(times[s][by_time[s][i]] + 0x147) << 16) / 0x28f5c28);
}

/* 0x455af0: a player's (or a driver's) time at a split into the tables, in its place */
void Opp_Insert(int who, int32_t cs, int s)
{
    int32_t t = fdiv((cs % 100) << 16, 0x640000) + (cs / 100) * 0x10000;   /* 0x40d4b0: 1/100 s to 16.16 s */
    if (s < 0 || s >= 16) return;
    if (who < computers) PENDING_NOTE(0x455af0, "Opp_Insert (a championship driver's slot, 0x541f90)");
    int slot = who;
    times[s][slot] = t;
    int i = 0;
    while (times[s][by_time[s][i]] < t && i != entries[s]) {
        if (++i > 15) goto done;
    }
    for (int k = 15; k > i; k--) {
        by_time[s][k] = by_time[s][k - 1];
        order[s][k] = order[s][k - 1];
        place[s][order[s][k]]++;
    }
    by_time[s][i] = (int8_t)slot;
    order[s][i] = (int8_t)who;
    place[s][who] = (int8_t)i;
done:
    entries[s]++;
}

bool Opp_Loaded(void) { return loaded; }                                                   /* 0x455ae0 */
int Opp_PlaceAt(int player, int s) { return s >= 0 && s < 16 ? place[s][player_slot[player & 15]] : 15; }   /* 0x455c80 */
int Opp_Entries(int s) { return s >= 0 && s < 16 ? entries[s] : 0; }                       /* 0x455c70 */
int Opp_DriverAt(int e, int s) { return s < 0 || s >= 16 || e < 0 || e >= entries[s] ? -1 : order[s][e]; }   /* 0x455cc0 */
int32_t Opp_TimeAt(int e, int s)                                                          /* 0x455d40 */
{
    if (s < 0 || s >= 16 || e < 0 || e >= N) return 0;
    return (int32_t)(((int64_t)(times[s][by_time[s][e]] + 0x147) << 16) / 0x28f5c28);
}
