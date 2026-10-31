/* The stage's centre line and each car's progress along it (see progress.h), ported from the
   disassembly of 0x420a30 and 0x421230 and the decompiler output of the small readers. The original
   keeps the last three tangents in a cache (0x538a8c); the port computes them each time, which gives
   the same values. */
#include "sim/progress.h"
#include "fixmath.h"
#include "game/gstate.h"
#include "world/stage.h"
#include <stdio.h>
#include <string.h>

enum { CAT_REC = 0x2c };
static const uint8_t *cat;   /* DAT_00538a9c */
static int npoints;          /* DAT_00538a88 */
static int finish;           /* DAT_00538a84 */
static bool circuit;         /* DAT_00538a94 */
static int32_t total;        /* DAT_00538c94 */
typedef struct { uint32_t idx, x4, passed, dist; int32_t frac; uint16_t idx16; } Rec;   /* 0x18 bytes */
static Rec rec[CAR_MAX];     /* 0x538aa8 */
static uint16_t cursor[CAR_MAX], best_idx[CAR_MAX];   /* 0x538a80, 0x538a98 */
static uint32_t best_d[CAR_MAX], since[CAR_MAX];      /* 0x538a78, 0x538c8c */

static int32_t rd32(const uint8_t *p) { return (int32_t)((uint32_t)p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24); }
static int32_t iabs(int32_t v) { return v < 0 ? -v : v; }

/* 0x421530: a point's x and z (y 0) */
void Cat_Point(int i, int32_t p[3])
{
    const uint8_t *r = cat + (size_t)i * CAT_REC;
    p[0] = rd32(r);
    p[1] = 0;
    p[2] = rd32(r + 8);
}

bool Stage_LoadCat(const char *stage)
{
    char name[32];
    uint32_t size = 0;
    snprintf(name, sizeof name, "%s.cat", stage);
    cat = Bfl_FindFile(&g_stage_bfl, name, NULL, &size, false);
    circuit = false;
    memset(rec, 0, sizeof rec);
    if (!cat) { npoints = finish = 0; return false; }
    npoints = (int)(size / CAT_REC);
    finish = npoints;
    for (int i = npoints - 1; i > 0; i--)
        if (cat[(size_t)i * CAT_REC + 0x18] & 1) { finish = i + 1; break; }
    int32_t a[3], b[3];
    Cat_Point(0, a);
    Cat_Point(finish - 1, b);
    uint32_t len = (uint32_t)finish;
    if (iabs(b[0] - a[0]) < 0x320000 && iabs(b[2] - a[2]) < 0x320000) {
        circuit = true;
        len = (gm32(0x52f2ac) >> 0x10 & 0xf) * (uint32_t)finish;   /* 0x406990: the laps */
    }
    total = (int32_t)(len << 16);
    return true;
}

int Cat_Points(void) { return npoints; }
int Cat_FinishPoint(void) { return finish; }
bool Cat_Circuit(void) { return circuit; }

static void unit2(int32_t v[3])
{
    uint32_t l = Fx_Sqrt((uint32_t)fx_mul(v[0], v[0]) + (uint32_t)fx_mul(v[2], v[2]));
    if (!l) { v[0] = v[2] = 0; return; }
    int32_t r = (int32_t)(0x100000000LL / (int32_t)l);
    v[0] = fx_mul(v[0], r);
    v[2] = fx_mul(v[2], r);
}

/* the unit directions from the previous point and to the next, summed and made unit */
void Cat_Tangent(int32_t out[3], int i)
{
    int32_t p[3], s[3] = {0, 0, 0};
    Cat_Point(i, p);
    if (i > 0) {
        int32_t q[3];
        Cat_Point(i - 1, q);
        int32_t d[3] = {p[0] - q[0], 0, p[2] - q[2]};
        unit2(d);
        s[0] = d[0], s[2] = d[2];
    }
    if (i < npoints - 1) {
        int32_t q[3];
        Cat_Point(i + 1, q);
        int32_t d[3] = {q[0] - p[0], 0, q[2] - p[2]};
        unit2(d);
        s[0] += d[0], s[2] += d[2];
    }
    unit2(s);
    out[0] = s[0];
    out[1] = 0;
    out[2] = s[2];
}

static int32_t dot3(const int32_t a[3], const int32_t b[3]) { return fx_mul(a[0], b[0]) + fx_mul(a[1], b[1]) + fx_mul(a[2], b[2]); }

void Car_ProgressStart(Car *c)
{
    if (!circuit || !finish) return;
    uint32_t best = 0x7d000000u;
    int bi = 0;
    for (int i = 0; i < finish; i++) {
        int32_t p[3];
        Cat_Point(i, p);
        int32_t dx = p[0] - CI32(c, 0x2d0), dz = p[2] - CI32(c, 0x2d8);
        if (iabs(dx) >= 0x640000 || iabs(dz) >= 0x640000) continue;
        uint32_t d = Fx_Sqrt((uint32_t)fx_mul(dx, dx) + (uint32_t)fx_mul(dz, dz));
        if ((int32_t)d < (int32_t)best) best = d, bi = i;
    }
    if (best != 0x7d000000u) rec[CI8(c, 0xb1a)].idx = rec[CI8(c, 0xb1a)].x4 = (uint32_t)bi;
}

/* the direction from the car to point i (x and z, divided by 512 when either is over 100 away), unit */
static void dir_to(const Car *c, int i, int32_t d[3])
{
    int32_t p[3];
    Cat_Point(i, p);
    d[0] = p[0] - CI32(c, 0x2d0);
    d[1] = 0;
    d[2] = p[2] - CI32(c, 0x2d8);
    if (iabs(d[0]) > 0x640000 || iabs(d[2]) > 0x640000) d[0] /= 512, d[2] /= 512;
    Vec_Unit(d);
}

/* 0x421230: the share of the current segment covered, along the tangent of its start */
static void segment_frac(const Car *c, Rec *r)
{
    int idx = (int)r->idx, prev = idx - 1;
    if (prev < 0) prev += finish;
    if (!circuit && idx <= 0) return;
    int32_t t[3], a[3], b[3];
    Cat_Tangent(t, prev);
    Cat_Point(idx, a);
    Cat_Point(prev, b);
    int32_t seg[3] = {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
    int32_t den = dot3(t, seg);
    int32_t rel[3] = {CI32(c, 0x2d0) - b[0], CI32(c, 0x2d4) - b[1], CI32(c, 0x2d8) - b[2]};
    int32_t num = dot3(t, rel);
    r->frac = den ? (int32_t)(((int64_t)num << 16) / den) : 0;
}

void Car_Progress(Car *c)
{
    if (!finish) return;
    int n = CI8(c, 0xb1a);
    Rec *r = &rec[n];
    /* a human's car searches 25 points a step for the nearest, and re-syncs to it every 26 steps */
    if (n < (int)Gs_SelBits14() && !Gs_SelBit25() && !Gs_SelBit24()) {
        for (int k = 0; k < 25; k++) {
            int32_t p[3];
            Cat_Point(cursor[n], p);
            int32_t d[3] = {fx_mul(p[0] - CI32(c, 0x2d0), 0x28f), 0, fx_mul(p[2] - CI32(c, 0x2d8), 0x28f)};
            uint32_t dd = (uint32_t)dot3(d, d);
            if (dd <= best_d[n] || cursor[n] == best_idx[n]) best_d[n] = dd, best_idx[n] = cursor[n];
            cursor[n] = (uint16_t)(cursor[n] + 1);
            if (cursor[n] == (uint16_t)npoints) cursor[n] = 0;
        }
        if (++since[n] > 25) {
            r->idx = best_idx[n];
            since[n] = 0;
        }
    }
    int idx = (int)r->idx, cur = idx;
    r->idx16 = (uint16_t)idx;
    int32_t d[3], t[3];
    dir_to(c, idx, d);
    Cat_Tangent(t, idx);
    if (dot3(d, t) < 0) {
        /* the point is behind the car: on to the next */
        if (circuit) {
            if ((uint32_t)idx < (uint32_t)finish - 1) r->idx++, r->idx16++, r->passed++;
            else r->idx = 0, r->idx16 = 0, r->passed++;
        } else if ((uint32_t)idx < (uint32_t)npoints - 1) {
            r->idx++, r->idx16++, r->passed++;
        } else {
            r->idx16++;
            r->passed = (uint32_t)finish + 1;
        }
        r->dist = 0;
        if (n < (int)Gs_SelBits14() && !Gs_SelBit27()) since[n] = 0;
        segment_frac(c, r);
        return;
    }
    /* else back a point if the previous one is ahead of the car */
    int prev;
    if (circuit) prev = (idx ? idx : finish) - 1;
    else if (idx) prev = idx - 1;
    else return;
    dir_to(c, prev, d);
    Cat_Tangent(t, prev);
    if (dot3(d, t) <= 0) return;
    r->idx = (uint32_t)prev;
    r->idx16 = (uint16_t)prev;
    if (!circuit && prev == finish - 2) r->passed = (uint32_t)prev;
    else r->passed--;
    int32_t a[3], b[3];
    Cat_Point(cur, a);
    Cat_Point(prev, b);
    int32_t s[3] = {a[0] - b[0], 0, a[2] - b[2]};
    if (iabs(s[0]) > 0x640000 || iabs(s[2]) > 0x640000) s[0] /= 512, s[2] /= 512;
    r->dist += Fx_Sqrt((uint32_t)fx_mul(s[0], s[0]) + (uint32_t)fx_mul(s[2], s[2]));
    segment_frac(c, r);
}

int32_t Car_StageShare(const Car *c)
{
    if (!finish) return 0;
    int32_t p = (int32_t)rec[CI8(c, 0xb1a)].passed;
    int32_t end = circuit ? (int32_t)(gm32(0x52f2ac) >> 0x10 & 0xf) * finish : finish;
    if (end <= p) return 0x10000;
    int32_t s = (p << 16) / (total >> 16);
    return s < 0 ? 0 : s > 0x10000 ? 0x10000 : s;
}

int Car_PointsPassed(const Car *c) { return (int)rec[CI8(c, 0xb1a)].passed; }

int Car_CatIndex(const Car *c) { return finish ? (int)rec[CI8(c, 0xb1a)].idx : 0; }
const uint8_t *Cat_Record(int i) { return finish ? cat + (size_t)i * CAT_REC : NULL; }
