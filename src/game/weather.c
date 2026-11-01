/* The rally's weather (see weather.h). */
#include "pending.h"
#include "game/weather.h"
#include "crt.h"
#include "fixmath.h"
#include "game/gstate.h"
#include "game/mem.h"
#include "game/timer.h"
#include "world/tod.h"
#include "exe.h"
#include "game/common.h"
#include "game/camera.h"
#include "game/pause.h"
#include "game/race.h"
#include "render/polyq.h"
#include "render/sprite3d.h"
#include "render/tex.h"
#include "sim/car.h"
#include "sim/grip.h"
#include "sim/progress.h"
#include "sim/sim.h"
#include "world/hpc.h"
#include "world/relight.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

enum { W = 0x52f100, MAIN = 0x52f1a0, RAIN = 0x52f1f0, RAIN2 = 0x52f240, LIGHT = 0x52f294 };

static uint32_t rally(void) { return gm32(0x52f2ac) & 0x1f; }
/* 0x4077d0: the stage's start (0) or end (1) time, hhmm */
static uint32_t stage_time(uint32_t r, uint32_t s, int which) { return gm8(0x516188 + (s + r * 12) * 2 + (uint32_t)which) * 100u; }
static int32_t fdiv(int32_t a, int32_t b) { return (int32_t)(((int64_t)a << 16) / b); }
/* rand() as a 16.16 fraction: rand * (float)(1/32767) * 65536.0, rounded */
static int32_t rand16(void) { return (int32_t)llrint((double)crt_rand() * (double)3.0518509447574615e-05f * 65536.0); }
static uint32_t table(uint32_t c) { return 0x51627c + (c + rally() * 3) * 0x28; }

/* 0x40dbe0: one weather for every stage */
static void fixed(uint32_t type)
{
    for (uint32_t i = 0; i < 0x14; i++) {
        gw32(W + 8 * i, type);   /* covers both words of every stage */
        gw32(W + 4 + 8 * i, type);
    }
}

/* 0x40d6c0: for each stage of block c the type most common within 1-20 percent either side of its
   share (the end's: the odd entries) */
static void spread(uint32_t c, uint32_t tbl, const int8_t *pct)
{
    int32_t out[4];
    for (int j = 0; j < 4; j++) {
        int32_t cnt[9] = {0};
        int p = pct[2 * j + 1];
        int lo = p - crt_rand() % 0x14 - 1, hi = crt_rand() % 0x14 + 1 + p;
        if (lo < 0) lo = 0;
        else if (hi > 100) hi = 100;
        int n = 0;
        if (lo < hi) {
            n = hi - lo;
            for (int i = lo; i < hi; i++) cnt[gm32(tbl + 4u * (uint32_t)(i / 10))]++;
        }
        int best = -1, bv = -100;
        for (int t = 0; t < 9; t++) {
            cnt[t] = fx_mul(fdiv(cnt[t] << 16, n << 16), 0x640000) >> 16;
            if (bv < cnt[t]) best = t, bv = cnt[t];
        }
        out[j] = best;
    }
    for (int j = 0; j < 4; j++) gw32(MAIN + c * 16 + 4u * (uint32_t)j, c == 2 && j > 1 ? 0 : (uint32_t)out[j]);
}

/* 0x40d9e0: the light of block c's stages, between the rally's dark and bright levels by the weather,
   by the time of day: dark before 5:01 and from 21:00, rising to 12:00, bright to 16:00, falling */
static void light(uint32_t c)
{
    uint32_t r = rally();
    for (uint32_t j = 0; j < 4; j++) {
        uint32_t s = c * 4 + j;
        int32_t a = gms32(W + 8 * s), b = gms32(W + 4 + 8 * s);
        if (a > 5) a -= 3;
        if (b > 5) a -= 3;   /* the second type's shift taken off the first, as the original does */
        int32_t f = fdiv(a + b, 10);
        if (f < 0) f = 0;
        else if (f > 0x10000) f = 0x10000;
        int32_t hi = gms32(0x516974 + 4 * r) + fx_mul(gms32(0x516998 + 4 * r), 0x10000 - f);
        int32_t lo = gms32(0x5169bc + 4 * r) + fx_mul(gms32(0x5169e0 + 4 * r), 0x10000 - f);
        uint32_t t = stage_time(r, s, 0);
        int32_t v;
        if (t < 0x1f5 || t > 0x833) v = lo >> 16;
        else if (t < 0x4b0) v = (fx_mul(hi - lo, fdiv((int32_t)(t / 100 - 5), 7)) + lo) >> 16;
        else if (t < 0x641) v = hi >> 16;
        else v = (fx_mul(lo - hi, fdiv((int32_t)(t / 100 - 0x10), 5)) + hi) >> 16;
        gw8(LIGHT + s, (uint8_t)v);
    }
}

/* 0x40d820: rain in the rallies that have it on wet stages (rally 6 stages 3, 6 and 8, rally 7 the
   first and third blocks), by day: a fifth of the time heavy (0x52f1f0), and whether any of the
   spread is above 80 (0x52f240) */
static void rain(void)
{
    for (uint32_t s = 0; s < 11; s++) {
        gw32(RAIN + 4 * s, 0);
        gw32(RAIN2 + 4 * s, 0);
        if (gm32(W + 8 * s) != 2 || gm32(W + 4 + 8 * s) != 2) continue;
        uint32_t r = Tod_Rally();
        bool wet = (r == 6 && (s == 2 || s == 5 || s == 7)) || (r == 7 && (s / 4 == 0 || s / 4 == 2));
        if (!wet || stage_time(rally(), s, 0) >= 0x834) continue;
        int v = fx_mul(rand16(), 0x640000) >> 16;
        if (v > 0x50) gw32(RAIN + 4 * s, 1);
        int lo = v - crt_rand() % 0x14 - 1, hi = crt_rand() % 0x14 + 1 + v;
        if (lo < 0) lo = 0;
        else if (hi > 100) hi = 100;
        int32_t c[2] = {0, 0}, n = 0;
        if (lo < hi) {
            n = hi - lo;
            for (int i = lo; i < hi; i++) c[i < 0x51 ? 1 : 0]++;
        }
        for (int k = 0; k < 2; k++) c[k] = fx_mul(fdiv(c[k] << 16, n << 16), 0x640000) >> 16;
        gw32(RAIN2 + 4 * s, c[0] != 0);
    }
}

void Weather_Choose(void)
{
    uint32_t m = Gs_Mode();
    if ((m == 2 || m == 9) && gm32(0x52f290)) {
        /* a weather chosen on the menus ("here" on the console) */
        fixed(gm32(0x52f0fc));
        for (uint32_t i = 0; i < 0x14; i++) gw32(MAIN + 4 * i, gm32(0x52f0fc)), gw32(RAIN + 4 * i, 0), gw32(RAIN2 + 4 * i, 0);
        light(0), light(1), light(2);
        return;
    }
    crt_srand(Time_Get());   /* srand(timeGetTime()) */
    bool same = Gs_SelBits14() > 1 && !Gs_ModeBit17();
    int32_t prev = same;
    for (uint32_t c = 0; c < 3; c++) {
        int32_t v[8];
        int8_t pct = 0, pc[8];
        int32_t dur = 0;
        uint32_t tbl = table(c);
        for (int k = 0; k < 8; k++) {
            if (!Gs_SelBit28()) {
                if (dur < 1) {
                    int32_t f = rand16(), u = fx_mul(f, 0xa0000);
                    int32_t type = gms32(tbl + 4u * (uint32_t)(u >> 16));
                    pct = (int8_t)(((int64_t)u * 0xa0000) >> 32);
                    if (k > 0) {
                        bool shifted = false;
                        int32_t a = prev, b = type;
                        if (a > 5) a -= 3, shifted = true;
                        if (b > 5) b -= 3, shifted = true;
                        int32_t d = b - a, lim = gm8(0x516968 + (uint32_t)prev);
                        if (lim < (d < 0 ? -d : d)) d = d < 1 ? -lim : lim;
                        int32_t t = a + d;
                        if (t > 2 && shifted) t += 3;
                        if (type != t) {
                            int idx = -1;
                            for (int i = 0; i < 10; i++)
                                if (t <= gms32(tbl + 4u * (uint32_t)i)) { idx = i; break; }
                            pct = (int8_t)(crt_rand() % 10 + idx * 10);
                            type = t;
                        }
                    }
                    dur = (crt_rand() % (gm8(0x51695c + (uint32_t)type) + 1) + gm8(0x5338f0 + (uint32_t)type)) * 2;
                    if (k) dur++;
                    prev = type;
                } else
                    dur--;
                if (c == 2 && k > 3) v[k] = 0, pc[k] = 0;
                else v[k] = prev, pc[k] = pct;
            } else {
                pc[k] = 0;
                v[k] = gms32(tbl);
            }
        }
        for (uint32_t j = 0; j < 4; j++) {
            gw32(W + c * 0x20 + 8 * j, (uint32_t)v[2 * j]);
            gw32(W + 4 + c * 0x20 + 8 * j, (uint32_t)(same ? v[2 * j] : v[2 * j + 1]));
        }
        spread(c, tbl, pc);
        light(c);
    }
    for (uint32_t s = 0; s < 11; s++)
        if (stage_time(rally(), s, 0) > 0x833) {
            gw32(W + 8 * s, 2);
            gw32(W + 4 + 8 * s, 2);
            gw32(MAIN + 4 * s, 2);
        }
    rain();
}


/* ---- The race's weather (0x45e5b0-0x462d80) ---- */

/* A player's weather record (0x178 bytes, the block at *0x547ac8; allocated for every car of the race,
   only the players' first 0x543e98 are kept). The comments give the original's offsets. */
typedef struct {
    int32_t type, want;      /* +0x00, +0x04: 0 none, 1 rain, 2 snow; the type it is to become */
    int32_t box[3];          /* +0x08: the centre of the box of drops around the view */
    int32_t ahead[3];        /* +0x14: last frame's point ahead of the camera */
    int32_t rel[3];          /* +0x20: the drops' fall less the box's motion, per frame */
    int32_t wind[3];         /* +0x2c: the drops' fall per frame (the wind and the drop) */
    int32_t side[3];         /* +0x38: snow: the camera's side (the flakes' sway) */
    int32_t step[3];         /* +0x44: the fall per frame scaled, negated (the splats' ray) */
    int32_t inv_dy;          /* +0x50: 1 / step y */
    int32_t level;           /* +0x54: 0 to 1.0 */
    int32_t amount, target;  /* +0x58, +0x5c: the drops drawn (16.16) and their aim */
    int32_t rate;            /* +0x60: the amount's change a frame */
    int32_t cover;           /* +0x64: 1.0 in the open, to 0 under a roof (0x45e9a0) */
    int32_t glow;            /* +0x68: the sun glow's eased visibility (0x462d80) */
    uint32_t point, last;    /* +0x6c, +0x70: the centre-line point the view's car is at, and last */
    int16_t ndrops, first;   /* +0x74, +0x76: its share of the 400 drops (0x543fb0) and the first */
    int16_t tri[25];         /* +0x78: rain: the ground grid's last triangles (5 x 5 cells of 4.8) */
    int32_t ground[25];      /* +0xac: the grid's ground heights */
    int32_t lift[25];        /* +0x110: the box's height over the ground less 6.1, per cell */
    int32_t covered;         /* +0x174: under a roof */
} WxRec;

/* per car, along the stage: the sky's blend (0x543eb8, 0x2c bytes) and the wet value (0x543ecc, 0xc) */
typedef struct {
    uint32_t point, last;    /* +0x00, +0x04 */
    int32_t base, part;      /* +0x08: by the point; +0x0c: within its segment */
    int32_t blend;           /* +0x10 = base + part */
    int32_t heading;         /* +0x14: the clouds' heading, degrees 16.16 (0x45f5d0) */
    int32_t prev;            /* +0x18: last step's blend */
    int32_t blend_f;         /* +0x1c: the blend at the frame's fraction (0x461bb0) */
    int32_t heading_prev;    /* +0x20 */
    int32_t heading_f;       /* +0x24 */
    int32_t dirty;           /* +0x28: the sky to be set again */
} WxSky;
typedef struct { uint32_t point, last; int32_t value; } WxWet;

static WxRec rec[CAR_MAX];
static WxSky sky[CAR_MAX];
static WxWet wet[CAR_MAX];
static int nalloc;            /* DAT_00547acc */
static Tex *t_rain, *t_splat, *t_splat2, *t_snow[3], *t_sunglow;   /* 0x543e90, 0x543ea4, 0x5477f0, 0x543ea8, 0x547ad0 */

/* the game state's variables (their addresses) */
enum {
    SCALE = 0x543d50,     /* the effects detail's share: 1.0, 0.25 or 0 */
    KIND = 0x543d54,      /* the stage's kind of weather (0x51b04c by type: 0 dry, 1 rain, 2 snow) */
    LV = 0x543d60,        /* the level along the stage: from, to, slope, first point, last point */
    WV = 0x543d74,        /* the wet value: the same five */
    SV = 0x543d88,        /* the sky's blend: the same five */
    N400 = 0x543da0,      /* the players / 400 */
    TRI = 0x543da8,       /* the rain streak's three 0x30-byte vertices (Poly_AddRaw) */
    SPLAT = 0x543ed0,     /* the splat's sprite (0x24 bytes) */
    FLAKE = 0x543f00,     /* the snowflake's */
    CARSPLAT = 0x547908,  /* the drops on the car's */
    DROPS = 0x543fb0,     /* 400 drops of 0x24 bytes */
    WIND_DIR = 0x547930, WIND_PREV = 0x54793c, WIND = 0x547940, WIND_AIM = 0x547944, WIND_RATE = 0x547948,
    WIND_HOLD = 0x54794c,
    BOLT_IN = 0x543ec0, BOLT_LEFT = 0x543ec4, BOLT = 0x543ec8, FLASH = 0x547ac0, FLASH_PREV = 0x547ac2,
};
/* a drop: +0x00 the position; +0x0c snow: the turn rate (0 to 4 degrees a frame); +0x10 the sway's
   size (0 to 1); +0x14 snow: the sway's angle (degrees 16.16), rain: the ground under it + 0.1; +0x18
   snow: its share of the wind's drift; +0x1c 75 (unused here); +0x1d the snowflake texture (0-2); +0x20
   it went through the ground this frame (a splat) */
static uint32_t drop(int i, uint32_t o) { return DROPS + 0x24 * (uint32_t)i + o; }

static int players(void) { return gm8(0x543e98); }
static int32_t scale(void) { return Frame_Scale(); }   /* DAT_0051bd3c */
static int32_t rnd(void) { return (int32_t)nearbyint((double)crt_rand() * (double)(float)(1.0 / 32767.0) * 65536.0); }
static int32_t share(const WxRec *r)   /* the amount as a share of the whole, at most 1 */
{
    int32_t v = fx_mul(r->amount, gms32(N400));
    return v > 0x10000 ? 0x10000 : v;
}
static void cam_rows(int p, int32_t pos[3], int32_t fwd[3])   /* 0x4bac40, 0x4baca0 on 0x538e2c[p] + 0x98 */
{
    Obj *o = Race_Camera(p);
    for (int k = 0; k < 3; k++) {
        if (pos) pos[k] = o ? o->local.m[12 + k] : 0;
        if (fwd) fwd[k] = o ? o->local.m[8 + k] : 0;
    }
}
/* 0x4209d0: the car's share of its centre-line segment (0x538ab8, the progress record's +0x10) */
static int32_t seg_frac(const Car *c)
{
    (void)c;
    PENDING_NOTE(0x4209d0, "the progress record's segment fraction (sim/progress.c has no accessor)");
    return 0;
}

void Weather_Alloc(int n)
{
    if (n > CAR_MAX) n = CAR_MAX;
    memset(rec, 0, sizeof rec), memset(sky, 0, sizeof sky), memset(wet, 0, sizeof wet);   /* zeroed blocks */
    nalloc = n;
}

static Tex *load_tex(const char *path)
{
    char p[260];
    snprintf(p, sizeof p, "%s", path);
    uint8_t found;
    return Bfl_FindTexture(Bfl_CommonArc(), p, &found, false, 0);
}

/* "%s%s" (0x517d98) of 0x40ed50's \Textures and a name */
static Tex *load_name(uint32_t name)
{
    char p[260];
    snprintf(p, sizeof p, "%s%s", exe_str(0x516a50), exe_str(name));
    return load_tex(p);
}

void Weather_Load(void)
{
    t_rain = load_name(0x51b194);
    t_splat = load_name(0x51b180);
    t_splat2 = load_name(0x51b168);
    for (int i = 0; i < 3; i++) {
        char p[260];
        snprintf(p, sizeof p, exe_str(0x51b150), exe_str(0x516a50), i + 1);   /* %s\NEWIMAGE\snow%d.tga */
        t_snow[i] = load_tex(p);
    }
    t_sunglow = load_name(0x51b138);
    Tex_SetBlend(t_sunglow, 1);
}

/* 0x45ea70 (Stage_Load): the stage's weather from its start and end types (Tod_Weather): the kind
   (0x51b04c; the end's when only the end has one, the start's level then 0, and the end's level 0
   when only the start has one), the levels (0x51b070) and the wet values (0x51b094); a change of level
   runs from a fifth of the way to the finish less a twenty-fifth (the original divides the fifth by
   five again for the end, not the finish) */
void Weather_StageLoad(void)
{
    const int32_t *w = Tod_Weather(Tod_Stage());
    int32_t end_kind = gms32(0x51b04c + 4 * (uint32_t)w[1]);
    gw32(KIND, gm32(0x51b04c + 4 * (uint32_t)w[0]));
    gw32(0x543e9c, gm32(0x51b070 + 4 * (uint32_t)w[0]));
    gw32(0x543fa8, gm32(0x51b070 + 4 * (uint32_t)w[1]));
    gw32(0x543e88, gm32(0x51b094 + 4 * (uint32_t)w[0]));
    gw32(0x543d9c, gm32(0x51b094 + 4 * (uint32_t)w[1]));
    if (gm32(KIND) == 0) {
        if (end_kind != 0) gw32(0x543e9c, 0), gw32(KIND, (uint32_t)end_kind);
    } else if (end_kind == 0)
        gw32(0x543fa8, 0);
    if (gm32(0x543e9c) != gm32(0x543fa8)) {
        uint32_t a = (uint32_t)Cat_FinishPoint() / 5;
        gw32(0x543e8c, a);
        gw32(0x543e94, (uint32_t)(Cat_FinishPoint() - (int32_t)a / 5));
    } else
        gw32(0x543e8c, 0), gw32(0x543e94, 0);
}

void Weather_Change(int32_t *wet_a, int32_t *wet_b, int32_t *at_a, int32_t *at_b)
{
    *wet_a = gms32(0x543e88);
    *wet_b = gms32(0x543d9c);
    *at_a = (int32_t)((int64_t)gms32(0x543e8c) * 65536);   /* (double) * 65536.0, rounded */
    *at_b = (int32_t)((int64_t)gms32(0x543e94) * 65536);
}

/* 0x45f300, 0x45e710, 0x45f3d0: a value from `from` to `to` between two centre-line points: the
   slope (to - from) / (p1 - p0), the division only when p1 > p0 (else the difference itself scales) */
static void ramp(uint32_t at, int32_t from, int32_t to, int32_t p0, int32_t p1)
{
    gw32(at, (uint32_t)from), gw32(at + 4, (uint32_t)to), gw32(at + 0x10, (uint32_t)p1), gw32(at + 0xc, (uint32_t)p0);
    int32_t d = (p1 - p0) * 0x10000;
    if (d > 0) d = (int32_t)(0x100000000LL / d);
    gw32(at + 8, (uint32_t)fx_mul(d, to - from));
}

/* 0x460c30: record n's level, and the drops it aims at: level x detail x its share of the 400 */
static void set_level(int32_t v, int n)
{
    rec[n].level = v;
    rec[n].target = fx_mul(fx_mul(gms32(SCALE), v), rec[n].ndrops << 16);
}

/* 0x45f300: the level's ramp; the players' records of the stage's kind at the start level */
static void level_setup(int32_t from, int32_t to, int32_t p0, int32_t p1, int32_t kind)
{
    ramp(LV, from, to, p0, p1);
    gw8(0x543e98, (uint8_t)Gs_SelBits14());
    for (int i = 0; i < players(); i++) {
        rec[i].want = kind, rec[i].type = kind;
        rec[i].last = 0, rec[i].point = 0;
        set_level(from, i);   /* (with last race's share of the drops: 0x45eed0 sets it again) */
    }
}

/* 0x45f3d0: the sky's blend ramp; every car's record at its start */
static void sky_setup(int32_t from, int32_t to, int32_t p0, int32_t p1)
{
    ramp(SV, from, to, p0, p1);
    for (int i = 0; i < nalloc; i++) {
        WxSky *s = &sky[i];
        s->last = 0, s->point = 0;
        s->base = gms32(SV), s->part = 0, s->blend = gms32(SV), s->prev = gms32(SV);
        s->dirty = 1;
    }
}

/* 0x45e710: the wet value's ramp; every car's record at its start, and its grip set by it */
static void wet_setup(int32_t from, int32_t to, int32_t p0, int32_t p1)
{
    ramp(WV, from, to, p0, p1);
    for (int i = 0; i < nalloc; i++) {
        wet[i].last = 0, wet[i].point = 0, wet[i].value = gms32(WV);
        Car_SurfaceGrip(Car_Get(i), Weather_CarWet(Car_Get(i)));
        CW32(Car_Get(i), 0xa74, CI32(Car_Get(i), 0xa78));   /* 0x4789b0 */
    }
}

/* 0x45f260: the ramps; the sky's from 0 to 1 over the stage when the time or the weather changes
   (not on circuits) */
static void ramps_setup(void)
{
    const int32_t *w = Tod_Weather(Tod_Stage());
    int32_t p1 = 0, to = 0, p0 = 0;
    if ((Tod_EndTime() != Tod_StartTime() || w[0] != w[1]) && !Cat_Circuit()) p1 = Cat_FinishPoint() - 1, to = 0x10000, p0 = 1;
    sky_setup(0, to, p0, p1);
    level_setup(gms32(0x543e9c), gms32(0x543fa8), gms32(0x543e8c), gms32(0x543e94), gms32(KIND));
    wet_setup(gms32(0x543e88), gms32(0x543d9c), gms32(0x543e8c), gms32(0x543e94));
}

/* 0x45eed0: the drops shared between the players (400 / players each); per record its grid cleared,
   its amount at its aim, the box one unit ahead of its camera and its drops scattered through it
   (24 x 12 x 24) */
static void drops_setup(void)
{
    int n = players();
    gw32(N400, (uint32_t)(((uint64_t)(uint32_t)n << 32) / 0x1900000));
    for (int i = 0; i < n; i++) {
        WxRec *r = &rec[i];
        for (int k = 0; k < 25; k++) r->tri[k] = -1, r->ground[k] = 0, r->lift[k] = 0;
        r->ndrops = (int16_t)(400 / n);
        r->first = (int16_t)(i * r->ndrops);
        int32_t v = fx_mul(fx_mul(gms32(SCALE), r->level), r->ndrops << 16);
        r->target = v, r->amount = v;
        r->covered = 0;
        r->rate = 0x10000, r->cover = 0x10000, r->glow = 0x10000;
        r->wind[0] = r->wind[1] = r->wind[2] = 0;
        int32_t pos[3], f[3];
        cam_rows(i, pos, f);
        for (int k = 0; k < 3; k++) r->box[k] = pos[k] + f[k], r->ahead[k] = r->box[k];
        for (int j = 0; j < r->ndrops; j++) {
            int d = r->first + j;
            int32_t x = fx_mul(rnd(), 0x180000), y = fx_mul(rnd(), 0xc0000), z = rnd();
            gw32(drop(d, 0), (uint32_t)(x + r->box[0] - 0xc0000));
            gw8(drop(d, 0x1c), 0x4b);
            gw32(drop(d, 4), (uint32_t)(y + r->box[1] - 0x60000));
            gw32(drop(d, 8), (uint32_t)(fx_mul(z, 0x180000) + r->box[2] - 0xc0000));
            gw32(drop(d, 0xc), (uint32_t)fx_mul(rnd(), 0x40000));
            gw32(drop(d, 0x10), (uint32_t)rnd());
            gw32(drop(d, 0x14), (uint32_t)fx_mul(rnd(), 0x1680000));
            gw32(drop(d, 0x18), (uint32_t)rnd());
            gw8(drop(d, 0x1d), (uint8_t)(crt_rand() % 3));
        }
    }
}

/* the sprite and vertex templates the drawing fills (as Race_StartLights sets them) */
static void wf(uint32_t va, float f) { memcpy(gm(va), &f, 4); }
static void templates(void)
{
    /* the streak: colours grey 150 (the tail clear, the head's two corners alpha 30), specular black,
       coordinates (0.5, 0), (0.875, 1), (0.125, 1) (1 a hair under: 0x3f7fff58) */
    gw32(0x543dc4, 0xff000000), gw32(0x543dc0, 0x969696);
    gw32(0x543e24, 0xff000000), gw32(0x543e20, 0x1e969696);
    gw32(0x543df4, 0xff000000), gw32(0x543df0, 0x1e969696);
    gw32(0x543dc8, 0x3f000000), gw32(0x543dcc, 0);
    gw32(0x543e28, 0x3e000000), gw32(0x543e2c, 0x3f7fff58);
    gw32(0x543df8, 0x3f600000), gw32(0x543dfc, 0x3f7fff58);
    /* the flake: 0.15 a side, white, alpha 254 (the light ramp's colour overwrites it) */
    gw32(0x543f0c, 0xffffd99a), gw32(0x543f10, 0x2666), gw32(0x543f14, 0x2666), gw32(0x543f18, 0xffffd99a);
    gw32(0x543f1c, 0xfeffffff);
    /* the splat: 0.04 by 0.07, grey 170 */
    gw32(0x543edc, 0xfffff5c3), gw32(0x543ee0, 0x11eb), gw32(0x543ee4, 0xa3d), gw32(0x543ee8, 0xffffee15);
    gw32(0x543eec, 0xaaaaaaaa);
    /* the drop on the car: 0.06 a side, grey 170 */
    gw32(0x547914, 0xfffff0a4), gw32(0x547918, 0xf5c), gw32(0x54791c, 0xf5c), gw32(0x547920, 0xfffff0a4);
    gw32(0x547924, 0xaaaaaaaa);
}

/* Race_StartLights 0x45eca0 */
void Weather_RaceStart(void)
{
    gw32(0x543ef8, 1);
    ramps_setup();
    uint32_t d = gm32(0x52afd0) >> 0x19 & 3;   /* 0x405cd0: the effects detail */
    gw32(SCALE, d == 0 ? 0x10000 : d == 1 ? 0x4000 : 0);
    templates();
    /* 0x45eea0: the wind along (0.707, 0, -0.707), still */
    gw32(WIND_DIR, 0xb4fd), gw32(WIND_DIR + 4, 0), gw32(WIND_DIR + 8, 0xffff4b03);
    gw32(WIND, 0), gw32(WIND_AIM, 0), gw32(WIND_RATE, 0), gw32(WIND_HOLD, 0);
    drops_setup();
    gw32(BOLT, 0xffff), gw32(BOLT_IN, 0), gw32(BOLT_LEFT, 0);   /* 0x45f240 */
    PENDING_NOTE(0x463070, "Sun_Direction (the sky's sun into the shadows' direction 0x5477f8)");
    /* the low and high light from the start's horizon (0x547998, 0x5479a4), every cell relit */
    Tod_StartLights();
    PENDING_NOTE(0x4b5760, "the light direction from the sun (0x5477f8)");
}

/* 0x45e610 (the replays' restarts 0x46c2a0, 0x46c390, 0x46c410, not ported): the players' records at
   the start level, full and in the open; their wet values at the start */
void Weather_Restart(void)
{
    for (int i = 0; i < players(); i++) {
        set_level(gms32(LV), i);
        rec[i].amount = rec[i].target;
        rec[i].covered = 0, rec[i].cover = 0x10000, rec[i].glow = 0x10000;
        wet[i].last = 0, wet[i].point = 0, wet[i].value = gms32(WV);
    }
}

/* a ramp's value at a point: `from` up to the first point, `to` from the last (`le`: the first point
   itself counts as before it) */
static int32_t ramp_at(uint32_t at, uint32_t pt)
{
    uint32_t p0 = gm32(at + 0xc), p1 = gm32(at + 0x10);
    if (pt <= p0) return gms32(at);
    if (pt >= p1) return gms32(at + 4);
    return fx_mul((int32_t)((pt - p0) * 0x10000), gms32(at + 8)) + gms32(at);
}

/* Race_Step 0x45e7f0 (in Race_Update, after the opponents): every car's wet value by its point (not on
   circuits), from the last listed car */
void Weather_CarsStep(void)
{
    if (Cat_Circuit()) return;
    for (int i = g_all.n - 1; i >= 0; i--) {
        Car *c = Car_Get(g_all.car[i]);
        WxWet *w = &wet[CI8(c, 0xb1a)];
        uint32_t pt = (uint32_t)Car_CatIndex(c);
        w->point = pt;
        if (pt != w->last) w->last = pt, w->value = ramp_at(WV, pt);
    }
}

int32_t Weather_CarWet(const Car *c)
{
    int n = CI8(c, 0xb1a);
    return n >= 0 && n < CAR_MAX ? wet[n].value : 0;
}

/* 0x45f6d0: the wind's gusts: a hold of 25 to 100 frames, then the strength eased to a new aim (0 to
   1, at 0.005 to 0.03 a frame), a new hold and aim when it gets there; halved aim and rate when a
   player has snow */
static void wind_step(void)
{
    int32_t hold = gms32(WIND_HOLD);
    if (hold != 0) {
        gw32(WIND_HOLD, hold - scale() < 0 ? 0 : (uint32_t)(hold - scale()));
        return;
    }
    int32_t cur = gms32(WIND), aim = gms32(WIND_AIM);
    if (cur != aim) {
        int32_t st = fx_mul(gms32(WIND_RATE), scale());
        bool there;
        if (aim < cur) {
            cur -= st;
            there = cur == aim;
            if (cur < aim) cur = aim, there = true;
        } else {
            cur += st;
            there = cur == aim;
            if (aim < cur) cur = aim, there = true;
        }
        gw32(WIND, (uint32_t)cur);
        if (!there) return;
    }
    gw32(WIND_HOLD, (uint32_t)(fx_mul(rnd(), 0x4b0000) + 0x190000));
    gw32(WIND_AIM, (uint32_t)fx_mul(rnd(), 0x10000));
    gw32(WIND_RATE, (uint32_t)(fx_mul(rnd(), 0x667) + 0x147));
    gw32(WIND_PREV, (uint32_t)cur);
    bool snow = false;
    for (int i = 0; i < players(); i++)
        if (rec[i].type == 2) { snow = true; break; }
    if (snow) {
        gw32(WIND_AIM, (uint32_t)fx_mul(gms32(WIND_AIM), 0x8000));
        gw32(WIND_RATE, (uint32_t)fx_mul(gms32(WIND_RATE), 0x8000));
    }
}

/* 0x492910 (the sky's vertex nearest the camera's direction for the bolt, -1 none) and 0x492b50 (one of
   three lightning colours into the sky's vertices): the sky's colours are not ported */
static int16_t sky_bolt(void)
{
    PENDING_NOTE(0x492910, "Sky_BoltVertex");
    return -1;
}
static void sky_bolt_colour(void)
{
    PENDING_NOTE(0x492b50, "Sky_BoltColour");
    crt_rand();   /* its one draw (rand() % 3) */
}

/* 0x45f890: the lightning: a wait of 125 to 500 frames, then a bolt (a sky vertex) for 20 to 40; each
   frame the flash is on 30 percent of the time when the first player's rain is above 0.8 */
static void lightning_step(void)
{
    int32_t in = gms32(BOLT_IN);
    if (in == 0) {
        int32_t left = gms32(BOLT_LEFT) - scale();
        gw32(BOLT_LEFT, (uint32_t)left);
        if (left < 0) {
            gw32(BOLT_IN, (uint32_t)(0x7d0000 + fx_mul(rnd(), 0x1770000)));
            gw32(BOLT_LEFT, (uint32_t)(fx_mul(rnd(), 0x140000) + 0x140000));
            gw16(BOLT, 0xffff);
        }
    } else {
        in -= scale();
        gw32(BOLT_IN, (uint32_t)in);
        if (in < 0) {
            gw16(BOLT, (uint16_t)sky_bolt());
            sky_bolt_colour();
            gw32(BOLT_IN, 0);
        }
    }
    gw16(FLASH_PREV, gm16(FLASH));
    if (rnd() < 0x4ccc && rec[0].level > 0xcccc && rec[0].type == 1) gw16(FLASH, gm16(BOLT));
    else gw16(FLASH, 0xffff);
}

/* 0x45f530: the record's level by the point its camera's car is at (not on circuits) */
static void level_step(WxRec *r, int n)
{
    if (Cat_Circuit()) return;
    uint32_t pt = (uint32_t)Car_CatIndex(Car_Get(n));   /* 0x422fb0: the camera's car */
    r->point = pt;
    if (pt == r->last) return;
    r->last = pt;
    set_level(ramp_at(LV, pt), n);
}

/* 0x45e8b0: the sky's blend by the point, and within its segment between the ramp's points */
static void sky_step(WxSky *s, int n)
{
    if (Cat_Circuit()) return;
    s->prev = s->blend;
    Car *c = Car_Get(n);
    uint32_t pt = (uint32_t)Car_CatIndex(c);
    s->point = pt;
    if (pt != s->last) s->last = pt, s->base = ramp_at(SV, pt);
    if (s->point < gm32(SV + 0x10) && gm32(SV + 0xc) <= s->point) s->part = fx_mul(gms32(SV + 8), seg_frac(c));
    else s->part = 0;
    s->blend = s->base + s->part;
}

/* 0x460a30: the wind's vector: its direction by the strength by the players' mean level (at least
   0.3), doubled when one has snow */
static void wind_vec(int32_t out[3])
{
    int n = players();
    bool snow = false;
    int32_t lv;
    if (n == 1) {
        lv = rec[0].level;
        if (rec[0].type == 2) snow = true;
        if (lv <= 0x4ccb) lv = 0x4ccc;
    } else {
        uint32_t sum = 0;
        for (int i = 0; i < n; i++) {
            sum += (uint32_t)rec[i].level;
            if (rec[i].type == 2) snow = true;
        }
        if (n) {
            lv = (int32_t)(((int64_t)(int32_t)sum << 16) / (n << 16));
            if (lv <= 0x4ccb) lv = 0x4ccc;
        } else
            lv = 0x4ccc;
    }
    int32_t u = fx_mul(lv, gms32(WIND));
    if (snow) u = fx_mul(u, 0x20000);
    for (int k = 0; k < 3; k++) out[k] = fx_mul(gms32(WIND_DIR + 4 * (uint32_t)k), u);
}

/* 0x421fe0: the heading of the camera's forward axis (0x538d30 + 100 p + 0x20), 4,096 a turn, 0
   along +z, positive toward +x */
static int16_t cam_heading(int p)
{
    int32_t f[3];
    Camera_Forward(p, f);
    uint32_t z = (uint32_t)(f[2] < 0 ? -f[2] : f[2]), x = (uint32_t)(f[0] < 0 ? -f[0] : f[0]);
    int16_t a;
    if (x == 0) a = 0;
    else if (z == 0) a = 0x400;
    else {
        bool neg = (x & 0x80000000u) != 0;   /* only for 0x80000000 */
        if (neg) x = (uint32_t)-(int32_t)x;
        if (z & 0x80000000u) z = (uint32_t)-(int32_t)z, neg = !neg;
        bool flip = (int32_t)z < (int32_t)x;
        uint32_t lo = flip ? z : x, hi = flip ? x : z;
        a = (int16_t)g_atan[(uint32_t)(((int64_t)(int32_t)lo << 16) / (int32_t)hi) >> 7];
        if (flip) a = (int16_t)(0x400 - a);
        if (neg) a = (int16_t)-a;
    }
    if (f[2] < 0) a = (int16_t)(0x800 - a);
    if (f[0] < 0) a = (int16_t)-a;
    return a;
}

/* 0x45f5d0: the clouds' heading turned by the wind across the view (6 percent of it), kept in 0 to
   360 (the last step's moved with it) */
static void heading_step(WxSky *s, int n)
{
    s->heading_prev = s->heading;
    int a = cam_heading(n) & 0xffff;
    int32_t v[3] = {-g_sin[a & 0xfff], 0, g_sin[(a + 0x400) & 0xfff]}, w[3];
    wind_vec(w);
    int32_t d = fx_mul(w[2], v[2]) + fx_mul(w[0], v[0]) + fx_mul(w[1], v[1]);
    int32_t h = s->heading + fx_mul(d, 0xf5c);
    s->heading = h;
    if (h > 0x1680000) s->heading = h - 0x1680000, s->heading_prev -= 0x1680000;
    else if (h < 0) s->heading = h + 0x1680000, s->heading_prev += 0x1680000;
}

/* 0x460270: the drops' fall: the wind by the share of drops; rain falls 1.2 to 2.0 a frame, snow 0.1
   to 0.3 */
static void fall(WxRec *r)
{
    int32_t a = share(r);
    int32_t u = fx_mul(a, gms32(WIND));
    for (int k = 0; k < 3; k++) r->wind[k] = fx_mul(gms32(WIND_DIR + 4 * (uint32_t)k), u);
    r->wind[1] = r->type == 1 ? -0x13333 - fx_mul(a, 0xcccd) : -0x1999 - fx_mul(a, 0x3333);
}

/* 0x45e9a0: under the roofs of rally 6 stage 1 (points 103-108) and rally 7 stage 3 (470-471) the
   drops fade out, 0.2 a frame, and back in the open */
static void cover_step(WxRec *r)
{
    r->covered = 0;
    if (Tod_Rally() == 6 && Tod_Stage() == 0 && r->point > 0x66 && r->point < 0x6d) r->covered = 1;
    if (Tod_Rally() == 7 && Tod_Stage() == 2 && r->point > 0x1d5 && r->point < 0x1d8) r->covered = 1;
    int32_t d = fx_mul(gms32(0x51bd3c), 0x3333);
    if (!r->covered) {
        r->cover += d;
        if (r->cover > 0x10000) r->cover = 0x10000;
    } else {
        r->cover -= d;
        if (r->cover < 0) r->cover = 0;
    }
}

/* 0x45f9d0: the box of drops moved with the camera and the drops with the wind. The box is centred
   14 ahead of the camera (further when zoomed), never lower than 6 above the camera or 6.75 above
   the car's wheels' ground; the drops move by the fall and against the box's own motion and wrap
   round it (24 across, 12 high). For rain the box's 5 x 5 grid of ground heights makes a drop that
   goes under the ground come back at the top, marked for a splat; snow sways by the wind's drift.
   With `step` the amount is eased toward its aim. */
static void drops_step(int step, WxRec *r, int p)
{
    int32_t drift[3] = {0, 0, 0}, dr = 0;
    fall(r);
    cover_step(r);
    int32_t pos[3], f[3];
    cam_rows(p, pos, f);
    Car *c = Car_Get(p);
    int32_t sum = 0;
    for (int k = 0; k < 4; k++) sum += CI32(c, 0x8dc + 4 * (uint32_t)k);
    int32_t ground = fx_mul(sum, 0x4000) + 0x6c000, floor_y = pos[1] + 0x60000;
    if (ground < floor_y) floor_y = ground;
    if (r->type == 2) {
        r->side[0] = f[2], r->side[1] = 0, r->side[2] = -f[0];
        Vec_Unit(r->side);
        int32_t u = fx_mul(gms32(WIND), 0x8000);
        drift[0] = fx_mul(gms32(WIND_DIR + 8), u);
        drift[2] = fx_mul(-gms32(WIND_DIR), u);
    }
    int32_t a[3], ahead[3];
    for (int k = 0; k < 3; k++) a[k] = fx_mul(f[k], 0xe0000), ahead[k] = pos[k] + a[k];
    if (ahead[1] < floor_y) ahead[1] = floor_y;
    int32_t dx = fx_mul(ahead[0] - r->ahead[0], gms32(0x51bd40)), dz = fx_mul(ahead[2] - r->ahead[2], gms32(0x51bd40));
    r->rel[0] = r->wind[0] - dx;
    r->rel[1] = r->wind[1];
    r->ahead[0] = ahead[0], r->ahead[1] = ahead[1];
    r->rel[2] = r->wind[2] - dz;
    r->ahead[2] = ahead[2];
    int32_t zoom = fx_mul((int32_t)(((int64_t)Camera_Zoom(p) << 16) / 0xa000) - 0x10000, 0x4ccc);
    for (int k = 0; k < 3; k++) r->box[k] = ahead[k] + fx_mul(a[k], zoom);
    int32_t st[3];
    for (int k = 0; k < 3; k++) st[k] = fx_mul(r->wind[k], scale()), r->step[k] = fx_mul(st[k], -0x10000);
    r->inv_dy = r->step[1] ? (int32_t)(0x100000000LL / r->step[1]) : 0;
    if (r->type == 2) dr = fx_mul(share(r), scale());
    if (r->type == 1) {
        int32_t x = r->box[0] + 0x9999a;
        for (int i = 0; i < 5; i++, x -= 0x4cccc) {
            int32_t z = r->box[2] + 0x9999a;
            for (int j = 0; j < 5; j++, z -= 0x4cccc) {
                int k = i * 5 + j;
                int32_t q[3] = {x, 0, z}, nrm[3];
                uint16_t surf;
                int32_t g = Hpc_Ground(q, nrm, &r->tri[k], &surf, r->ground[k]);
                r->ground[k] = g;
                r->lift[k] = (r->box[1] - g) - 0x61999;
            }
        }
    }
    int32_t bx = r->box[0], by = r->box[1], bz = r->box[2];
    uint32_t ci = 0, cj = 0;
    for (int n = 0; n < r->ndrops; n++) {
        int d = r->first + n;
        int32_t y = gms32(drop(d, 4)) + (st[1] - by);
        int32_t x = gms32(drop(d, 0)) + (st[0] - bx);
        int32_t z = gms32(drop(d, 8)) + (st[2] - bz);
        if (r->type == 2) {
            int32_t u = fx_mul(dr, gms32(drop(d, 0x18)));
            y += fx_mul(drift[1], u);
            x += fx_mul(drift[0], u);
            z += fx_mul(drift[2], u);
        }
        gw32(drop(d, 0x20), 0);
        if (x > 0xc0000) x += (int32_t)(((uint32_t)x + 0xbffffu) / 0x180000 * (uint32_t)-3 * 0x80000u);
        else if (x < -0xc0000) x += (int32_t)((0xbffffu - (uint32_t)x) / 0x180000 * 3 * 0x80000u);
        if (r->type == 1) {
            /* the cell (z before its wrap: beyond +12 it is row 0) */
            ci = (int32_t)(0xc0000u - (uint32_t)x) > 0x4cccb ? (0xc0000u - (uint32_t)x) / 0x4cccc : 0;
            cj = (int32_t)(0xc0000u - (uint32_t)z) > 0x4cccb ? (0xc0000u - (uint32_t)z) / 0x4cccc : 0;
            if (ci < 5 && cj < 5) y += r->lift[ci * 5 + cj];
        }
        if (y > 0x60000) {
            do y -= 0xc0000; while (y > 0x60000);
        } else if (y < -0x60000) {
            do y += 0xc0000; while (y < -0x60000);
            gw32(drop(d, 0x20), 1);
        }
        if (r->type == 1 && ci < 5 && cj < 5) {
            y -= r->lift[ci * 5 + cj];
            gw32(drop(d, 0x14), (uint32_t)(r->ground[ci * 5 + cj] + 0x1999));
        }
        if (z > 0xc0000) {
            do z -= 0x180000; while (z > 0xc0000);
        } else
            while (z < -0xc0000) z += 0x180000;
        gw32(drop(d, 0), (uint32_t)(r->box[0] + x));
        gw32(drop(d, 4), (uint32_t)(r->box[1] + y));
        gw32(drop(d, 8), (uint32_t)(z + r->box[2]));
        if (r->type == 2) {
            int32_t ang = gms32(drop(d, 0x14)) + fx_mul(gms32(drop(d, 0xc)), scale());
            if (ang > 0x1680000) ang -= 0x1680000;
            gw32(drop(d, 0x14), (uint32_t)ang);
        }
    }
    if (step && r->amount != r->target) {
        int32_t v = fx_mul(r->rate, scale());
        if (r->amount < r->target) {
            r->amount += v;
            if (r->target < r->amount) r->amount = r->target;
        } else if (r->target < r->amount) {
            r->amount -= v;
            if (r->amount < r->target) r->amount = r->target;
        }
    }
}

/* 0x460b60: a record's change of type: from none to rain or snow at no drops, aiming at its level
   (the drops placed once at once); back to none once its drops are gone. (Nothing sets +0x04 after
   the set-up, so in the release this never happens.) */
static void type_step(WxRec *r, int p)
{
    if (r->want == r->type) return;
    if (r->type == 0) {
        if (r->want == 1 || r->want == 2) {
            r->amount = 0;
            r->target = fx_mul(fx_mul(gms32(SCALE), r->level), r->ndrops << 16);
            r->type = r->want;
            drops_step(0, r, p);
        }
    } else if ((r->type == 1 || r->type == 2) && r->want == 0) {
        r->target = 0;
        if (r->amount == 0) r->type = 0;
    }
}

void Weather_Step(void)
{
    wind_step();
    lightning_step();
    for (int i = 0; i < players(); i++) {
        WxRec *r = &rec[i];
        level_step(r, i);
        sky_step(&sky[i], i);
        heading_step(&sky[i], i);
        if (r->type == 1 || r->type == 2) drops_step(1, r, i);
        type_step(r, i);
    }
}

void Weather_Interp(int32_t frac)
{
    for (int i = 0; i < nalloc; i++) {
        WxSky *s = &sky[i];
        s->blend_f = fx_mul(frac, s->blend - s->prev) + s->prev;
        s->heading_f = s->heading_prev + fx_mul(frac, s->heading - s->heading_prev);
    }
}

bool Weather_Wet(int car) { return Weather_Kind(car) == 1; }
uint8_t Weather_Kind(int car) { return car >= 0 && car < CAR_MAX ? (uint8_t)rec[car].type : 0; }
int32_t Weather_Level(int car) { return car >= 0 && car < CAR_MAX ? rec[car].level : 0; }

/* ---- drawing ---- */

_Static_assert(sizeof(Sprite3d) == 0x24, "the original's sprite record");
static void sprite(uint32_t at, Tex *t)
{
    Sprite3d s;
    memcpy(&s, gm(at), sizeof s);
    Sprite3d_Add(&s, t);
}
static float ff(int32_t v) { return (float)((double)v * 1.52587890625e-05); }   /* fild; fmul 1/65536 */

/* 0x460ca0: drops on the car (splat2 at random points of its body) */
static void car_drops(int32_t share_, const Car *c)
{
    (void)share_, (void)c, (void)t_splat2;   /* (splat2 its texture) */
    PENDING_NOTE(0x460ca0, "Weather_CarDrops (random points of the damage slots' meshes, 0x469c30)");
}

/* 0x460390: a record's drops. Rain: a streak per drop, a triangle from its tail (the drop less twice
   its relative fall) to a head across the view (the camera's forward crossed with the fall, 0.06 to
   0.1 wide by the share of drops), its head's alpha 30 in the open; a splat where a drop came through
   the ground; then the drops on the car (not while paused). Snow: a flake per drop, coloured by the
   terrain's light ramp at 0.8, swaying along the camera's side. */
static void draw_drops(int n, int p)
{
    WxRec *r = &rec[n];
    int count = r->amount >> 16;
    int32_t keep[3] = {r->box[0], r->box[1], r->box[2]};
    if (r->type == 1) {
        uint32_t a = 0x1e;
        int32_t cv = r->cover;
        if (cv == 0x10000) gw8(0x547927, 0xaa), gw8(0x543eef, 0xaa);
        else if (cv == 0) a = 0, gw8(0x543eef, 0), gw8(0x547927, 0);
        else {
            a = (uint32_t)(((int64_t)cv * 0x1e0000) >> 32) & 0xff;
            uint8_t b = (uint8_t)(((int64_t)r->cover * 0xaa0000) >> 32);
            gw8(0x543eef, b), gw8(0x547927, b);
        }
        gw32(0x543df0, a << 24 | 0x969696);
        gw32(0x543e20, gm32(0x543df0));
        int32_t f[3];
        cam_rows(p, NULL, f);
        int32_t s[3] = {fx_mul(f[1], r->rel[2]) - fx_mul(f[2], r->rel[1]), fx_mul(f[2], r->rel[0]) - fx_mul(f[0], r->rel[2]),
                        fx_mul(f[0], r->rel[1]) - fx_mul(f[1], r->rel[0])};
        Vec_Unit(s);
        int32_t w = fx_mul(fx_mul(share(r), 0xcccd) + 0x13333, 0xccc);
        float sf[3], tf[3];
        for (int k = 0; k < 3; k++) sf[k] = ff(fx_mul(s[k], w)), tf[k] = ff(fx_mul(r->rel[k], 0x20000));
        for (int i = 0; i < count; i++) {
            int d = r->first + i;
            float x = ff(gms32(drop(d, 0))), y = ff(gms32(drop(d, 4)));
            double z = (double)gms32(drop(d, 8)) * 1.52587890625e-05;   /* kept on the FPU stack */
            wf(TRI, (float)((double)x - tf[0]));
            wf(TRI + 4, (float)((double)y - tf[1]));
            wf(TRI + 8, (float)(z - tf[2]));
            wf(TRI + 0x30, (float)((double)x + sf[0]));
            wf(TRI + 0x34, (float)((double)y + sf[1]));
            wf(TRI + 0x38, (float)(z + sf[2]));
            wf(TRI + 0x60, (float)((double)x - sf[0]));
            wf(TRI + 0x64, (float)((double)y - sf[1]));
            wf(TRI + 0x68, (float)(z - sf[2]));
            Poly_AddRaw(gm(TRI), t_rain, 0x16);   /* bucket 0x10, no z writes, no culling */
            if (gm32(drop(d, 0x20))) {
                /* where its fall crossed the ground (+0x14) */
                int32_t q[3] = {gms32(drop(d, 0)), gms32(drop(d, 4)) - 0xc0000, gms32(drop(d, 8))};
                int32_t t = fx_mul(gms32(drop(d, 0x14)) - q[1], r->inv_dy);
                for (int k = 0; k < 3; k++) gw32(SPLAT + 4 * (uint32_t)k, (uint32_t)(fx_mul(r->step[k], t) + q[k]));
                sprite(SPLAT, t_splat);
            }
        }
        if (!Pause_Active()) car_drops(share(r), Car_Get(p));   /* 0x422fb0: the camera's car */
    } else {
        gw32(0x543f1c, Light_ByteRamp(0xcccc));
        for (int i = 0; i < count; i++) {
            int d = r->first + i;
            int64_t ai = (int64_t)nearbyint((double)gms32(drop(d, 0x14)) * (1.0 / 5760.0));   /* degrees to 4,096 */
            int32_t u = fx_mul(g_sin[(uint16_t)ai & 0xfff], gms32(drop(d, 0x10)));
            for (int k = 0; k < 3; k++) gw32(FLAKE + 4 * (uint32_t)k, (uint32_t)(gms32(drop(d, 4 * (uint32_t)k)) + fx_mul(r->side[k], u)));
            sprite(FLAKE, t_snow[gm8(drop(d, 0x1d)) % 3]);
        }
    }
    r->box[0] = keep[0], r->box[1] = keep[1], r->box[2] = keep[2];
}

void Weather_Draw(int p)
{
    if (p < 0 || p >= CAR_MAX) return;
    if (rec[p].type == 1 || rec[p].type == 2) draw_drops(p, p);
    /* 0x41f3a0: more than one player and the first's process in the replay (state 10 of the process
       record 0x537dd0's +4; inferred) */
    bool replay = Gs_SelBits14() > 1 && Race_State(0) == 10;
    if (Gs_SelBits14() != 1 && !replay && !Gs_ModeBit17()) return;
    PENDING_NOTE(0x462d80, "Weather_SunGlow (the sky's sun, the z buffer's read-back 0x4bc490)");
}

