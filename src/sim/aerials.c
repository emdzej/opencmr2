/* The cars' whip aerials (see aerials.h). */
#include "sim/aerials.h"
#include "sim/car.h"
#include "sim/parts.h"
#include "game/camera.h"
#include "game/mem.h"
#include "game/weather.h"
#include "render/line3d.h"
#include "world/hpc.h"
#include "world/obj.h"
#include "world/relight.h"
#include "crt.h"
#include "fixmath.h"
#include <math.h>
#include <string.h>

enum { AERIALS = 3 };

/* A record (0x3c bytes): [0..2] the swing (x and z; y held at 1.0), lying: the foot on the ground;
   [3..5] the last step's; [6..8] the drawn; [9..11] lying: the tip; [12], [13] the swing's velocity;
   [14] lying */
typedef struct {
    int32_t s[3], prev[3], drawn[3], tip[3];
    int32_t v[2];
    int32_t down;
} Aerial;

static Aerial aer[CAR_MAX][AERIALS];   /* 0x590c6c */
static int32_t air[3];                 /* 0x590b50: the air past the current car, in its frame */
static int32_t tint[3];                /* 0x590c54: the car's light (R, G, B as 0..1) */
static Car *cur_car;                   /* 0x590d74 */

static int idx(const Car *c) { int n = CI8(c, 0xb1a); return n >= 0 && n < CAR_MAX ? n : 0; }
static int32_t dt(void) { return gms32(0x519c8c); }
static int32_t rd(const uint8_t *p) { return (int32_t)((uint32_t)p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24); }
static uint32_t len3(const int32_t v[3]) { return Fx_Sqrt((uint32_t)fx_mul(v[0], v[0]) + (uint32_t)fx_mul(v[1], v[1]) + (uint32_t)fx_mul(v[2], v[2])); }
static void unit3(int32_t v[3])
{
    uint32_t l = len3(v);
    if (!l) { v[0] = v[1] = v[2] = 0; return; }
    int32_t r = (int32_t)(0x100000000LL / (int32_t)l);
    for (int i = 0; i < 3; i++) v[i] = fx_mul(v[i], r);
}
/* Vec_Mul3 0x4b9c20 / Vec_RotateT 0x4b9da0 on a matrix of rows of four */
static void vmul(int32_t o[3], const int32_t v[3], const int32_t *m)
{
    for (int k = 0; k < 3; k++) o[k] = fx_mul(m[8 + k], v[2]) + fx_mul(m[4 + k], v[1]) + fx_mul(m[k], v[0]);
}
static void vrot_t(int32_t o[3], const int32_t v[3], const int32_t *m)
{
    for (int r = 0; r < 3; r++) o[r] = fx_mul(m[4 * r + 2], v[2]) + fx_mul(m[4 * r + 1], v[1]) + fx_mul(m[4 * r], v[0]);
}
static void car_orient(int32_t m[12], const Car *c) { for (uint32_t i = 0; i < 12; i++) m[i] = CI32(c, 4 * i); }

/* the car's .cin section 3: the count, and aerial k's 0x20 bytes */
static int count_of(const Car *c)
{
    const uint8_t *q = Parts_Cin3(idx(c));
    int n = q ? rd(q + 0xc) : 0;
    return n < 0 ? 0 : n > AERIALS ? AERIALS : n;   /* (the original has room for three) */
}
static const uint8_t *desc(const Car *c, int k) { return Parts_Cin3(idx(c)) + 0x10 + 0x20 * k; }
static void desc_v(int32_t v[3], const uint8_t *d, int at) { for (int i = 0; i < 3; i++) v[i] = rd(d + at + 4 * i); }

int Aerials_Count(int car) { Car *c = Car_Get(car); return c ? count_of(c) : 0; }

int Aerials_Down(int car, int k, int32_t swing[2])
{
    const Aerial *a = &aer[car][k];
    swing[0] = a->s[0], swing[1] = a->s[2];
    return a->down;
}

void Aerials_Reset(int car, int k, int32_t down)
{
    if (car < 0 || car >= CAR_MAX || k < 0 || k >= AERIALS) return;
    Aerial *a = &aer[car][k];
    memset(a, 0, sizeof *a);
    a->down = down;
}

/* 0x484f40: aerial k laid on the ground once: the foot under the base, the tip its length along the
   axis made flat to the ground (the ground's x axis when the axis is square to it); the damage
   record's flag set */
static void lay(int k)
{
    Aerial *a = &aer[idx(cur_car)][k];
    if (a->down) return;
    const uint8_t *d = desc(cur_car, k);
    int32_t m[12], b[3], n[3], ax[3], w[3];
    int16_t tri = 0;
    uint16_t surf = 0;
    car_orient(m, cur_car);
    desc_v(b, d, 0);
    vmul(a->s, b, m);
    for (uint32_t i = 0; i < 3; i++) a->s[i] += CI32(cur_car, 0x2d0 + 4 * i);
    a->s[1] = Hpc_Ground(a->s, n, &tri, &surf, a->s[1]);
    desc_v(b, d, 0xc);
    vmul(ax, b, m);
    int32_t dn = fx_mul(n[2], ax[2]) + fx_mul(n[0], ax[0]) + fx_mul(n[1], ax[1]);
    for (int i = 0; i < 3; i++) w[i] = ax[i] - fx_mul(n[i], dn);
    if (!len3(w)) {
        int32_t u = fx_mul(n[0], 0x10000);
        w[0] = 0x10000 - fx_mul(n[0], u), w[1] = -fx_mul(n[1], u), w[2] = -fx_mul(n[2], u);
    }
    unit3(w);
    int32_t l = rd(d + 0x18);
    for (int i = 0; i < 3; i++) a->tip[i] = a->s[i] + fx_mul(w[i], l);
    a->down = 1;
    uint8_t *dm = Damage_Get(CI8(cur_car, 0xb1a));
    if (dm) dm[0x4c0 + 4 * k] = 1, dm[0x4c1 + 4 * k] = 0, dm[0x4c2 + 4 * k] = 0, dm[0x4c3 + 4 * k] = 0;
}

/* 0x4853c0: the air past the car, in its frame: its velocity less the wind gusting between 0.7 and
   1.0 of itself (a rand() a step) */
static void air_past(const int32_t wind[3])
{
    int32_t m[12], v[3] = {CI32(cur_car, 0x408), CI32(cur_car, 0x40c), CI32(cur_car, 0x410)}, w[3], o[3];
    car_orient(m, cur_car);
    vrot_t(air, v, m);
    int32_t r = (int32_t)nearbyint((double)crt_rand() * (double)(1.0f / 32767.0f) * 65536.0);
    int32_t k = fx_mul(r, 0x4ccc) + 0xb333;
    for (int i = 0; i < 3; i++) w[i] = fx_mul(wind[i], k);
    vrot_t(o, w, m);
    for (int i = 0; i < 3; i++) air[i] -= o[i];
}

/* 0x4854a0: a standing aerial's swing pushed by the air across its axis, pulled back by a spring of
   2 and damped by 0.2, moved by its velocity; y held at 1.0 */
static void swing(int k)
{
    Aerial *a = &aer[idx(cur_car)][k];
    if (a->down) return;
    int32_t ax[3];
    desc_v(ax, desc(cur_car, k), 0xc);
    memcpy(a->prev, a->s, sizeof a->prev);
    int32_t d = fx_mul(ax[2], air[2]) + fx_mul(ax[0], air[0]) + fx_mul(ax[1], air[1]);
    a->v[0] -= air[0] - fx_mul(ax[0], d);
    a->v[1] -= air[2] - fx_mul(ax[2], d);
    a->v[0] += fx_mul(dt(), -(fx_mul(a->v[0], 0x3333) + fx_mul(a->s[0], 0x20000)));
    a->v[1] += fx_mul(dt(), -(fx_mul(a->v[1], 0x3333) + fx_mul(a->s[2], 0x20000)));
    a->s[0] += fx_mul(dt(), a->v[0]);
    a->s[2] += fx_mul(dt(), a->v[1]);
    a->s[1] = 0x10000;
}

/* 0x484e00: per listed car (from the last, not +0xc0c): upside down (its up row on the ground's
   normal under -0.8) and loose (+0xc00, not +0xb64) with a wheel off the ground (+0xb30-+0xb33), the
   aerials laid down; otherwise they swing */
void Aerials_Step(const CarList *l)
{
    int32_t wind[3];
    Wind_Vector(wind);
    for (int i = l->n - 1; i >= 0; i--) {
        cur_car = Car_Get(l->car[i]);
        if (!cur_car || CI32(cur_car, 0xc0c)) continue;
        Car *c = cur_car;
        int32_t up = fx_mul(CI32(c, 0x374), CI32(c, 0x494)) + fx_mul(CI32(c, 0x36c), CI32(c, 0x48c)) + fx_mul(CI32(c, 0x370), CI32(c, 0x490));
        bool off = !c->s[0xb31] || !c->s[0xb30] || !c->s[0xb33] || !c->s[0xb32];
        int n = count_of(c);
        if (!CI32(c, 0xb64) && CI32(c, 0xc00) && up < -0xcccc && off)
            for (int k = n - 1; k >= 0; k--) lay(k);
        else {
            air_past(wind);
            for (int k = n - 1; k >= 0; k--) swing(k);
        }
    }
}

void Aerials_Interp(int32_t frac)
{
    for (int i = g_all.n - 1; i >= 0; i--) {
        Car *c = Car_Get(g_all.car[i]);
        if (!c || CI32(c, 0xc0c)) continue;
        for (int k = count_of(c) - 1; k >= 0; k--) {
            Aerial *a = &aer[idx(c)][k];
            for (int j = 0; j < 3; j++) a->drawn[j] = a->prev[j] + fx_mul(a->s[j] - a->prev[j], frac);
        }
    }
}

/* 0x4863d0: 1.0 within 10 units, falling to 0 at 50 (any axis past 50: 0) */
static int32_t fade(const int32_t d[3])
{
    for (int i = 0; i < 3; i++)
        if ((d[i] < 0 ? -d[i] : d[i]) > 0x320000) return 0;
    int32_t l = (int32_t)len3(d);
    if (l > 0x320000) return 0;
    if (l - 0xa0000 < 1) return 0x10000;
    int32_t k = fx_mul(l - 0xa0000, 0x666);
    return 0x10000 - (k > 0x10000 ? 0x10000 : k);
}

static uint8_t alpha(uint8_t a, int32_t f)
{
    int32_t v = fx_mul((int32_t)a << 16, f) >> 16;
    return (uint8_t)(v < 0 ? 0 : v > 0xff ? 0xff : v);
}

/* 0x485860: aerial k. Standing (when the car is in the pass): from the base in the car's drawn frame
   (the second model's world), five segments: the axis times 0.2 of the swing's height each, plus
   the swing (x and z, times the step) made unit times the length bent in quadratically along the
   axis' square directions; lying: foot to tip, each end a unit toward the camera. The colour the
   aerial's tinted by the car's light, its alpha faded by distance. */
static void draw_one(int k, const int32_t cam[3], bool shown)
{
    const Aerial *a = &aer[idx(cur_car)][k];
    const uint8_t *d = desc(cur_car, k);
    uint8_t col[4];
    for (int i = 0; i < 3; i++) col[i] = (uint8_t)(((int64_t)tint[i] * ((int32_t)d[0x1c + i] << 16)) >> 32);
    col[3] = d[0x1f];
    if (!a->down) {
        if (!shown || !cur_car->second) return;
        const int32_t *M = cur_car->second->world.m;
        int32_t base[3], P[3], dd[3];
        desc_v(base, d, 0);
        vmul(P, base, M);
        for (int i = 0; i < 3; i++) P[i] += M[12 + i], dd[i] = P[i] - cam[i];
        int32_t f = fade(dd);
        if (f <= 0) return;
        col[3] = alpha(col[3], f);
        int32_t u[3] = {fx_mul(a->drawn[0], dt()), a->drawn[1], fx_mul(a->drawn[2], dt())};
        unit3(u);
        int32_t l = rd(d + 0x18);
        for (int i = 0; i < 3; i++) u[i] = fx_mul(u[i], l);
        int32_t ax[3], aw[3];
        desc_v(ax, d, 0xc);
        vmul(aw, ax, M);
        int32_t h = fx_mul(u[1], 0x3333);
        for (int i = 0; i < 3; i++) aw[i] = fx_mul(aw[i], h);
        int32_t e1[3], e3[3];
        if (ax[0] == 0) e1[0] = 0x10000, e1[1] = 0, e1[2] = 0;
        else {
            int32_t q = ax[0];
            e1[0] = 0x10000 - fx_mul(ax[0], q), e1[1] = -fx_mul(ax[1], q), e1[2] = -fx_mul(ax[2], q);
            unit3(e1);
        }
        if (ax[2] == 0) e3[0] = 0, e3[1] = 0, e3[2] = 0x10000;
        else {
            int32_t q = ax[2];
            e3[0] = -fx_mul(ax[0], q), e3[1] = -fx_mul(ax[1], q), e3[2] = 0x10000 - fx_mul(ax[2], q);
            unit3(e3);
        }
        int32_t b[3], bw[3];
        for (int i = 0; i < 3; i++) b[i] = fx_mul(e3[i], u[2]) + fx_mul(e1[i], u[0]);
        vmul(bw, b, M);
        int32_t from[3] = {P[0], P[1], P[2]};
        for (int s = 1; s < 6; s++) {
            int32_t t = s << 16, tt = fx_mul(t, 0x3333);
            tt = fx_mul(tt, tt);
            int32_t to[3];
            for (int i = 0; i < 3; i++) to[i] = P[i] + fx_mul(bw[i], tt) + fx_mul(aw[i], t);
            Line3d_Add(from, to, col, col);
            memcpy(from, to, sizeof from);
        }
        return;
    }
    int32_t dd[3];
    for (int i = 0; i < 3; i++) dd[i] = cam[i] - a->s[i];
    int32_t f = fade(dd);
    if (f <= 0) return;
    col[3] = alpha(col[3], f);
    int32_t from[3], to[3];
    if ((int32_t)len3(dd) > 0x10000) unit3(dd);
    for (int i = 0; i < 3; i++) from[i] = a->s[i] + dd[i], dd[i] = cam[i] - a->tip[i];
    if ((int32_t)len3(dd) > 0x10000) unit3(dd);
    for (int i = 0; i < 3; i++) to[i] = a->tip[i] + dd[i];
    Line3d_Add(from, to, col, col);
}

/* 0x485690: per car (from the last; not +0xc0c, not hidden from this view +0xb68, nor the camera's own
   car in its interior view) the car's light (Light_ByteRamp of +0xa70) as 0..1 per channel, and each
   aerial drawn, standing ones when the car's interior (not loaded by the port) or first wheel is in
   this pass. Inside: the interior view or a trackside camera behind the car (0x423fc0). */
void Aerials_Draw(const int16_t *cars, int n, int player, const int32_t cam[3])
{
    bool inside = Camera_TsBehind(player) || Camera_Kind(player) == 10;
    int camcar = Camera_Car(player) & 0xff;
    for (int i = n - 1; i >= 0; i--) {
        cur_car = Car_Get(cars[i]);
        if (!cur_car || CI32(cur_car, 0xc0c) || (inside && camcar == cars[i]) || CI32(cur_car, 0xb68 + 4 * (uint32_t)player)) continue;
        uint32_t lc = Light_ByteRamp(CI32(cur_car, 0xa70));
        for (int k = 0; k < 3; k++) {
            int32_t v = (int32_t)(lc >> (8 * k) & 0xff) * 0x106;
            tint[k] = v > 0x10000 ? 0x10000 : v;
        }
        uint8_t mask = cur_car->wheel[0] ? cur_car->wheel[0]->mask : 0;
        for (int k = count_of(cur_car) - 1; k >= 0; k--) draw_one(k, cam, (mask & (1u << (player & 31))) != 0);
    }
}
