/* The fireworks (see fireworks.h), ported from 0x47e4d0-0x480600. */
#include "game/fireworks.h"
#include "audio/snd.h"
#include "crt.h"
#include "exe.h"
#include "fixmath.h"
#include "game/common.h"
#include "game/gstate.h"
#include "game/lamps.h"
#include "game/mem.h"
#include "game/paths.h"
#include "render/sprite3d.h"
#include "world/relight.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A rocket (0x938 bytes in the original): the trail is a ring of 20 points (the head the next to
   write), each spark of the burst is drawn mirrored four ways (x, -x, -x and -z, -z) */
enum { TRAIL = 20, SPARKS = 18 };
typedef struct {
    int32_t pos[3], vel[3];                 /* +0x000 */
    int32_t trail[TRAIL][3];                /* +0x018 */
    int32_t spark[SPARKS][3];               /* +0x108: from the burst point */
    int32_t pos_was[3], pos_at[3];          /* +0x1e0, +0x1ec: the last step's, the drawn */
    int32_t trail_was[TRAIL][3];            /* +0x1f8 */
    int32_t trail_at[TRAIL][3];             /* +0x2e8 */
    int32_t spark_was[SPARKS][3];           /* +0x3d8 */
    int32_t spark_at[SPARKS][3];            /* +0x4b0 */
    int32_t spark_vel[SPARKS][3];           /* +0x588 */
    int32_t speed;                          /* +0x660: the burst's */
    int32_t fuse, burn;                     /* +0x664, +0x668 */
    int32_t rise_g, burst_g;                /* +0x66c, +0x670 */
    int32_t half;                           /* +0x674: the double burst's turn of colour */
    uint8_t col[6][4];                      /* +0x678: the rocket, the trail, two spark pairs */
    int8_t count, head;                     /* +0x690, +0x691: the trail's points shown, the next */
    uint8_t colour;                         /* +0x692: the flash's */
    int8_t whistle;                         /* +0x693: the voice, as a byte */
    int32_t state, kind;                    /* +0x694: 0 free, 1 rising, 2 burst; +0x698 */
    int32_t glint[TRAIL];                   /* +0x69c: the trail point's brighter colour */
    int32_t on[SPARKS * 4];                 /* +0x6ec: each mirrored spark shown */
    int32_t pick[SPARKS * 4];               /* +0x80c: its colour of the pair */
    int32_t sparks, twice, flicker;         /* +0x92c, +0x930, +0x934 */
} Rocket;

static Rocket *rockets;          /* DAT_00590af8 */
static int nrockets;             /* DAT_00590afc */
static Sprite3d big, small;      /* DAT_00590b04, _b08 */
static int32_t dirs[3][3][3];    /* DAT_00590b0c: three rings of three directions */
static int32_t pads[4][3];       /* 0x5909c8 */
static const Tex *tex;           /* DAT_00590b00 */
static int sounds;               /* DAT_005909bc */

bool Fireworks_On(void) { return Gs_SuperSpecial(); }

/* rand() as 0..1.0: times the float 1/32767 and 65536, rounded */
static int32_t rnd(void) { return (int32_t)nearbyint((double)crt_rand() * (double)(1.0f / 32767.0f) * 65536.0); }
/* a component of +-0..1.0, the sign first */
static int32_t rnd_signed(void)
{
    if (rnd() < 0x8001) return rnd();
    return -rnd();
}
static int32_t tbl(uint32_t a, int i) { return (int32_t)exe_u32(a + 4u * (uint32_t)i); }
static void colour(uint8_t c[4], uint32_t v) { for (int i = 0; i < 4; i++) c[i] = (uint8_t)(v >> 8 * i); }

void Fireworks_Free(void)
{
    free(rockets);
    rockets = NULL;
    nrockets = 0;
}

/* 0x47e4d0: n rockets free; the sprites (1.0 and 0.75 across); the burst's directions at 15, 45 and
   75 degrees each way (x sin a sin b, y cos b, z cos a sin b); the pads at the arena's side (0x51f4e0
   added); the lamps' second sprite texture; the eleven sounds from FireWork.bfl */
void Fireworks_Setup(int n)
{
    Fireworks_Free();
    nrockets = n;
    if (n) {
        rockets = calloc((size_t)n, sizeof *rockets);
        memset(&big, 0, sizeof big);
        big.e[0] = (int32_t)0xffff8000, big.e[1] = 0x8000, big.e[2] = 0x8000, big.e[3] = (int32_t)0xffff8000;
        small = big;
        small.e[0] = (int32_t)0xffffa000, small.e[1] = 0x6000, small.e[2] = 0x6000, small.e[3] = (int32_t)0xffffa000;
        int32_t a = 0xf0000;
        for (int r = 0; r < 3; r++, a += 0x1e0000) {
            int ia = Fx_DegIndex(a), na = (int)nearbyint((double)a * -(1.0 / 5760.0)) & 0xffff;
            int32_t b = 0xf0000;
            for (int d = 0; d < 3; d++, b += 0x1e0000) {
                int ib = Fx_DegIndex(b);
                int nb = (int)nearbyintf((float)b * (float)-(1.0 / 5760.0)) & 0xffff;
                dirs[r][d][0] = fx_mul(g_sin[ia], g_sin[ib]);
                dirs[r][d][1] = g_sin[(0x400 - nb) & 0xfff];
                dirs[r][d][2] = fx_mul(g_sin[(0x400 - na) & 0xfff], g_sin[ib]);
            }
        }
    }
    tex = Lamps_SpriteTex(1);
    for (int i = 0; i < 4; i++) {
        pads[i][0] = 0x5b50000 + tbl(0x51f4e0, 0);
        pads[i][1] = 0x20000 + tbl(0x51f4e0, 1);
        pads[i][2] = 0xc20000 + 0xc0000 * i + tbl(0x51f4e0, 2);
    }
    sounds = Snd_Count();   /* 0x4805f0 */
    static const uint32_t names[11] = {0x51f61c, 0x51f600, 0x51f5e4, 0x51f5c8, 0x51f5ac, 0x51f594,
                                       0x51f57c, 0x51f564, 0x51f54c, 0x51f534, 0x51f51c};
    for (int i = 0; i < 11; i++) {
        char p[260];
        snprintf(p, sizeof p, "%s%s", Path_Sounds(), exe_str(names[i]));
        Snd_LoadNamed(p, Bfl_Firework());
    }
}

/* 0x47fcb0: a rocket from p with velocity v in the first free slot: the trail gathered at p; the fuse
   25 (a burst's rocket 5 to 15); five colours from 0x51f4ec; a burst of sparks (half the first
   rockets, every one from a burst: its test is above 1.0) with each mirrored spark shown above a
   share of 0.1-0.9 (doubled for one in 40, which turns colour half way), burning 10 plus 15 times the
   share shown, else five rockets on (kind 2) or none; the launch sound and, mostly, a whistle */
static void spawn(const int32_t p[3], const int32_t v[3], bool sub)
{
    int k = -1;
    for (int i = 0; i < nrockets; i++)
        if (rockets[i].state == 0) {
            k = i;
            break;
        }
    if (k < 0) return;
    Rocket *r = &rockets[k];
    r->state = 1;
    for (int i = 0; i < 3; i++) r->pos[i] = r->pos_was[i] = p[i], r->vel[i] = v[i];
    r->count = 0;
    r->head = 0;
    for (int t = 0; t < TRAIL; t++) {
        for (int i = 0; i < 3; i++) r->trail[t][i] = r->trail_was[t][i] = r->pos[i];
        r->glint[t] = 0;
    }
    r->flicker = 0;
    r->fuse = sub ? fx_mul(rnd(), 0xa0000) + 0x50000 : 0x190000;
    r->rise_g = 0xa3d;
    r->burst_g = 0x624;
    int c = crt_rand() % 6;
    r->colour = (uint8_t)c;
    colour(r->col[0], (uint32_t)tbl(0x51f4ec, c));
    r->col[0][3] = 0xff;
    memcpy(r->col[1], r->col[0], 4);
    memcpy(r->col[2], r->col[0], 4);
    colour(r->col[3], (uint32_t)tbl(0x51f4ec, crt_rand() % 6));
    r->col[3][3] = 0xff;
    colour(r->col[5], (uint32_t)tbl(0x51f4ec, crt_rand() % 6));
    r->col[5][3] = 0xff;
    colour(r->col[4], (uint32_t)tbl(0x51f4ec, crt_rand() % 6));
    r->col[4][3] = 0xff;
    int32_t u = rnd();
    if (!sub) r->kind = u < 0x8000 ? 1 : 2;
    else r->kind = u < 0x10001 ? 1 : 0;
    if (r->kind == 1) {
        int32_t share = fx_mul((crt_rand() % 9 + 1) << 16, 0x1999);
        if (rnd() < 0xfd71) r->twice = 0;
        else r->twice = 1, share = fx_mul(share, 0x20000);
        int shown = 0;
        for (int s = 0; s < SPARKS * 4; s++) {
            if ((uint32_t)share < (uint32_t)rnd()) r->on[s] = 0;
            else r->on[s] = 1, shown++;
            r->pick[s] = rnd() < 0x8001 ? 0 : 1;
        }
        int32_t f = (int32_t)(((int64_t)shown << 32) / 0x480000);
        r->burn = fx_mul(f, 0xf0000) + 0xa0000;
        r->sparks = 1;
        r->speed = fx_mul(0x10000 - f, 0xccc) + 0x11eb;
        if (r->twice) {
            r->half = r->burn;
            r->burn = fx_mul(r->burn, 0x20000);
        }
    } else {
        memset(r->on, 0, sizeof r->on);
        r->burn = 0x50000;
        r->speed = 0x11eb;
        r->sparks = 0;
    }
    Voice_Play(sounds + 10, 0xccc, 0x5622, 0, false, false);
    if (rnd() > 0x1999) {
        int32_t vol = fx_mul(rnd(), 0x4000) + 0x4000;
        r->whistle = (int8_t)Voice_Play(sounds + (crt_rand() & 1), vol, 0x5622, 0, false, false);
    } else
        r->whistle = -1;
}

/* 0x480380: from a random pad, a heading at random, out by up to 1.0 and up 4.0, made unit, at
   1.5 to 2.5 a step (the speed computed, then not used: the unit vector is launched) */
void Fireworks_Launch(void)
{
    int32_t h = fx_mul(rnd(), 0x1680000);
    int ih = Fx_DegIndex(h);
    int pad = crt_rand() % 4;
    int32_t s = g_sin[ih], c = g_sin[(ih + 0x400) & 0xfff];
    int32_t out = rnd();
    int32_t v[3] = {fx_mul(s, out), 0x40000, fx_mul(c, out)};
    Vec_Unit(v);
    (void)(fx_mul(rnd(), 0x10000) + 0x18000);
    spawn(pads[pad], v, false);
}

void Fireworks_Step(void)
{
    for (int k = 0; k < nrockets; k++) {
        Rocket *r = &rockets[k];
        memcpy(r->pos_was, r->pos, sizeof r->pos);
        memcpy(r->trail_was, r->trail, sizeof r->trail);
        memcpy(r->spark_was, r->spark, sizeof r->spark);
        if (r->state == 1) {
            /* rising: the fall pulled off the climb; four trail points scattered along the step back
               and across it */
            r->vel[1] -= r->rise_g;
            int32_t d[3];
            for (int i = 0; i < 3; i++) d[i] = r->pos[i], r->pos[i] += r->vel[i], d[i] -= r->pos[i];
            int32_t u[3] = {d[0], d[1], d[2]};
            Vec_Unit(u);
            for (int n = 0; n < 4; n++) {
                int32_t t = rnd(), *p = r->trail[(int)r->head];
                for (int i = 0; i < 3; i++) p[i] = r->pos[i] + fx_mul(d[i], t);
                int32_t q[3];
                q[0] = rnd_signed();
                q[1] = rnd_signed();
                q[2] = rnd_signed();
                for (int i = 0; i < 3; i++) q[i] = fx_mul(q[i], 0xccc);
                int32_t dot = fx_mul(u[2], q[2]) + fx_mul(u[0], q[0]) + fx_mul(u[1], q[1]);
                for (int i = 0; i < 3; i++) p[i] += q[i] - fx_mul(u[i], dot);
                r->glint[(int)r->head] = rnd() < 0x8001 ? 0 : 1;
                int8_t h = r->head;
                r->head = (int8_t)(h + 1);
                if (r->head > 0x13) r->head = (int8_t)(h - 0x13);
            }
            r->count = (int8_t)(r->count + 4);
            if (r->count > 0x13) r->count = 0x13;
            r->fuse -= 0x10000;
            if (r->fuse < 1) {
                /* (the whistle's voice kept as a byte: it is found only for sound 0's) */
                if (r->whistle != -1 && Voice_Playing(r->whistle)) Voice_Stop(r->whistle);
                r->state = 2;
                gw32(0x5909b8, 0x10000);
                gw32(0x5909c4, (uint32_t)tbl(0x51f504, r->colour));
                if (!r->sparks) {
                    if (r->kind == 2) {
                        int m = (crt_rand() & 3) + 1;
                        for (int j = 0; j < m; j++) {
                            int32_t v[3];
                            v[0] = rnd_signed();
                            v[1] = rnd_signed();
                            v[2] = rnd_signed();
                            for (int i = 0; i < 3; i++) v[i] = fx_mul(v[i], 0xe666);
                            spawn(r->pos, v, true);
                        }
                    }
                } else {
                    memcpy(r->pos_was, r->pos, sizeof r->pos);
                    memcpy(r->trail_was, r->trail, sizeof r->trail);
                    for (int ring = 0; ring < 3; ring++)
                        for (int d2 = 0; d2 < 3; d2++) {
                            int a = ring * 6 + d2, b = a + 3;
                            memset(r->spark[a], 0, 12), memset(r->spark[b], 0, 12);
                            memset(r->spark_was[a], 0, 12), memset(r->spark_was[b], 0, 12);
                            int32_t s = fx_mul(rnd(), 0xa3d) + r->speed;
                            for (int i = 0; i < 3; i++) r->spark_vel[a][i] = fx_mul(fx_mul(dirs[ring][d2][i], s), 0x60000);
                            memcpy(r->spark_vel[b], r->spark_vel[a], 12);
                            r->spark_vel[b][1] = -r->spark_vel[a][1];
                        }
                    Voice_Play(crt_rand() % 6 + 2 + sounds, 0x10000, 0x5622, 0, false, false);
                }
            }
        } else if (r->state == 2) {
            /* burst: the trail drawn in, the sparks falling, the flicker, out when burnt */
            if (r->count > 0) {
                int8_t h = r->head;
                r->count = (int8_t)(r->count - 4);
                r->head = (int8_t)(h + 4);
                if (r->head > 0x13) r->head = (int8_t)(h - 0x10);
            }
            for (int s = 0; s < SPARKS; s++) {
                r->spark_vel[s][1] -= r->burst_g;
                for (int i = 0; i < 3; i++) r->spark[s][i] += r->spark_vel[s][i];
            }
            r->burn -= 0x10000;
            r->flicker = !r->flicker;
            if (r->burn < 1) r->state = 0;
        }
    }
}

/* 0x480220: the flash (decaying by 0.5 a step) by its square root in the flash colour over the low
   light colour */
void Fireworks_Light(void)
{
    uint32_t f = gm32(0x5909b8), l = f ? Fx_Sqrt(f) : 0, fc = gm32(0x5909c4), base = gm32(0x5909c0);
    uint8_t c[4] = {0};
    for (int i = 0; i < 3; i++) {
        int32_t v = (int32_t)((uint32_t)fx_mul((int32_t)((fc >> 8 * i & 0xff) << 16), (int32_t)l) >> 16) + (int32_t)(base >> 8 * i & 0xff);
        c[i] = (uint8_t)(v > 0xff ? 0xff : v);
    }
    int32_t nf = (int32_t)f - 0x8000;
    gw32(0x5909b8, (uint32_t)(nf < 0 ? 0 : nf));
    Light_SetLow(c, true);
}

void Fireworks_Interp(int32_t frac)
{
    for (int k = 0; k < nrockets; k++) {
        Rocket *r = &rockets[k];
        if (!r->state) continue;
        for (int i = 0; i < 3; i++) r->pos_at[i] = r->pos_was[i] + fx_mul(r->pos[i] - r->pos_was[i], frac);
        for (int t = 0; t < TRAIL; t++)
            for (int i = 0; i < 3; i++) r->trail_at[t][i] = r->trail_was[t][i] + fx_mul(r->trail[t][i] - r->trail_was[t][i], frac);
        for (int s = 0; s < SPARKS; s++)
            for (int i = 0; i < 3; i++) r->spark_at[s][i] = r->spark_was[s][i] + fx_mul(r->spark[s][i] - r->spark_was[s][i], frac);
    }
}

/* 0x47f510: a spark colour: fading out over the last 10 steps (from a or, past the double burst's
   turn, b); a double burst blends from a to b over 5 steps either side of its turn */
static void spark_colour(const Rocket *r, uint8_t out[4], const uint8_t a[4], const uint8_t b[4])
{
    int32_t t = r->burn;
    if (t < 0xa0001) {
        int32_t f = fx_mul(t, 0x1999);
        if (f > 0x10000) f = 0x10000;
        else if (f < 0) f = 0;
        const uint8_t *c = r->twice ? b : a;
        for (int i = 0; i < 3; i++) out[i] = (uint8_t)(((int64_t)(c[i] << 16) * f) >> 32);
        out[3] = 0xff;
        return;
    }
    if (r->twice) {
        t -= r->half;
        if (t < 0x50001) {
            if (t < -0x50000) {
                memcpy(out, b, 4);
                return;
            }
            int32_t f = fx_mul(t, 0x1999) + 0x8000;
            for (int i = 0; i < 3; i++) {
                int32_t v = fx_mul((a[i] << 16) - (b[i] << 16), f) + (b[i] << 16);
                if (v > 0xff0000) v = 0xff0000;
                out[i] = (uint8_t)((uint32_t)v >> 16);
            }
            out[3] = 0xff;
            return;
        }
    }
    memcpy(out, a, 4);
}

static void put(Sprite3d *s, const int32_t p[3], const uint8_t c[4])
{
    memcpy(s->pos, p, 12);
    memcpy(s->rgba, c, 4);
    Sprite3d_Add(s, tex);
}

/* 0x47f740: a rising rocket as a big sprite; a burst's sparks four ways, the colour of the pair by the
   spark's pick, swapped on the flicker; the trail's points shown, brightest at the head, a glint 100
   brighter */
void Fireworks_Draw(void)
{
    int from = 0;
    for (int k = 0; k < nrockets; k++) {
        Rocket *r = &rockets[k];
        if (r->state == 1) {
            put(&big, r->pos_at, r->col[0]);
            from = 0;
        } else if (r->state == 2) {
            uint8_t c0[4], c1[4];
            spark_colour(r, c0, r->col[2], r->col[3]);
            spark_colour(r, c1, r->col[4], r->col[5]);
            if (r->sparks)
                for (int s = 0; s < SPARKS; s++) {
                    const int32_t *q = r->spark_at[s];
                    static const int sx[4] = {1, -1, -1, 1}, sz[4] = {1, 1, -1, -1};
                    for (int m = 0; m < 4; m++) {
                        if (!r->on[s * 4 + m]) continue;
                        int32_t p[3] = {r->pos_at[0] + sx[m] * q[0], r->pos_at[1] + q[1], r->pos_at[2] + sz[m] * q[2]};
                        bool second = r->pick[s * 4 + m] ? r->flicker : !r->flicker;
                        put(&small, p, second ? c1 : c0);
                    }
                }
            from = TRAIL - r->count;
        }
        if (!r->state) continue;
        int t = from + r->head;
        if (t > 0x13) t -= TRAIL;
        int32_t f = 0x3333;
        for (int n = 0; n < r->count; n += 4) {
            uint8_t dim[4], lit[4];
            for (int i = 0; i < 3; i++) {
                dim[i] = (uint8_t)(((int64_t)(r->col[1][i] << 16) * f) >> 32);
                lit[i] = (uint8_t)(((int64_t)((r->col[1][i] << 16) + 0x640000) * f) >> 32);
            }
            dim[3] = 0xff, lit[3] = 0xff;
            f += 0x3333;
            for (int j = 0; j < 4; j++) {
                put(&small, r->trail_at[t], r->glint[t] ? lit : dim);
                if (++t > 0x13) t -= TRAIL;
            }
        }
    }
}
