/* Option 0's balls (see balls.h). The pool is 100 records of 0x5c bytes at 0x58e4c8: +0x00 the velocity
   (a step), +0x0c the position, +0x18 the ground's normal, +0x24 the brightness, +0x28-+0x43 the last
   step's three, +0x44 the ground's height, +0x48 the life, +0x4c the ground triangle, +0x50 the glow,
   +0x54 in use, +0x58 the car that fired it. The cars' timers are at 0x58e4a8. */
#include "game/balls.h"
#include "game/gstate.h"
#include "game/lamps.h"
#include "sim/damage.h"
#include "sim/walls.h"
#include "world/glow.h"
#include "world/hpc.h"
#include "world/particles.h"
#include "fixmath.h"
#include "crt.h"
#include <math.h>
#include <string.h>

enum { BALLS = 100 };

typedef struct {
    int32_t vel[3], pos[3], n[3], bright;
    int32_t ppos[3], pn[3], pbright;
    int32_t ground, life;
    int16_t tri;
    Glow *glow;
    bool on;
    uint8_t car;
} Ball;

static Ball ball[BALLS];
static int32_t wait[CAR_MAX];

static int32_t iabs(int32_t v) { return v < 0 ? -v : v; }
static int32_t dot3(const int32_t a[3], const int32_t b[3]) { return fx_mul(a[0], b[0]) + fx_mul(a[1], b[1]) + fx_mul(a[2], b[2]); }
static uint32_t len3(const int32_t v[3]) { return Fx_Sqrt((uint32_t)fx_mul(v[0], v[0]) + (uint32_t)fx_mul(v[1], v[1]) + (uint32_t)fx_mul(v[2], v[2])); }
static void cross(int32_t o[3], const int32_t a[3], const int32_t b[3])
{
    o[0] = fx_mul(a[1], b[2]) - fx_mul(a[2], b[1]);
    o[1] = fx_mul(a[2], b[0]) - fx_mul(a[0], b[2]);
    o[2] = fx_mul(a[0], b[1]) - fx_mul(a[1], b[0]);
}
/* rand() as 0..1 (0x5112f0 times 0x5112e0), rounded by fistp */
static int32_t rand_pos(void) { return (int32_t)nearbyint((double)crt_rand() * (double)(float)(1.0 / 32767.0) * 65536.0); }

/* Car_PointGround 0x4932b0 (its surface unused here) */
static int32_t ground(const int32_t p[3], int32_t n[3], int16_t *tri, int32_t hint)
{
    uint16_t surf;
    return Hpc_Ground(p, n, tri, &surf, hint);
}

bool Balls_On(void) { return (Gs_SuperSpecial() || Gs_SelBit24() || Gs_SelBit27()) && Gs_Option(0); }

void Balls_Setup(void)
{
    int32_t z[3] = {0, 0, 0};
    for (int i = 0; i < BALLS; i++) {
        Ball *b = &ball[i];
        b->on = false;
        b->glow = Glow_Add(1, z, z, 0x3333, 0x3333, Lamps_SpriteTex(2), Lamps_ShaftTex(2), 0x10000, NULL, 0xb4, 0x20000);
        Glow_Show(b->glow, false);
        b->life = 0;
        b->tri = -1;
        b->n[0] = 0, b->n[1] = 0x10000, b->n[2] = 0;
    }
    memset(wait, 0, sizeof wait);
}

/* At the front corner pair's middle (the first two box corners, +0x270 and +0x27c) raised along the
   up row (+0x36c) by +0x774; the velocity 0.8 forward plus the car's, kept only along the forward row
   (+0x360); on the ground (its height plus 0.5) */
void Balls_Fire(int car)
{
    if (car < 0 || car >= CAR_MAX || 0 < wait[car]) return;
    for (int i = 0; i < BALLS; i++) {
        Ball *b = &ball[i];
        if (b->on) continue;
        Car *c = Car_Get(car);
        int32_t s = CI32(c, 0x774);
        for (uint32_t k = 0; k < 3; k++)
            b->pos[k] = fx_mul(CI32(c, 0x36c + 4 * k), s) + fx_mul(CI32(c, 0x27c + 4 * k) - CI32(c, 0x270 + 4 * k), 0x8000) + CI32(c, 0x270 + 4 * k);
        int32_t f[3] = {CI32(c, 0x360), CI32(c, 0x364), CI32(c, 0x368)};
        for (uint32_t k = 0; k < 3; k++) b->vel[k] = fx_mul(f[k], 0xcccc) + CI32(c, 0x408 + 4 * k);
        int32_t d = dot3(b->vel, f);
        for (int k = 0; k < 3; k++) b->vel[k] = fx_mul(f[k], d);
        b->life = 0x320000;
        b->bright = 0x10000;
        b->on = true;
        b->car = (uint8_t)car;
        b->ground = 0;
        b->tri = -1;
        int32_t g = ground(b->pos, b->n, &b->tri, 0);
        b->ground = g;
        b->pos[1] = g + 0x8000;
        memcpy(b->ppos, b->pos, sizeof b->pos);
        memcpy(b->pn, b->n, sizeof b->n);
        b->pbright = b->bright;
        wait[car] = 0x100000;
        return;
    }
}

/* The timers run down; a ball keeps its last step, ages, rolls by its velocity on the ground and
   leaves a glow particle (type 0x1f) behind it: drifting at 0.3 of a random push across its path less
   half the path, plus the path. It fades over its last 5 seconds. */
void Balls_Step(void)
{
    int32_t dt = gms32(0x519c8c);
    for (int i = 0; i < CAR_MAX; i++)
        if (0 < wait[i] && (wait[i] -= dt) < 0) wait[i] = 0;
    for (int i = 0; i < BALLS; i++) {
        Ball *b = &ball[i];
        if (!b->on) continue;
        memcpy(b->ppos, b->pos, sizeof b->pos);
        memcpy(b->pn, b->n, sizeof b->n);
        b->pbright = b->bright;
        b->life -= dt;
        if (b->life <= 0) {
            b->on = false;
            continue;
        }
        int32_t old[3];
        memcpy(old, b->pos, sizeof old);
        for (int k = 0; k < 3; k++) b->pos[k] += b->vel[k];
        int32_t g = ground(b->pos, b->n, &b->tri, b->ground);
        b->ground = g;
        b->pos[1] = g + 0x8000;
        int32_t d[3] = {b->pos[0] - old[0], b->pos[1] - old[1], b->pos[2] - old[2]}, u[3] = {0, 0, 0};
        uint32_t l = len3(d);
        if (l) {
            int32_t r = (int32_t)(0x100000000LL / (int32_t)l);
            for (int k = 0; k < 3; k++) u[k] = fx_mul(d[k], r);
        }
        int32_t r[3];
        r[0] = 0x8000 - rand_pos();
        r[1] = 0x8000 - rand_pos();
        r[2] = 0x8000 - rand_pos();
        int32_t a = dot3(r, u);
        for (int k = 0; k < 3; k++) r[k] -= fx_mul(u[k], a);
        for (int k = 0; k < 3; k++) r[k] = fx_mul(r[k] + fx_mul(u[k], (int32_t)0xffff8000), 0x4ccc) + d[k];
        int32_t p[3] = {b->pos[0] - r[0], b->pos[1] - r[1], b->pos[2] - r[2]};
        Car *c = Car_Get(b->car);
        Particle_Emit(0x1f, p, r, b->pos[1] - 0x50000, NULL, NULL, 0, NULL, c && c->second ? c->second->mask : 0xff);
        if (0x50000 < b->life) b->bright = 0x10000;
        else {
            int32_t v = fx_mul(b->life, 0x3333);
            b->bright = 0x10000 < v ? 0x10000 : v < 0 ? 0 : v;
        }
    }
}

/* A ball of another car within 100 units, between the box's y range and inside its flat extents: an
   impulse of 6 along its direction raised by 0.4 (made unit first), at the ball's offset lowered by
   3.5; the car lifted by 0.25, loose (+0xc00, +0x96c 1.0) and damaged (kind 2); the ball gone */
void Balls_Hit(Car *c, const int32_t *box)
{
    for (int i = 0; i < BALLS; i++) {
        Ball *b = &ball[i];
        if (!b->on || b->car == (uint8_t)CI8(c, 0xb1a)) continue;
        int32_t d[3] = {b->pos[0] - CI32(c, 0x2d0), b->pos[1] - CI32(c, 0x2d4), b->pos[2] - CI32(c, 0x2d8)};
        if (0x63ffff < iabs(d[0]) || 0x63ffff < iabs(d[1]) || 0x63ffff < iabs(d[2])) continue;
        if (b->pos[1] < box[B_YMIN] || box[B_YMAX] < b->pos[1]) continue;
        if (box[B_EXT_F] < iabs(dot3(&box[B_FWD], d)) || box[B_EXT_S] < iabs(dot3(&box[B_SIDE], d))) continue;
        int32_t imp[3];
        for (int k = 0; k < 3; k++) imp[k] = fx_mul(b->vel[k], 0x20000);
        uint32_t l = len3(imp);
        /* (the original divides without a test: a still ball would fault; none can be still here, as
           a ball moves along the forward row at the launch speed) */
        int32_t r = l ? (int32_t)(0x100000000LL / (int32_t)l) : 0;
        for (int k = 0; k < 3; k++) imp[k] = fx_mul(imp[k], r);
        imp[1] += 0x6666;
        for (int k = 0; k < 3; k++) imp[k] = fx_mul(imp[k], 0x60000);
        d[1] -= 0x38000;
        CW32(c, 0x40c, CI32(c, 0x40c) + 0x4000);
        int32_t ld[3], li[3], t[3];
        Walls_RotT(c, ld, d);
        Walls_RotT(c, li, imp);
        cross(t, li, ld);
        for (uint32_t k = 0; k < 3; k++) {
            CW32(c, 0x5c4 + 4 * k, CI32(c, 0x5c4 + 4 * k) + imp[k]);
            CW32(c, 0x5d0 + 4 * k, CI32(c, 0x5d0 + 4 * k) + t[k]);
        }
        CW32(c, 0xc00, 1);
        CW32(c, 0x96c, 0x10000);
        Vec_Unit(imp);
        Car_Damage(c, b->pos, imp, 0, 2, false);
        b->on = false;
        b->life = 0;
    }
}

/* The glows between the last step and this one: shown with the brightness, the plane under the ball
   (half a unit down) with the ground's normal; the glow's direction is its position (as the original) */
void Balls_Frame(int32_t frac)
{
    for (int i = 0; i < BALLS; i++) {
        Ball *b = &ball[i];
        if (!b->on) {
            Glow_Show(b->glow, false);
            continue;
        }
        int32_t p[3], n[3];
        for (int k = 0; k < 3; k++) {
            p[k] = fx_mul(b->pos[k] - b->ppos[k], frac) + b->ppos[k];
            n[k] = fx_mul(b->n[k] - b->pn[k], frac) + b->pn[k];
        }
        Vec_Unit(n);
        int32_t v = fx_mul(b->bright - b->pbright, frac) + b->pbright;
        if (v < 1) {
            Glow_Show(b->glow, false);
            continue;
        }
        Glow_Show(b->glow, true);
        Glow_Level(b->glow, v);
        Glow_Move(b->glow, p, p);
        int32_t at[3] = {p[0], p[1] - 0x8000, p[2]};
        Glow_Plane(b->glow, at, n, 0);
    }
}

bool Balls_At(int i, int32_t pos[3])
{
    if (i < 0 || i >= BALLS || !ball[i].on) return false;
    memcpy(pos, ball[i].pos, sizeof ball[i].pos);
    return true;
}
