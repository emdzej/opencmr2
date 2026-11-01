/* The start lights (see lights.h). */
#include "game/lights.h"
#include "game/common.h"
#include "game/mem.h"
#include "render/tex.h"
#include "world/glow.h"
#include "world/tod.h"
#include "exe.h"
#include "fixmath.h"
#include <stdio.h>
#include <string.h>

/* the exe's tables: per layout the second glow's offset along the gantry (0x51b6b8, 0: none), the
   lights' z (0x51b2a8); per layout and light the x and y (0x51b1a8, 8 bytes), the colour (0x51b2b8);
   per colour and layout the size (0x51b6c8); per rally the stages with 8 lights (0x51b6f8, u16 bits);
   per layout, step and light whether it is lit (0x51b338) */
static int32_t second(int l) { return gms32(0x51b6b8 + 4 * (uint32_t)l); }

static bool have;              /* DAT_00547adc */
static FxMat gantry;           /* 0x547b40 */
static Tex *tex[2];            /* 0x547cc8, 0x547ccc */
static bool built;             /* DAT_00547cd4 */
static int count, layout;      /* DAT_00547cd8, DAT_00547ad8 */
static uint8_t step;           /* DAT_00547b80 */
static struct { Glow *a, *b; int32_t level; } light[8];   /* 0x547ae0 */

int Lights_Count(void) { return built ? count : 0; }
int32_t Lights_Level(int i) { return i >= 0 && i < 8 ? light[i].level : 0; }

void Lights_Gantry(const uint8_t *bsp)
{
    have = bsp != NULL;
    if (!bsp) return;
    static const int rows[4] = {0, 4, 8, 12};
    for (int r = 0; r < 4; r++)
        for (int k = 0; k < 3; k++) {
            const uint8_t *p = bsp + 12 * r + 4 * k;
            gantry.m[rows[r] + k] = (int32_t)((uint32_t)p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24);
        }
}

void Lights_Load(void)
{
    static const uint32_t name[2] = {0x51b724, 0x51b70c};   /* \NEWIMAGE\lgt_red.tga, lgt_gre.tga */
    for (int i = 0; i < 2; i++) {
        char p[260];
        snprintf(p, sizeof p, "%s%s", exe_str(0x516a50), exe_str(name[i]));   /* 0x40ed50's \Textures */
        uint8_t found;
        tex[i] = Bfl_FindTexture(Bfl_CommonArc(), p, &found, false, 0);
        if (tex[i]) Tex_SetBlend(tex[i], 1);
    }
}

static int32_t unit_row(int32_t out[3], const int32_t *row)
{
    int32_t v[3] = {row[0], row[1], row[2]};
    int32_t l = (int32_t)Fx_Sqrt((uint32_t)fx_mul(v[0], v[0]) + (uint32_t)fx_mul(v[1], v[1]) + (uint32_t)fx_mul(v[2], v[2]));
    int32_t r = l ? (int32_t)(0x100000000LL / l) : 0;   /* the original divides by a zero length */
    for (int k = 0; k < 3; k++) out[k] = fx_mul(v[k], r);
    return l;
}

/* 0x4b9c20 */
static void vmul(int32_t o[3], const int32_t v[3], const int32_t m[16])
{
    for (int k = 0; k < 3; k++) o[k] = fx_mul(m[8 + k], v[2]) + fx_mul(m[4 + k], v[1]) + fx_mul(m[k], v[0]);
}

/* 0x463410: the frame's rows unit (their lengths the scale per axis), the glows' direction the
   frame's -z; each light at its table place times the scales through the frame, a kind 2 glow of its
   colour's size times the x scale, a second one offset along the frame's x when the layout has it */
void Lights_Setup(void)
{
    built = false;
    if (!have) return;
    const int32_t *t = gantry.m + 12;
    if (!t[0] && !t[1] && !t[2]) return;
    uint32_t stage = Tod_Stage();
    if (stage == 10) {
        count = 7;
        layout = Tod_Rally() == 3 ? 2 : 3;
    } else if (gm16(0x51b6f8 + 2 * (Tod_Rally() & 0xff)) & (1u << (stage & 0x1f))) count = 8, layout = 1;
    else count = 7, layout = 0;
    int32_t m[16] = {0};
    int32_t sx = unit_row(m, gantry.m), sy = unit_row(m + 4, gantry.m + 4), sz = unit_row(m + 8, gantry.m + 8);
    const int32_t back[3] = {0, 0, -0x10000};
    int32_t dir[3];
    vmul(dir, back, m);
    unit_row(dir, dir);
    int32_t off[3];
    for (int k = 0; k < 3; k++) off[k] = fx_mul(gantry.m[k], second(layout));
    for (int i = 0; i < count; i++) {
        uint32_t n = (uint32_t)(i + layout * 8);
        int32_t lp[3] = {fx_mul(gms32(0x51b1a8 + 8 * n), sx), fx_mul(gms32(0x51b1ac + 8 * n), sy),
                         fx_mul(gms32(0x51b2a8 + 4 * (uint32_t)layout), sz)};
        int32_t p[3];
        vmul(p, lp, m);
        for (int k = 0; k < 3; k++) p[k] += t[k];
        uint32_t col = gm32(0x51b2b8 + 4 * n);
        int32_t size = fx_mul(sx, gms32(0x51b6c8 + 4 * (col + (uint32_t)layout * 3)));
        Tex *tx = col < 2 ? tex[col] : NULL;
        light[i].a = Glow_Add(2, p, dir, size, size, tx, tx, 0, NULL, 0, 0);
        light[i].b = NULL;
        if (second(layout)) {
            for (int k = 0; k < 3; k++) p[k] += off[k];
            light[i].b = Glow_Add(2, p, dir, size, size, tx, tx, 0, NULL, 0, 0);
        }
        light[i].level = 0;
        Glow_Show(light[i].a, false);
        if (second(layout)) Glow_Show(light[i].b, false);
    }
    step = 6;
    built = true;
}

/* 0x463bd0: each light's level 0.2 a step towards its state at this step of the countdown, shown
   while above 0 */
void Lights_Step(void)
{
    if (!built) return;
    for (int i = 0; i < count; i++) {
        uint32_t n = (uint32_t)i + ((uint32_t)layout * 7 + step) * 8;
        int32_t want = gm32(0x51b338 + 4 * n) ? 0x10000 : 0;
        int32_t d = want - light[i].level;
        if ((d < 0 ? -d : d) < 0x3333) light[i].level = want;
        else light[i].level += d < 1 ? -0x3333 : 0x3333;
        bool on = light[i].level > 0;
        Glow_Show(light[i].a, on);
        if (second(layout)) Glow_Show(light[i].b, on);
        Glow_Level(light[i].a, light[i].level);
        if (second(layout)) Glow_Level(light[i].b, light[i].level);
    }
}

void Lights_Countdown(int s)
{
    step = (uint8_t)s;
    if ((uint8_t)s > 7) step = 6;
}

void Lights_Reset(void)
{
    step = 6;
    if (!built) return;
    for (int i = 0; i < count; i++) {
        light[i].level = 0;
        Glow_Show(light[i].a, false);
        if (second(layout)) Glow_Show(light[i].b, false);
    }
}
