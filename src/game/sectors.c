/* The sectors (see sectors.h). */
#include "game/sectors.h"
#include "game/gstate.h"
#include "game/car_load.h"
#include "sim/car.h"
#include "sim/progress.h"
#include "pending.h"
#include <string.h>

typedef struct {
    int16_t point;       /* +0x00 */
    int16_t lap_was;     /* +0x02 */
    int16_t last_lap;    /* +0x04 */
    int16_t last;        /* +0x06 */
    int16_t lap;         /* +0x08 */
    int16_t next;        /* +0x0a */
    int16_t fin_lap;     /* +0x0c */
    int16_t fin;         /* +0x0e */
    uint16_t most;       /* +0x10 */
    int16_t steps;       /* +0x12 */
    int16_t splits;      /* +0x14 */
    uint8_t moved, split, f18, first, finished;   /* +0x16-+0x1a */
} Rec;
static Rec rec[CAR_MAX];          /* 0x542e78 */
static int npoints, nsplits;      /* DAT_00542c70, DAT_00542c74 */
static int32_t split_at[64];      /* 0x542c7c: the split points, 16.16 */
static int ncars;                 /* DAT_00542c68 */
static int laps;                  /* DAT_00542c6c */
static bool both_ways, laps_mode; /* DAT_00542cae, DAT_00542cad */

int Sectors_Count(void) { return nsplits; }
bool Sectors_Finished(int car) { return car >= 0 && car < CAR_MAX && rec[car].finished; }
bool Sectors_Split(int car) { return car >= 0 && car < CAR_MAX && rec[car].split; }
bool Sectors_Moved(int car) { return car >= 0 && car < CAR_MAX && rec[car].moved; }
int Sectors_SplitIndex(int car) { return car >= 0 && car < CAR_MAX ? rec[car].splits : 0; }
/* the splits shown (DAT_00542cac, 0x458a00's end): one fewer than the points in a rally */
int Sectors_Last(int car) { return car >= 0 && car < CAR_MAX ? rec[car].last : 0; }
int Sectors_Most(int car) { return car >= 0 && car < CAR_MAX ? rec[car].most : 0; }
bool Sectors_LapFirst(int car) { return car >= 0 && car < CAR_MAX && rec[car].first; }
int32_t Sectors_Point(int i) { return i >= 0 && i < 64 ? split_at[i] : 0; }
int Sectors_Shown(void) { return Gs_SelBit24() || Gs_SelBit25() ? nsplits : nsplits - 1; }

/* 0x420190 for a rally: the players */
static int race_cars(void) { return Cars_Racing(); }   /* 0x420190 */

/* 0x458a00: the flagged points; round a circuit (selection bit 24) four splits evenly spaced
   (the whole points below i x points / 4), with bit 25 two (the first two flagged) */
static void points(void)
{
    nsplits = 0;
    npoints = Cat_FinishPoint();
    if (!npoints) return;
    for (int i = 0; i < npoints && nsplits < 64; i++)
        if (Cat_Record(i)[0x18] & 1) split_at[nsplits++] = i << 16;
    both_ways = Gs_SelBit24() || Gs_SelBit25();
    if (!both_ways) return;
    nsplits = Gs_SelBit24() ? 4 : 2;
    if (Gs_SelBit24()) {
        int32_t q = (int32_t)(((int64_t)npoints << 32) / ((int64_t)nsplits << 16));
        for (int i = 0; i < nsplits; i++) split_at[i] = (int32_t)(((int64_t)i << 16) * q >> 32) << 16;
    }
}

/* 0x458090: the points, the cars racing, the laps (bits 16-19; twice 0x52f2b0's bits 7-10 with
   its bit 11 and kind 2), the time trial's laps mode (modes 7, 12) */
void Sectors_Init(void)
{
    points();
    laps_mode = false;
    ncars = race_cars();
    laps = (int)(gm32(0x52f2ac) >> 0x10 & 0xf);   /* Gs_Laps 0x406990 */
    uint32_t w = gm32(0x52f2b0);
    if ((w >> 0xb & 1) && (w & 7) == 2) laps = (int)(w >> 7 & 0xf) << 1;   /* 0x4082e0, 0x4082b0, 0x4082c0 */
    if (Gs_Mode() == 7 || Gs_Mode() == 12) laps_mode = true;
}

static int before(int i);

/* 0x458bd0: in a rally the car's point, the first split next, the finish the last split. Round a
   circuit (bit 24; the port's starts are always 0x458100's 1) the point before the car's, the lap
   -1 (the line ahead of the grid), split 1 next, the finish split 0 of the last lap (none in the
   laps mode: lap 1000) */
static void start(int n)
{
    Rec *r = &rec[n];
    memset(r, 0, sizeof *r);
    if (!Gs_SelBit24() && !Gs_SelBit25()) {
        r->point = (int16_t)Car_CatIndex(Car_Get(n));
        r->next = 1;
        r->fin = (int16_t)(nsplits - 1);
        return;
    }
    r->point = (int16_t)before(Car_CatIndex(Car_Get(n)));
    if (!Gs_SelBit24()) {
        PENDING_NOTE(0x458bd0, "Sectors_StartCar (selection bit 25's head to head)");
        return;
    }
    r->lap_was = -1;
    if (laps_mode) r->next = 0, r->fin_lap = 1000;
    else r->next = 1, r->fin_lap = (int16_t)laps;
}

void Sectors_Start(void)
{
    if (Gs_SelBit27()) PENDING_NOTE(0x458100, "Sectors_Start (head to head)");
    for (int i = 0; i < race_cars(); i++) start(i);
}

/* 0x459320: the point before i (round a circuit to its end, else 0) */
static int before(int i)
{
    if (--i == -1) return both_ways ? npoints - 1 : 0;
    return i;
}

/* 0x458fd0: the next split point reached */
static void check_split(Rec *r, int p)
{
    if (r->lap_was != r->lap || (p << 16) != split_at[r->next]) return;
    r->split = 1;
    r->last_lap = r->lap;
    r->last = r->next;
    r->next++;
    if (r->last == 0) r->first = 1;
    if (r->next == nsplits) r->next = 0, r->lap++;
    if (nsplits < ++r->splits) r->splits = 1;
}

/* 0x458f30: round a circuit, past the last point to 0 a lap more (to 0 from 1000), back over it
   a lap less (not below -1; in the laps mode the last split cleared) */
static void lap_step(Rec *r, int cur, int nx)
{
    if (nx < r->point && nx == 0 && cur == npoints - 1) {
        r->lap_was++;
        r->f18 = 1;
        if (r->lap_was == 1000) r->lap_was = 0;
    }
    if (r->point < nx && nx == npoints - 1 && cur == 0) {
        if (--r->lap_was < -1) r->lap_was = -1;
        if (laps_mode) r->last = 0;
    }
}

/* 0x4590a0: the laps mode's splits: a new lap restarts them (the first when the last was the one
   before the line) */
static void lap_split(Rec *r, int p)
{
    if (r->f18) {
        if (r->last == nsplits - 1) r->first = 1;
        r->last = 0;
        r->next = 1;
        r->splits = 0;
        r->split = 1;
        return;
    }
    if ((p << 16) != split_at[r->next]) return;
    r->split = 1;
    r->last = r->next;
    if (++r->next == nsplits) r->next = 0;
    if (nsplits <= ++r->splits) r->splits = 0;
}

/* 0x458e00 */
static void move(int n, int p)
{
    Rec *r = &rec[n];
    int cur = r->point;
    if (p == cur) return;
    int dir = both_ways ? ((p - cur < -0x32 || (0 < p - cur && p - cur < 0x32)) ? 1 : -1) : (p - cur < 1 ? -1 : 1);
    for (int k = 0; cur != p && k < npoints; k++) {
        r->steps = (int16_t)(r->steps + dir);
        int nx = cur + dir;
        if ((int)r->most < r->steps) r->most = (uint16_t)r->steps;
        if (nx < 0) nx = npoints - 1;
        if (npoints <= nx) nx = 0;
        if (both_ways) lap_step(r, cur, nx);
        if (!laps_mode) check_split(r, nx);
        else lap_split(r, nx);
        /* 0x459180: not with 0x52f2b0's bit 11 and kind 1 */
        if (r->last_lap == r->fin_lap && r->last == r->fin) {
            uint32_t w = gm32(0x52f2b0);
            r->finished = !((w >> 0xb & 1) && (w & 7) == 1);
        }
        PENDING_NOTE(0x459250, "Camera_TracksideHint");
        cur = nx;
    }
    r->point = (int16_t)p;
    r->moved = 1;
}

void Sectors_Step(void)
{
    for (int i = 0; i < ncars; i++) {
        Rec *r = &rec[i];
        r->moved = r->split = r->f18 = r->first = 0;
        if (!r->finished) move(i, before(Car_CatIndex16(Car_Get(i))));
    }
}
