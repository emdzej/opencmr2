/* The loose car (see free_body.h). */
#include "sim/free_body.h"
#include "sim/damage.h"
#include "sim/car_internal.h"
#include "sim/fade.h"
#include "fixmath.h"
#include "game/mem.h"
#include "pending.h"

static void v3load(int32_t v[3], const Car *c, uint32_t o) { for (uint32_t k = 0; k < 3; k++) v[k] = CI32(c, o + 4 * k); }
static void v3store(Car *c, uint32_t o, const int32_t v[3]) { for (uint32_t k = 0; k < 3; k++) CW32(c, o + 4 * k, v[k]); }
static int32_t dot3(const int32_t a[3], const int32_t b[3]) { return fx_mul(a[0], b[0]) + fx_mul(a[1], b[1]) + fx_mul(a[2], b[2]); }
static int32_t iabs(int32_t v) { return v < 0 ? -v : v; }
static int32_t dot_at(const Car *c, uint32_t a, uint32_t b)
{
    int32_t x[3], y[3];
    v3load(x, c, a);
    v3load(y, c, b);
    return dot3(x, y);
}
/* the inline normalisation on a field */
static void unit_at(Car *c, uint32_t o)
{
    int32_t v[3];
    v3load(v, c, o);
    Vec_Unit(v);
    v3store(c, o, v);
}
/* 0x4b9da0 with the orientation (+0x750, its rows at +0x00) */
static void rot_t(const Car *c, int32_t out[3], const int32_t v[3])
{
    for (uint32_t r = 0; r < 3; r++)
        out[r] = fx_mul(CI32(c, 16 * r + 8), v[2]) + fx_mul(CI32(c, 16 * r + 4), v[1]) + fx_mul(CI32(c, 16 * r), v[0]);
}
/* 10 dt as a byte: the roll counters' step */
static uint8_t count_step(void) { return (uint8_t)(((int64_t)gms32(0x519c8c) * 0xa0000) >> 32); }

/* The contact point in the car's frame (+0x5dc): n times k, turned, within the half size (+0x204) */
static void contact_point(Car *c, int32_t k)
{
    int32_t n[3], l[3], v[3];
    v3load(n, c, 0x48c);
    for (int i = 0; i < 3; i++) l[i] = fx_mul(n[i], k);
    rot_t(c, v, l);
    for (uint32_t i = 0; i < 3; i++) {
        int32_t h = CI32(c, 0x204 + 4 * i);
        if (h < iabs(v[i])) v[i] = v[i] < 0 ? -h : h;
        CW32(c, 0x5dc + 4 * i, v[i]);
    }
}

/* A landing's or a roll's hit for the damage: the lowest of the four lower box corners (moved by
   twice the half height along up when upside down), straight up */
static void ground_hit(Car *c)
{
    int32_t pt[3] = {0, 0x7d000000, 0};
    for (uint32_t i = 0; i < 4; i++)
        if (CI32(c, 0x274 + 12 * i) < pt[1])
            pt[0] = CI32(c, 0x270 + 12 * i), pt[1] = CI32(c, 0x274 + 12 * i), pt[2] = CI32(c, 0x278 + 12 * i);
    if (CI32(c, 0x370) < 0) {
        int32_t h = fx_mul(CI32(c, 0x208), 0x20000);
        for (uint32_t k = 0; k < 3; k++) pt[k] += fx_mul(CI32(c, 0x36c + 4 * k), h);
    }
    static const int32_t up[3] = {0, 0x10000, 0};
    Car_Damage(c, pt, up, 0, 0, false);
}

/* 0x42f9d0: the body lifted out of the ground by the deepest of the free corners under it, or, with
   none under, lowered onto the nearest above */
static void lift(Car *c)
{
    int32_t up = 0, down = -0x3e80000;
    bool have_up = false, have_down = false;
    for (int i = 7; i >= 0; i--) {
        if (c->s[0xb2c + i]) continue;
        int32_t d = CI32(c, 0x8dc + 4 * (uint32_t)i) - CI32(c, 0x274 + 12 * (uint32_t)i);
        if (d >= 0 && up < d) have_up = true, up = d;
        if (d < 1 && down < d) have_down = true, down = d;
    }
    int32_t m = have_up ? up : have_down ? down : 0;
    CW32(c, 0x2d4, CI32(c, 0x2d4) + m);
    for (uint32_t i = 0; i < 8; i++) CW32(c, 0x274 + 12 * i, CI32(c, 0x274 + 12 * i) + m);
}

/* 0x42fb20's spin pull for an axis whose ground component changed sign: the spin's other two
   components moved by rate towards the part along the ground normal (in the car's frame) */
static int32_t spin_pull(Car *c, int keep, int32_t rate)
{
    int32_t n[3], nb[3], s[3];
    v3load(n, c, 0x48c);
    rot_t(c, nb, n);
    v3load(s, c, 0x420);
    int32_t d = dot3(s, nb);
    for (int k = 0; k < 3; k++)
        if (k != keep) CW32(c, 0x420 + 4 * (uint32_t)k, s[k] + fx_mul(fx_mul(nb[k], d) - s[k], rate));
    return d;
}

/* 0x42fb20: the rows against the ground normal (+0x91c/+0x920/+0x924). An axis crossing the ground
   plane pulls the spin towards the normal and, slow enough, locks it (+0xba0, +0xba8; +0xba4 is never
   set here); a locked axis is laid in the plane with its spin damped. A row held steep for over 100
   counts (+0xb25-+0xb27, 10 a step) starts a roll's damage (+0xbdc-+0xbe4). A crossing with a corner
   moving into the ground at over 0.2 is a hit. */
static void settle(Car *c)
{
    int32_t s[3];
    v3load(s, c, 0x420);
    int32_t w = (int32_t)Fx_Sqrt((uint32_t)fx_mul(s[0], s[0]) + (uint32_t)fx_mul(s[1], s[1]) + (uint32_t)fx_mul(s[2], s[2]));
    w = fx_mul(w, 0x50000);
    if (w > 0xcccc) w = 0xcccc;
    int32_t rate = fx_mul(0x10000 - w, 0x8000);
    int32_t vn = iabs(dot_at(c, 0x408, 0x48c));
    if (CI32(c, 0x778) - vn < 0x8000) rate = 0x8000;
    rate = fx_mul(rate, gms32(0x519c8c));
    if (rate > 0x10000) rate = 0x10000;
    uint8_t inc = count_step();
    bool flip[3];
    for (uint32_t r = 0; r < 3; r++) {
        int32_t f = dot_at(c, 0x48c, 0x360 + 12 * r), was = CI32(c, 0x91c + 4 * r);
        flip[r] = (was > 0 && f < 0) || (was < 0 && f > 0);
        CW32(c, 0x91c + 4 * r, f);
    }
    int32_t f0 = CI32(c, 0x91c), f1 = CI32(c, 0x920), f2 = CI32(c, 0x924);
    if (!CI32(c, 0xbe4)) {
        c->s[0xb27] = (f0 < -0xe666 || 0xe666 < f0) ? (uint8_t)(c->s[0xb27] + inc) : 0;
        if (100 < c->s[0xb27]) CW32(c, 0xbe4, 1);
    }
    if (!CI32(c, 0xbdc)) {
        c->s[0xb25] = f1 < -0xf333 ? (uint8_t)(c->s[0xb25] + inc) : 0;
        if (100 < c->s[0xb25]) CW32(c, 0xbdc, 1);
    }
    if (!CI32(c, 0xbe0)) {
        c->s[0xb26] = (f2 < -0xf333 || 0xf333 < f2) ? (uint8_t)(c->s[0xb26] + inc) : 0;
        if (100 < c->s[0xb26]) CW32(c, 0xbe0, 1);
    }
    /* a lock is let go when the spin about the other two axes passes 0.06 */
    static const uint8_t other[3][2] = {{1, 2}, {0, 2}, {0, 1}};
    for (uint32_t r = 0; r < 3; r++) {
        uint32_t f = 0xba0 + 4 * r;
        if (!CI32(c, f)) continue;
        int32_t a = CI32(c, 0x420 + 4u * other[r][0]), b = CI32(c, 0x420 + 4u * other[r][1]);
        if (0xf5b < (int32_t)Fx_Sqrt((uint32_t)fx_mul(b, b) + (uint32_t)fx_mul(a, a))) CW32(c, f, 0);
    }
    if (flip[0] && !CI32(c, 0xba0)) {
        int32_t d = spin_pull(c, 0, rate);
        if (iabs(d) < 0xf5c && CI32(c, 0x778) < 0x8000) CW32(c, 0xba0, 1);
    }
    if (flip[1] && !CI32(c, 0xba4)) spin_pull(c, 1, rate);
    if (flip[2] && !CI32(c, 0xba8)) {
        int32_t d = spin_pull(c, 2, rate);
        if (iabs(d) < 0xf5c && CI32(c, 0x778) < 0x8000) CW32(c, 0xba8, 1);
    }
    if (CI32(c, 0xba0) || CI32(c, 0xba4) || CI32(c, 0xba8)) {
        int32_t n[3];
        v3load(n, c, 0x48c);
        for (uint32_t r = 0; r < 3; r++) {
            if (!CI32(c, 0xba0 + 4 * r)) continue;
            int32_t row[3], g = CI32(c, 0x91c + 4 * r);
            v3load(row, c, 0x360 + 12 * r);
            for (int k = 0; k < 3; k++) row[k] -= fx_mul(n[k], g);
            Vec_Unit(row);
            v3store(c, 0x360 + 12 * r, row);
            for (int k = 0; k < 2; k++) {
                uint32_t o = 0x420 + 4u * other[r][k];
                CW32(c, o, fx_mul(CI32(c, o), 0xe666));
            }
        }
        /* up made square to forward, side from the two */
        int32_t f[3], u[3], x[3];
        v3load(f, c, 0x360);
        v3load(u, c, 0x36c);
        int32_t p = dot3(u, f);
        for (int k = 0; k < 3; k++) u[k] -= fx_mul(f[k], p);
        Vec_Unit(u);
        v3store(c, 0x36c, u);
        x[0] = fx_mul(f[1], u[2]) - fx_mul(f[2], u[1]);
        x[1] = fx_mul(f[2], u[0]) - fx_mul(f[0], u[2]);
        x[2] = fx_mul(f[0], u[1]) - fx_mul(f[1], u[0]);
        Vec_Unit(x);
        v3store(c, 0x378, x);
    }
    if (!flip[0] && !flip[1] && !flip[2]) return;
    int32_t hit = 0;
    uint32_t i;
    for (i = 0; i < 8; i++) {
        hit = dot_at(c, 0x48c, 0x42c + 12 * i);
        if (hit < -0x3333) break;
    }
    if (i == 8) return;
    CW32(c, 0x5c4, hit);
    CW32(c, 0x5c8, 0);
    CW32(c, 0x5cc, 0);
    contact_point(c, -0x30000);
    ground_hit(c);
}

/* 0x431220: the up row eased back towards the last step's (+0x390) when the car is near level or
   upside down, the side row towards its last (+0x39c) when the car is on its side, by an amount
   from the change (12 dt a unit, at most 1: mode 1 halves to all of it, mode 0 0.1 to 0.4); the
   rows made square again and the orientation (+0x750) set from them and the position. */
static void ease_rows(Car *c, int mode)
{
    int32_t base = mode ? 0x8000 : 0x1999, gain = mode ? 0x8000 : 0x4ccc;
    int32_t r = fx_mul(gms32(0x519c8c), 0xc0000);
    int32_t u[3], s[3], d[3];
    if (0xfae1 < iabs(CI32(c, 0x920))) {
        for (uint32_t k = 0; k < 3; k++) d[k] = CI32(c, 0x36c + 4 * k) - CI32(c, 0x390 + 4 * k);
        int32_t l = (int32_t)Fx_Sqrt((uint32_t)fx_mul(d[0], d[0]) + (uint32_t)fx_mul(d[1], d[1]) + (uint32_t)fx_mul(d[2], d[2]));
        if (0 < l) {
            int32_t t = fx_mul(l, r);
            if (t > 0x10000) t = 0x10000;
            int32_t a = fx_mul(t, gain) + base;
            for (uint32_t k = 0; k < 3; k++) CW32(c, 0x36c + 4 * k, CI32(c, 0x390 + 4 * k) + fx_mul(d[k], a));
            unit_at(c, 0x36c);
        }
    }
    if (0x3333 < iabs(CI32(c, 0x924)) && iabs(CI32(c, 0x91c)) < 0x1999) {
        for (uint32_t k = 0; k < 3; k++) d[k] = CI32(c, 0x378 + 4 * k) - CI32(c, 0x39c + 4 * k);
        v3load(u, c, 0x36c);
        int32_t p = dot3(d, u);
        for (int k = 0; k < 3; k++) d[k] -= fx_mul(u[k], p);
        int32_t l = (int32_t)Fx_Sqrt((uint32_t)fx_mul(d[0], d[0]) + (uint32_t)fx_mul(d[1], d[1]) + (uint32_t)fx_mul(d[2], d[2]));
        if (0 < l) {
            int32_t t = fx_mul(l, r);
            if (t > 0x10000) t = 0x10000;
            int32_t a = (fx_mul(t, gain) - 0x10000) + base;
            /* (the side row moves from where it is, not from the last step's, unlike the up row) */
            for (uint32_t k = 0; k < 3; k++) CW32(c, 0x378 + 4 * k, CI32(c, 0x378 + 4 * k) + fx_mul(d[k], a));
            unit_at(c, 0x378);
        }
    }
    v3load(u, c, 0x36c);
    v3load(s, c, 0x378);
    int32_t p = dot3(s, u);
    for (int k = 0; k < 3; k++) s[k] -= fx_mul(u[k], p);
    Vec_Unit(s);
    v3store(c, 0x378, s);
    int32_t f[3] = {fx_mul(u[1], s[2]) - fx_mul(u[2], s[1]), fx_mul(u[2], s[0]) - fx_mul(u[0], s[2]),
                    fx_mul(u[0], s[1]) - fx_mul(u[1], s[0])};
    Vec_Unit(f);
    v3store(c, 0x360, f);
    for (uint32_t row = 0; row < 3; row++)
        for (uint32_t k = 0; k < 3; k++) CW32(c, 0x10 * row + 4 * k, CI32(c, 0x360 + 12 * row + 4 * k));
    for (uint32_t k = 0; k < 3; k++) CW32(c, 0x30 + 4 * k, CI32(c, 0x2d0 + 4 * k));
}

void Car_FreeBody(Car *c)
{
    bool settled = false;
    CW16(c, 0xb18, 0);
    CW32(c, 0x77c, 0);
    for (uint32_t k = 0; k < 3; k++) CW32(c, 0x498 + 4 * k, CI32(c, 0x48c + 4 * k));
    for (uint32_t o = 0x504; o < 0x564; o += 4) CW32(c, o, CI32(c, o - 0x60));
    if (!CI32(c, 0xbfc)) {
        if (!Fade_Rising(CI8(c, 0xb1a))) {
            /* a car on its upper corners for 100 steps is taken off (+0xbf8) */
            for (uint32_t i = 4; i < 8; i++) {
                if (c->s[0xb2c + i]) continue;
                if (CI32(c, 0x95c) < 0x640000) CW32(c, 0x95c, CI32(c, 0x95c) + gms32(0x519c8c));
                else CW32(c, 0xbf8, 1);
                break;
            }
        } else {
            /* while the recovery's fade runs up the car is held: the last rows and position */
            for (uint32_t k = 0; k < 9; k++) CW32(c, 0x360 + 4 * k, CI32(c, 0x384 + 4 * k));
            for (uint32_t k = 0; k < 3; k++) CW32(c, 0x2d0 + 4 * k, CI32(c, 0x2e8 + 4 * k));
        }
    } else {
        /* a roll's damage pending: its counter runs down, then the car is taken off */
        uint32_t o = CI32(c, 0xbdc) ? 0xb25 : CI32(c, 0xbe0) ? 0xb26 : 0xb27;
        uint8_t k = count_step();
        c->s[o] = c->s[o] < k ? 0 : (uint8_t)(c->s[o] - k);
        if (!c->s[o]) CW32(c, 0xbfc, 0);
        if (!CI32(c, 0xbfc)) {
            CW32(c, 0xbe4, 0);
            CW32(c, 0xbe0, 0);
            CW32(c, 0xbdc, 0);
            c->s[0xb27] = c->s[0xb25] = c->s[0xb26] = 0;
            CW32(c, 0xbf8, 1);
        }
    }
    uint8_t touching = c->s[0xb34];
    for (uint32_t k = 0; k < 4; k++) gw16(0x53ca50 + 2 * k, (uint16_t)(c->s[0xaae + 2 * k] | c->s[0xaaf + 2 * k] << 8));
    Car_CornerGround(c, 8);
    Car_Contacts(c);
    if (!c->s[0xb34]) {
        for (uint32_t i = 0; i < 8; i++) {
            if (iabs(CI32(c, 0x274 + 12 * i) - CI32(c, 0x8dc + 4 * i)) < 0x3333) {
                Car_GroundNormal(c);
                settle(c);
                settled = true;
                break;
            }
        }
    } else {
        Car_GroundNormal(c);
        lift(c);
        Car_Contacts(c);
        if (!touching) {
            /* a landing: the first free corner's speed into the ground as the hit */
            for (uint32_t i = 0; i < 8; i++) {
                if (c->s[0xb2c + i]) continue;
                int32_t keep[3];
                v3load(keep, c, 0x5c4);
                CW32(c, 0x5c4, dot_at(c, 0x48c, 0x42c + 12 * i));
                CW32(c, 0x5c8, 0);
                CW32(c, 0x5cc, 0);
                contact_point(c, -0x20000);
                ground_hit(c);
                v3store(c, 0x5c4, keep);
                break;
            }
        } else {
            settle(c);
            settled = true;
        }
        int32_t vn = dot_at(c, 0x408, 0x48c);
        if (vn < 0)
            for (uint32_t k = 0; k < 3; k++) CW32(c, 0x408 + 4 * k, CI32(c, 0x408 + 4 * k) - fx_mul(CI32(c, 0x48c + 4 * k), vn));
    }
    for (uint32_t r = 0; r < 3; r++) CW32(c, 0x91c + 4 * r, dot_at(c, 0x48c, 0x360 + 12 * r));
    if (!settled) {
        CW32(c, 0xba8, 0);
        CW32(c, 0xba4, 0);
        CW32(c, 0xba0, 0);
    }
    /* the clearance (+0x958) and the wheel lowest under its ground (+0xb2b): the four wheels at their
       radius (+0x774 front, +0x770 rear) below the corners, against the mean of the corner's two
       grounds */
    int8_t was = CI8(c, 0xb2b);
    CW32(c, 0x958, 0);
    c->s[0xb2b] = 0xff;
    for (uint32_t i = 0; i < 4; i++) {
        int32_t h = CI32(c, i < 2 ? 0x774 : 0x770);
        int32_t y = fx_mul(CI32(c, 0x370), h) + CI32(c, 0x274 + 12 * i);
        int32_t d = fx_mul(CI32(c, 0x8ec + 4 * i) + CI32(c, 0x8dc + 4 * i), 0x8000) - y;
        if (CI32(c, 0x958) < d) CW32(c, 0x958, d), c->s[0xb2b] = (uint8_t)i;
    }
    if (!CI32(c, 0x958)) {
        CW32(c, 0x958, 0x1999);
        for (uint32_t i = 0; i < 4; i++) {
            if (c->s[0xb2c + i]) continue;
            int32_t d = CI32(c, 0x274 + 12 * i) - CI32(c, 0x8dc + 4 * i);
            if (d < CI32(c, 0x958)) CW32(c, 0x958, d < 1 ? 0 : d);
        }
        CW32(c, 0x958, -CI32(c, 0x958));
    }
    if (CI8(c, 0xb2b) != -1 && CI8(c, 0xb2b) != was) {
        /* a new wheel under: the spin across the ground normal cut by 0.05 dt */
        int32_t n[3], nb[3], s[3];
        v3load(n, c, 0x48c);
        rot_t(c, nb, n);
        v3load(s, c, 0x420);
        int32_t d = dot3(s, nb), k = fx_mul(gms32(0x519c8c), 0xccc);
        for (uint32_t q = 0; q < 3; q++) CW32(c, 0x420 + 4 * q, s[q] - fx_mul(s[q] - fx_mul(nb[q], d), k));
    }
    ease_rows(c, 1);
    Car_Travel(c);
    int32_t up = CI32(c, 0x920);
    if (CI32(c, 0xc04) || up < 0) {
        CW32(c, 0xc04, 1);
        if (up < 0) CW32(c, 0xc10, 1);
    }
    int32_t t = CI32(c, 0x96c) - fx_mul(gms32(0x519c8c), 0x1eb8);
    CW32(c, 0x96c, t < 0 ? 0 : t);
    if (0 < CI32(c, 0x96c) || up < 0xfc29) return;
    if (CI32(c, 0xc10) && (0x28e < iabs(CI32(c, 0x420)) || 0x28e < iabs(CI32(c, 0x428)))) return;
    if (c->s[0xb28]) {
        /* back on its wheels: road following again */
        CW32(c, 0xc00, 0);
        CW32(c, 0x96c, 0);
        CW32(c, 0xc04, 0);
        CW32(c, 0xc10, 0);
    }
}
