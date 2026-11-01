/* The race's particle effects (see effects.h). */
#include "game/effects.h"
#include <stdbool.h>
#include <string.h>
#include "world/tod.h"
#include "game/weather.h"
#include "game/camera.h"
#include "crt.h"
#include "exe.h"
#include "fixmath.h"
#include "game/common.h"
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
        PENDING_NOTE(0x45b580, "the wheels' dust and gravel 0x45b580, the splashes 0x45c820, 0x49b3e0");
    }
    PENDING_NOTE(0x45af90, "the exhaust's flames 0x45af90");
}
