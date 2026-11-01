/* The race's particle effects (see effects.h). */
#include "game/effects.h"
#include <stdbool.h>
#include <math.h>
#include <string.h>
#include "world/tod.h"
#include "game/weather.h"
#include "game/camera.h"
#include "crt.h"
#include "exe.h"
#include "fixmath.h"
#include "game/common.h"
#include "game/gstate.h"
#include "game/race.h"
#include "game/lamps.h"
#include "game/mem.h"
#include "game/timer.h"
#include "pending.h"
#include "sim/car.h"
#include "world/particles.h"
#include <stdio.h>

/* 0x543650-0x54367b: spark4, paint, glass, exhst03, exhst02, (0x543664 exhst02 again), spray2 (the
   WSpray one), snow_004, spray2 (Wsplash), spray2f, snow_spray */
enum { T_SPARK, T_PAINT, T_GLASS, T_EXH3, T_EXH2, T_EXH2B, T_WSPRAY, T_SNOW, T_SPLASH, T_SPLASHF, T_SNOWSPRAY, T_N };
static Tex *tex[T_N];
static Tex *leaf[5];        /* 0x54367c */
static Tex *grass[2];       /* 0x543690: GR_001, GR_002 */
static Tex *grass2[2];      /* 0x543698: GR_003, GR_004 */
static Tex *gravel[5];      /* 0x5436a0 */
static Tex *dust[8];        /* 0x5436b8 */
static Tex *detail[4];      /* 0x5436d8: DE_001-DE_004 */
static Tex *drop;           /* 0x5435c8 */

Tex *Effects_Tex(uint32_t va)
{
    if (va >= 0x543650 && va < 0x543650 + 4 * T_N) return tex[(va - 0x543650) / 4];
    return va == 0x5435c8 ? drop : NULL;
}

static Tex *load(const char *path)
{
    uint8_t found;
    char p[260];
    snprintf(p, sizeof p, "%s", path);
    return Bfl_FindTexture(Bfl_CommonArc(), p, &found, false, 0x10);
}

/* "%s%s" of 0x40ed50's \Textures and a name */
static Tex *load_name(uint32_t name)
{
    char p[260];
    snprintf(p, sizeof p, "%s%s", exe_str(0x516a50), exe_str(name));
    return load(p);
}

/* "%s%s%03d.tga" (0x51ac98) */
static Tex *load_num(uint32_t name, int n)
{
    char p[260];
    snprintf(p, sizeof p, "%s%s%03d.tga", exe_str(0x516a50), exe_str(name), n);
    return load(p);
}

/* 0x499020: two records of the sparks and the chips (0x592888 and 0x593728), three pairs of 16.16
   values each */
static void chips_setup(void)
{
    for (uint32_t b = 0; b < 2; b++) {
        uint32_t at = b ? 0x593728 : 0x592888;
        gw32(at, b ? 0 : 0x8000);
        gw32(at + 4, 0);
        gw32(at + 0x18, 0xffff);
        gw32(at + 0x1c, 0xffff);
        gw32(at + 0x30, 0);
        gw32(at + 0x34, 0xffff);
    }
}

void Effects_LoadTextures(void)
{
    Timer_Centis();
    tex[T_SPARK] = load_name(0x51adcc);
    tex[T_PAINT] = load_name(0x51adb8);
    tex[T_GLASS] = load_name(0x51ada4);
    tex[T_EXH3] = load_name(0x51ad84);
    tex[T_EXH2] = load_name(0x51ad64);
    tex[T_SNOW] = load_name(0x51ad44);
    tex[T_WSPRAY] = load_name(0x51ad28);
    tex[T_SPLASH] = load_name(0x51ad08);
    Tex_SetBlend(tex[T_SPLASH], 1);
    tex[T_SPLASHF] = load_name(0x51ace8);
    Tex_SetBlend(tex[T_SPLASHF], 1);
    tex[T_SNOWSPRAY] = load_name(0x51acc4);
    for (int i = 0; i < 8; i++) dust[i] = load_num(0x51aca8, i);
    for (int i = 0; i < 4; i++) {
        detail[i] = load_num(0x51ac80, i + 1);
        Tex_SetBlend(detail[i], 1);
    }
    for (int i = 0; i < 5; i++) leaf[i] = load_num(0x51ac68, i);
    for (int i = 0; i < 2; i++) grass[i] = load_num(0x51ac54, i + 1);
    for (int i = 0; i < 2; i++) grass2[i] = load_num(0x51ac54, i + 3);
    drop = load_name(0x51ac38);
    Tex_SetBlend(drop, 1);
    for (int i = 0; i < 5; i++) gravel[i] = load_num(0x51ac20, i);
    Timer_Centis();
    tex[T_EXH2B] = tex[T_EXH2];
    chips_setup();
}

static int32_t fdiv(int32_t a, int32_t b) { return (int32_t)(((int64_t)a << 16) / b); }

int32_t Stones_Setup(int32_t height, int32_t time)
{
    gw32(0x593868, (uint32_t)time);
    int32_t a = fdiv(fx_mul(height, 0x40000), time);
    gw32(0x593894, (uint32_t)a);
    gw32(0x593864, (uint32_t)fx_mul(a, 0x8000));
    int32_t b = fx_mul(a, 0x5553);
    gw32(0x593898, (uint32_t)b);
    uint32_t len = Fx_Sqrt((uint32_t)(fx_mul(b, b) + fx_mul(a, a) + fx_mul(a, a)));
    gw32(0x593870, (uint32_t)(int32_t)(0x100000000LL / (int32_t)len));
    gw8(0x59389c, (uint8_t)((((int64_t)time << 16) / 0xc0000) >> 16));
    for (uint32_t i = 0; i < 8; i++) gw32(0x593874 + 4 * i, 0);
    gw32(0x59386c, 0xffff);
    gw32(0x593860, 0xffff);
    return fdiv(fx_mul(height, 0x80000), fx_mul(time, time));
}

/* ---- the types' callbacks (0x45d250-0x45dea0) ---- */

/* the car's position added: the particles of a car's wheels move in its frame's origin */
static void to_car(Particle *p)
{
    if (p->car >= 8) return;
    Car *c = Car_Get(p->car);
    for (int a = 0; a < 3; a++) p->pos[a] += CI32(c, 0x2d0 + 4 * (uint32_t)a);
}

/* 0x45de80, after the emit: the car (the emitter's argument) */
static void cb_45de80(Particle *p, PType *t, void *a) { (void)t; p->car = (int16_t)*(const int *)a; }
/* 0x45d250: raised by half its scale */
static void cb_45d250(Particle *p, PType *t, void *a) { (void)t, (void)a; p->pos[1] += p->scale / 2; }
/* 0x45d270: raised by half its scale, the scale at most 1.5, the car's position added */
static void cb_45d270(Particle *p, PType *t, void *a)
{
    (void)t, (void)a;
    p->pos[1] += p->scale / 2;
    if (p->scale > 0x18000) p->scale = 0x18000;
    to_car(p);
}
/* 0x45dea0 */
static void cb_45dea0(Particle *p, PType *t, void *a) { (void)t, (void)a; to_car(p); }
/* 0x45d3a0: the leaves sway on a circle of a radius picked by the particle's address, turning 0x71 a
   step (the first step from a turn the address picks), one way or the other by the type */
static void cb_45d3a0(Particle *p, PType *t, void *a)
{
    (void)a;
    static const int32_t radius[10] = {0x1999, 0x3333, 0x4ccc, 0x6666, 0x8000, 0x9999, 0xb333, 0xcccc, 0xe666, 0x10000};
    int32_t addr = (int32_t)Particle_Addr(Particle_Index(p));
    if (p->life == 0xc70000) p->spin = (int16_t)((uint32_t)(addr / 7 & 0xffffff) % 0x24 * 0x71);
    p->spin = (int16_t)(p->spin + 0x71);
    if (p->spin > 0x1000) p->spin = (int16_t)(p->spin - 0x1000);
    if (p->spin < 0) p->spin = (int16_t)(p->spin + 0x1000);
    uint32_t u = (t->flags & 4) ? (uint16_t)p->spin : (uint32_t)(0x1000 - p->spin);
    int32_t r = radius[(uint32_t)(addr / 3 & 0xffffff) % 10];
    if (p->car < 8) {
        p->pos[0] += fx_mul(r, g_sin[(u + 0x400) & 0xfff]);
        p->pos[2] += fx_mul(r, g_sin[u & 0xfff]);
        to_car(p);
    }
}
/* 0x45d2d0: the detail dust follows a kind 9 lamp of its car (0x543580, not ported) */
static void cb_45d2d0(Particle *p, PType *t, void *a) { (void)p, (void)t, (void)a; PENDING_NOTE(0x45d2d0, "the detail dust's step"); }

/* the sparks' and chips' callbacks (0x499070-0x499ac0), not ported yet */
static void cb_499070(Particle *p, PType *t, void *a) { (void)p, (void)t, (void)a; PENDING_NOTE(0x499070, "the sparks' draw"); }
static void cb_4994d0(Particle *p, PType *t, void *a) { (void)p, (void)t, (void)a; PENDING_NOTE(0x4994d0, "the sparks' step"); }
static void cb_4994f0(Particle *p, PType *t, void *a) { (void)p, (void)t, (void)a; PENDING_NOTE(0x4994f0, "the paint chips' draw"); }
static void cb_499710(Particle *p, PType *t, void *a) { (void)p, (void)t, (void)a; PENDING_NOTE(0x499710, "the paint chips' spawn"); }
static void cb_499ac0(Particle *p, PType *t, void *a) { (void)p, (void)t, (void)a; PENDING_NOTE(0x499ac0, "the glass chips' draw"); }
static void cb_499a80(Particle *p, PType *t, void *a) { (void)p, (void)t, (void)a; PENDING_NOTE(0x499a80, "the glass chips' spawn"); }

/* the box extents (-e, e, e, -e) of most types */
#define BOX(e) (int32_t)-(e), (e), (e), (int32_t)-(e)

void Effects_Types(void)
{
    /* the particles in use: 400, 100 or none (detail 3 leaves the stack's value: the port the count) */
    uint32_t level = gm32(0x52afd0) >> 25 & 3;
    if (level != 3) Particles_SetCount(level == 0 ? 400 : level == 1 ? 100 : 0);

    /* 0: exhaust smoke */
    PType_Begin(0);
    PType_Tex(tex[T_EXH3], BOX(0x10000));
    PType_Motion(0xc80000, 6, 0x10000, 0, 0, false, false);
    PType_Fade(0x1e, 0, 3, true);
    PType_Grow(0x28f, 0x10000, 0x5a1);
    PType_Spin(0);
    PType_Spread(0x28f, 0x28f, 0x28f, true);
    PType_End();
    PType_Begin(1);
    PType_Copy(0);
    PType_Tex(tex[T_EXH2], BOX(0x10000));
    PType_Fade(0x50, 0, 3, true);
    PType_End();
    PType_Begin(2);
    PType_Tex(tex[T_EXH2B], BOX(0x10000));
    PType_Motion(0x12c0000, 6, 0x10000, 0, 0, false, false);
    PType_Fade(100, 0, 1, true);
    PType_Grow(0x1999, 0x10000, 0xb43);
    PType_Spin(0);
    PType_Spread(0x28f, 0x28f, 0x28f, true);
    PType_End();
    /* 3: the detail dust */
    PType_Begin(3);
    PType_Copy(0);
    PType_Motion(0x40000, 0, 0, 0, 0, false, false);
    PType_Grow(0xccc, 0x2147, 0xccc);
    PType_Anim(detail, 4, 0, 0x10000, false, false, BOX(0x10000));
    PType_Fade(0xfe, 0xfe, 0, false);
    PType_RandomScale(0x1999, 0x3333);
    PType_Funcs(NULL, NULL, cb_45d2d0, cb_45de80, 0);
    PType_End();
    /* 6, 4, 7: snow */
    PType_Begin(6);
    PType_Tex(tex[T_SNOW], BOX(0x6666));
    PType_Motion(0x210000, 0x20, 0, 0, 0, false, false);
    PType_Fade(0xd2, 0, 6, true);
    PType_Colour(0xff, 0xff, 0xff, true);
    PType_Grow(0x4ccc, 0x18000, 0x7ae);
    PType_Spin(0);
    PType_Spread(0x51e, 0x28f, 0x51e, false);
    PType_Funcs(NULL, NULL, cb_45d270, cb_45de80, 0);
    PType_End();
    PType_Begin(4);
    PType_Copy(6);
    PType_Tex(tex[T_SNOW], BOX(0xb333));
    PType_Motion(0x780000, 0x20, 0, 0, 0, false, false);
    PType_Fade(100, 0, 2, true);
    PType_End();
    PType_Begin(7);
    PType_Copy(6);
    PType_Tex(tex[T_SNOW], BOX(0xb333));
    PType_Grow(0x4ccc, 0x13333, 0x147a);
    PType_Motion(0x640000, 0, 0xa0000, 0, 0, false, false);
    PType_Fade(0xd2, 0, 6, true);
    PType_Colour(0xff, 0xff, 0xff, true);
    PType_Grow(0x4ccc, 0x18000, 0x7ae);
    PType_Funcs(NULL, NULL, cb_45d250, NULL, 0);
    PType_End();
    /* 10, 8, 11: dust clouds */
    PType_Begin(10);
    PType_Copy(6);
    PType_Anim(dust, 8, 0, 0x20000, false, false, BOX(0x4ccc));
    PType_Fade(100, 0, 0, false);
    PType_Colour(0xff, 0xff, 0xff, true);
    PType_Funcs(NULL, NULL, cb_45d270, cb_45de80, 0);
    PType_End();
    PType_Begin(8);
    PType_Copy(4);
    PType_Anim(dust, 8, 0, 0x20000, true, true, BOX(0xb333));
    PType_Colour(0x98, 0x7e, 0x66, true);
    PType_End();
    PType_Begin(0xb);
    PType_Copy(7);
    PType_Anim(dust, 8, 0, 0x20000, true, true, BOX(0xb333));
    PType_Grow(0x4ccc, 0x13333, 0x147a);
    PType_Motion(0x640000, 0, 0xa0000, 0, 0, false, false);
    PType_Fade(100, 0, 0, false);
    PType_Colour(0xff, 0xff, 0xff, true);
    PType_Funcs(NULL, NULL, cb_45d250, NULL, 0);
    PType_End();
    PType_Begin(5);
    PType_Copy(4);
    PType_Tex(tex[T_SNOW], BOX(0x6666));
    PType_Motion(0x300000, 0x20, 0x30000, 0, 0, false, false);
    PType_Fade(100, 0, 2, true);
    PType_Grow(0x4ccc, 0x10000, 0x1999);
    PType_Spin(0x88);
    PType_Spread(0, 0, 0, false);
    PType_End();
    PType_Begin(0xc);
    PType_Copy(5);
    PType_Anim(dust, 8, 0, 0x20000, true, true, (int32_t)0xffffc000, 0x4000, 0x8000, (int32_t)0xffffc000);
    PType_Fade(0x1e, 0, 4, true);
    PType_Grow(0x4ccc, 0x5555, 0x3332);
    PType_Spin(0);
    PType_End();
    PType_Begin(0xd);
    PType_Copy(5);
    PType_Anim(dust, 8, 0, 0x20000, true, true, (int32_t)0xffff0000, 0x10000, 0x20000, (int32_t)0xffff0000);
    PType_Fade(0x46, 0, 2, true);
    PType_End();
    /* 14, 15: splashes */
    PType_Begin(0xe);
    PType_Copy(5);
    PType_Motion(0x1900000, 0, 0, 0, 0, false, false);
    PType_Spin(0);
    PType_Tex(tex[T_SPLASHF], 0, 0x20000, 0x20000, 0);
    PType_Grow(0x1999, 0x10000, 0xa3d);
    PType_Fade(0xff, 0, 0xf, true);
    PType_Spread(0, 0, 0, false);
    PType_Funcs(NULL, NULL, cb_45dea0, cb_45de80, 0);
    PType_End();
    PType_Begin(0xf);
    PType_Copy(0xe);
    PType_Tex(tex[T_SPLASH], 0, 0, 0x20000, (int32_t)0xfffe0000);
    PType_End();
    /* 16-23: leaves and grass */
    PType_Begin(0x10);
    PType_Anim(leaf, 5, 0x20000, 0x10000, true, true, BOX(0xcccc));
    PType_Motion(0xc80000, 0x28f, 0, 0, 0, false, true);
    PType_Fade(0xff, 0xff, 0, true);
    PType_Colour(0xff, 0xff, 0xff, true);
    PType_RandomScale(0x1999, 0x147a);
    PType_Spin(0xb);
    PType_Spread(0, 0, 0, false);
    PType_Funcs(NULL, NULL, cb_45d3a0, cb_45de80, 0);
    PType_End();
    PType_Begin(0x11);
    PType_Copy(0x10);
    PType_Fade(0xff, 0xff, 0, false);
    PType_End();
    PType_Begin(0x12);
    PType_Copy(0x10);
    PType_Anim(grass, 2, 0x40000, 0x40000, true, true, BOX(0x18000));
    PType_Motion(0xc80000, 0x28f, 0, 0, 0, false, true);
    PType_Fade(0xff, 0xff, 0, true);
    PType_Colour(0xff, 0xff, 0xff, true);
    PType_RandomScale(0x1999, 0x147a);
    PType_Spin(0xb);
    PType_Spread(0, 0, 0, false);
    PType_Funcs(NULL, NULL, cb_45d3a0, cb_45de80, 0);
    PType_End();
    PType_Begin(0x13);
    PType_Copy(0x12);
    PType_Fade(0xff, 0xff, 0, false);
    PType_End();
    PType_Begin(0x14);
    PType_Copy(0x12);
    PType_Anim(grass2, 2, 0x40000, 0x40000, true, true, BOX(0x18000));
    PType_End();
    PType_Begin(0x15);
    PType_Copy(0x14);
    PType_Fade(0xff, 0xff, 0, false);
    PType_End();
    PType_Begin(0x17);
    PType_Copy(0x13);
    PType_Fade(0xff, 0xff, 0, false);
    PType_RandomScale(0x147a, 0xa3d);
    PType_End();
    PType_Begin(0x16);
    PType_Copy(0x12);
    PType_Fade(0xff, 0xff, 0, true);
    PType_RandomScale(0x147a, 0xa3d);
    PType_End();
    /* 24: drops */
    PType_Begin(0x18);
    PType_Tex(drop, BOX(0x4000));
    PType_Motion(0xc80000, 0xccc, 0, 0x1999, 0, true, true);
    PType_Fade(0xff, 0xff, 0, true);
    PType_Grow(0x1999, 0x1999, 0);
    PType_Spin(0);
    PType_Spread(0, 0, 0, false);
    PType_Funcs(NULL, NULL, cb_45dea0, cb_45de80, 0);
    PType_End();
    /* 26-28: sparks, paint and glass chips */
    PType_Begin(0x1a);
    PType_Tex(tex[T_SPARK], BOX(0x3333));
    PType_Motion(0x140000, 0x147, 0, 0, 0, false, true);
    PType_Fade(0xfe, 0, 0x22, true);
    PType_Grow(0x10000, 0x10000, 0);
    PType_Spin(0);
    PType_Spread(0, 0, 0, false);
    PType_Funcs(cb_499070, NULL, cb_4994d0, NULL, 0);
    PType_End();
    PType_Begin(0x1b);
    PType_Tex(tex[T_PAINT], BOX(0x3333));
    PType_Motion(0x140000, 0x147, 0, 0, 0, false, true);
    PType_Fade(0xff, 0xff, 0, false);
    PType_Grow(0x10000, 0x10000, 0);
    PType_Spin(0);
    PType_Spread(0, 0, 0, false);
    PType_Funcs(cb_4994f0, NULL, NULL, cb_499710, 0);
    PType_End();
    PType_Begin(0x1c);
    PType_Tex(tex[T_GLASS], BOX(0x3333));
    PType_Motion(0x640000, 0x189, 0, 0, 0, false, true);
    PType_Fade(0xff, 0xff, 0, false);
    PType_Grow(0x10000, 0x10000, 0);
    PType_Spin(0);
    PType_Spread(0, 0, 0, false);
    PType_Funcs(cb_499ac0, NULL, NULL, cb_499a80, 0);
    PType_End();
    /* 29, 30: gravel, with the thrown stones' gravity */
    int32_t g = Stones_Setup(0x10000, 0x190000);
    PType_Begin(0x1d);
    PType_Anim(gravel, 5, 0x20000, 0x10000, true, true, BOX(0x666));
    PType_Motion(0xf0000, fx_mul(g, 0x3333), 0, 0xb333, 0, true, false);
    PType_Fade(0xff, 0xff, 0, false);
    PType_Colour(0xff, 0, 0xff, true);
    PType_RandomScale(0x10000, 0x20000);
    PType_Spin(0);
    PType_Spread(0, 0, 0, false);
    PType_End();
    PType_Begin(0x1e);
    PType_Anim(gravel, 5, 0x20000, 0x10000, true, true, BOX(0x8f5));
    PType_Motion(0x190000, g, 0, 0xb333, 0, true, false);
    PType_Fade(0xff, 0xff, 0, false);
    PType_Colour(0xff, 0, 0xff, true);
    PType_RandomScale(0x10000, 0x20000);
    PType_Spin(0);
    PType_Spread(0, 0, 0, false);
    PType_End();
    /* 31: the sparks' glow, with the hazard lamps' sprite */
    PType_Begin(0x1f);
    PType_Copy(0x1a);
    PType_Tex(Lamps_SpriteTex(2), (int32_t)0xffffe667, 0x1999, 0x1999, (int32_t)0xffffe667);
    PType_Funcs(NULL, NULL, NULL, NULL, 0);
    PType_End();
}

/* ---- the wheels (0x45def0, 0x45af00-0x45dc00) ---- */

/* per car and wheel (n = 4 car + wheel) in the game state: 0x543200 the wheel's ground point, 0x543400
   the point of the last mark, 0x543b58 the mark's run, 0x543830 the point from the car, 0x543140 a
   jump flag; 0x543708 / 0x543ad8, 0x543788 the touch, 0x5435d0 the surface, 0x5430a0 the steps on the
   ground; 0x5439b8 the car's wheels down; 0x543120 the shade (bytes) */
static uint32_t v3(uint32_t base, int n) { return base + 0xc * (uint32_t)n; }
static uint32_t v1(uint32_t base, int n) { return base + 4 * (uint32_t)n; }

static uint32_t flat_len(int32_t dx, int32_t dz)
{
    int32_t l = (int32_t)Fx_Sqrt((uint32_t)(fx_mul(dx, dx) + fx_mul(dz, dz)));
    return (uint32_t)(l < 0 ? -l : l);
}

void Wheels_Track(int car)
{
    if (car >= 8) return;
    Car *c = Car_Get(car);
    gw32(v1(0x5439b8, car), 0);
    int32_t m[16];
    for (int i = 0; i < 16; i++) m[i] = CI32(c, 4 * (uint32_t)i);   /* the orientation (+0x750) */
    for (int w = 0; w < 4; w++) {
        int n = 4 * car + w;
        uint32_t cur = v3(0x543200, n), last = v3(0x543400, n), run = v3(0x543b58, n);
        if (!gm32(v1(0x543140, n))) {
            int32_t dx = gms32(last) - gms32(cur), dz = gms32(last + 8) - gms32(cur + 8);
            int32_t l = (int32_t)flat_len(dx, dz);
            if (l < 0x14ccd) {
                /* a mark's run once the wheel is 0.1 from the last point */
                if (l > 0x1999) {
                    for (int a = 0; a < 3; a++) gw32(last + 4 * a, gm32(cur + 4 * a));
                    gw32(run, (uint32_t)dx), gw32(run + 4, 0), gw32(run + 8, (uint32_t)dz);
                }
            } else {
                /* 1.3 or more: a jump, no run */
                for (int a = 0; a < 3; a++) gw32(last + 4 * a, gm32(cur + 4 * a)), gw32(run + 4 * a, 0);
                gw32(v1(0x543140, n), 1);
            }
        } else {
            for (int a = 0; a < 3; a++) gw32(last + 4 * a, gm32(cur + 4 * a));
            gw32(v1(0x543140, n), 0);
        }
        uint32_t touch = (uint32_t)CI32(c, 0xbac + 4 * (uint32_t)w);
        gw32(v1(0x543ad8, n), gm32(v1(0x543708, n)));
        gw32(v1(0x543788, n), touch);
        gw32(v1(0x5435d0, n), (uint32_t)(int32_t)CI16(c, 0xaae + 2 * (uint32_t)w));
        if (touch == 1) {
            gw32(v1(0x5430a0, n), gm32(v1(0x5430a0, n)) + 1);
            gw32(v1(0x5439b8, car), gm32(v1(0x5439b8, car)) + 1);
        } else {
            gw32(v1(0x5430a0, n), 0);
        }
        /* the ground point: the wheel's contact point (+0x3c0) 0.35 down, turned with the car */
        int32_t o[3] = {CI32(c, 0x3c0 + 0xc * (uint32_t)w), CI32(c, 0x3c4 + 0xc * (uint32_t)w) - 0x5999,
                        CI32(c, 0x3c8 + 0xc * (uint32_t)w)}, r[3];
        for (int k = 0; k < 3; k++) r[k] = fx_mul(m[8 + k], o[2]) + fx_mul(m[4 + k], o[1]) + fx_mul(m[k], o[0]);
        for (int a = 0; a < 3; a++) {
            gw32(v3(0x543830, n) + 4 * a, (uint32_t)r[a]);
            gw32(cur + 4 * a, (uint32_t)(r[a] + CI32(c, 0x2d0 + 4 * (uint32_t)a)));
        }
        int32_t sh = 0xff00 - CI32(c, 0xa70);
        if (sh > 0xff00) sh = 0xff00;
        else if (sh < 0) sh = 0;
        gw8(0x543120 + (uint32_t)n, (uint8_t)(sh >> 8));
    }
}

/* 0x45d1e0: the light under the car (0x5435c0, its half towards white 0x5435cc, the grey 0x543808) */
static void light_set(int car, uint32_t rgba)
{
    if (car >= 8) return;
    gw32(0x5435c0, rgba);
    gw8(0x5435cc, (uint8_t)((rgba & 0xff) >> 1) + 0x7f);
    gw8(0x5435cd, (uint8_t)((rgba >> 8 & 0xff) >> 1) + 0x7f);
    gw8(0x5435ce, (uint8_t)((rgba >> 16 & 0xff) >> 1) + 0x7f);
    gw8(0x5435cf, (uint8_t)(rgba >> 24));
    gw8(0x543808, (uint8_t)(((rgba & 0xff) + (rgba >> 8 & 0xff) + (rgba >> 16 & 0xff)) / 3));
}

/* 0x45dc00: a particle of grass (types 0x10-0x17 by the flags and the wheel's side) at pos (from the
   car) with the wheel's x and z and a random rise below 0.15; leaves take one of ten colours
   (0x51abf8) */
static void grass_emit(int car, int wheel, const int32_t pos[3], const int32_t vel[3], bool lit, bool second,
                       bool third, const uint8_t rgb[3])
{
    if (car >= 8) return;
    bool left = wheel == 0 || wheel == 2;
    int type = !lit ? (left ? 0x10 : 0x11) : third ? (left ? 0x16 : 0x17) : second ? (left ? 0x14 : 0x15)
                                                                                   : (left ? 0x12 : 0x13);
    /* (the original also takes the angle of +0x6c0 and scales it by the speed along it, unused) */
    int32_t v[3] = {vel[0], crt_rand() % 0x2666, vel[2]};
    uint8_t col[4];
    if (!lit) {
        uint32_t k = (uint32_t)(crt_rand() % 10);
        for (int i = 0; i < 3; i++) col[i] = gm8(0x51abf8 + 4 * k + (uint32_t)i);
    } else {
        memcpy(col, rgb, 3);
    }
    col[3] = 0xff;
    int arg = car;
    Particle_Emit(type, pos, v, pos[1], NULL, col, gm8(0x543120 + 4 * (uint32_t)car + (uint32_t)wheel), &arg,
                  Car_Get(car)->second->mask);
}

/* 0x45d540: grass and leaves thrown by each wheel down on a grass surface (the colours by rally),
   above 15 of the rear wheel's spin a third of the time */
static void wheels_grass(int car)
{
    if (car >= 8) return;
    Car *c = Car_Get(car);
    int count = 0;   /* (kept from wheel to wheel) */
    for (int w = 0; w < 4; w++) {
        int16_t s = CI16(c, 0xac6 + 2 * (uint32_t)w);
        bool leaves = s == 0x19 || (s >= 0x2e && s <= 0x32) || s == 0x4f;
        bool a = s == 0x15 || s == 0x1d || (s >= 0x20 && s <= 0x25) || s == 0x27 || s == 0x33 || s == 0x36 ||
                 s == 0x47 || s == 0x4f || s == 0x19;
        bool b = s == 0x16 || s == 0x26 || s == 0x28 || s == 0x2c || s == 0x34 || s == 0x35 || s == 0x37 || s == 0x38;
        bool d = s == 0x17 || s == 0x29;
        bool grass = a || b || d || s == 0x18;
        bool first = false, second = false;
        uint8_t col[3] = {0, 0, 0};
        if (grass) {
            /* (an unset colour or flag keeps the stack's value; the port zero) */
            switch (Tod_Rally()) {
            case 0:
                if (a) first = true, col[0] = 0x3d, col[1] = 0x40, col[2] = 0x0e;
                else if (b) second = true, col[0] = 0x66, col[1] = 0x63, col[2] = 0x4a;
                break;
            case 1: case 8:
                second = true;
                if (a) col[0] = 0x35, col[1] = 0x39, col[2] = 0x2d;
                else if (b) col[0] = 0x60, col[1] = 0x4f, col[2] = 0x40;
                break;
            case 2: first = true, col[0] = 0x50, col[1] = 0x5b, col[2] = 0x42; break;
            case 4:
                if (a) first = true, col[0] = 0x20, col[1] = 0x34, col[2] = 0x03;
                else if (b) second = true, col[0] = 0x9a, col[1] = 0x8e, col[2] = 0x20;
                break;
            case 5:
                if (a) first = true, col[0] = 0x4f, col[1] = 0x49, col[2] = 0x33;
                else if (b) second = true, col[0] = 0x50, col[1] = 0x40, col[2] = 0x1f;
                break;
            case 6:
                if (a || (!b && d)) first = true, col[0] = 0x49, col[1] = 0x58, col[2] = 0x3a;
                else if (b) second = true, col[0] = 0x2f, col[1] = 0x3b, col[2] = 0x21;
                break;
            case 7:
                if (a) first = true, col[0] = 0x1f, col[1] = 0x29, col[2] = 0x01;
                else if (b) second = true, col[0] = 0x37, col[1] = 0x46, col[2] = 0x28;
                else if (d) second = true, col[0] = 0x31, col[1] = 0x3f, col[2] = 0x23;
                else if (s == 0x18) first = true, col[0] = 0x41, col[1] = 0x51, col[2] = 0x31;
                break;
            default: first = true, col[0] = col[1] = col[2] = 0xf0; break;
            }
        }
        if (!(leaves || grass) || !CI32(c, 0xbac + 4 * (uint32_t)w)) continue;
        int32_t sp = Car_WheelSpeed(c, 2, 0);
        if (sp < 0) sp = -sp;
        if (sp > 0xa0000) count = (crt_rand() % 4) / 4;   /* (always 0) */
        if (sp > 0xf0000) count = (crt_rand() % 3) / 2;
        uint32_t o = 0x42c + 0xc * (uint32_t)w;
        int32_t vel[3] = {-(CI32(c, o) / 4), CI32(c, o + 4), -(CI32(c, o + 8) / 4)};
        int n = 4 * car + w;
        for (int k = 0; k < count; k++) {
            int32_t pos[3];
            for (int a = 0; a < 3; a++) pos[a] = gms32(v3(0x543200, n) + 4 * (uint32_t)a) - CI32(c, 0x2d0 + 4 * (uint32_t)a);
            if (!leaves) {
                /* fewer on a wet stage: 3 at dry, 1 at the wettest */
                int32_t wet = Weather_Wet(car) ? fx_mul(Weather_Level(car), 0xff0000) >> 16 : 0;
                int m = (0x100 - wet) / 128 + 1;
                if (first) for (int i = 0; i < m; i++) grass_emit(car, w, pos, vel, true, false, false, col);
                if (second) for (int i = 0; i < m; i++) grass_emit(car, w, pos, vel, true, true, false, col);
            } else {
                grass_emit(car, w, pos, vel, false, false, false, col);
            }
        }
    }
}

/* rand() as 0..1 (0x5112f0 times 0x5112e0) and as -1..0 (times 0x5112e8), rounded by fistp */
static int32_t rand_pos(void)
{
    return (int32_t)nearbyint((double)crt_rand() * (double)(float)(1.0 / 32767.0) * 65536.0);
}
static int32_t rand_neg(void)
{
    return (int32_t)nearbyint((double)crt_rand() * (double)(float)(1.0 / 32767.0) * -65536.0);
}

static void vscale(int32_t out[3], const int32_t v[3], int32_t k)
{
    for (int a = 0; a < 3; a++) out[a] = fx_mul(v[a], k);
}

static void car_vec(int32_t out[3], const Car *c, uint32_t o)
{
    for (int a = 0; a < 3; a++) out[a] = CI32(c, o + 4 * (uint32_t)a);
}

static int32_t iabs(int32_t v) { return v < 0 ? (int32_t)(0u - (uint32_t)v) : v; }

/* Wheel_Slip 0x465d70: wheel w's slip as 0..1, the larger of |+0x880| less 0.05 and |+0x870| less
   0.15, each from 0 */
int32_t Wheel_Slip(int car, int w)
{
    if (car >= 8) return 0;
    Car *c = Car_Get(car);
    int32_t a = (int32_t)((uint32_t)iabs(CI32(c, 0x880 + 4 * (uint32_t)w)) - 0xccc);
    if (a < 0) a = 0;
    int32_t b = (int32_t)((uint32_t)iabs(CI32(c, 0x870 + 4 * (uint32_t)w)) - 0x2666);
    if (b < 0) b = 0;
    if (a < b) a = b;
    if (a <= 0) return 0;
    a = fdiv(a, 0x10000);
    return a < 0x10000 ? a : 0x10000;
}

/* Car_RecordFlag 0x41f3d0: the +4 word of the car's record in the table at 0x537f3c (a heap pointer
   per car, set up by the race's loads); that table is not ported: clear, as for a player's car */
static int32_t Car_RecordFlag(int car)
{
    (void)car;
    PENDING_NOTE(0x41f3d0, "the cars' records (0x537f3c)");
    return 0;
}

/* Wheels_Dust 0x45b580: what each touching wheel throws up: dust (type 8) or snow (type 4) from
   one axle, snow only (type 6) from the other (the second: the rear on a car whose +0xb1e is 7, else
   the front). Dust comes from a dusty material or surface (+0xaae, +0xac6) that passes a random
   draw, in any rally but 0, or from a wheel spinning (Wheel_Slip from 0.9) on materials 0xd and
   0x18; none in the rain. Snow from materials 0x11 and 0x13-0x17 (the second axle 0x13-0x15, not in
   heavy snow). The colour is by rally and surface, white on snow; it starts white and stays from
   wheel to wheel when not set. Then fewer below 30 of wheel 0's speed (at that fraction), 30% fewer
   on the second axle, half from any car but 0. The particle sits between the wheel's ground point
   and its axle partner's (w ^ 1), nearer its own (much nearer on the second axle), moved back along
   the last mark's run by up to all of it, and is thrown back with a quarter of the wheel's velocity
   (+0x42c) and a tenth of its rise; scaled down above 35 (second axle 50) of wheel 2's speed, else
   plus 0.4 of (0.1 of the +0x870 slip along the wheel's forward axis and 0.3 of -0.5..0.5 along its
   side axis). A car with its record flag (0x41f3d0) or in race state 9 takes type 7 (11 with dust)
   placed by the wheel's velocity. */
static void Wheels_Dust(int car)
{
    uint8_t rgb[4] = {0xff, 0xff, 0xff, 0xff};   /* (kept from wheel to wheel) */
    if (car >= 8) return;
    Car *c = Car_Get(car);
    int count = 100;   /* (raised by half on a slipping wheel and never read) */
    for (int w = 0; w < 4; w++) {
        bool rear = w == 2 || w == 3;
        bool rwd = CI8(c, 0xb1e) == 7;
        /* the second axle: the rear on a car whose +0xb1e is 7, else the front */
        bool second = rwd == rear;
        int32_t mat = CI16(c, 0xaae + 2 * (uint32_t)w);
        int16_t s = CI16(c, 0xac6 + 2 * (uint32_t)w);
        bool a = mat == 0xb || s == 0x5b || s == 0x5c || s == 0x5d;
        bool b = mat == 0xa || mat == 6 || s == 0x1b || s == 0x1c || s == 0x11 || s == 0x12 || s == 0x48;
        bool d9 = mat == 9;
        bool spin = (mat == 0xd || mat == 0x18) && Wheel_Slip(car, w) >= 0xe666;
        if (d9 && rand_pos() > 0x3333) d9 = false;
        if (b && rand_pos() > 0x6666) b = false;
        if (a && rand_pos() > 0x8000) a = false;
        Tod_Rally();   /* (called twice, the first unused) */
        bool dust = ((Tod_Rally() & 0xff) != 0 && (a || b || d9)) || spin;
        if (Weather_Kind(car) == 1 && fx_mul(Weather_Level(car), 0xff0000) >> 16 > 1) {
            dust = false;   /* none in the rain */
        } else if (dust && iabs(Car_WheelSpeed(c, 0, 0)) > 0x1e0000 && car < (int)Gs_SelBits14()) {
            PENDING_NOTE(0x45c750, "the player's wheel spin tally (0x45c750 car, w, 1, 0)");
        }
        if (second) {
            dust = false;   /* (types 10 and 11 from the second axle never) */
        } else if (dust) {
            rgb[0] = 0xeb, rgb[1] = 0xb8, rgb[2] = 0xa0;
            bool light = s == 2 || s == 3 || s == 4 || s == 5 || s == 0x11 || s == 0x1b || s == 0x5d;
            bool dark = s == 8 || s == 9 || s == 0xa || s == 0x12 || s == 0x1c || s == 0x5b;
            bool white = s == 6 || s == 0xa;
            switch (Tod_Rally() & 0xff) {
            case 0: rgb[0] = 0x90, rgb[1] = 0x7f, rgb[2] = 0x78; break;
            case 1:
                if (light) rgb[0] = 0xeb, rgb[1] = 0xb8, rgb[2] = 0xa0;
                else rgb[0] = 0x9c, rgb[1] = 0x8f, rgb[2] = 0x87;
                break;
            case 4: rgb[0] = 0xb6, rgb[1] = 0x91, rgb[2] = 0x43; break;
            case 5:
                if (white) rgb[0] = 0xff, rgb[1] = 0xff, rgb[2] = 0xff;
                else if (dark) rgb[0] = 0x7a, rgb[1] = 0x71, rgb[2] = 0x5c;
                else rgb[0] = 0xbb, rgb[1] = 0x86, rgb[2] = 0x3e;
                break;
            case 7: dust = false; break;   /* (no dust in rally 7; the colour stays set) */
            case 8: rgb[0] = 0x9c, rgb[1] = 0x8f, rgb[2] = 0x87; break;
            default: rgb[0] = rgb[1] = rgb[2] = 0xf0; break;
            }
        }
        /* snow: materials 0x11 and 0x13-0x17, on the second axle only 0x13-0x15 */
        bool snow = mat == 0x11 || (mat >= 0x13 && mat <= 0x17);
        bool snow2 = mat >= 0x13 && mat <= 0x15;
        if (second && !snow2) snow = false;
        else if (snow) rgb[0] = rgb[1] = rgb[2] = 0xff;
        if ((second && Car_RecordFlag(car)) ||
            (snow && second && Weather_Kind(car) == 2 && fx_mul(Weather_Level(car), 0xff0000) >> 16 > 100))
            snow = false;
        bool on = dust || snow;
        if (!CI32(c, 0xbac + 4 * (uint32_t)w)) on = false;
        int32_t limit = second ? 0x320000 : 0x230000;
        int type = 4;
        int32_t sp = iabs(Car_WheelSpeed(c, 0, 0));
        if (sp < 0x1e0000 && rand_pos() > fdiv(sp, 0x1e0000)) on = false;
        if (second) {
            type = 6;
            if (!Car_RecordFlag(car) && rand_pos() > 0xb333) on = false;
        }
        if (car > 0 && rand_pos() > 0x8000) continue;
        if (!on) continue;
        if (type == 4) crt_rand();   /* (a rand() % 2 left unused) */
        int n = 4 * car + w;
        int32_t run[3];
        for (int k = 0; k < 3; k++)
            run[k] = gms32(v3(0x543200, n) + 4 * (uint32_t)k) - gms32(v3(0x543400, n) + 4 * (uint32_t)k);
        int32_t slip = (int32_t)((uint32_t)iabs(CI32(c, 0x880 + 4 * (uint32_t)w)) - 0xccc);
        int32_t side = (int32_t)((uint32_t)iabs(CI32(c, 0x870 + 4 * (uint32_t)w)) - 0xccc);
        int32_t wv[3], fwd[3], lat[3];
        uint32_t wo = 0x42c + 0xc * (uint32_t)w;   /* the wheel's velocity */
        car_vec(wv, c, wo);
        /* the forward (+0x6b4 front, +0x6c0 rear) axis times the +0x870 slip, the side axis (+0x6cc,
           +0x6d8) times -0.5..0.5 */
        int32_t ax[3];
        car_vec(ax, c, rear ? 0x6c0 : 0x6b4);
        vscale(fwd, ax, CI32(c, 0x870 + 4 * (uint32_t)w));
        int32_t r = -0x8000 - rand_neg();
        car_vec(ax, c, rear ? 0x6d8 : 0x6cc);
        vscale(lat, ax, r);
        bool even;
        if (type == 5) {   /* (never: the type is 4 or 6 here) */
            vscale(lat, lat, 0x1999);
            even = true;
        } else {
            vscale(lat, lat, 0x4ccc);
            even = false;
        }
        if (!second) even = true;
        vscale(fwd, fwd, 0x1999);
        int32_t vel[3];
        for (int k = 0; k < 3; k++) vel[k] = fwd[k] + wv[k] + lat[k];
        if ((slip > 0 || side > 0) && fdiv(slip, 0x20000) + fdiv(side, 0x8000) > 0x8000) count = count * 3 / 2;
        if (Car_RecordFlag(car) || Race_State(car) == 9) {
            /* (the original reads the race's process slot of any car; the port's Race_State is -1
               past the players) */
            type = 7;
            even = true;
        }
        if (dust) type += 4;
        int32_t back[3];
        vscale(back, run, rand_neg());
        /* the fraction towards the axle partner (w ^ 1): 0..1, on the second axle divided by
           1.33..10.33 and by 3..17 unless even (always even on the first) */
        int32_t t = rand_pos();
        if (!even) t = fdiv(t, 0x1547a - 9 * rand_neg());
        if (second && !even) t = fdiv(t, 0x30000 - 14 * rand_neg());
        int32_t p0[3], p1[3], pt[3];
        for (int k = 0; k < 3; k++) {
            p0[k] = fx_mul(gms32(v3(0x543200, 4 * car + (w ^ 1)) + 4 * (uint32_t)k), t);
            p1[k] = fx_mul(gms32(v3(0x543200, n) + 4 * (uint32_t)k), 0x10000 - t);
            pt[k] = p0[k] + p1[k];
        }
        int arg = car;
        uint8_t shade = gm8(0x543120 + (uint32_t)n);
        uint8_t passes = c->second->mask;
        if (type == 6 || type == 10 || type == 4 || type == 8) {
            int32_t pos[3], v[3];
            for (int k = 0; k < 3; k++) pos[k] = pt[k] - CI32(c, 0x2d0 + 4 * (uint32_t)k) + back[k];
            if (type != 4 && type != 8 && !rwd && rear) {
                /* (never: types 6 and 10 on the rear axle mean +0xb1e is 7) 0.4 of the car's length
                   (wheel 2 less wheel 0) forward */
                int32_t len[3];
                for (int k = 0; k < 3; k++)
                    len[k] = gms32(v3(0x543200, 4 * car + 2) + 4 * (uint32_t)k) -
                             gms32(v3(0x543200, 4 * car) + 4 * (uint32_t)k);
                vscale(len, len, 0x6666);
                for (int k = 0; k < 3; k++) pos[k] += len[k];
            }
            v[0] = -(wv[0] / 4), v[1] = wv[1] / 10, v[2] = -(wv[2] / 4);
            int32_t ws = iabs(Car_WheelSpeed(c, 2, 0));
            if (ws > limit) {
                int32_t f = fdiv(limit, ws);
                vscale(v, v, f);
            } else {
                int32_t dv[3];
                for (int k = 0; k < 3; k++) dv[k] = vel[k] - wv[k];
                vscale(dv, dv, 0x6666);
                for (int k = 0; k < 3; k++) v[k] += dv[k];
            }
            Particle_Emit(type, pos, v, pos[1] - 0x10000, NULL, rgb, shade, &arg, passes);
        } else {
            /* types 7 and 11: the point less the wheel's velocity, thrown with that velocity */
            int32_t pos[3];
            for (int k = 0; k < 3; k++) pos[k] = pt[k] - wv[k];
            Particle_Emit(type, pos, wv, pt[1] - 0x10000, NULL, rgb, shade, &arg, passes);
        }
    }
    (void)count;
}

/* Vec_Mul3 0x4b9c20 with the car's orientation (+0x750, the car record's first 16 ints): v as a row
   vector times its 3 x 3 */
static void vec_mul3(int32_t out[3], const int32_t v[3], const Car *c)
{
    for (int k = 0; k < 3; k++)
        out[k] = fx_mul(CI32(c, 4 * (uint32_t)(8 + k)), v[2]) + fx_mul(CI32(c, 4 * (uint32_t)(4 + k)), v[1]) +
                 fx_mul(CI32(c, 4 * (uint32_t)k), v[0]);
}

/* Wheels_Splash 0x45c820: water thrown by each wheel in water (materials 0xe, 0xf): a splash sprite
   (type 14 or 15, mirror images, by the wheel's side and the car's side axis against the camera's
   forward axis; half the time the other way at random on the first axle, half the time none on the
   second). A wet surface (materials 0x18, 0x19, 0x2a) in the rain above 30 of 255 picks the spray
   (type 12, alpha the rain's level) but also clears the flag that lets a wheel emit, so the spray
   never comes; a wheel that sets the flag keeps it for the wheels after it, which then throw type 0
   (the exhaust's smoke) unless they are in water themselves. Fewer below 30 of wheel 0's speed (at
   that fraction), half from any car but 0. The particle sits between the wheel's ground point and
   its axle partner's (w ^ 1), much nearer its own (the fraction 0..1 divided by 1.33..10.33 and by
   3..17), on the second axle (and for type 0) moved back along the last mark's run by up to all of
   it; thrown at a tenth of the wheel's rise and a fifth of its x and z backwards (type 0: two thirds
   on the front wheels, a third on the rear), scaled down above 30 of wheel 2's speed. */
static void Wheels_Splash(int car)
{
    uint8_t rgba[4] = {0xff, 0xff, 0xff, 0xff};   /* (kept from wheel to wheel) */
    if (car >= 8) return;
    Car *c = Car_Get(car);
    bool emits = false;   /* (kept from wheel to wheel) */
    for (int w = 0; w < 4; w++) {
        bool on = false;
        int type = 0;
        bool left = w == 0 || w == 2;
        int n = 4 * car + w;
        int32_t run[3];
        for (int k = 0; k < 3; k++)
            run[k] = gms32(v3(0x543200, n) + 4 * (uint32_t)k) - gms32(v3(0x543400, n) + 4 * (uint32_t)k);
        /* the second axle: the rear on a car whose +0xb1e is 7, else the front (as Wheels_Dust) */
        bool second = (CI8(c, 0xb1e) == 7) == (w == 2 || w == 3);
        int32_t mat = CI16(c, 0xaae + 2 * (uint32_t)w);
        bool water = mat == 0xe || mat == 0xf;
        bool wet = mat == 0x19 || mat == 0x18 || mat == 0x2a;
        if (Weather_Kind(car) == 1) {
            int32_t level = fx_mul(Weather_Level(car), 0xff0000) >> 16;
            if (level > 0x1e && wet) {
                rgba[3] = (uint8_t)level;
                type = 0xc;
                emits = false;   /* (so type 12 is never thrown) */
            }
        }
        int count = 1;
        if (water) {
            emits = true;
            rgba[3] = 0xff;
            int32_t f[3];
            Camera_Forward(car, f);   /* 0x4baca0 on the camera record 0x538d30 + 100 car */
            int32_t dot = fx_mul(CI32(c, 0x360), f[0]) + fx_mul(CI32(c, 0x364), f[1]) + fx_mul(CI32(c, 0x368), f[2]);
            type = (dot < 0 ? !left : left) ? 0xe : 0xf;
            if (rand_pos() < 0x8000) {
                if (second) count = 0;
                else type = rand_pos() >= 0x8000 ? 0xf : 0xe;
            }
            on = true;
        } else if (emits) {
            on = true;   /* (type 0, or 12 when the rain set it: then emits is clear) */
        }
        if (!CI32(c, 0xbac + 4 * (uint32_t)w)) on = false;
        int32_t sp = iabs(Car_WheelSpeed(c, 0, 0));
        if (sp < 0x1e0000) {
            int32_t f = fdiv(sp, 0x1e0000);
            if (rand_pos() > f) on = false;
        }
        if (car > 0 && rand_pos() > 0x8000) continue;
        if (!on) continue;
        for (; count > 0; count--) {
            int32_t back[3];
            vscale(back, run, rand_neg());
            bool splash = !second && water;
            /* (one division only for a first-axle wheel in water with a type other than 14 and 15:
               never) */
            int32_t t = rand_pos();
            t = fdiv(t, 0x1547a - 9 * rand_neg());
            if (!splash || type == 0xe || type == 0xf) t = fdiv(t, 0x30000 - 14 * rand_neg());
            int32_t pt[3];
            for (int k = 0; k < 3; k++) {
                int32_t p0 = fx_mul(gms32(v3(0x543200, 4 * car + (w ^ 1)) + 4 * (uint32_t)k), t);
                int32_t p1 = fx_mul(gms32(v3(0x543200, n) + 4 * (uint32_t)k), 0x10000 - t);
                pt[k] = p0 + p1;
                if (!splash) pt[k] += back[k];
            }
            int32_t pos[3], v[3];
            for (int k = 0; k < 3; k++) pos[k] = pt[k] - CI32(c, 0x2d0 + 4 * (uint32_t)k);
            uint32_t wo = 0x42c + 0xc * (uint32_t)w;   /* the wheel's velocity */
            int32_t wx = CI32(c, wo), wy = CI32(c, wo + 4), wz = CI32(c, wo + 8);
            v[1] = wy / 10;
            if (type == 0xe || type == 0xf) {
                v[0] = wx / -5, v[2] = wz / -5;
            } else if (w == 2 || w == 3) {
                v[0] = wx / -3, v[2] = wz / -3;
            } else {
                v[0] = (int32_t)(0u - 2u * (uint32_t)wx) / 3, v[2] = (int32_t)(0u - 2u * (uint32_t)wz) / 3;
            }
            int32_t ws = Car_WheelSpeed(c, 2, 0);
            if (ws > 0x1e0000) vscale(v, v, fdiv(0x1e0000, ws));
            int arg = car;
            uint8_t shade = gm8(0x543120 + (uint32_t)n);
            uint8_t passes = c->second->mask;
            if (type == 0xd) {
                /* (never) a third of the wheel's velocity, at the point itself, its ground 6.2 below */
                v[0] = wx / 3, v[1] = wy / 3, v[2] = wz / 3;
                Particle_Emit(0xd, pt, v, pt[1] - 0x630000, NULL, rgba, shade, &arg, passes);
            } else {
                /* (type 0 has no callback to add the car's place: its puff sits about the world's
                   origin, as in the original) */
                Particle_Emit(type, pos, v, pos[1] - 0x10000, NULL, rgba, shade, &arg, passes);
            }
        }
    }
}

/* Stones_Kind 0x49b0d0: the kind of stone (0-12, -1 none) a surface throws in the rally, kept for the
   last surface asked (0x593860, the kind at 0x59386c, both reset by Stones_Setup) */
static int Stones_Kind(int16_t s)
{
    if (s == gms16(0x593860)) return gms16(0x59386c);
    int k = -1;
    switch (Tod_Rally() & 0xff) {
    case 0: k = s == 0xd ? 2 : s == 0x13 ? 1 : s == 0x1a ? 0 : -1; break;
    case 1:
        switch (s) {
        case 3: k = 6; break;
        case 4: case 6: k = 7; break;
        case 5: case 7: k = 8; break;
        case 8: k = 2; break;
        case 9: case 0xb: k = 9; break;
        case 0xa: case 0xc: k = 10; break;
        case 0x1b: k = 1; break;
        default: break;
        }
        break;
    case 2: k = s == 0x1a || s == 0x22 ? 0 : s == 0x1c || s == 0x1d ? 1 : -1; break;
    case 4: k = s == 2 ? 3 : s == 0x47 ? 5 : s == 0x53 ? 4 : -1; break;
    case 5:
        if (s == 3 || s == 8 || s == 0xd || (s >= 0x10 && s <= 0x13) || s == 0x1b || s == 0x1c) k = 3;
        break;
    case 7: k = s == 0xf ? 9 : s == 0x13 || s == 0x14 ? 0xb : s == 0x1a || s == 0x1f ? 0xc : -1; break;
    default: break;
    }
    gw16(0x59386c, (uint16_t)k);
    gw16(0x593860, (uint16_t)s);
    return k;
}

/* Wheels_Stones 0x49b3e0: stones and mud thrown by each spinning wheel (+0xbe8) of an upright car on
   the ground, by the kind its surface gives in the rally (Stones_Kind): per kind (13) its colour
   (0x5206f8), stone or mud (0x52072c: type 29 or 30), thrown on a dry stage too (0x520760, the
   stones) and the steps between two (0x520794: bytes at dry and at the wettest, counted down per
   player and wheel at 0x593874). A wheel throws when its slips (+0x870 and +0x880, along x and z;
   the front wheels' turned by the steering angle +0xb10) sum above 0.35: from its contact point
   (+0x3c0) 0.35 down turned with the car (its ground 0.1 below that), along the slip's direction by
   0.5..1 of the stones' speed (0x593894), up by 0.5..1 of it, to a side by up to a third of it
   (0x593898), all times the slip's length over the stones' (0x593870) at most 1; stones twice as far
   along, half as high, 0.7 to the side. Not thrown when its x points one way (negative on the front
   wheels, positive on the rear) and its z the other (negative on wheels 0 and 2, positive on 1 and
   3). Its place follows the car's model (+0x71c, the local matrix's translation), its shade the
   car's +0xa70. */
static void Wheels_Stones(int car)
{
    Car *c = Car_Get(car);
    if (!CI32(c, 0xb74)) return;
    int player = CI8(c, 0xb1a);
    for (int w = 0; w < 4; w++) {
        uint32_t u = (uint32_t)w;
        if (!CI32(c, 0xbac + 4 * u)) continue;
        int k = Stones_Kind(CI16(c, 0xac6 + 2 * u));
        int32_t wet = Weather_CarWet(c);   /* Weather_CarValue 0x460c80 */
        if (k < 0) continue;
        uint32_t kk = (uint32_t)k;
        if (!wet && !gm32(0x520760 + 4 * kk)) continue;
        uint32_t cnt = (uint32_t)(0x593874 + 4 * player + w);
        gw8(cnt, (uint8_t)(gm8(cnt) - 1));
        if (gms8(cnt) > 0) continue;
        bool stone = gm32(0x52072c + 4 * kk) != 0;
        if (!stone && iabs(Car_WheelSpeed(c, w, 0)) > 0x1e0000)
            PENDING_NOTE(0x45c750, "the player's wheel spin tally (0x45c750 player, w, 0, 1)");
        int32_t lo = gms8(0x520794 + 2 * kk), hi = gms8(0x520795 + 2 * kk);
        gw8(cnt, (uint8_t)((fx_mul((hi - lo) * 0x10000, wet) >> 16) + lo));
        bool spin = CI32(c, 0xbe8 + 4 * u) != 0;
        int32_t a = spin ? CI32(c, 0x870 + 4 * u) : fx_mul(CI32(c, 0x870 + 4 * u), 0x4ccc);
        int32_t b = CI32(c, 0x880 + 4 * u);
        if (!spin || (int32_t)((uint32_t)iabs(a) + (uint32_t)iabs(b)) <= 0x5999) continue;
        int32_t x, z;
        if (w < 2) {
            uint32_t ang = CI16(c, 0xb10) & 0xffffu;
            int32_t co = g_sin[(ang + 0x400) & 0xfff], si = g_sin[ang & 0xfff];
            int32_t ax = fx_mul(a, co), az = fx_mul(a, -si), bx = fx_mul(b, si), bz = fx_mul(b, co);
            if (stone) {
                ax = fx_mul(ax, 0x20000), az = fx_mul(az, 0x20000);
                bx = fx_mul(bx, 0x8000), bz = fx_mul(bz, 0x8000);
            }
            x = bx + ax, z = bz + az;
        } else {
            x = a, z = b;
            if (stone) x = fx_mul(x, 0x20000), z = fx_mul(z, 0x8000);
        }
        /* the direction (d) and its perpendicular on the ground (-dz, 0, dx) */
        int32_t len = (int32_t)Fx_Sqrt((uint32_t)(fx_mul(x, x) + fx_mul(z, z)));
        int32_t inv = fdiv(0x10000, len);
        int32_t dx = fx_mul(inv, x), dz = fx_mul(inv, z);
        int32_t o[3] = {CI32(c, 0x3c0 + 0xc * u), CI32(c, 0x3c4 + 0xc * u) - 0x5999, CI32(c, 0x3c8 + 0xc * u)}, pos[3];
        vec_mul3(pos, o, c);
        int32_t ground = pos[1] - 0x1999;
        int32_t r1 = rand_pos(), r2 = rand_pos(), r3 = rand_pos();
        int32_t along = gms32(0x593894) - fx_mul(r1, gms32(0x593864));
        int32_t up = gms32(0x593894) - fx_mul(r2, gms32(0x593864));
        int32_t side = gms32(0x593898) - fx_mul(r3, gms32(0x593898));
        if (rand_pos() > 0x8000) side = -side;
        int32_t f = (int32_t)Fx_Sqrt((uint32_t)(fx_mul(a, a) + fx_mul(b, b)));
        f = fx_mul(f, 0x8000);
        f = fx_mul(f, gms32(0x593870));
        if (f > 0x10000) f = 0x10000;
        along = fx_mul(along, f), up = fx_mul(up, f), side = fx_mul(side, f);
        if (stone) along = fx_mul(along, 0x20000), up = fx_mul(up, 0x8000), side = fx_mul(side, 0xb333);
        int32_t vel[3] = {fx_mul(dx, along) + fx_mul(-dz, side), up, fx_mul(dz, along) + fx_mul(dx, side)};
        int towards = 0;
        if (w < 2 ? vel[0] < 0 : vel[0] > 0) towards++;
        if ((w & 1) == 0 ? vel[2] < 0 : vel[2] > 0) towards++;
        if (towards >= 2) continue;
        uint8_t shade = (uint8_t)(0xff - (uint8_t)((CI32(c, 0xa70) * 0xff) >> 16));
        int32_t wv[3];
        vec_mul3(wv, vel, c);
        uint8_t rgba[4];
        for (int i = 0; i < 4; i++) rgba[i] = gm8(0x5206f8 + 4 * kk + (uint32_t)i);
        Particle_Emit(stone ? 0x1d : 0x1e, pos, wv, ground, &c->main->local.m[12], rgba, shade, NULL,
                      c->second->mask);
    }
}

void Effects_Car(int car)
{
    if (car >= 8) return;
    Car *c = Car_Get(car);
    if (CI32(c, 0xc0c) || (gm32(0x52afd0) >> 25 & 3) == 2) return;
    uint32_t rgba = 0xff000000u | (gm32(0x543f28 + 0x10 * (uint32_t)car) & 0xffffff);   /* 0x463270 */
    PENDING_NOTE(0x462aa0, "the light under the wheels (0x543f28)");
    light_set(car, rgba);
    if (Camera_Kind(car) != 3) {
        wheels_grass(car);   /* 0x45d540 */
        Wheels_Dust(car);    /* 0x45b580 */
        Wheels_Splash(car);  /* 0x45c820 */
        Wheels_Stones(car);  /* 0x49b3e0 */
    }
    PENDING_NOTE(0x45af90, "the exhaust's flames 0x45af90");
}
