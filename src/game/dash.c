/* The race's gauges (see dash.h). */
#include "game/dash.h"
#include "game/gstate.h"
#include "game/hud.h"
#include "game/mem.h"
#include "game/common.h"
#include "crt.h"
#include "exe.h"
#include "fixmath.h"
#include "font.h"
#include "bfl.h"
#include "render/d3d7.h"
#include "render/twod.h"
#include "sim/car.h"
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* the players' smoothed speed and revs and their previous steps' (0x53ce44, 0x53cfa0; 0x53ce68,
   0x53ce60), the interpolated ones (0x53ce38, 0x53ce54), the gear's index (0x53ce70: R, N, 1-6 as
   0-7), the gear box's y and its previous (0x53ce98, 0x53ce10), the flash on a change (0x53ce18), the
   last index (0x53cfb0), the steps at a stop (0x53cfa8), the kind (0x53ce78: 1 digital, 0 the dial) */
static int32_t speed[2], revs[2], speed_was[2], revs_was[2], speed_at[2], revs_at[2];
static int32_t gear_idx[2], box_y[2], box_was[2], flash[2], gear_last[2], stopped[2];
static int32_t kind[4];
static bool bar;                          /* DAT_0053ce4c: the split screen's plain bar */
static uint32_t lit_table;                /* DAT_0053ce50: the lit part's widths, 28 halfwords */
static Tex *t_mph, *t_kph, *t_rev, *t_digital;   /* 0x53cfd0, 0x53cfd4, 0x53cfd8, 0x53cfdc */
static Rect16 dig_back, dig_lit;          /* 0x53cfe0/0x53cfe4, 0x53cfe8/0x53cfec */
static int16_t dial_w, dial_h;            /* 0x519d0c, 0x519d0e (0x519d08 the source's corner, 0, 0) */
static int32_t seen_w, seen_h = -1;       /* 0x53ce5c, 0x519dec */
static int32_t aspect;                    /* DAT_0053ce40 */

/* the original's tests for a 1,024-wide mode with the texture memory (0x405c10, 0x4b7560, 0x4b7590);
   the port mounts Com640 (Bfl_MountCommon), so the 640 sizes */
static bool hires(void) { return false; }

static const uint8_t *col(uint32_t a)
{
    static uint8_t c[4][4];
    static int n;
    uint8_t *r = c[n++ & 3];
    for (int i = 0; i < 4; i++) r[i] = gm8(a + (uint32_t)i);
    return r;
}

/* the high halfword of (v << 16) times k, the screen's fractions */
static int16_t at(int v, int32_t k) { return (int16_t)(((int64_t)(v << 16) * k) >> 32); }

static Tex *com_tex(uint32_t name)
{
    char b[260];
    snprintf(b, sizeof b, "%s%s", exe_str(0x516a50), exe_str(name));   /* "%s%s" 0x517d98 of 0x40ed50 */
    uint8_t found;
    return Bfl_FindTexture(Bfl_Com(), b, &found, false, 0);
}

/* 0x445a60 (0x40f020): the units' textures, the digital rev counter's (its unlit half at the top, the
   lit one 0x7a down, 0xb5 by 0x7b) and the dial (0x96 square) */
void Dash_Load(void)
{
    t_mph = com_tex(0x519e58);
    t_kph = com_tex(0x519e3c);
    t_digital = com_tex(0x519e14);
    int16_t w = hires() ? 0x125 : 0xb5, h = hires() ? 0xc5 : 0x7b;
    dig_back = (Rect16){0, 0, w, h};
    dig_lit = (Rect16){0, (int16_t)(h - 1), w, h};
    t_rev = com_tex(0x519df4);
    dial_w = dial_h = hires() ? 0xf4 : 0x96;
}

/* 0x445d30: each player's kind, the digital counter on a split screen (with the bar), else by the
   options' setting (0x52afbc bits 1-2: 1 digital, 2 the dial) or by the car's class (0x519d44) */
static void kinds(void)
{
    for (uint32_t i = 0; i < Gs_Players() && i < 4; i++) {
        if (Hud_SplitScreen()) {
            kind[i] = 1;
            bar = true;
            continue;
        }
        uint32_t o = gm32(0x52afbc) >> 1 & 3;   /* 0x405ef0 */
        if (o) kind[i] = o == 1;
        else {
            int car = (int)(gm32(0x531350 + 0x30u * i) & 0x3f);   /* 0x4086b0 (not in mode 4) */
            kind[i] = (int32_t)gm32(0x519d44 + 4u * exe_u32(0x516b40 + 4u * (uint32_t)car));   /* 0x40ee90 */
        }
        bar = false;
    }
}

/* 0x445c80 (0x40efa0 and the race's rebuild) */
void Dash_Reset(void)
{
    for (int i = 0; i < 2; i++) {
        speed_was[i] = box_was[i] = revs_was[i] = gear_idx[i] = stopped[i] = revs[i] = speed[i] = 0;
        box_y[i] = -1;
    }
    kinds();
    lit_table = hires() ? 0x519db4 : 0x519d7c;
}

/* 0x445db0: the gear box's place again (on a change of the screen's size) */
static void reset_box(void)
{
    stopped[0] = stopped[1] = 0;
    box_y[0] = box_y[1] = -1;
}

/* a random 0 to 1.0 as the original: rand() by 1/32767 (0x5112f0) by 65536.0 (0x5112e0), rounded */
static int32_t rand_unit(void)
{
    uint32_t b = exe_u32(0x5112f0);
    float k;
    memcpy(&k, &b, 4);
    return (int32_t)lrintf((float)crt_rand() * k * 65536.0f);
}

/* 0x445ea0 (Race_Update, the first two players):
   - the speed (0x42b600's first wheel, in mph or km/h by 0x52afb8 bit 31) and the revs (+0x7ac over
     +0x794) eased, 0.6 of the new;
   - at under 1.0 for more than five steps the needle trembles about 0.09-0.12, and below 0.08 always
     about 0.07-0.08;
   - the gear's index; the box slides to its letter, at most 0.011 of the height a step, and flashes
     for 6 steps when it changes. */
void Dash_Update(int p)
{
    if (p > 1) return;
    if (flash[p] > 0) flash[p]--;
    speed_was[p] = speed[p];
    revs_was[p] = revs[p];
    const Car *c = Car_Get(p);
    gear_idx[p] = CI8(c, 0xb1e);
    int32_t v = Car_WheelSpeed(c, 0, (gm32(0x52afb8) >> 31) ? 0 : 1);
    speed[p] = fx_mul(v, 0x9999) + fx_mul(speed[p], 0x6666);
    int32_t q = (int32_t)(((int64_t)CI32(c, 0x7ac) << 16) / CI32(c, 0x794));
    revs[p] = fx_mul(revs[p], 0x6666) + fx_mul(q, 0x9999);
    /* (the original tests +0xb48 == 1 here, both ways the same) */
    if (speed[p] > 0xffff) stopped[p] = 0;
    else if (stopped[p] > 5) {
        if (revs[p] < 0x1eb8) revs[p] = fx_mul(rand_unit(), 0x7ae) + 0x170a;
    } else stopped[p]++;
    if (revs[p] < 0x147a) revs[p] = fx_mul(rand_unit(), 0x28f) + 0x11eb;
    if (speed[p] < 0) speed[p] = -speed[p];
    gear_idx[p] = gear_idx[p] == 7 ? 0 : gear_idx[p] + 1;
    int w, h;
    D3d_ScreenSize(&w, &h);
    int32_t to = fx_mul(h << 16, gear_idx[p] * -0xae1 + 0xbe66) >> 16;
    if (box_y[p] == -1) box_y[p] = to;
    int32_t d = to - box_y[p], m = fx_mul(h << 16, 0x2d7) >> 16;
    box_was[p] = box_y[p];
    if (d > m) d = m;
    if (d < -m) d = -m;
    box_y[p] += d;
    if (gear_idx[p] != gear_last[p]) {
        if (flash[p] == 0) flash[p] = 6;
        gear_last[p] = gear_idx[p];
    }
}

/* 0x445df0 (Race_PoseInterp): the shown speed and revs between the last two steps */
void Dash_Interp(int32_t frac)
{
    for (uint32_t i = 0; i < Gs_SelBits14() && i < 2; i++) {
        speed_at[i] = fx_mul(speed_was[i], 0x10000 - frac) + fx_mul(speed[i], frac);
        revs_at[i] = fx_mul(revs_was[i], 0x10000 - frac) + fx_mul(revs[i], frac);
    }
}

/* 0x445dd0: a player's shown revs */
int32_t Dash_Revs(int p) { return p < 2 ? revs_at[p] : 0; }

/* the gear letters "RN123456" (0x519e88) */
static char gear_char(int i) { return (char)gm8(0x519e88 + (uint32_t)i); }

/* 0x446270: the digital counter at the bottom right, the lit part (0x519d7c's widths by the revs, with
   a faint few more pixels) under its frame; on a split screen instead a bar, its part over 65/480 of
   the height red, in a white frame, each player's half. Then the gear in ocr_60pt and the speed in
   ocr_12pt above it. */
static void digital(int p)
{
    int W, H;
    D3d_ScreenSize(&W, &H);
    int16_t ox = 0, oy = 0;
    if (bar) {
        int32_t k = (W * 0x4b / 0x280) << 16;
        int32_t len = fx_mul(revs[p], k) >> 16;   /* (the step's revs, not the shown) */
        if (!Gs_ModeBit18()) {
            if (p == 0) ox = (int16_t)-(W / 2);
        } else if (p == 0) oy = (int16_t)-(H / 2);
        int32_t lim = H * 0x41 / 0x1e0, over = 0;
        if (lim < len) over = len - lim;
        else lim = len;
        int16_t x0 = (int16_t)(at(W, 0xfd70) - W * 0x82 / 0x280 + ox), y0 = (int16_t)(at(H, 0xfd70) - H * 0x28 / 0x1e0 + oy);
        int16_t bh = (int16_t)(H * 0x14 / 0x1e0), fw = (int16_t)(W * 0x4b / 0x280 + 2);
        int16_t r[4] = {x0, y0, (int16_t)lim, bh};
        Rect_Fill(r, col(0x519d24), 3);
        if (over) {
            r[0] = (int16_t)(r[0] + r[2]), r[2] = (int16_t)over;
            Rect_Fill(r, col(0x519d28), 3);
        }
        int16_t left[4] = {(int16_t)(x0 - 1), (int16_t)(y0 - 1), 1, (int16_t)(bh + 2)};
        Rect_Fill(left, col(0x519d1c), 3);
        int16_t top[4] = {left[0], left[1], fw, 1};
        Rect_Fill(top, col(0x519d1c), 3);
        int16_t right[4] = {(int16_t)(at(W, 0xfd70) + 1 + ox + (W * 0x4b / 0x280 - W * 0x82 / 0x280)), left[1], 1, (int16_t)(bh + 2)};
        Rect_Fill(right, col(0x519d1c), 3);
        int16_t bottom[4] = {left[0], (int16_t)(at(H, 0xfd70) + oy + (bh - H * 0x28 / 0x1e0)), fw, 1};
        Rect_Fill(bottom, col(0x519d1c), 3);
    } else {
        int32_t v = fx_mul(revs_at[p], 0x250000) - 0xa0000;
        int i = v >> 16, lit = 0;
        if (i >= 0) {
            int32_t f = v - i * 0x10000;
            int a = gm16(lit_table + 2u * (uint32_t)i);
            int32_t d = (gm16(lit_table + 2u * (uint32_t)i + 2) - a) << 16;
            lit = a + (fx_mul(f, d) >> 16);
            int top = gm16(lit_table + 0x36);
            if (top <= lit) lit = top;
            int more = hires() ? 5 : 3;
            if (dig_lit.w <= more + lit) more = dig_lit.w - lit - 1;
            Rect16 src = {dig_lit.x, dig_lit.y, (int16_t)lit, dig_lit.h};
            Rect16 dst = {(int16_t)(at(W, 0xfd70) - dig_lit.w), (int16_t)(at(H, 0xfd70) - dig_lit.h), (int16_t)lit, dig_lit.h};
            Sprite_Add(&src, &dst, t_digital, 3, 0, NULL, NULL, col(0x519d1c), 8);
            if (more) {
                uint8_t faint[4] = {gm8(0x519d1c), gm8(0x519d1d), gm8(0x519d1e), 0x5a};
                dst.x = (int16_t)(dst.x + dst.w), src.x = (int16_t)(src.x + src.w);
                dst.w = src.w = (int16_t)more;
                Sprite_Add(&src, &dst, t_digital, 3, 0, NULL, NULL, faint, 8);
            }
        }
        /* the lit part again (none below the table), then the frame */
        Rect16 src = {dig_lit.x, dig_lit.y, (int16_t)lit, dig_lit.h};
        Rect16 dst = {(int16_t)(at(W, 0xfd70) - dig_lit.w), (int16_t)(at(H, 0xfd70) - dig_lit.h), (int16_t)lit, dig_lit.h};
        Sprite_Add(&src, &dst, t_digital, 3, 0, NULL, NULL, col(0x519d1c), 8);
        Rect16 bs = dig_back, bd = {(int16_t)(at(W, 0xfd70) - dig_lit.w), (int16_t)(at(H, 0xfd70) - dig_lit.h), dig_lit.w, dig_lit.h};
        Sprite_Add(&bs, &bd, t_digital, 3, 0, NULL, NULL, col(0x519d1c), 8);
    }
    char b[24], u[8];
    int16_t x = (int16_t)(at(W, 0xfd70) + 1 + ox), y = (int16_t)(at(H, 0xfd70) + 3 + oy);
    snprintf(b, sizeof b, "%c", gear_char(gear_idx[p]));
    y = (int16_t)(y + H * -2 / 0x1e0);
    Font_Print(6, b, x, y, col(0x519d1c), 0x24);
    x = (int16_t)(x + W * -0x67 / 0x280);
    y = (int16_t)(y + H * 3 / 0x1e0);
    snprintf(u, sizeof u, "%s", exe_str((gm32(0x52afb8) >> 31) ? 0x519e80 : 0x519e7c));   /* 0x405eb0 */
    char s[40];
    snprintf(s, sizeof s, exe_str(0x519e74), u, speed[p] >> 16);
    y = (int16_t)(y + H * -4 / 0x1e0);
    Font_Print(5, s, x + 1, y + 1, col(0x519d20), 0x21);
    Font_Print(5, s, x, y, col(0x519d1c), 0x21);
}

/* 0x4471b0: the needle, three triangles about the dial's centre at (px, py) of the screen: corners a
   and b across the needle at its two widths, the hub at c and d along it, the tip at e; the screen's
   scale is 640 by 480 in 16.16, the vertical by the aspect */
static void needle(const int32_t pos[2], int32_t a, int32_t b, int32_t c, int32_t d, int32_t e, uint32_t ang, const uint8_t *rgba)
{
    int W, H;
    D3d_ScreenSize(&W, &H);
    uint8_t c1[4] = {0xf6, 0xf6, 0xf6, rgba[3]}, c2[4] = {0xfa, 0xfa, 0xfa, rgba[3]};
    int32_t sx = hires() ? 0x4000000 : 0x2800000, sy = hires() ? 0x3000000 : 0x1e00000;
    int32_t cx = fx_mul(pos[0], W << 16) - (dial_w / 2) * 0x10000, cy = fx_mul(pos[1], H << 16);
    int32_t s = g_sin[ang & 0xfff], co = g_sin[(ang + 0x400) & 0xfff];
    int32_t ax = fx_mul(fx_mul(a, s), sx), ay = fx_mul(fx_mul(fx_mul(a, co), sy), aspect);
    int32_t bx = fx_mul(fx_mul(b, s), sx), by = fx_mul(fx_mul(fx_mul(b, co), sy), aspect);
    int32_t ex = fx_mul(-fx_mul(e, co), sx), ey = fx_mul(fx_mul(fx_mul(e, s), sy), aspect);
    int32_t dx = fx_mul(-fx_mul(d, co), sx), dy = fx_mul(fx_mul(fx_mul(d, s), sy), aspect);
    int32_t gx = fx_mul(-fx_mul(c, co), sx), gy = fx_mul(fx_mul(fx_mul(c, s), sy), aspect);
    int32_t p1[2] = {gx + ax + cx, ay + gy + cy}, p2[2] = {gx - ax + cx, gy - ay + cy};
    int32_t p3[2] = {dx + bx + cx, by + dy + cy}, p4[2] = {dx - bx + cx, dy - by + cy};
    int32_t tip[2] = {ex + cx, ey + cy};
    Tri_Add(p1, p3, tip, c1, 2);
    Tri_Add(p1, tip, p2, c2, 2);
    Tri_Add(p4, p2, tip, c1, 2);
}

/* 0x447100: the dial, its right edge at x of the screen and centred on y */
static void dial(const int32_t pos[2])
{
    int W, H;
    D3d_ScreenSize(&W, &H);
    Rect16 src = {0, 0, dial_w, dial_h};
    Rect16 dst = {(int16_t)(((int64_t)(W << 16) * pos[0] >> 32) - dial_w), (int16_t)(((int64_t)(H << 16) * pos[1] >> 32) - dial_h / 2), dial_w, dial_h};
    Sprite_Add(&src, &dst, t_rev, 2, 0, NULL, NULL, col(0x519d40), 8);
}

/* 0x446bf0: the dial at the bottom right with its needle (drawn under and over the dial), the speed in
   lcd_640 above it and the unit's texture under it, and the gears' column (hel_13pt) to its left
   with the box at the gear (red in reverse, green in neutral, else blue), flashing white for three
   steps on a change and fading back over three */
static void analog(int p)
{
    int W, H;
    D3d_ScreenSize(&W, &H);
    char spd[16];
    snprintf(spd, sizeof spd, exe_str(0x519e94), speed[p] >> 16);
    aspect = 0x15553;
    if (W != seen_w || H != seen_h) {
        reset_box();
        seen_w = W, seen_h = H;
    }
    int32_t pos[2] = {0xf333, 0xd113};
    int32_t c = revs_at[p] + 0x1eb8;
    uint32_t ang = (uint32_t)(int32_t)(int16_t)(0x732 - (int16_t)(((int64_t)c * 0xb330000) >> 32));
    needle(pos, 0x100, 0xcc, -0x8cc, 0x1799, 0x18cc, ang, col(0x519d2c));
    dial(pos);
    needle(pos, 0x100, 0x100, -0x8cc, 0x1799, 0x18cc, ang, col(0x519d2c));
    int16_t r[4] = {(int16_t)(at(W, 0xf45a) - 5), 0, at(W, 0x666), at(H, 0x886)};
    uint32_t ca = gear_idx[p] == 0 ? 0x519d38 : gear_idx[p] == 1 ? 0x519d3c : 0x519d34;
    uint8_t k[4] = {gm8(ca), gm8(ca + 1), gm8(ca + 2), gm8(ca + 3)};
    int f = flash[p];
    if (f > 0) {
        if (f < 4) {
            for (int i = 0; i < 3; i++) k[i] = (uint8_t)(k[i] + (0xff - k[i]) * f / 3);
            k[3] = (uint8_t)(k[3] + (gm8(0x519d18) - k[3]) * f / 3);
        } else k[0] = k[1] = k[2] = 0xff, k[3] = gm8(0x519d18);
    }
    uint8_t dim[4] = {k[0], k[1], k[2], (uint8_t)(k[3] >> 1)};
    r[1] = (int16_t)box_was[p];
    Rect_Fill(r, dim, 3);
    r[1] = (int16_t)box_y[p];
    Rect_Fill(r, k, 3);
    Font_Print(4, spd, fx_mul(W << 16, 0xf467) >> 16, fx_mul(H << 16, 0xd90f) >> 16, col(0x519d1c), 0x24);
    Tex *u = (gm32(0x52afb8) >> 31) ? t_mph : t_kph;   /* 0x405eb0 */
    int16_t uw = u ? u->width : 0, uh = u ? u->height : 0;
    Rect16 us = {0, 0, uw, uh};
    Rect16 ud = {(int16_t)(at(W, 0xf78d) - uw / 2), (int16_t)((int16_t)((uint32_t)(H * 0xd113) >> 16) - (hires() ? 0x12 : 0xc)), uw, uh};
    Sprite_Add(&us, &ud, u, 2, 0, NULL, NULL, col(0x519d1c), 8);
    int i = 0;
    for (int32_t y = 0xc553; y > 0x6e4b; y -= 0xae1, i++) {
        char g[4];
        snprintf(g, sizeof g, "%c", gear_char(i));
        Font_Print(0, g, (fx_mul(W << 16, 0xf78d) >> 16) - 5, fx_mul(H << 16, y) >> 16, col(0x519d1c), 0x12);
    }
}

/* 0x446210 (Race_DrawView, before the HUD): the first two players' gauges with the display flag 0x10 */
void Dash_Draw(int p)
{
    if (p >= 2 || !(Hud_Flags(Gs_Byte537ef6() + p) & 0x10)) return;
    if (kind[Gs_Byte537ef6() + p]) digital(p);
    else analog(p);
}
