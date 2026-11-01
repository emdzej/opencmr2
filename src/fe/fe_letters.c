/* The main menu's letters (0x4f3c10-0x4f4760): 200 letters of "colinmcrae" running along a spline
   through 19 points, the shape of the selected item's (0x525528, 0x98 bytes each; 0x525490 with the
   cheat option 14), morphing from one shape to the next in a quarter of a second; the rally item adds
   a trail of fifteen dots, the arcade item six trails of ten. */
#include "fe/fe.h"
#include "fe/fe_page.h"
#include "fe/fe_res.h"
#include "fe/fe_strip.h"
#include "fe/pages.h"
#include "crt.h"
#include "exe.h"
#include "fixmath.h"
#include "font.h"
#include "game/mem.h"
#include "render/d3d7.h"
#include "render/twod.h"

static int32_t pos[200];            /* DAT_00819994: each letter's place on the curve */
static uint32_t shape_to, shape_from, sub;   /* DAT_00819cb4, DAT_00819dac, DAT_00819da8 */
static int32_t group[6][10];        /* DAT_00819cb8: the arcade's trails */
static int32_t speed[6];            /* DAT_00819de0 */
static int32_t blend[19][2];        /* DAT_00819df8: a morph's points */
static int32_t trail[15];           /* DAT_00819ea4: the rally's trail */
static int phase;                   /* DAT_00819e90: 0 until the first frame */
static uint32_t last = (uint32_t)-1;   /* DAT_0052548c */
static enum { CHEAT, SHAPE, BLEND } ctrl_kind;   /* DAT_00819990: where the curve's points are */
static uint32_t ctrl_shape;

static int32_t ctrl(int k, int c)
{
    if (ctrl_kind == BLEND) return blend[k][c];
    uint32_t base = ctrl_kind == CHEAT ? 0x525490 : 0x525528 + ctrl_shape * 0x98;
    return gms32(base + 8u * (uint32_t)k + 4u * (uint32_t)c);
}
static int32_t shape_pt(uint32_t s, int k, int c) { return gms32(0x525528 + s * 0x98 + 8u * (uint32_t)k + 4u * (uint32_t)c); }
static int32_t fx64(int64_t v) { return (int32_t)(v >> 16); }

/* 0x4f3c10: the point at t (16.16, 0 to 1) of the Hermite curve through points 1 to 17, the tangents
   from the neighbours with the tension 0x525a80 */
static void spline(int32_t t, int32_t out[2])
{
    if (t == 0) {
        out[0] = ctrl(1, 0), out[1] = ctrl(1, 1);
        return;
    }
    if (t == 0x10000) {
        out[0] = ctrl(17, 0), out[1] = ctrl(17, 1);
        return;
    }
    int i = ((t << 4) >> 16) + 1;
    int32_t start = ((i - 1) * 0x10000) / 16;
    int32_t u = (start - t + 0x1000) * 16;
    int32_t u2 = fx64((int64_t)u * u), u3 = fx64((int64_t)u * u2);
    int32_t h0 = u3 * 2 + 0x10000 - u2 * 3, h1 = u2 * 3 - u3 * 2, h2 = u3 - u2 * 2 + u, h3 = u3 - u2;
    int32_t k = 0x10000 - gms32(0x525a80);
    for (int c = 0; c < 2; c++) {
        int64_t a = (int64_t)h2 * (((ctrl(i + 2, c) - ctrl(i, c)) * k) / 2);
        int64_t b = (int64_t)h3 * (((ctrl(i + 1, c) - ctrl(i - 1, c)) * k) / 2);
        out[c] = (ctrl(i + 1, c) * h0 + fx64(b) + ctrl(i, c) * h1 + fx64(a)) >> 16;
    }
}

/* 0x4f3fb0: v scaled by the time since the last frame (a fiftieth of a second the first time) */
static int32_t step(int32_t v)
{
    if (last == (uint32_t)-1) return fx64((int64_t)v * 0x51e);
    int32_t q = (int32_t)(((int64_t)(Fe_Now() - last) << 32) / 0x3e80000);
    return fx64((int64_t)v * q);
}

/* the original's signed remainder of 65536, then into [0, 1) */
static int32_t wrap(int32_t v)
{
    if (v < 0) return v % 0x10000 + 0x10000;
    if (v > 0) return v % 0x10000;
    return v;
}

static int16_t main_id(int i) { return pg_81d6d8.items[i].id; }

/* 0x4f3dd0 */
void Fe_LettersInit(void)
{
    for (int i = 0; i < 200; i++) pos[i] = (i << 16) / 200;
    for (int i = 0; i < 15; i++) trail[i] = 0;
    for (int i = 0; i < 6; i++) speed[i] = 0;
    for (int g = 0; g < 6; g++)
        for (int k = 0; k < 10; k++) group[g][k] = g * 0x4000 / 6;
    last = (uint32_t)-1;
    switch (main_id(pg_81d6d8.sel)) {
    case 0: case 1: case 2: case 3:
        shape_to = shape_from = sub = (uint32_t)main_id(pg_81d6d8.sel);
        break;
    case 4:
        shape_to = shape_from = 4;
        sub = (Fe_Now() - Fe_MainTime()) / 500 % 3;
        break;
    case 5: shape_to = shape_from = sub = 7; break;
    case 6: shape_to = shape_from = sub = 8; break;
    }
}

/* 0x4f4050 */
void Fe_LettersUpdate(void)
{
    int32_t d = step(0xccc);
    for (int i = 0; i < 200; i++) pos[i] = wrap(pos[i] - d);
    uint32_t now = Fe_Now(), since = now - Fe_MainTime();
    if (gm32(0x52e890) >> 0xe & 1) ctrl_kind = CHEAT;
    else if (phase == 0) {
        phase = 1;
        ctrl_kind = SHAPE, ctrl_shape = shape_to;
    } else if (phase == 1) {
        switch (main_id(pg_81d6d8.sel)) {
        case 0: case 1: case 2: case 3: shape_to = (uint32_t)main_id(pg_81d6d8.sel); break;
        case 4:
            shape_to = sub + 4;
            sub = since / 500 % 3;
            break;
        case 5: shape_to = 7; break;
        case 6: shape_to = 8; break;
        }
        uint32_t v = shape_to;
        int prev = Fe_ScrollPrev();
        if (prev != -1) {
            if (main_id(pg_81d6d8.sel) == 4 && since > 0xfa) {
                v = since / 500 % 3 + 3;
                if (v < 4) v = 6;
            } else {
                v = shape_from;
                switch (main_id(prev)) {
                case 0: case 1: case 2: case 3: v = (uint32_t)main_id(prev); break;
                case 4: v = sub + 4; break;
                case 5: v = 7; break;
                case 6: v = 8; break;
                }
            }
        }
        shape_from = v;
        uint32_t e = main_id(pg_81d6d8.sel) == 4 ? since % 500 : since;
        if (e < 0xfb) {
            int32_t f = (int32_t)(((int64_t)((int32_t)e << 16) << 16) / 0xfa0000);
            for (int k = 0; k < 19; k++)
                for (int c = 0; c < 2; c++) {
                    int32_t a = shape_pt(shape_from, k, c), b = shape_pt(shape_to, k, c);
                    blend[k][c] = a + (fx64((int64_t)((b - a) << 16) * f) >> 16);
                }
            ctrl_kind = BLEND;
        } else
            ctrl_kind = SHAPE, ctrl_shape = shape_to;
    }
    trail[0] = wrap(trail[0] - step(0x1333));
    for (int i = 14; i > 0; i--) trail[i] = trail[i - 1];
    for (int g = 0; g < 6; g++) {
        speed[g] += (int32_t)(((int64_t)((crt_rand() % 0x65 - 0x32) << 16) << 16) / 0x3e80000);
        if (speed[g] > 0x18000) speed[g] = 0x18000;
        if (speed[g] < 0x8000) speed[g] = 0x8000;
    }
    d = step(0x3333);
    for (int g = 0; g < 6; g++) {
        group[g][0] = wrap(group[g][0] - fx64((int64_t)d * speed[g]));
        for (int k = 9; k > 0; k--) group[g][k] = group[g][k - 1];
    }
    last = Fe_Now();
}

static void screen(int *w, int *h) { D3d_ScreenSize(w, h); }

/* 0x4f45a0: the letters, half transparent */
void Fe_LettersDraw(void)
{
    int w, h;
    screen(&w, &h);
    static const uint8_t c[4] = {0xff, 0xff, 0xff, 0x80};
    Font_SetCurrent(0, c);
    const char *s = exe_str(0x525480);
    for (int i = 0; i < 200; i++) {
        int32_t p[2];
        spline(pos[i], p);
        Font_DrawGlyph((unsigned char)s[i % 10], (int16_t)(w * p[0] / 0x280), (int16_t)(h * p[1] / 0x1e0));
    }
}

static void dot(Tex *t, const int32_t p[2], uint8_t alpha)
{
    if (!t) return;
    int w, h;
    screen(&w, &h);
    uint8_t c[4] = {0xff, 0xff, 0xff, alpha};
    Rect16 src = {0, 0, t->width, t->height}, d = {(int16_t)(w * p[0] / 0x280 - 2), (int16_t)(h * p[1] / 0x1e0 + 3), t->width, t->height};
    Sprite_Add(&src, &d, t, 1, 0, NULL, NULL, c, 8);
}

/* 0x4f4650: the rally's trail, fading, the bigger dots at its head */
void Fe_LettersTrail(void)
{
    for (int k = 14, a = 0xdf2; a >= 0; k--, a -= 0xff) {
        int32_t p[2];
        spline(trail[k], p);
        dot(g_fe.dot[k * 4 / 15], p, (uint8_t)(0xff - a / 15));
    }
}

/* 0x4f4760: the arcade's six trails */
void Fe_LettersTrails(void)
{
    for (int g = 0; g < 6; g++)
        for (int k = 9, a = 0x8f7; a >= 0; k--, a -= 0xff) {
            int32_t p[2];
            spline(group[g][k], p);
            dot(g_fe.dot[k * 4 / 10], p, (uint8_t)(0xff - a / 10));
        }
}
