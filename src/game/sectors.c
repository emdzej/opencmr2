/* The sectors (see sectors.h). */
#include "game/sectors.h"
#include "game/gstate.h"
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
static int race_cars(void) { return (int)Gs_SelBits14(); }

void Sectors_Init(void)
{
    /* 0x458a00 */
    nsplits = 0;
    npoints = Cat_FinishPoint();
    if (npoints) {
        for (int i = 0; i < npoints && nsplits < 64; i++)
            if (Cat_Record(i)[0x18] & 1) split_at[nsplits++] = i << 16;
        both_ways = Gs_SelBit24() || Gs_SelBit25();
        if (both_ways) PENDING_NOTE(0x458a00, "Sectors_Init (the arcade's evenly spaced splits)");
    }
    laps_mode = false;
    ncars = race_cars();
    if (Gs_Mode() == 7 || Gs_Mode() == 12) laps_mode = true;
}

/* 0x458bd0 for a rally: the car's point, the first split next, the finish the last split */
static void start(int n)
{
    Rec *r = &rec[n];
    memset(r, 0, sizeof *r);
    if (Gs_SelBit24() || Gs_SelBit25()) PENDING_NOTE(0x458bd0, "Sectors_Start (the arcade)");
    r->point = (int16_t)Car_CatIndex(Car_Get(n));
    r->next = 1;
    r->fin = (int16_t)(nsplits - 1);
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
        if (both_ways) PENDING_NOTE(0x458f30, "Sectors_Move (both ways)");
        if (!laps_mode) check_split(r, nx);
        else PENDING_NOTE(0x4590a0, "Sectors_Move (laps)");
        if (r->last_lap == r->fin_lap && r->last == r->fin) r->finished = 1;   /* 0x459180 */
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
