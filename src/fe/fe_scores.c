/* The High Scores pages (0x823c18 from the options, 0x820d38 from a driver's View): the default
   leaderboards in the game's settings (0x52b044, by 0x405fe0) by difficulty, and a driver's own
   (0x408cb0). */
#include "fe/fe.h"
#include "fe/fe_page.h"
#include "fe/fe_strip.h"
#include "fe/pages.h"
#include "exe.h"
#include "font.h"
#include "game/mem.h"
#include "game/profile.h"
#include "pending.h"
#include "render/d3d7.h"
#include "render/twod.h"
#include "text.h"
#include <stdio.h>

static void screen(int *w, int *h) { D3d_ScreenSize(w, h); }

static int difficulty;   /* DAT_008196e0: the table shown, 0-2 (cycled by Return) */

/* 0x4f3a70: novice first; a driver's codes made (0x4f8b30) */
void fe_4f3a70(Menu *m, bool back)
{
    (void)m, (void)back;
    difficulty = 0;
    if (gm8(0x818848)) PENDING_NOTE(0x4f8b30, "a driver's record codes");
}

/* 0x4f3a90: Return: the next difficulty */
void fe_4f3a90(Menu *m, MenuItem *it)
{
    (void)m, (void)it;
    if (++difficulty > 2) difficulty = 0;
    Menu_Sound(4);
}

/* 0x4e3230: the trail (a driver's: Drivers, View, the driver, the page), the list, the main menu's
   strip unlit */
void fe_4e3230(Menu *m)
{
    int w, h;
    screen(&w, &h);
    if (!gm8(0x818848))
        Fe_Titles(m, w * 0x18 / 0x280, h * 0x26 / 0x1e0, true, 2, NULL, -1);
    else {
        const char *names[4] = {Text_Get(0xb), Text_Get(0x59), Player_Name(0), Text_Get(m->text)};
        Fe_TitleRow(w * 0x18 / 0x280, h * 0x26 / 0x1e0, names, 4);
    }
    Fe_Header();
    List_Draw(m, NULL, -1, -1, 0, true);
    Strip_DrawMain(&pg_81d6d8, false, NULL);
}

/* The tables: the difficulty's name (font 2, at title_y of 480; none when -1), a band above the rows
   with the column heads (font 0), five to eleven rows 25 of 480 apart from top of 480, a line under
   each. Each row: its number (font 3, unlit) at 100 of 640, then the row function's columns. */
typedef void TableRow(int i, int y, int w);
static void table(int title_y, int top, const int *cols, const int *heads, int ncols, int nrows, TableRow *row)
{
    int w, h;
    screen(&w, &h);
    if (title_y >= 0 && difficulty >= 0 && difficulty <= 2)
        Font_Print(2, Text_Get(0x167 + difficulty), w / 2, h * title_y / 0x1e0, gm(0x524968), 10);
    int16_t x = (int16_t)(w * 0x1e / 0x280), wd = (int16_t)(w * 0x280 / 0x280 + w * 0x1e / 0x280 * -2);
    int y0 = h * top / 0x1e0, step = h * 0x19 / 0x1e0;
    int16_t r[4] = {x, (int16_t)(y0 - step), wd, (int16_t)step};
    Rect_Fill(r, gm(0x524978), 4);
    for (int i = 0; i < ncols; i++)
        Font_Print(0, Text_Get(heads[i]), w * cols[i] / 0x280, h * 0x13 / 0x1e0 + (y0 - step), gm(0x524984), 0x12);
    for (int i = 0; i < nrows; i++) {
        int y = h * 0x13 / 0x1e0 + y0 + step * i;
        row(i, y, w);
        int16_t l[4] = {x, (int16_t)(y0 + step * (i + 1)), wd, 1};
        Rect_Fill(l, gm(0x52496c), 1);
    }
}

static void number(int font, int v, int x, int y, uint32_t c)
{
    char b[16];
    snprintf(b, sizeof b, exe_str(0x5250a4), v);
    Font_Print(font, b, x, y, gm(c), 0x12);
}

/* centiseconds as minutes:seconds.hundredths (0x516e3c) */
static void time_cs(uint32_t v, int x, int y)
{
    char b[32];
    snprintf(b, sizeof b, exe_str(0x516e3c), v / 6000, v / 100 % 0x3c, v % 100);
    Font_Print(3, b, x, y, gm(0x524968), 0x12);
}

/* an entry's tag, car (0x40ede0: the front end's names) and gear */
static void entry_common(uint32_t e, int y, int w, int xtag, int xcar, int xgear)
{
    uint32_t v = gm32(e + 4);
    Font_Print(1, (const char *)gm(e), w * xtag / 0x280, y, gm(0x524968), 0x12);
    Font_Print(1, Text_Get((int)(v & 0x3f) + 0x98), w * xcar / 0x280, y, gm(0x524968), 0x12);
    Font_Print(1, exe_str(v & 0x40 ? 0x524db8 : 0x524db4), w * xgear / 0x280, y, gm(0x524968), 0x12);
}

static Menu *shown;   /* the page being drawn (its selection picks the table) */

/* Rally - Championship (0x52b044 + difficulty * 0x3c, 12 bytes each: the tag, then the car bits 0-5,
   automatic bit 6, the points bits 7-13, the position bits 14-17) */
static void champ_row(int i, int y, int w)
{
    uint32_t e = 0x52b044 + (uint32_t)difficulty * 0x3c + (uint32_t)i * 0xc, v = gm32(e + 4);
    number(3, i + 1, w * 100 / 0x280, y, 0x52496c);
    entry_common(e, y, w, 0x96, 0x122, 0x1ae);
    number(3, (int)(v >> 0xe & 0xf) + 1, w * 0x1e5 / 0x280, y, 0x524968);
    number(3, (int)(v >> 7 & 0x7f), w * 0x21c / 0x280, y, 0x524968);
}

/* 0x4e3340 */
void fe_4e3340(Menu *m)
{
    int w, h;
    screen(&w, &h);
    Fe_Header();
    Fe_Titles(m, w * 0x18 / 0x280, h * 0x26 / 0x1e0, true, 2, NULL, -1);
    static const int cols[5] = {0x96, 0x122, 0x1ae, 0x1e5, 0x21c}, heads[5] = {0x16a, 0x16b, 0x16c, 0x16d, 0x16e};
    table(100, 200, cols, heads, 5, 5, champ_row);
    Fe_Hint(Text_Get(0x16f), true);
}

/* Rally - Single Rally: the rally chosen on the strip, by difficulty (from 0x52b044 + 0xb4: 0x3c
   each; the position bits 7-10, the time at +8) */
static void single_row(int i, int y, int w)
{
    uint32_t e = 0x52b044 + (uint32_t)(((difficulty + (shown->sel + 1) * 3) * 5 + i) * 0xc), v = gm32(e + 4);
    number(3, i + 1, w * 100 / 0x280, y, 0x52496c);
    entry_common(e, y, w, 0x8c, 0x10e, 0x18b);
    number(3, (int)(v >> 7 & 0xf) + 1, w * 0x1bd / 0x280, y, 0x524968);
    time_cs(gm32(e + 8), w * 0x208 / 0x280, y);
}

/* 0x4e4130: the rallies' strip (scroller 6) lit under the table */
void fe_4e4130(Menu *m)
{
    int w, h;
    screen(&w, &h);
    shown = m;
    Fe_Header();
    Fe_Titles(m, w * 0x18 / 0x280, h * 0x26 / 0x1e0, true, 2, NULL, -1);
    static const int cols[5] = {0x8c, 0x10e, 0x18b, 0x1bd, 0x208}, heads[5] = {0x16a, 0x16b, 0x16c, 0x16d, 0x174};
    table(100, 200, cols, heads, 5, 5, single_row);
    Strip_Draw(Fe_Scroll(6), true);
    Fe_Hint(Text_Get(0x16f), true);
}

/* Rally - Stage Times: the stage chosen on the strip, ten (eleven for the odd ones) of the 0xb a
   stage keeps (0x52b044 + 0x654, 8 bytes each: the tag, then the car, automatic, the time bits 7-22) */
static void stage_row(int i, int y, int w)
{
    uint32_t e = 0x52b044 + 0x654 + (uint32_t)((shown->sel * 0xb + i) * 8), v = gm32(e + 4);
    number(3, i + 1, w * 100 / 0x280, y, 0x52496c);
    entry_common(e, y, w, 0x96, 0x122, 0x1b8);
    time_cs(v >> 7 & 0xffff, w * 0x208 / 0x280, y);
}

/* 0x4e4fc0: no difficulty; the stages' strip (scroller 7) */
void fe_4e4fc0(Menu *m)
{
    int w, h;
    screen(&w, &h);
    shown = m;
    Fe_Header();
    Fe_Titles(m, w * 0x18 / 0x280, h * 0x26 / 0x1e0, true, 2, NULL, -1);
    static const int cols[5] = {100, 0x96, 0x122, 0x1b8, 0x208}, heads[5] = {0x17d, 0x16a, 0x16b, 0x16c, 0x174};
    table(-1, 0x5a, cols, heads, 5, (m->sel % 2 != 0) + 10, stage_row);
    Strip_Draw(Fe_Scroll(7), true);
    Fe_Hint(Text_Get(0x172), true);
}

/* Arcade - Championship: by the strip's choice and difficulty (0x52b044 + 0xff4, 12 bytes each: the
   position bits 7-9, the points bits 10-15) */
static void arcade_row(int i, int y, int w)
{
    uint32_t e = 0x52b044 + 0xff4 + (uint32_t)(((difficulty + shown->sel * 3) * 5 + i) * 0xc), v = gm32(e + 4);
    number(3, i + 1, w * 100 / 0x280, y, 0x52496c);
    entry_common(e, y, w, 0x96, 0x122, 0x1ae);
    number(3, (int)(v >> 10 & 0x3f), w * 0x21c / 0x280, y, 0x524968);
    number(3, (int)(v >> 7 & 7) + 1, w * 0x1e5 / 0x280, y, 0x524968);
}

/* 0x4e5c90: the strip (scroller 8) */
void fe_4e5c90(Menu *m)
{
    int w, h;
    screen(&w, &h);
    shown = m;
    Fe_Header();
    Fe_Titles(m, w * 0x18 / 0x280, h * 0x26 / 0x1e0, true, 2, NULL, -1);
    static const int cols[5] = {0x96, 0x122, 0x1ae, 0x21c, 0x1e5}, heads[5] = {0x16a, 0x16b, 0x16c, 0x16e, 0x16d};
    table(0x4b, 0xa0, cols, heads, 5, 5, arcade_row);
    Strip_Draw(Fe_Scroll(8), true);
    Fe_Hint(Text_Get(0x16f), true);
}

/* Arcade - Lap Times: the eight locations' records (0x52b044 + 0x1210, 8 bytes each, in that order)
   named in the order 6, 3, 1, 4, 0, 2, 5, 7 (the names don't follow the records: kept) */
static void lap_row(int i, int y, int w)
{
    static const int order[8] = {6, 3, 1, 4, 0, 2, 5, 7};
    uint32_t e = 0x52b044 + 0x1210 + (uint32_t)((i / 3 * 3 + i % 3) * 8), v = gm32(e + 4);
    Font_Print(1, Text_Get(order[i] + 0x27), w * 0x82 / 0x280, y, gm(0x52496c), 0x12);
    entry_common(e, y, w, 0xd2, 0x159, 0x1d6);
    time_cs(v >> 7 & 0xffff, w * 0x20d / 0x280, y);
}

/* 0x4e6a80 */
void fe_4e6a80(Menu *m)
{
    int w, h;
    screen(&w, &h);
    Fe_Header();
    Fe_Titles(m, w * 0x18 / 0x280, h * 0x26 / 0x1e0, true, 2, NULL, -1);
    static const int cols[5] = {0x82, 0xd2, 0x159, 0x1d6, 0x20d}, heads[5] = {0x173, 0x16a, 0x16b, 0x16c, 0x174};
    table(-1, 0xa0, cols, heads, 5, 8, lap_row);
    Fe_Hint(Text_Get(0x172), true);
}

/* 0x4f2d90, 0x4f2e70, 0x4f2f40: a strip's widths for the page (scrollers 6, 7, 8): as Scroll_Measure
   but keeping the selection's slide (+0x68, +0x6c) */
static void measure_keep(int k, Menu *m)
{
    FeScroll *s = Fe_Scroll(k);
    int cur = s->cur, prev = s->prev;
    Scroll_Measure(s, m);
    s->cur = cur, s->prev = prev;
}

void fe_4f2d90(Menu *m, bool back) { (void)back; measure_keep(6, m); }
void fe_4f2e70(Menu *m, bool back) { (void)back; measure_keep(7, m); }
void fe_4f2f40(Menu *m, bool back) { (void)back; measure_keep(8, m); }
void fe_4f3970(Menu *m) { (void)m; Scroll_Update(Fe_Scroll(6)); }   /* 0x4f3970 */
void fe_4f3980(Menu *m) { (void)m; Scroll_Update(Fe_Scroll(7)); }   /* 0x4f3980 */

/* 0x4f3990: the arcade championship's second row (0xe8) off, and the first chosen, with novice shown */
void fe_4f3990(Menu *m)
{
    if (difficulty == 0) {
        m->sel = 0;
        m->items[1].flags &= (uint8_t)~MIF_ENABLED;
    } else
        m->items[1].flags |= MIF_ENABLED;
    Scroll_Update(Fe_Scroll(8));
}
