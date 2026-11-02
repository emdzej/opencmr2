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
#include <string.h>

static void screen(int *w, int *h) { D3d_ScreenSize(w, h); }

static int difficulty;   /* DAT_008196e0: the table shown, 0-2 (cycled by Return) */

/* ---- a driver's record codes (0x4f8b30): each record packed into 32 bits with its key, written as
   digits for the player to send in ---- */

static char codes_stage[8 * 11][0x19];   /* DAT_0082547c: by rally and stage (0x4f9240) */
static char codes_champ[3][0x19];        /* DAT_00825398: by difficulty (0x4f9280) */
static char codes_arcade[2][3][0x19];    /* DAT_008253e4: by class and difficulty (0x4f92a0) */
static char codes_rally[8][3][0x19];     /* DAT_00825d14: by rally and difficulty (0x4f9260) */
static char codes_lap[8][0x19];          /* DAT_00825f6c: by location (0x4f92c0) */

static uint8_t kb(uint32_t va) { const uint8_t *p = exe_data(va, 1); return p ? *p : 0; }

/* 0x4fb9c0: the value's decimal digits (least first, none for 0), then five of check * 256 + the
   bytes' sum + 2 */
static void code_digits(uint32_t v, uint8_t check, char *out)
{
    uint8_t sum = (uint8_t)((v >> 24) + (v >> 16) + (v >> 8) + v + check);
    int n = 0;
    for (; v; v /= 10) out[n++] = (char)('0' + v % 10);
    uint32_t c = (uint32_t)check * 0x100 + (uint8_t)(sum + 2);
    for (int i = 0; i < 5; i++, c /= 10) out[n++] = (char)('0' + c % 10);
    out[n] = 0;
}

/* 0x4fb8d0: the four bytes each plus a key from the tables at 0x526ea4, 0x526eb0, 0x526ebc and
   0x526eab-0x526eae (three times); the check byte plus six more of them */
static void code_encode(uint32_t v, uint8_t kind, char *out)
{
    uint8_t b[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
    for (uint32_t i = 0; i < 4; i++)
        b[i] = (uint8_t)(b[i] + kb(0x526ebc + i) + kb(0x526eb0 + i) + kb(0x526ea4 + i) + kb(0x526eae - i) * 3);
    uint8_t check = (uint8_t)(kb(0x526ec2) + kb(0x526eb6) + kb(0x526eaa) + kb(0x526ec0) + kb(0x526eb4) + kb(0x526ea8) + kind);
    code_digits((uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24, check, out);
}

/* 0x4f8a90: a space after every fourth digit (one at the end too when the count divides by 4) */
static void code_space(char *s)
{
    char t[0x19];
    snprintf(t, sizeof t, "%s", s);
    int n = (int)strlen(t), o = 0;
    if (n < 1) {
        s[0] = 0;
        return;
    }
    for (int i = 1;; i++) {
        s[o++] = t[i - 1];
        if (i % 4 == 0) s[o++] = ' ';
        if (n <= i) break;
    }
    s[o] = 0;
}

/* the record's key (its word's bits 8-12) over the four bytes, in place (the next record keeps some
   of the bits so changed), the kind and key into the check */
static void code_make(uint32_t *v, uint32_t word, int kind, char *out)
{
    uint8_t key = (uint8_t)(word >> 8);
    for (int i = 0; i < 4; i++) *v ^= (uint32_t)((key & 0x1f) << 1) << (8 * i);
    code_encode(*v, (uint8_t)(key << 3 | kind), out);
    code_space(out);
}

/* 0x4f8b30: the five kinds from the driver's records (0x408cb0, the profile + 0x84). The packed word
   keeps the bits a kind doesn't set from the record before (after its key went over them), and the
   arcade championship packs the last championship record's position and points, not its own: both
   kept. Checked against the original in Unicorn on random records: all codes the same. */
static void codes_make(void)
{
    uint32_t r = Player_Records(0), v = 0;
    if (!r) return;
    for (uint32_t ra = 0; ra < 8; ra++)   /* the stage times: kind 0 */
        for (uint32_t j = 0; j < 10 + (ra & 1); j++) {
            uint32_t w = gm32(r + 0x150 + ra * 0x60 + j * 8), t = gm32(r + 0x154 + ra * 0x60 + j * 8);
            char *out = codes_stage[ra * 0xb + j];
            if (!(w & 0x80) || t / 6000 > 0xf) {
                out[0] = 0;
                continue;
            }
            uint32_t u = (~w & 0xffffffc0u) << 0x19 | (w & 0x3f) << 0xc | ((j & 0xf) << 4 | (ra & 0xf)) << 4 | (v & 0x7ffc000fu);
            v = t % 100 << 0x18 | t / 100 % 0x3c << 0x12 | ((u ^ ((t / 6000 ^ u) & 0xf)) & 0x8003ffffu);
            code_make(&v, w, 0, out);
        }
    for (uint32_t d = 0; d < 3; d++)   /* the single rallies: kind 1 */
        for (uint32_t j = 0; j < 8; j++) {
            uint32_t e = r + 0x30 + d * 0xc + j * 0x24, w = gm32(e), t = gm32(e + 8);
            char *out = codes_rally[j][d];
            if (!(w & 0x80) || t / 6000 > 0x3f) {
                out[0] = 0;
                continue;
            }
            uint32_t u = (~w & 0x40) << 9 | (w & 0x3f) << 9 | (j & 7) << 6 | (v & 0xffff003fu);
            v = (t % 100 | (d & 3) << 7) << 0x16 | t / 100 % 0x3c << 0x10 | ((u ^ ((t / 6000 ^ u) & 0x3f)) & 0x8000ffffu);
            code_make(&v, w, 1, out);
        }
    uint32_t last = 0;
    for (uint32_t d = 0; d < 3; d++) {   /* the championships: kind 2 */
        uint32_t w = gm32(r + d * 0x10), p = gm32(r + 4 + d * 0x10);
        last = p;
        if (!(w & 0x80)) {
            codes_champ[d][0] = 0;
            continue;
        }
        uint32_t u = ((d & 3) << 0x12 | (p & 0x1fc0)) << 1 | (p & 0xf) << 0xe | (v & 0xffe0007fu);
        u ^= (w ^ u) & 0x3f;
        v = u ^ ((~w ^ u) & 0x40);
        code_make(&v, w, 2, codes_champ[d]);
    }
    for (uint32_t d = 0; d < 3; d++)   /* the arcade championships: kind 3 */
        for (uint32_t j = 0; j < 2; j++) {
            uint32_t w = gm32(r + 0x450 + d * 0xc + j * 0x24);
            if (!(w & 0x80)) {
                codes_arcade[j][d][0] = 0;
                continue;
            }
            uint32_t u = ((d & 3) << 0xe | (last & 0x7c0)) << 1 | (last & 7) << 0xc | (v & 0xfffe007fu);
            v = (~w & 0x40) | (j & 3) << 0x11 | ((u ^ ((w ^ u) & 0x3f)) & 0xfff9ffbfu);
            code_make(&v, w, 3, codes_arcade[j][d]);
        }
    for (uint32_t i = 0; i < 8; i++) {   /* the lap records: kind 4 */
        uint32_t k = i / 3 * 3 + i % 3, w = gm32(r + 0x4bc + k * 8), t = gm32(r + 0x4c0 + k * 8);
        if (!(w & 0x80) || t / 6000 > 0x3f) {
            codes_lap[i][0] = 0;
            continue;
        }
        v = t % 100 << 0x10 | t / 100 % 0x3c << 10 | (t / 6000 & 0x3f) << 0x17 | (~w & 0x40) | (i & 7) << 7 |
            ((v ^ ((w ^ v) & 0x3f)) & 0xe000003fu);
        code_make(&v, w, 4, codes_lap[i]);
    }
}

/* for checks against the original: the codes made, and one of them (0x4f9240-0x4f92c0) */
void Fe_MakeCodes(void) { codes_make(); }
const char *Fe_Code(int kind, int a, int b)
{
    switch (kind) {
    case 0: return codes_stage[a * 0xb + b];
    case 1: return codes_rally[a][b];
    case 2: return codes_champ[a];
    case 3: return codes_arcade[a][b];
    default: return codes_lap[a];
    }
}

/* 0x4f3a70: novice first; a driver's codes made */
void fe_4f3a70(Menu *m, bool back)
{
    (void)m, (void)back;
    difficulty = 0;
    if (gm8(0x818848)) codes_make();
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

/* ---- a driver's own (from Drivers, View: 0x820d38): the record, its code, or "no record" ---- */

/* 0x4d4040: the trail View, the driver, the page */
static void driver_titles(Menu *m)
{
    int w, h;
    screen(&w, &h);
    const char *names[3] = {Text_Get(0x17c), Player_Name(0), Text_Get(m->text)};
    Fe_TitleRow(w * 0x18 / 0x280, h * 0x26 / 0x1e0, names, 3);
}

/* the car and gear of a record's word */
static void car_gear(uint32_t w, int y, int sw, int xcar, int xgear)
{
    Font_Print(1, Text_Get((int)(w & 0x3f) + 0x98), sw * xcar / 0x280, y, gm(0x524968), 0x12);
    Font_Print(1, exe_str(w & 0x40 ? 0x524db8 : 0x524db4), sw * xgear / 0x280, y, gm(0x524968), 0x12);
}

/* no record: 0x171 in the middle */
static void none(int y, int w) { Font_Print(1, Text_Get(0x171), w / 2, y, gm(0x524968), 0x12); }
static void code(const char *c, int x, int y, int w) { Font_Print(0, c, w * x / 0x280, y, gm(0x524968), 0x12); }

static void dchamp_row(int i, int y, int w)
{
    (void)i;
    uint32_t r = Player_Records(0), e = r + (uint32_t)difficulty * 0x10, wd = gm32(e), p = gm32(e + 4);
    if (!(wd & 0x80)) return none(y, w);
    car_gear(wd, y, w, 0xb4, 0x136);
    number(3, (int)(p & 0xf) + 1, w * 0x172 / 0x280, y, 0x524968);
    number(3, (int)(p >> 6 & 0xff), w * 0x1b3 / 0x280, y, 0x524968);
    code(codes_champ[difficulty], 0x208, y, w);
}

/* 0x4e3a80: Rally - Championship */
void fe_4e3a80(Menu *m)
{
    Fe_Header();
    driver_titles(m);
    static const int cols[5] = {0xb4, 0x136, 0x172, 0x1b3, 0x208}, heads[5] = {0x16b, 0x16c, 0x16d, 0x16e, 0x170};
    table(100, 200, cols, heads, 5, 1, dchamp_row);
    Fe_Hint(Text_Get(0x16f), true);
}

static void dsingle_row(int i, int y, int w)
{
    (void)i;
    uint32_t r = Player_Records(0), e = r + 0x30 + (uint32_t)((shown->sel * 3 + difficulty) * 0xc), wd = gm32(e);
    if (!(wd & 0x80)) return none(y, w);
    car_gear(wd, y, w, 0xb4, 0x136);
    number(3, (int)(gm32(e + 4) & 0xf) + 1, w * 0x172 / 0x280, y, 0x524968);
    time_cs(gm32(e + 8), w * 0x1b3 / 0x280, y);
    code(codes_rally[shown->sel][difficulty], 0x212, y, w);
}

/* 0x4e48b0: Rally - Single Rally */
void fe_4e48b0(Menu *m)
{
    shown = m;
    Fe_Header();
    driver_titles(m);
    static const int cols[5] = {0xb4, 0x136, 0x172, 0x1b3, 0x212}, heads[5] = {0x16b, 0x16c, 0x16d, 0x174, 0x170};
    table(100, 200, cols, heads, 5, 1, dsingle_row);
    Strip_Draw(Fe_Scroll(6), true);
    Fe_Hint(Text_Get(0x16f), true);
}

static void dstage_row(int i, int y, int w)
{
    uint32_t r = Player_Records(0), e = r + 0x150 + (uint32_t)((i + shown->sel * 0xc) * 8), wd = gm32(e);
    number(3, i + 1, w * 100 / 0x280, y, 0x52496c);
    if (!(wd & 0x80)) return none(y, w);
    car_gear(wd, y, w, 0xe6, 0x163);
    time_cs(gm32(e + 4), w * 0x19f / 0x280, y);
    code(codes_stage[shown->sel * 0xb + i], 0x208, y, w);
}

/* 0x4e5630: Rally - Stage Times */
void fe_4e5630(Menu *m)
{
    shown = m;
    Fe_Header();
    driver_titles(m);
    static const int cols[5] = {100, 0xe6, 0x163, 0x19f, 0x208}, heads[5] = {0x17d, 0x16b, 0x16c, 0x174, 0x170};
    table(-1, 0x5a, cols, heads, 5, (m->sel % 2 != 0) + 10, dstage_row);
    Strip_Draw(Fe_Scroll(7), true);
    Fe_Hint(Text_Get(0x172), true);
}

static void darcade_row(int i, int y, int w)
{
    (void)i;
    uint32_t r = Player_Records(0), k = (uint32_t)((shown->sel * 3 + difficulty) * 0xc), wd = gm32(r + 0x450 + k), p = gm32(r + 0x454 + k);
    if (!(wd & 0x80)) return none(y, w);
    car_gear(wd, y, w, 0xbe, 0x14a);
    number(3, (int)(p >> 5 & 0x3f), w * 0x1ae / 0x280, y, 0x524968);
    number(3, (int)(p & 7) + 1, w * 0x17c / 0x280, y, 0x524968);
    code(codes_arcade[shown->sel][difficulty], 0x1fe, y, w);
}

/* 0x4e63d0: Arcade - Championship */
void fe_4e63d0(Menu *m)
{
    shown = m;
    Fe_Header();
    driver_titles(m);
    static const int cols[5] = {0xbe, 0x14a, 0x1ae, 0x17c, 0x1fe}, heads[5] = {0x16b, 0x16c, 0x16e, 0x16d, 0x170};
    table(100, 200, cols, heads, 5, 1, darcade_row);
    Strip_Draw(Fe_Scroll(8), true);
    Fe_Hint(Text_Get(0x16f), true);
}

static void dlap_row(int i, int y, int w)
{
    static const int order[8] = {6, 3, 1, 4, 0, 2, 5, 7};
    uint32_t r = Player_Records(0), k = (uint32_t)(i / 3 * 3 + i % 3), wd = gm32(r + 0x4bc + k * 8);
    Font_Print(1, Text_Get(order[i] + 0x27), w * 100 / 0x280, y, gm(0x52496c), 0x12);
    if (!(wd & 0x80)) return none(y, w);
    car_gear(wd, y, w, 0x109, 0x181);
    time_cs(gm32(r + 0x4c0 + k * 8), w * 0x1b8 / 0x280, y);
    code(codes_lap[i], 0x21c, y, w);
}

/* 0x4e7120: Arcade - Lap Times */
void fe_4e7120(Menu *m)
{
    Fe_Header();
    driver_titles(m);
    static const int cols[5] = {100, 0x109, 0x181, 0x1b8, 0x21c}, heads[5] = {0x173, 0x16b, 0x16c, 0x174, 0x170};
    table(-1, 0xa0, cols, heads, 5, 8, dlap_row);
    Fe_Hint(Text_Get(0x172), true);
}
