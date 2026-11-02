/* The car-to-car contacts (see contact.h). The original's work globals are 0x5913dc-0x5914d0 (the
   contact lists and the group push) and 0x5915e0/0x591394 (the two boxes), 0x5915e8 (the normal). */
#include "sim/contact.h"
#include "sim/walls.h"
#include "sim/scenery.h"
#include "sim/damage.h"
#include "game/effects.h"
#include "game/balls.h"
#include "game/gstate.h"
#include "fixmath.h"
#include "pending.h"
#include <string.h>

static uint8_t ncont[CAR_MAX];             /* 0x5913dc: the cars each car touches this step */
static uint8_t cont[CAR_MAX][8];           /* 0x5913f8 */
static bool touching;                      /* DAT_00591390: the last pair whose boxes were tested touched */
bool Cars_Touching(void) { return touching; }   /* 0x487130 */
static struct {
    int32_t v[3];                          /* 0x5914c8 */
    bool corners;                          /* DAT_005913d8 */
    int32_t done[8];                       /* 0x59146c */
} G;

static int32_t iabs(int32_t v) { return v < 0 ? -v : v; }
static int32_t dot3(const int32_t a[3], const int32_t b[3]) { return fx_mul(a[2], b[2]) + fx_mul(a[0], b[0]) + fx_mul(a[1], b[1]); }
static void cross(int32_t o[3], const int32_t a[3], const int32_t b[3])
{
    o[0] = fx_mul(a[1], b[2]) - fx_mul(a[2], b[1]);
    o[1] = fx_mul(a[2], b[0]) - fx_mul(a[0], b[2]);
    o[2] = fx_mul(a[0], b[1]) - fx_mul(a[1], b[0]);
}
static void car_pos(const Car *c, int32_t p[3]) { p[0] = CI32(c, 0x2d0), p[1] = CI32(c, 0x2d4), p[2] = CI32(c, 0x2d8); }

/* 0x4b9c20 with the orientation (+0x750): v times its 3 x 3 */
static void vmul(const Car *c, int32_t o[3], const int32_t v[3])
{
    for (uint32_t k = 0; k < 3; k++)
        o[k] = fx_mul(CI32(c, 32 + 4 * k), v[2]) + fx_mul(CI32(c, 16 + 4 * k), v[1]) + fx_mul(CI32(c, 4 * k), v[0]);
}

/* 0x48c900: a touching car moved once, then the cars it touches */
static void push_one(int n)
{
    if (n < 0 || n >= 8 || G.done[n]) return;
    G.done[n] = 1;
    Car *c = Car_Get(n);
    for (uint32_t k = 0; k < 3; k++) CW32(c, 0x2d0 + 4 * k, CI32(c, 0x2d0 + 4 * k) + G.v[k]);
    for (uint32_t i = 0; i < 8; i++)
        for (uint32_t k = 0; k < 3; k++) CW32(c, 0x270 + 12 * i + 4 * k, CI32(c, 0x270 + 12 * i + 4 * k) + G.v[k]);
    if (G.corners) {
        int32_t *b = Walls_BoxRec(n);
        if (b[B_VALID])
            for (int i = 0; i < 4; i++)
                for (int k = 0; k < 3; k++) b[B_CORNER + 3 * i + k] += G.v[k];
    }
    for (int i = 0; i < ncont[n]; i++) push_one(cont[n][i]);
}

void Cars_PushGroup(int n, int other, const int32_t v[3], bool corners)
{
    memcpy(G.v, v, sizeof G.v);
    G.corners = corners;
    memset(G.done, 0, sizeof G.done);
    if (n < 0 || n >= 8) return;
    G.done[n] = 1;
    if (other != -1 && other >= 0 && other < 8) G.done[other] = 1;
    for (int i = 0; i < ncont[n]; i++) push_one(cont[n][i]);
}

/* The car, its eight box corners and its flat box's four moved by m */
static void move_by(Car *c, int32_t *b, const int32_t m[3])
{
    for (uint32_t k = 0; k < 3; k++) CW32(c, 0x2d0 + 4 * k, CI32(c, 0x2d0 + 4 * k) + m[k]);
    for (uint32_t i = 0; i < 8; i++)
        for (uint32_t k = 0; k < 3; k++) CW32(c, 0x270 + 12 * i + 4 * k, CI32(c, 0x270 + 12 * i + 4 * k) + m[k]);
    for (int i = 0; i < 4; i++)
        for (int k = 0; k < 3; k++) b[B_CORNER + 3 * i + k] += m[k];
}

/* 0x487b80: the two spheres (the reaches summed) overlap */
static bool near(int32_t ra, int32_t rb, const int32_t a[3], const int32_t b[3])
{
    int32_t r = ra + rb, d[3] = {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
    if (iabs(d[0]) > r || iabs(d[1]) > r || iabs(d[2]) > r) return false;
    return fx_mul(d[2], d[2]) + fx_mul(d[0], d[0]) + fx_mul(d[1], d[1]) < fx_mul(r, r);
}

/* 0x48a5f0: the two flat boxes, each car taking half of each push (0x488640). The contact point is the
   mean of the listed corners (a's eight-corner points for a's corners in b's box, b's for b's in a's),
   relative to a; the normal the sum of the faces' axes met, each turned to agree with the sum so far,
   made unit. With corners one way only, each car's push is kept along the normal (and the cars
   touching it moved by n d, as for the scenery). The contact into each car's frame (+0x5dc). */
static bool contact(Car *a, int32_t *ba, Car *b, int32_t *bb, CollBoxes *w, int32_t n[3])
{
    if (!Scenery_BoxPair(a, ba, b, bb, 0x8000, w)) return false;
    int32_t sum[3] = {0, 0, 0}, ap[3], bp[3];
    n[0] = n[1] = n[2] = 0;
    car_pos(a, ap);
    for (int i = 0; i < w->ncar; i++) {
        uint32_t q = w->car_at[i];
        sum[0] += CI32(a, 0x270 + 12 * q) - ap[0];
        sum[2] += CI32(a, 0x278 + 12 * q) - ap[2];
        const int32_t *x = &bb[w->car_ax[i] == 0 ? B_FWD : B_SIDE];
        bool neg = dot3(x, n) < 0;
        for (int k = 0; k < 3; k++) n[k] += neg ? -x[k] : x[k];
    }
    for (int i = 0; i < w->nobj; i++) {
        uint32_t q = w->obj_at[i];
        sum[0] += CI32(b, 0x270 + 12 * q) - ap[0];
        sum[2] += CI32(b, 0x278 + 12 * q) - ap[2];
        const int32_t *x = &ba[w->obj_ax[i] == 0 ? B_FWD : B_SIDE];
        bool neg = dot3(x, n) < 0;
        for (int k = 0; k < 3; k++) n[k] += neg ? -x[k] : x[k];
    }
    int32_t r = (int32_t)(0x100000000LL / ((int32_t)(w->ncar + w->nobj) << 16));
    sum[0] = fx_mul(sum[0], r), sum[2] = fx_mul(sum[2], r);
    Vec_Unit(n);
    if (w->ncar == 0 || w->nobj == 0) {
        int32_t d = dot3(n, w->push), nd[3], m[3];
        for (int k = 0; k < 3; k++) nd[k] = fx_mul(n[k], d), m[k] = nd[k] - w->push[k];
        move_by(a, ba, m);
        Cars_PushGroup(CI8(a, 0xb1a), CI8(b, 0xb1a), nd, true);
        d = dot3(n, w->opush);
        for (int k = 0; k < 3; k++) nd[k] = fx_mul(n[k], d), m[k] = nd[k] - w->opush[k];
        move_by(b, bb, m);
        Cars_PushGroup(CI8(b, 0xb1a), CI8(a, 0xb1a), nd, true);
    }
    int32_t l[3];
    Walls_RotT(a, l, sum);
    for (uint32_t k = 0; k < 3; k++) CW32(a, 0x5dc + 4 * k, l[k]);
    car_pos(a, ap), car_pos(b, bp);
    for (int k = 0; k < 3; k++) sum[k] += ap[k] - bp[k];
    Walls_RotT(b, l, sum);
    for (uint32_t k = 0; k < 3; k++) CW32(b, 0x5dc + 4 * k, l[k]);
    return true;
}

/* One car's side of the response: the knock (the contact lowered by four units along the car's up
   column, the car loose, +0xc00, and +0x96c 1.0), the impulse I (world) into +0x5c4, its turn about the
   contact (twice, four times for a knock) into +0x5d0 */
static void take(Car *c, const int32_t imp[3], bool knock)
{
    int32_t l[3], t[3], p[3];
    Walls_RotT(c, l, imp);
    for (uint32_t k = 0; k < 3; k++) CW32(c, 0x5c4 + 4 * k, CI32(c, 0x5c4 + 4 * k) + imp[k]);
    for (uint32_t k = 0; k < 3; k++) p[k] = CI32(c, 0x5dc + 4 * k);
    cross(t, l, p);
    int32_t s = knock ? 0x40000 : 0x20000;
    if (knock) CW32(c, 0xc00, 1), CW32(c, 0x96c, 0x10000);
    for (uint32_t k = 0; k < 3; k++) CW32(c, 0x5d0 + 4 * k, CI32(c, 0x5d0 + 4 * k) + fx_mul(t[k], s));
    Car_ClampTurn(c);
}

/* The velocity of the car's contact point (+0x5dc): the spin (+0x420) across it, turned into the
   world, plus the velocity (+0x408) */
static void point_vel(const Car *c, int32_t v[3])
{
    int32_t s[3], p[3], w[3];
    for (uint32_t k = 0; k < 3; k++) s[k] = CI32(c, 0x420 + 4 * k), p[k] = CI32(c, 0x5dc + 4 * k);
    cross(w, s, p);
    vmul(c, v, w);
    for (uint32_t k = 0; k < 3; k++) v[k] += CI32(c, 0x408 + 4 * k);
}

static bool would_knock(const Car *c, const Car *a, const Car *b, int32_t j)
{
    return !CI32(c, 0xc00) && 0x9999 < iabs(j) && !(CI32(a, 0xb64) && CI32(b, 0xb64));
}

/* 0x48ae90: the response. Both contacts within the half sizes (+0x204, x and z; 0x48c6e0); the
   contact points' velocities along the normal give a common one by the masses (+0x75c): the
   restitution term is the difference times 0, so it is a plain inelastic hit. Each car's change
   (above 0.6 a knock, not when both have +0xb64) along the normal; a car that is not upright
   (+0xb35 not 1) passes a downward share to the other; times the impulse scale 0x519c90 into each.
   The damage at the first listed corner (kind 0 for the car whose corner it is, 2 for the other);
   with the option 4 the impulses are turned into velocity (twice, less the impulse). The sparks
   along the sliding direction when it is above 0.2, at most 1.0 (twice the speed), not while either
   is recovering (+0xb70). */
static void respond(Car *a, Car *b, const int32_t *ba, const int32_t *bb, const CollBoxes *w, const int32_t n[3])
{
    for (uint32_t k = 0; k < 3; k += 2) {
        Car *cs[2] = {a, b};
        for (int i = 0; i < 2; i++) {
            int32_t p = CI32(cs[i], 0x5dc + 4 * k), h = CI32(cs[i], 0x204 + 4 * k);
            if (h < iabs(p)) CW32(cs[i], 0x5dc + 4 * k, p < 0 ? -h : h);
        }
    }
    int32_t va[3], vb[3];
    point_vel(a, va);
    point_vel(b, vb);
    int32_t na = dot3(n, va), nb = dot3(n, vb), d = fx_mul(na - nb, 0);
    int32_t ma = CI32(a, 0x75c), mb = CI32(b, 0x75c);
    int32_t mom = fx_mul(nb, mb) - fx_mul(d, mb) + fx_mul(na, ma);
    int32_t u = (int32_t)(((int64_t)mom << 16) / (ma + mb));
    int32_t ja = u - na, jb = u + d - nb, ia[3], ib[3];
    for (int k = 0; k < 3; k++) ia[k] = fx_mul(n[k], ja);
    bool ka = would_knock(a, a, b, ja);
    if (ka)
        for (uint32_t k = 0; k < 3; k++) CW32(a, 0x5dc + 4 * k, CI32(a, 0x5dc + 4 * k) + fx_mul(-CI32(a, 0x364 + 12 * k), 0x40000));
    for (int k = 0; k < 3; k++) ib[k] = fx_mul(n[k], jb);
    bool kb = would_knock(b, a, b, jb);
    if (kb)
        for (uint32_t k = 0; k < 3; k++) CW32(b, 0x5dc + 4 * k, CI32(b, 0x5dc + 4 * k) + fx_mul(-CI32(b, 0x364 + 12 * k), 0x40000));
    if (CI8(a, 0xb35) != 1 && ia[1] < 0) ib[1] -= ia[1], ia[1] = 0;
    if (CI8(b, 0xb35) != 1 && ib[1] < 0) ia[1] -= ib[1], ib[1] = 0;
    int32_t K = gms32(0x519c90);
    for (int k = 0; k < 3; k++) ia[k] = fx_mul(ia[k], K);
    take(a, ia, ka);
    for (int k = 0; k < 3; k++) ib[k] = fx_mul(ib[k], K);
    take(b, ib, kb);
    if (w->ncar) {
        const int32_t *pt = &ba[B_CORNER + 3 * w->car_at[0]];
        Car_Damage(a, pt, n, 0, 0, false);
        Car_Damage(b, pt, n, 0, 2, false);
    } else if (w->nobj) {
        const int32_t *pt = &bb[B_CORNER + 3 * w->obj_at[0]];
        Car_Damage(a, pt, n, 0, 2, false);
        Car_Damage(b, pt, n, 0, 0, false);
    }
    int32_t rel[3], rows[9];
    for (int k = 0; k < 3; k++) rel[k] = va[k] - vb[k];
    int32_t rn = dot3(n, rel);
    for (int k = 0; k < 3; k++) rows[k] = rel[k] - fx_mul(n[k], rn);
    if (Gs_Option(4)) {
        Car *cs[2] = {a, b};
        const int32_t *is[2] = {ia, ib};
        for (int i = 0; i < 2; i++)
            for (uint32_t k = 0; k < 3; k++) {
                CW32(cs[i], 0x5c4 + 4 * k, CI32(cs[i], 0x5c4 + 4 * k) - is[i][k]);
                CW32(cs[i], 0x408 + 4 * k, CI32(cs[i], 0x408 + 4 * k) + 2 * is[i][k]);
            }
    }
    int32_t tl = (int32_t)Fx_Sqrt((uint32_t)fx_mul(rows[0], rows[0]) + (uint32_t)fx_mul(rows[1], rows[1]) + (uint32_t)fx_mul(rows[2], rows[2]));
    if (tl <= 0x3333) return;
    int32_t inv = (int32_t)(0x100000000LL / -tl);
    for (int k = 0; k < 3; k++) rows[k] = fx_mul(rows[k], inv);
    cross(&rows[3], &rows[0], n);
    Vec_Unit(&rows[3]);
    memcpy(&rows[6], n, 3 * sizeof *n);
    /* the points relative to each car, flat, at 0.8; with no corner listed (the edges' case) the
       original's locals still hold the two impulses, which it uses (kept) */
    int32_t pa[3], pb[3];
    const int32_t *pt = w->ncar ? &ba[B_CORNER + 3 * w->car_at[0]] : w->nobj ? &bb[B_CORNER + 3 * w->obj_at[0]] : NULL;
    if (pt) {
        int32_t ap[3], bp[3];
        car_pos(a, ap), car_pos(b, bp);
        for (int k = 0; k < 3; k++) pa[k] = pt[k] - ap[k], pb[k] = pt[k] - bp[k];
    } else
        memcpy(pa, ia, sizeof pa), memcpy(pb, ib, sizeof pb);
    pa[1] = 0, pb[1] = 0;
    for (int k = 0; k < 3; k++) pa[k] = fx_mul(pa[k], 0xcccc), pb[k] = fx_mul(pb[k], 0xcccc);
    int32_t s = fx_mul(tl, 0x20000);
    if (0x10000 < s) s = 0x10000;
    if (CI32(a, 0xb70) || CI32(b, 0xb70)) return;
    Sparks_Add(s, pa, a, rows, 0x90000, 0x6666);
    Sparks_Add(s, pb, b, rows, 0x90000, 0x6666);
}

void Cars_Collide(const int16_t *list, int n)
{
    int32_t touched[8] = {0};
    memset(ncont, 0, sizeof ncont);
    for (int i = 0; i < n - 1; i++)
        for (int j = i + 1; j < n; j++) {
            Car *a = Car_Get(list[i]), *b = Car_Get(list[j]);
            /* the first of b's four cells that is one of a's (an unset -1 matches one too) */
            bool shared = false;
            for (uint32_t y = 0; y < 4 && !shared; y++)
                for (uint32_t x = 0; x < 4 && !shared; x++)
                    shared = (int16_t)CI16(a, 0xb00 + 2 * x) == (int16_t)CI16(b, 0xb00 + 2 * y);
            if (!shared) continue;
            int32_t ap[3], bp[3];
            car_pos(a, ap), car_pos(b, bp);
            if (!near(CI32(a, 0x758), CI32(b, 0x758), ap, bp)) continue;
            int ia = CI8(a, 0xb1a), ib = CI8(b, 0xb1a);
            touched[ib & 7] = touched[ia & 7] = 1;
            if (CI32(a, 0xc08) || CI32(b, 0xc08) || CI32(a, 0xc0c) || CI32(b, 0xc0c)) continue;
            if (CI32(a, 0xc18) && CI32(b, 0xc18)) continue;
            int32_t *ba = Walls_CarBox(a), *bb = Walls_CarBox(b), nrm[3];
            CollBoxes w;
            bool hit = contact(a, ba, b, bb, &w, nrm);
            touching = false;
            if (!hit) continue;
            touching = true;
            if (ncont[ia] < 8) cont[ia][ncont[ia]++] = (uint8_t)ib;
            if (ncont[ib] < 8) cont[ib][ncont[ib]++] = (uint8_t)ia;
            respond(a, b, ba, bb, &w, nrm);
        }
    /* +0xc08 (a car passing through the others, as after a restart) lasts while +0x970 runs down and
       then until the car touches none; +0x974 counts its time at a quarter */
    int32_t dt = gms32(0x519c8c);
    for (int i = n - 1; i >= 0; i--) {
        Car *c = Car_Get(list[i]);
        if (!CI32(c, 0xc08)) {
            CW32(c, 0x974, 0);
            continue;
        }
        if (!CI32(c, 0x970)) {
            if (!touched[CI8(c, 0xb1a) & 7]) CW32(c, 0xc08, 0);
        } else {
            int32_t t = CI32(c, 0x970) - dt;
            CW32(c, 0x970, t < 0 ? 0 : t);
        }
        CW32(c, 0x974, CI32(c, 0x974) + fx_mul(dt, 0x4000));
    }
    if (Balls_On())
        for (int i = n - 1; i >= 0; i--) {
            Car *c = Car_Get(list[i]);
            Balls_Hit(c, Walls_CarBox(c));   /* 0x47d850 */
        }
}
