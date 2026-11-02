/* The race's intro (see intro.h). */
#include "game/intro.h"
#include "game/gstate.h"
#include "game/mem.h"
#include "game/profile.h"
#include "game/timer.h"
#include "render/d3d7.h"
#include "render/rstate.h"
#include "render/twod.h"
#include "world/tod.h"
#include "exe.h"
#include "font.h"
#include "pending.h"
#include "text.h"
#include <stdio.h>
#include <string.h>

static bool starting;     /* DAT_00536ac8 */
static uint32_t start;    /* DAT_00536acc: when it began, 1/100 s */

void Intro_Start(void) { starting = true; }

static void rgb_alpha(uint8_t c[4], uint32_t a, int alpha)
{
    for (int i = 0; i < 3; i++) c[i] = gm8(a + (uint32_t)i);
    c[3] = (uint8_t)alpha;
}

/* 0x4100a0: no bar in mode 4's late rounds (0x407820, not ported) or with 0x52afb4 bit 17 for a
   second player slot (0x537ef6), which clears the target instead */
static bool no_bar(void)
{
    if (Gs_Mode() == 4) PENDING_NOTE(0x407820, "LoadBar (the network's rounds)");
    if (Gs_ModeBit17() && Gs_Byte537ef6()) {
        PENDING_NOTE(0x49dc60, "Render_ClearTarget (the load bar's)");
        return true;
    }
    return false;
}

void LoadBar_Draw(int pct, int alpha)
{
    if (no_bar()) return;
    int w, h;
    D3d_ScreenSize(&w, &h);
    uint8_t on[4], off[4];
    rgb_alpha(on, 0x516cd4, alpha), rgb_alpha(off, 0x516cdc, alpha);
    int b8 = w * 8 / 0x280, b3 = w * 3 / 0x280;
    for (int i = 0, k = 100; k < 0x4b0; i++, k += 100) {
        /* the box is kept at 0x536bd8 in the original */
        int16_t r[4] = {(int16_t)(w * 0x268 / 0x280 + (b8 + b3) * i - 10 * b3 - 11 * b8), (int16_t)(h * 0x1b2 / 0x1e0),
                        (int16_t)(w * 8 / 0x280), (int16_t)(w * 8 / 0x280)};
        Rect_Fill(r, pct < 100 && pct <= k / 0xb ? on : off, 1);
    }
}

/* 0x40fd30: a word in the big font at (x, y), and unless it is the last a 2-pixel bar after it;
   the next word's x */
static int word(int x, int y, const char *s, bool last, int alpha)
{
    int w, h;
    D3d_ScreenSize(&w, &h);
    uint8_t c[4];
    rgb_alpha(c, 0x516cd0, alpha);
    Font_Print(2, s, x, y, c, 0x11);
    if (last) return x;
    int bx = w * 10 / 0x280 + x + Font_TextWidth(2, s);
    int16_t r[4] = {(int16_t)bx, (int16_t)(h * 200 / 0x1e0), 2, (int16_t)(h * 0x3c / 0x1e0)};
    Rect_Fill(r, c, 1);
    return bx + w * 10 / 0x280;
}

/* 0x4aa090: to lower case, with the font's own codes for a few signs */
static void font_lower(char *s)
{
    for (; *s; s++) {
        signed char c = (signed char)*s;
        if ((c > '@' && c < '[') || (c > -0x41 && c < -0x20)) *s = (char)(c + ' ');
        switch (c) {
        case '$': *s = (char)-0x57; break;
        case '*': *s = (char)-0x52; break;
        case '{': *s = (char)-0x43; break;
        case '|': *s = (char)-0x4f; break;
        case '}': *s = (char)-0x42; break;
        case '~': *s = (char)-0x5d; break;
        case -0x67: *s = (char)-0x29; break;
        case -0x2d: *s = (char)-0xd; break;
        case -9: *s = (char)-0x44; break;
        default: break;
        }
    }
}

static void time_str(char *b, size_t n, uint32_t t)
{
    snprintf(b, n, exe_str(0x516e3c), t / 6000, (t / 100) % 0x3c, t % 100);
}

/* 0x408cb0: a player's profile block, 0 without one (the original reads through NULL then) */
static uint32_t profile_of(int n)
{
    uint32_t v = gm32(0x531350 + 0x30u * (uint32_t)n);
    return (v & 0x3c0000) == 0x3c0000 ? 0 : 0x52fa8c + (v >> 0x12 & 0xf) * 0x650;
}

/* Race_IntroInfo 0x410100: "<0x6b> | <the kind> | <the place> <the stage>" across the middle (the
   super special only text 0x93; mode 4 the rally), and at the bottom right the record (0x52b044's
   table) with its holder under text 0x6d, then each player without a set profile flag its own best,
   and text 0x70 over them */
static void intro_info(int alpha)
{
    int w, h;
    D3d_ScreenSize(&w, &h);
    uint8_t c[4];
    rgb_alpha(c, 0x516cd0, alpha);
    int x = w * 0x1e / 0x280, y = h / 2;
    char b[128];
    if (Gs_SuperSpecial()) {
        word(x, y, Text_Get(0x93), true, alpha);
        return;
    }
    uint32_t m = Gs_Mode(), rec, nameat, own, ownset;
    switch (m) {
    case 0: case 1: case 2: case 3: case 8: case 9: case 10: {
        x = word(x, y, Text_Get(0x6b), false, alpha);
        x = word(x, y, Text_Get(0x41), false, alpha);
        snprintf(b, sizeof b, "%s", Text_Get((int)Tod_Rally()));
        font_lower(b);
        x = word(x, y, b, false, alpha);
        if (Tod_Stage() == 10) snprintf(b, sizeof b, "%s", Text_Get(0xba));
        else snprintf(b, sizeof b, exe_str(0x516098), Text_Get(0x40), (int)Tod_Stage() + 1);
        word(x, y, b, true, alpha);
        uint32_t k = Tod_Rally() * 0xb + Tod_Stage();
        rec = 0x52b044 + 0x658 + k * 8, nameat = 0x52b044 + 0x654 + k * 8;
        own = 0x154 + (Tod_Stage() + Tod_Rally() * 0xc) * 8, ownset = own - 4;
        break;
    }
    case 4:
        x = word(x, y, Text_Get(0x6b), false, alpha);
        x = word(x, y, Text_Get(0x6f), false, alpha);
        snprintf(b, sizeof b, "%s", Text_Get((int)Tod_Rally()));
        font_lower(b);
        word(x, y, b, true, alpha);
        return;
    case 5: case 6: case 7: case 11: case 12: {
        static const int names[8] = {6, 3, 1, 4, 0, 2, 5, 7};   /* 0x40fe50 */
        uint32_t k = (gm32(0x52f2ac) >> 10 & 3) * 3 + (gm32(0x52f2ac) >> 12 & 3);
        x = word(x, y, Text_Get(0x6b), false, alpha);
        x = word(x, y, Text_Get(0x6c), false, alpha);
        snprintf(b, sizeof b, "%s", Text_Get(names[k < 8 ? k : 7]));
        font_lower(b);
        word(x, y, b, true, alpha);
        rec = 0x52b044 + 0x1214 + k * 8, nameat = 0x52b044 + 0x1210 + k * 8;
        own = 0x4c0 + k * 8, ownset = own - 4;
        break;
    }
    default:
        return;
    }
    int tx = w * 0x268 / 0x280, nx = w * 0x22a / 0x280, ty = h * 0x1a6 / 0x1e0, line = h * 0x18 / 0x1e0;
    time_str(b, sizeof b, gm32(rec) >> 7 & 0xffff);
    Font_Print(0, b, tx, ty, c, 0x24);
    snprintf(b, sizeof b, "%s", (const char *)gm(nameat));
    Font_Print(0, b, nx, ty, c, 0x24);
    Font_Print(0, Text_Get(0x6d), tx, ty - line, c, 0x24);
    int shown = 0;
    for (int i = 0; i < (int)Gs_SelBits14(); i++) {
        if (Player_HasProfile(i)) continue;
        uint32_t pr = profile_of(i);
        int yy = ty - line * (shown + 3);
        shown++;
        if (!pr || !(gm8(pr + ownset) & 0x80)) snprintf(b, sizeof b, "%s", exe_str(0x516e30));
        else time_str(b, sizeof b, gm32(pr + own));
        Font_Print(0, b, tx, yy, c, 0x24);
        snprintf(b, sizeof b, "%s", Player_Name(i));
        Font_Print(0, b, nx, yy, c, 0x24);
    }
    if (shown > 0) Font_Print(0, Text_Get(0x70), tx, ty - line * (shown + 3), c, 0x24);
}

void Intro_Draw(int player, int state)
{
    (void)player;
    if (starting) start = Timer_Centis(), starting = false;
    if (state >= 8 || Gs_Mode() == 4) return;
    uint32_t t = Timer_Centis() - start;
    if (t >= 0xe1) return;
    int a = t < 0x19 ? 0xff : 0xff - (int)((t * 0xff - 0x18e7u) / 200);
    int w, h;
    D3d_ScreenSize(&w, &h);
    uint8_t c[4] = {0x9c, 0xb4, 0xac, (uint8_t)a};
    int16_t r[4] = {0, 0, (int16_t)w, (int16_t)h};
    Rect_Fill(r, c, 1);
    intro_info(a);
    LoadBar_Draw(100, a);
}

/* Race_DrawStats 0x410c80 (state 2's draw), the first slot only: unless 0x4100a0, the target cleared
   to the loading screen's colour, the stage's name and records, the loading bar. The original draws it
   three times over (filling the swap chain; the port once a frame) and with option bit 2 the frame
   rate and the T&L kind (debugging, not ported). */
void Intro_Loading(void)
{
    if (no_bar()) return;
    Render_SetClearColour(0x9c, 0xb4, 0xac);
    Render_ClearTarget();
    Render_ClearZ(false);
    intro_info(0xff);
    LoadBar_Draw(0, 0xff);
}
