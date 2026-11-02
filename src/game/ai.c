/* The computer drivers (see ai.h), ported from 0x47bda0-0x47d330 and 0x498590-0x498d2c. */
#include "game/ai.h"
#include "bfl.h"
#include "crt.h"
#include "fixmath.h"
#include "game/car_load.h"
#include "game/gstate.h"
#include "game/mem.h"
#include "game/timer.h"
#include "pending.h"
#include "platform.h"
#include "sim/drive.h"
#include "sim/progress.h"
#include "world/stage.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The file (one of .ai0-.ai2, copied: the original copies it into the pool at 0x541f8c):
     +0      six ints: each car's set (0: no driver)
     +24     the set count A (its low byte), A sets of 0x88 bytes: +0 the state mask, +4 up to 16
             entries of 8 bytes (+0 a section id, +2 the map, +3 the racing line), +0x84 the entry count
     then    the map count, maps of 0x14 bytes: five (output, net) byte pairs, one per control
     then    the line count, 16-byte records (the lines' names)
     then    the net count, nets of 0x20 bytes: +1.. the inputs' state codes, +8 the layer count,
             +0xc.. the layers' sizes (+0x14.. the weight pointers, set by the loader)
     then    each net's weights, (inputs + 1) x outputs ints per layer
     then    64 ints: the squashing curve (0 to 1.0 in 1/16 steps of the sum)
     then    0x10 bytes per centre-line point (370): +0 a byte for 0x47d0e0, +4 the section id, +8-+0xe
             the verges' margins in units
     then    per line 4 x 370 ints: the heading, the speed, x and z (relative to point 0, made absolute
             by 0x498590) */
enum { PTS = 370, LINE = 4 * PTS * 4, MAXN = 64 };
static uint8_t *buf;                 /* DAT_0058e378 */
static uint32_t blen;
static uint32_t set_off[MAXN];       /* DAT_0058e394[1..] */
static uint32_t map_off[MAXN];       /* DAT_0058e3ac[1..] */
static uint32_t net_off[MAXN];       /* DAT_0058e44c[1..] */
static uint32_t wt_off[MAXN][4];     /* the nets' +0x14.. */
static int nsets, nmaps, nlines, nnets;   /* DAT_0058e49d, _49e, _49c, _49f */
static uint32_t sig_off;             /* DAT_0058e4a0 */
static uint32_t pts_off;             /* DAT_0058e4a4 */
static uint32_t line_off;            /* DAT_00592748 (the first) */

static int32_t act[5 * 12];          /* DAT_0058e088; the layers from act[12] (0x58e0b8), 12 each */
static int32_t st[42];               /* DAT_0058e178: the car's state, by index */
static uint8_t ncars, nflank;        /* DAT_0058e0b0, _0b1 */
static int8_t recovery;              /* DAT_0058e21c */
static uint8_t flank[6];             /* DAT_0058e21d */
static uint8_t point_byte[6];        /* DAT_0058e223 */
static int8_t side[6];               /* DAT_0058e229 */
static int32_t edge[6];              /* DAT_0058e230 */
static uint32_t seed;                /* DAT_0058e26c */
static uint8_t chase[6];             /* DAT_0058e270 */

static int32_t rd32(uint32_t o) { return o + 4 <= blen ? (int32_t)(buf[o] | buf[o + 1] << 8 | buf[o + 2] << 16 | (uint32_t)buf[o + 3] << 24) : 0; }
static void wr32(uint32_t o, int32_t v)
{
    if (o + 4 > blen) return;
    for (int i = 0; i < 4; i++) buf[o + i] = (uint8_t)((uint32_t)v >> 8 * i);
}
static int8_t rd8(uint32_t o) { return o < blen ? (int8_t)buf[o] : 0; }
static int32_t pt8(int i, int k) { return rd8(pts_off + 16u * (uint32_t)i + (uint32_t)k); }   /* a point's byte k */
/* line l (1-based)'s array a (0 heading, 1 speed, 2 x, 3 z) at point i */
static uint32_t line_at(int l, int a, int i) { return line_off + (uint32_t)(l - 1) * LINE + (uint32_t)a * PTS * 4 + 4u * (uint32_t)i; }
static int32_t line(int l, int a, int i) { return rd32(line_at(l, a, i)); }
static int32_t fdiv(int32_t a, int32_t b) { return (int32_t)(((int64_t)a << 16) / b); }
static int32_t heading(const Car *c) { return Fx_Atan2Deg(CI32(c, 0x368), CI32(c, 0x360)); }

bool Ai_Loaded(void) { return buf != NULL; }
int32_t Ai_EdgeDistance(int n) { return n >= 0 && n < 6 ? edge[n] : 0; }

/* 0x47c2f0: the difficulty's file (selection bit 24 only), its tables found; 0x498590 turns the
   lines' x and z (up to the finish point) to the world by point 0 */
void Ai_LoadLines(const char *stage)
{
    free(buf);
    buf = NULL;
    blen = 0;
    if (!Gs_SelBit24()) return;
    char p[48];
    snprintf(p, sizeof p, "%s.ai%u", stage, Gs_Mode2());
    uint32_t sz = 0;
    const uint8_t *f = Bfl_FindFile(&g_stage_bfl, p, NULL, &sz, false);
    if (!f || sz < 28) {
        char m[64];
        snprintf(m, sizeof m, "ai: no %s", p);
        plat_log(m);
        return;
    }
    buf = malloc(sz);
    memcpy(buf, f, sz);
    blen = sz;
    uint32_t o = 24;
    nsets = rd8(o), o += 4;
    for (int i = 1; i <= nsets && i < MAXN; i++, o += 0x88) set_off[i] = o;
    nmaps = rd8(o), o += 4;
    for (int i = 1; i <= nmaps && i < MAXN; i++, o += 0x14) map_off[i] = o;
    nlines = rd8(o), o += 4;
    o += 0x10u * (uint32_t)(nlines > 0 ? nlines : 0);
    nnets = rd8(o), o += 4;
    for (int i = 1; i <= nnets && i < MAXN; i++, o += 0x20) net_off[i] = o;
    for (int i = 1; i <= nnets && i < MAXN; i++) {
        int L = rd32(net_off[i] + 8);
        for (int j = 0; j + 1 < L && j < 4; j++) {
            wt_off[i][j] = o;
            o += (uint32_t)((rd8(net_off[i] + 0xc + (uint32_t)j) + 1) * rd8(net_off[i] + 0xd + (uint32_t)j) * 4);
        }
    }
    sig_off = o;
    pts_off = o + 0x100;
    line_off = pts_off + 0x1720;
    int32_t p0[3];
    Cat_Point(0, p0);
    int fin = Cat_FinishPoint();
    for (int l = 1; l <= nlines; l++)   /* 0x498590 */
        for (int j = 0; j < fin; j++) {
            wr32(line_at(l, 2, j), line(l, 2, j) + p0[0]);
            wr32(line_at(l, 3, j), line(l, 3, j) + p0[2]);
        }
}

/* 0x47c5c0: the layers' bias inputs */
static void bias(void)
{
    for (int k = 12; k < 60; k += 12) act[k] = 0x10000;
}

/* 0x47c1b0: the CPU cars (from the player count; every car in the demo) without a pad (0x53acc8
   -1): the port tells CPU cars by their index */

/* 0x47c1e0: rand() seeded from the clock (0 in the demo) unless `go` with `keep`; per racing car a draw:
   below 0x4ccd the CPU car's revs (+0x7a4) at its maximum (+0x794) times 1.0 less (the difficulty's
   figure mod 10)/65536, else none; at the go launched (Car_Launch 0x43f570) */
static void start_revs(bool go, bool keep)
{
    if (!go || keep) seed = Timer_Now();
    if (Gs_Byte52ea52()) seed = 0;
    crt_srand(seed);
    crt_rand();
    int n = Cars_Racing();
    for (int i = 0; i < n; i++) {
        int r = crt_rand(), v;
        switch (Gs_Mode2()) {
        case 0: v = r * 2; break;
        case 1: v = r + 0x8000; break;
        case 2: v = i < 3 ? 0x10000 : r + 0x8000; break;
        default: v = 0; break;
        }
        int32_t f = r < 0x4ccd ? 0x10000 - v % 10 : 0;
        if (i >= (int)Gs_SelBits14()) {
            Car *c = Car_Get(i);
            CW32(c, 0x7a4, fx_mul(f, CI32(c, 0x794)));
            if (go && !keep) Car_Launch(c);
        }
    }
}

/* 0x47bda0 */
void Ai_Init(void)
{
    bias();
    start_revs(false, false);
    ncars = (uint8_t)Cars_Racing();   /* 0x47cc30 */
    nflank = 1;
}

void Ai_Go(bool restart) { start_revs(true, restart); }   /* 0x47bdc0 */

/* 0x498620: the state for car c (st[0x15] its centre-line point i) on racing line l, the entries
   picked by `mask`: 0x13 the line's step into i, 0x14 the angle to point i, 0x17 the bend to i + 5
   and 4 the heading always; by bit: 0 the heading against point i + 5, 1 against i + 2, 3 +0x870,
   4 the speed less the line's at i + 1, 6 the speed, 8 the offset across the line, 9 the heading
   against the centre line, 10 the slide, 11 +0xb10, 12 the speed less the line's, 14 the heading
   against the line's (eased to the centre line's above 60 when braking for it), 0x12 against the
   line's step, 0x14 against the line's i + 2. */
static void state(const Car *c, uint32_t mask, int32_t *s, int l)
{
    int n = Cat_Points(), i = s[0x15];
    int pt[6] = {i - 1, i, i + 5, i + 2, i + 2, i + 1};
    for (int k = 0; k < 6; k++) {
        if (pt[k] - n >= 0) pt[k] -= n;
        if (pt[k] < 0) pt[k] = n - 1;
    }
    int32_t hdg = heading(c);
    s[4] = hdg;
    int32_t slide = Fx_Atan2Deg(CI32(c, 0x410), CI32(c, 0x408));
    int32_t x = CI32(c, 0x2d0), z = CI32(c, 0x2d8);
    int32_t P5[3], Q[3], Pi[3], Pm[3];
    int32_t a5 = 0, a2 = 0, lc = 0;   /* lc: never set without a line (the original's stack) */
    Cat_Point(pt[2], P5);
    if (mask & 1) a5 = Fx_Atan2Deg(P5[2] - z, P5[0] - x);
    if (l) lc = Fx_Atan2Deg(line(l, 3, pt[4]) - z, line(l, 2, pt[4]) - x);
    if (mask & 2) {
        Cat_Point(pt[3], Q);
        a2 = Fx_Atan2Deg(Q[2] - z, Q[0] - x);
    }
    /* the speed (+0x778) in whole units: (v << 16) / 2^26, times 65536 */
    int32_t sp = (int32_t)(((int64_t)CI32(c, 0x778) << 16) / 0x4000000);
    int32_t speed = (int32_t)((uint32_t)sp << 16);
    Cat_Point(pt[1], Pi);
    Cat_Point(pt[0], Pm);
    int32_t seg = Fx_Atan2Deg(Pi[2] - Pm[2], Pi[0] - Pm[0]);
    s[0x13] = Fx_WrapDeg(seg);
    s[0x14] = Fx_Atan2Deg(Pi[2] - z, Pi[0] - x);
    uint32_t dist = Fx_Sqrt((uint32_t)fx_mul(z - Pi[2], z - Pi[2]) + (uint32_t)fx_mul(x - Pi[0], x - Pi[0]));
    /* the line's step into i; without a line the high half of the speed's 64-bit store */
    int32_t ldir = l ? Fx_Atan2Deg(line(l, 3, pt[1]) - line(l, 3, pt[0]), line(l, 2, pt[1]) - line(l, 2, pt[0]))
                     : (speed < 0 ? -1 : 0);
    s[0x17] = Fx_WrapDeg(seg - Fx_Atan2Deg(P5[2] - Pi[2], P5[0] - Pi[0]));
    /* the line entries with no line read before the lines (0x592744); taken as 0 here */
    for (int b = 0; b < 32; b++) {
        if (!(mask & 1u << b)) continue;
        switch (b) {
        case 0: s[6] = Fx_WrapDeg(hdg - a5); break;
        case 1: s[7] = Fx_WrapDeg(hdg - a2); break;
        case 3: s[3] = CI32(c, 0x870); break;
        case 4: s[0x28] = speed - (l ? line(l, 1, pt[5]) : 0); break;
        case 6: s[0] = speed; break;
        case 8: s[0xd] = fx_mul((int32_t)dist, g_sin[Fx_DegIndex(Fx_WrapDeg(seg - s[0x14]))]); break;
        case 9: s[0xe] = Fx_WrapDeg(hdg - seg); break;
        case 10: s[1] = Fx_WrapDeg(hdg - slide); break;
        case 11: s[2] = CI16(c, 0xb10); break;
        case 12: s[0x18] = speed - (l ? line(l, 1, pt[1]) : 0); break;
        case 14: {
            int32_t la = l ? line(l, 0, pt[1]) : 0, ls = l ? line(l, 1, pt[1]) : 0, t = la;
            if (ls > 0x3c0000) {
                if (speed < 0x280000) t = seg;
                else if (ls - speed > 0) {
                    int32_t u = fx_mul(ls - 0x280000, 0xb333);
                    int32_t v = u - (ls - 0x280000) + (ls - speed);
                    if (v > 0) t = la - fx_mul(fdiv(v, u), Fx_WrapDeg(la - seg));
                }
            }
            s[0x19] = Fx_WrapDeg(hdg - t);
            break;
        }
        case 0x12: s[0x1e] = Fx_WrapDeg(hdg - ldir); break;
        case 0x14: s[0x20] = Fx_WrapDeg(hdg - lc); break;
        }
    }
}

/* 0x498ca0: the net's inputs (layer 0 from 1) from the state by code */
static void inputs(uint32_t net)
{
    int n = rd8(net + 0xc);
    for (int i = 1; i <= n && 12 + i < 24; i++) {
        int32_t v;
        switch ((uint8_t)rd8(net + (uint32_t)i)) {
        case 0: v = st[6]; break;
        case 1: v = st[7]; break;
        case 3: v = st[3]; break;
        case 4: v = st[0x28]; break;
        case 6: v = st[0]; break;
        case 8: v = st[0xd]; break;
        case 9: v = st[0xe]; break;
        case 10: v = st[1]; break;
        case 11: v = st[2]; break;
        case 12: v = st[0x18]; break;
        case 14: v = st[0x19]; break;
        case 0x12: v = st[0x1e]; break;
        case 0x14: v = st[0x20]; break;
        default:
            plat_log("ai: an unknown input code");   /* the original exits */
            continue;
        }
        act[12 + i] = v;
    }
}

/* 0x47ca30: each layer's nodes (from 1) as the weighted sum of the one before (bias first; zero
   weights skipped) put through the squashing curve: the sum in 1/16ths, mirrored below zero, +-1.0
   from 4.0 (the last layer's plain sum is never taken: its test is one off) */
static void layers(int nn)
{
    uint32_t net = net_off[nn];
    int L = rd32(net + 8);
    for (int j = 0; j + 1 < L && j < 4; j++) {
        int nin = rd8(net + 0xc + (uint32_t)j), nout = rd8(net + 0xd + (uint32_t)j);
        uint32_t w = wt_off[nn][j];
        for (int o = 1; o <= nout; o++) {
            int32_t sum = 0;
            for (int i = 0; i <= nin; i++, w += 4) {
                int32_t k = rd32(w);
                if (k && 12 + 12 * j + i < 60) sum += fx_mul(act[12 + 12 * j + i], k);
            }
            int at = 12 + 12 * (j + 1) + o;
            if (at >= 60) continue;
            if (j + 1 > L - 1) {
                act[at] = sum;
                continue;
            }
            int32_t q = (int32_t)(((int64_t)sum << 16) / 0x10000000);
            if (q < 0) act[at] = -q < 64 ? -rd32(sig_off + 4u * (uint32_t)-q) : (int32_t)0xffff0000;
            else act[at] = q < 64 ? rd32(sig_off + 4u * (uint32_t)q) : 0x10000;
        }
    }
}

/* 0x47c9a0: the map's five controls, each a net's output (each net run once) */
static void run_map(int m, int32_t out[5])
{
    if (m < 1 || m >= MAXN || m > nmaps) return;
    uint32_t done = 0;
    for (int j = 0; j < 5; j++) {
        int o = rd8(map_off[m] + 4u * (uint32_t)j), nn = rd8(map_off[m] + 4u * (uint32_t)j + 1);
        if (!nn || nn < 0 || nn >= MAXN) continue;
        if (!(done & 1u << (nn & 31))) {
            done |= 1u << (nn & 31);
            inputs(net_off[nn]);
            layers(nn);
        }
        int at = o + rd32(net_off[nn] + 8) * 12;
        if (at >= 0 && at < 60) out[j] = act[at];
    }
}

/* 0x47cbc0: the set's entry for the point's section: the racing line and the map */
static void pick(int set, int i, int *l, int *m)
{
    if (set < 1 || set >= MAXN || set > nsets) return;
    uint32_t s = set_off[set];
    int32_t sec = rd32(pts_off + 16u * (uint32_t)i + 4);
    for (int e = 0; e < rd32(s + 0x84); e++)
        if (sec == rd8(s + 4 + 8u * (uint32_t)e)) {
            *l = rd8(s + 7 + 8u * (uint32_t)e);
            *m = rd8(s + 6 + 8u * (uint32_t)e);
            return;
        }
}

/* 0x47c5e0: off the line: a verge near (by the point's margins at +8-+0xe, the side by the offset)
   or the car turned across the road, a fixed reply: 1 right + throttle, 2 right + brake, 3 left +
   throttle, 4 left + brake, 5 throttle, 6 brake; 0 to drive by the nets */
static int recover(const int32_t *s)
{
    int32_t lat = s[0xd], hd = s[0xe];
    int i = s[0x15];
    int bend = s[0x17] < -0xa0000 ? 1 : s[0x17] > 0xa0000 ? 2 : 0;
    if (lat < 0) {
        int32_t d8 = lat + (pt8(i, 0xd) << 16), d = lat + (pt8(i, 0xe) << 16), near = 0;
        if (d < 0x40000 && pt8(i, 0xe) < 0x11) near = d;
        if (d8 > 0 && d8 < 0x40000) near = d8;
        if (near > 0) {
            if (hd > 0x5a0000) return 1;
            if (hd - fx_mul(-near, 0xf0000) + 0x3c0000 < 0) return 2;
        }
        if (d8 < 0) {
            if (hd < 1) return hd < -0x6e0000 ? 2 : hd < -0x45ffff ? 6 : 4;
            return hd > 0x6e0000 ? 1 : hd > 0x45ffff ? 5 : 3;
        }
        int32_t e = lat + (pt8(i, 0xc) << 16);
        if (e < 0 && bend != 2) {
            if (hd > 0x3c0000) return 1;
            if (hd < -0x2d0000) return 2;
            if (hd - fx_mul(-e, 0x50000) < 0) return 3;
        }
    } else {
        int32_t d4 = (pt8(i, 9) << 16) - lat, d = (pt8(i, 10) << 16) - lat, near = 0;
        if (d < 0x40000 && pt8(i, 10) < 0x11) near = d;
        if (d4 > 0 && d4 < 0x40000) near = d4;
        if (near > 0) {
            if (hd < -0x5a0000) return 3;
            if (hd - fx_mul(near, 0xf0000) > 0x3c0000) return 4;
        }
        if (d4 < 0) {
            if (hd < 1) return hd < -0x6e0000 ? 3 : hd < -0x45ffff ? 5 : 1;
            return hd > 0x6e0000 ? 4 : hd > 0x45ffff ? 6 : 2;
        }
        int32_t e = (pt8(i, 8) << 16) - lat;
        if (e < 0 && bend != 1) {
            if (hd > 0x2d0000) return 4;
            if (hd < -0x3c0000) return 3;
            if (hd - fx_mul(e, 0x50000) > 0) return 1;
        }
    }
    if (hd > 0x780000) return 4;
    return hd < -0x780000 ? 2 : 0;
}

/* 0x47cd10: a slow car (+0x778 under 0xc800) of the race up to 5 points ahead (10 when it faces
   the other way) within 45 degrees and the clear width (the distance less 7, over 7, plus 2,
   0..5) across: pass it on the side away from the bend (1 / 2; 3 / 4 above 50 with the brake).
   Returns the car or -1. */
static int overtake(int n, int *out, int i, const int32_t *s)
{
    *out = 0;
    int cand[6] = {0}, reach[6] = {0};
    Car *cars[6];
    for (int k = 0; k < 6; k++) cars[k] = Car_Get(k);
    int fin = Cat_FinishPoint(), N = (int8_t)ncars;
    int32_t x = CI32(cars[n], 0x2d0), z = CI32(cars[n], 0x2d8), hdg = s[4];
    for (int k = 0; k < N && k < 6; k++)
        if (k != n && CI32(cars[k], 0x778) < 0xc800) cand[k] = 1;
    for (int k = 0; k < N && k < 6; k++)
        if (cand[k]) {
            int32_t r = Fx_WrapDeg(hdg - heading(cars[k]));
            reach[k] = r < -0x5a0000 || r > 0x5a0000 ? 10 : 5;
        }
    for (int k = 0; k < N && k < 6; k++)
        if (cand[k]) {
            int d = Car_CatIndex(cars[k]) - i;
            if (d < -100) d += fin;
            if (d < 0 || reach[k] < d) cand[k] = 0;
        }
    for (int k = 0; k < N && k < 6; k++) {
        if (!cand[k]) continue;
        int32_t dz = CI32(cars[k], 0x2d8) - z, dx = CI32(cars[k], 0x2d0) - x;
        int32_t d = (int32_t)Fx_Sqrt((uint32_t)fx_mul(dx, dx) + (uint32_t)fx_mul(dz, dz));
        int32_t ang = Fx_Atan2Deg(dz, dx), rel = Fx_WrapDeg(hdg - ang);
        cand[k] = 0;
        if (rel > 0x2d0000 || rel < -0x2d0000) continue;
        int32_t w = fdiv(d - 0x70000, 0x70000) + 0x20000;
        if (w > 0x50000) w = 0x50000;
        else if (w < 0) w = 0;
        int32_t across = fx_mul(d, g_sin[Fx_DegIndex(rel)]);
        int ahead = i + 5;
        if (ahead - fin >= 0) ahead -= fin;
        int32_t P[3];
        Cat_Point(ahead, P);
        int32_t road = Fx_Atan2Deg(P[2] - z, P[0] - x);
        if (across > w || across < -w) continue;
        if (Fx_WrapDeg(ang - road) < 0) *out = s[0] > 0x320000 ? 4 : 2;
        else *out = s[0] > 0x320000 ? 3 : 1;
        return k;
    }
    return -1;
}

/* 0x47d0e0 (option 3): the first car (DAT_0058e0b1's one) 1-5 points ahead, within 3 units and 20
   degrees of the heading, alongside (80-120 degrees off): the side to give way to (+-1). Returns the
   point's byte +0. */
static uint8_t alongside(int n, uint8_t *flags, int *count, int8_t *sd)
{
    int cand[6] = {1, 1, 1, 1, 1, 1};
    Car *cars[6];
    for (int k = 0; k < 6; k++) *sd = 0, cars[k] = Car_Get(k);
    int fin = Cat_FinishPoint(), N = nflank;
    cand[n] = 0;
    int32_t x = CI32(cars[n], 0x2d0), z = CI32(cars[n], 0x2d8);
    *count = 0;
    int me = Car_CatIndex(cars[n]);
    for (int k = 0; k < N && k < 6; k++)
        if (cand[k]) {
            int d = Car_CatIndex(cars[k]) - me;
            if (d < 0 || d > 5) {
                if (d < -100) d += fin;
                if (d < 1 || d > 5) cand[k] = 0;
            }
        }
    for (int k = 0; k < N && k < 6; k++)
        if (cand[k]) {
            int32_t dz = CI32(cars[k], 0x2d8) - z, dx = CI32(cars[k], 0x2d0) - x;
            if (fx_mul(dx, dx) + fx_mul(dz, dz) > 0x90000) cand[k] = 0;
        }
    int32_t hdg = heading(cars[n]);
    for (int k = 0; k < N && k < 6; k++)
        if (cand[k]) {
            int32_t r = Fx_WrapDeg(hdg - heading(cars[k]));
            if (r > 0x140000 || r < -0x140000) cand[k] = 0;
        }
    for (int k = 0; k < N && k < 6; k++)
        if (cand[k]) {
            int32_t a = Fx_WrapDeg(Fx_Atan2Deg(CI32(cars[k], 0x2d8) - z, CI32(cars[k], 0x2d0) - x) - hdg);
            cand[k] = 0;
            if (a > 0x500000 && a < 0x780000) *sd = 1;
            if (a < -0x500000 && a > -0x780000) *sd = -1;
        }
    for (int k = 0; k < N && k < 6; k++) flags[k] = (uint8_t)cand[k], *count += cand[k];
    return (uint8_t)pt8(me, 0);
}

/* 0x47cc50 (option 0): car 0 within 0-7 points ahead and 10 degrees of the heading */
static void aim(int n, int i)
{
    chase[n] = 1;
    Car *p = Car_Get(0), *c = Car_Get(n);
    int32_t hdg = heading(c);
    int d = Car_CatIndex(p) - i;
    if (d < -100) d += Cat_FinishPoint();
    if (d >= 0 && d < 8) {
        int32_t r = Fx_WrapDeg(hdg - Fx_Atan2Deg(CI32(p, 0x2d8) - CI32(c, 0x2d8), CI32(p, 0x2d0) - CI32(c, 0x2d0)));
        if (r <= 0xa0000 && r >= -0xa0000) return;
    }
    chase[n] = 0;
}

void Ai_Drive(Car *c, int n, bool peek)
{
    if (!buf || n < 0 || n > 5) return;
    int32_t out[5] = {0};   /* +0x1d0, +0x1d1, the throttle, the brake, the handbrake */
    int l = n, m = 0;
    recovery = 0;
    point_byte[n] = 0;
    int set = rd32(4u * (uint32_t)n);
    if (!set) return;
    int i = Car_CatIndex(c);
    st[0x15] = i;
    pick(set, i, &l, &m);
    if (set >= MAXN || set > nsets) return;
    uint32_t mask = (uint32_t)rd32(set_off[set]) | 0xfffffffc;   /* written back, as the original */
    wr32(set_off[set], (int32_t)mask);
    state(c, mask, st, l);
    int32_t left = pt8(i, 10) << 16, right = -(pt8(i, 0xe) << 16);
    edge[n] = left - st[0xd] < st[0xd] - right ? left - st[0xd] : st[0xd] - right;
    recovery = (int8_t)recover(st);
    uint32_t sel = gm32(0x52f2ac);
    if ((sel >> 10 & 3) == 2 && (sel >> 12 & 3) == 1 && i > 0xdd && i < 0xe4) recovery = 0;
    int ov = 0;
    overtake(n, &ov, i, st);
    if (Gs_Option(3)) {
        int cnt;
        point_byte[n] = alongside(n, flank, &cnt, &side[n]);
    }
    if (Gs_Option(0)) aim(n, i);
    if (recovery) {
        static const uint8_t reply[9][4] = {
            {0}, {0, 1, 1, 0}, {0, 1, 0, 1}, {1, 0, 1, 0}, {1, 0, 0, 1}, {0, 0, 1, 0}, {0, 0, 0, 1}, {0, 1, 0, 0}, {1, 0, 0, 0},
        };
        if (recovery > 0 && recovery <= 8)
            for (int k = 0; k < 4; k++) out[k] = reply[recovery][k] ? 0x3f : 0;
        if ((st[0xe] > 0x2d0000 || st[0xe] < -0x2d0000) && st[0] > 0x18ffff) out[2] = out[3] = 0;
    } else {
        run_map(m, out);
        switch (ov) {
        case 1: out[0] = 0, out[1] = 0x3f; break;
        case 2: out[0] = 0x3f, out[1] = 0; break;
        case 3: out[0] = 0, out[1] = 0x3f, out[3] = 0x3f; break;
        case 4: out[1] = 0, out[0] = 0x3f, out[3] = 0x3f; break;
        }
        if (side[n] == -1) {
            if (st[0x1e] > -0x140000) out[0] = 0, out[1] = 0x3f;
        } else if (side[n] == 1 && st[0x1e] < 0x140000)
            out[0] = 0x3f, out[1] = 0;
        if (st[3] < -30000 && st[0] > 0xa0000) out[2] = 0;
        if (st[0x19] > -0xa0000 && st[2] < -0x7d) out[0] = 0;
        if (st[0x19] < 0xa0000 && st[2] > 0x7d) out[1] = 0;
    }
    int32_t hb = out[4];
    if (Gs_Option(0) && chase[n]) hb = 1;
    if (peek) return;
    c->s[0x1d0] = c->s[0x1d1] = c->s[0x1d2] = c->s[0x1d3] = 0;
    CW32(c, 0x1d8, 0);
    if (out[0] > 0) c->s[0x1d0] = 0x3f;
    if (out[1] > 0) c->s[0x1d1] = 0x3f;
    if (out[2] > 0) c->s[0x1d2] = 0x3f;
    if (out[3] > 0) c->s[0x1d3] = 0x3f;
    if (hb > 0) CW32(c, 0x1d8, 1);
}

/* 0x47d330: after the finish (selection bit 26) a player's car driven on: none within five points of
   the end; from the state (bits 1, 6, 8, 9, no line) the throttle below 50, the brake above 70, the
   steering toward point i + 2 beyond 5 degrees, and turned across (beyond 90) the brake with the
   steering away (no throttle above 25) */
bool Ai_AutoDrive(int n, bool peek)
{
    Car *c = Car_Get(n);
    int i = Car_CatIndex16(c);
    if ((uint32_t)Cat_Points() < (uint32_t)(i + 5)) return false;
    int32_t s[42] = {0};
    s[0x15] = i;
    state(c, 0x342, s, 0);
    int l = 0, r = 0, thr = 0, brk = 0;
    if (s[0] < 0x320000) thr = 1;
    if (s[0] > 0x460000) brk = 1;
    if (s[7] > 0x50000) r = 1;
    if (s[7] < -0x50000) l = 1;
    int turn = 0;
    if (s[0xe] > 0x5a0000) turn = 4;
    if (s[0xe] < -0x5a0000) turn = 2;
    if (turn) {
        if (turn == 2) brk = 1, r = 1;
        else l = 1, brk = 1;
        if (s[0] > 0x18ffff) thr = 0;
    }
    if (!peek) {
        c->s[0x1d0] = c->s[0x1d1] = c->s[0x1d2] = c->s[0x1d3] = 0;
        CW32(c, 0x1d8, 0);
        if (l) c->s[0x1d0] = 0x3f;
        if (r) c->s[0x1d1] = 0x3f;
        if (thr) c->s[0x1d2] = 0x3f;
        if (brk) c->s[0x1d3] = 0x3f;
    }
    return true;
}
