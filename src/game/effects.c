/* The race's particle effects (see effects.h). */
#include "game/effects.h"
#include "exe.h"
#include "fixmath.h"
#include "game/common.h"
#include "game/lamps.h"
#include "game/mem.h"
#include "game/timer.h"
#include "pending.h"
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

/* the types' callbacks, not ported yet */
static void cb_45d2d0(Particle *p, PType *t, void *a) { (void)p, (void)t, (void)a; PENDING_NOTE(0x45d2d0, "the dust's step"); }
static void cb_45d270(Particle *p, PType *t, void *a) { (void)p, (void)t, (void)a; PENDING_NOTE(0x45d270, "the smoke's step"); }
static void cb_45d250(Particle *p, PType *t, void *a) { (void)p, (void)t, (void)a; PENDING_NOTE(0x45d250, "a particle's step"); }
static void cb_45d3a0(Particle *p, PType *t, void *a) { (void)p, (void)t, (void)a; PENDING_NOTE(0x45d3a0, "the leaves' step"); }
static void cb_45dea0(Particle *p, PType *t, void *a) { (void)p, (void)t, (void)a; PENDING_NOTE(0x45dea0, "the splashes' step"); }
static void cb_45de80(Particle *p, PType *t, void *a) { (void)p, (void)t, (void)a; PENDING_NOTE(0x45de80, "a particle's spawn"); }
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
