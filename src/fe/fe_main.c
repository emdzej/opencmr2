/* The language page and the main menu's callbacks (0x4ef270-0x4ef420, 0x4f25d0, 0x4f3400, 0x4e2da0,
   0x4d4ba0). */
#include "audio/music.h"
#include "exe.h"
#include "fe/fe.h"
#include "fe/fe_page.h"
#include "fe/fe_res.h"
#include "fe/fe_strip.h"
#include "fe/pages.h"
#include "game/input.h"
#include "game/mem.h"
#include "game/select.h"
#include "pending.h"
#include "render/d3d7.h"
#include "render/twod.h"
#include "font.h"
#include "text.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t idle_since;   /* DAT_0081904c */

static void screen(int *w, int *h) { D3d_ScreenSize(w, h); }

/* 0x4ef190: every scroller's widths again (after a language change) */
void Fe_ScrollWidths(void)
{
    for (int k = 0; k < 12; k++) {
        FeScroll *s = Fe_Scroll(k);
        Menu *m = s->page;
        int cur = s->cur, prev = s->prev;
        uint32_t t = s->t;
        if (m) Scroll_Measure(s, m);
        else {
            int w, h;
            screen(&w, &h);
            s->gap = w * 0x18 / 0x280;
            s->off = s->off0 = 0;
        }
        s->cur = cur, s->prev = prev, s->t = t;
    }
}

/* 0x4f3400: the language page enters at the current language */
void fe_4f3400(Menu *m, bool back)
{
    (void)back;
    pg_8221d8.sel = (int8_t)(gm32(0x52afb4) & 7);
    Scroll_Measure(Fe_Scroll(3), &pg_8221d8);
    Fe_Scroll(3)->page = m;
}

void fe_4f39e0(Menu *m) { (void)m; Scroll_Update(Fe_Scroll(3)); }

/* 0x4e2da0: the strip and the language's flag in dots (the pictures at 0x52401c, 0xd8 bytes each,
   picked by 0x524008) */
void fe_4e2da0(Menu *m)
{
    Strip_Draw(Fe_Scroll(3), true);
    int w, h;
    screen(&w, &h);
    int cell = w * 0x12 / 0x280;
    int x = ((cell - w * 0xb / 0x280) / 2 - cell * 0x12 / 2) + w / 2;
    uint32_t pic = gm32(0x524008 + 4u * (uint32_t)m->sel);
    Fe_DotPicture((int16_t)x, (int16_t)(h * 100 / 0x1e0), gm(0x52401c + pic * 0xd8));
}

/* 0x4ef270: a language chosen: the mode word's language, its text and credits, the difficulty names,
   the strips measured again; from now on the page belongs to the options */
void fe_4ef270(Menu *m, bool back)
{
    if (back) return;
    gw32(0x52afb4, (gm32(0x52afb4) & ~7u) | ((uint32_t)m->sel & 7));   /* 0x4ea8a0 */
    Text_Select(m->sel);
    Fe_LoadCredits();
    gw32(0x52afb8, (gm32(0x52afb8) & 0x7fffffffu) | (uint32_t)((gm32(0x52afb4) & 7) == 0) << 31);   /* 0x405ec0 */
    Fe_DifficultyNames();
    Fe_ScrollWidths();
    if (!m->parent) Fe_BuildControllers();
    Menu_SetParent(m, &pg_81f118);
    for (int i = 0; i < m->n; i++) Menu_SetTarget(m, (int16_t)i, &pg_81f118);
}

/* 0x4ef300: the main menu's strip at rest, the idle clock started, out of the network mode */
void fe_4ef300(Menu *m, bool back)
{
    (void)back;
    idle_since = Fe_Now();
    Fe_LettersInit();   /* 0x4f3dd0 */
    Scroll_Measure(Fe_Scroll(0), m);
    gw32(0x52afb4, gm32(0x52afb4) & ~0x80000u);
}

/* 0x4ef420: the slide, the letters, and after 30.5 s without a key the demo (page 0x8214b8) */
void fe_4ef420(Menu *m)
{
    (void)m;
    Scroll_Update(Fe_Scroll(0));
    Fe_LettersUpdate();   /* 0x4f4050 */
    uint32_t now = Fe_Now();
    uint32_t held = 0;
    for (int i = 0; i < 8; i++) held |= Input_Record(i)->held;   /* 0x49e940 */
    if (held) idle_since = now;
    if ((int32_t)(now - idle_since) > 0x7724) {
        gw8(0x52ea52, 1);   /* 0x406330 */
        Menu_Goto(&pg_8214b8);
    }
}

/* 0x4d4350: an item's text when selectable, in the selection's colour, the enabled's or the rest's */
static void plain_item(const char *t, int x, int y, uint32_t flags, int i, const Menu *m)
{
    uint8_t f = m->items[i].flags;
    if (!(f & MIF_SELECTABLE)) return;
    Font_Print(1, t, x, y, gm(m->sel == i ? 0x524968 : (f & MIF_ENABLED) ? 0x52496c : 0x524970), flags);
}

/* 0x4d43e0: the plain page (0x81fe38): the header, the title centred at the top and the items' texts
   under it, 20 lines apart from 100 (in pixels, not scaled to the screen) */
void fe_4d43e0(Menu *m)
{
    int w, h;
    screen(&w, &h);
    Fe_Header();
    Font_Print(1, Text_Get(m->text), w / 2, 10, gm(0x524968), 10);
    for (int i = 0; i < m->n; i++) plain_item(Text_Get(m->items[i].text), w / 2, (i * 5 + 0x19) * 4, 10, i, m);
}

/* 0x4d4ba0 */
void fe_4d4ba0(Menu *m)
{
    int w, h;
    screen(&w, &h);
    Fe_Header();
    Fe_Titles(m, w * 0x18 / 0x280, h * 0x26 / 0x1e0, true, -1, NULL, -1);
    Strip_DrawMain(m, true, NULL);
    Fe_LettersDraw();
    if (m->items[m->sel].id == 1) Fe_LettersTrail();
    if (m->items[m->sel].id == 2) Fe_LettersTrails();
}

/* 0x4f25d0: the strip's place kept; drivers (item 0) sets mode 2 and the driver flag; the selection
   and the cleared player records (0x4d05f0) */
void fe_4f25d0(Menu *m, bool back)
{
    (void)back;
    Fe_SetScrollPrev(m->sel);
    if (m->items[m->sel].id == 0) {
        gw8(0x818848, 1);
        Fe_SetMode(2);
    } else
        gw8(0x818848, 0);
    Sel_Reset();
    for (uint32_t a = 0x817784; a < 0x817c84; a += 0x100) gw8(a, 0);   /* 0x4d05f0 */
    gw32(0x817780, 0xffffffffu);
}

/* ---- the Quit page (0x823df8): yes to 0x820798 (whose frame leaves the game, 0x4ef5e0), no back ---- */

static bool quit_no;   /* DAT_00819878 */

/* 0x4e2ab0: the trail, "Are you sure?" over the rows, the main menu's strip unlit */
void fe_4e2ab0(Menu *m)
{
    int w, h;
    screen(&w, &h);
    Fe_Header();
    Fe_Titles(m, w * 0x18 / 0x280, h * 0x26 / 0x1e0, true, 1, NULL, -1);
    List_Draw(m, Text_Get(0x89), -1, -1, 0, true);
    Strip_DrawMain(&pg_81d6d8, false, NULL);   /* 0x4f8410 */
}

/* 0x4f3b00: "No" first */
void fe_4f3b00(Menu *m, bool back)
{
    (void)back;
    quit_no = false;
    m->sel = 1;
}

/* 0x4f3b20: no */
void fe_4f3b20(Menu *m, MenuItem *it)
{
    (void)m, (void)it;
    quit_no = true;
}

/* 0x4f3b30: left other than by no or back (the main menu's strip moved on): to the quit */
void fe_4f3b30(Menu *m, bool back)
{
    (void)m;
    if (!quit_no && !back) Menu_Goto(&pg_820798);   /* 0x4f8990 */
}

/* 0x4ef5e0: the front end left for nothing: the quit */
void fe_4ef5e0(Menu *m)
{
    (void)m;
    Fe_Leave(false, false, false);
}

/* ---- the Options page (0x81f118) ---- */

/* 0x4e2590: the trail, the rows, the main menu's strip unlit */
void fe_4e2590(Menu *m)
{
    int w, h;
    screen(&w, &h);
    Fe_Header();
    Fe_Titles(m, w * 0x18 / 0x280, h * 0x26 / 0x1e0, true, 1, NULL, -1);
    List_Draw(m, NULL, -1, -1, 0, true);
    Strip_DrawMain(&pg_81d6d8, false, NULL);   /* 0x4f8410 */
}

/* 0x4f29b0: the Cheats row (1000) on when a cheat has been found (0x52afc0's bits 0-7, by 0x406360)
   or one of the cheat options 4-11 (0x52e890, by 0x406410) is on */
void fe_4f29b0(Menu *m, bool back)
{
    (void)back;
    Fe_SetPlayer(0);
    bool any = gm8(0x52afc0) != 0 || (gm32(0x52e890) & 0xff0) != 0;
    MenuItem *it = Menu_Item(m, 1000);
    if (any) it->flags |= MIF_ENABLED | MIF_SELECTABLE;
    else it->flags &= (uint8_t)~(MIF_ENABLED | MIF_SELECTABLE);
}

/* ---- the Sound page (0x81fa78): the music, effects and speech volumes (0x52afb8 bits 0-6, 7-13,
   14-20) in tens ---- */

static uint32_t vols_kept[3];   /* DAT_0081903c, DAT_00819860, DAT_00819754 */

static uint32_t vol(int shift) { return gm32(0x52afb8) >> shift & 0x7f; }   /* 0x405e40, 0x405e70, 0x405ea0 */

/* 0x405e10, 0x405e50, 0x405e80 (only the music's sets the player's volume) */
static void set_vol(int shift, uint32_t v)
{
    gw32(0x52afb8, (gm32(0x52afb8) & ~(0x7fu << shift)) | (v & 0x7f) << shift);
    if (shift == 0) Music_SetVolume((int)(v & 0x7f));
}

/* the three set, and the menus' sounds at the effects' volume (0..0x4000) */
static void set_vols(uint32_t music, uint32_t fx, uint32_t speech)
{
    set_vol(0, music);
    set_vol(7, fx);
    set_vol(14, speech);
    Menu_SetVolume((int32_t)(vol(7) << 0x10) / 100 / 4);
}

/* 0x4f2840: the volumes kept for a cancel, the rows from them */
void fe_4f2840(Menu *m, bool back)
{
    (void)back;
    for (int i = 0; i < 3; i++) {
        vols_kept[i] = vol(7 * i);
        m->items[i].value = (uint8_t)(vol(7 * i) / 10);
    }
}

/* 0x4f2b00: the rows into the volumes */
void fe_4f2b00(Menu *m)
{
    set_vols(m->items[0].value * 10u, m->items[1].value * 10u, m->items[2].value * 10u);
}

/* 0x4f2b70: back puts the kept volumes back; done keeps the rows' */
void fe_4f2b70(Menu *m, bool back)
{
    if (back) set_vols(vols_kept[0], vols_kept[1], vols_kept[2]);
    else fe_4f2b00(m);
}

/* the Sound page's labels: the three volumes "name n", Done (4) plain */
static void sound_row(Menu *m, MenuItem *it, bool two, int x, int y, uint32_t c, int w)
{
    (void)m, (void)two, (void)w;
    char b[256];
    if (it->id < 0 || it->id == 3) return;
    if (it->id < 3) snprintf(b, sizeof b, exe_str(0x516098), Text_Get(it->text), it->value * 10);
    else if (it->id == 4) snprintf(b, sizeof b, "%s", Text_Get(it->text));
    else return;
    Font_Print(1, b, x, y, gm(c), 0x11);
}

/* 0x4e2610: the trail; the rows (centred a little higher than the option pages', between 8+38 and
   384 of 480); the main menu's strip unlit */
void fe_4e2610(Menu *m)
{
    int w, h;
    screen(&w, &h);
    int top = (h * 8 / 0x1e0 + h * 0x26 / 0x1e0 + h * 0x180 / 0x1e0) / 2 - h * 0x24 / 0x1e0 * m->n / 2;
    Fe_Header();
    Fe_Titles(m, w * 0x18 / 0x280, h * 0x26 / 0x1e0, true, 2, NULL, -1);
    Rows_Draw(m, sound_row, false, false, top, h * 2 / 0x1e0 + h * 0x12 / 0x1e0);
    Strip_DrawMain(&pg_81d6d8, false, NULL);
}

/* ---- the Cheats page (0x81be78): the eight cheats found so far (0x52afc0 byte 0) turned on or off
   (byte 1; byte 3 keeps them for a cancel) ---- */

/* 0x4f28c0: the old settings kept (0x4eab80); each row on when found (0x406360) or its cheat option
   on (the even rows 4-7, the odd 8-11, by 0x406410), and its value whether it is on (0x4063d0) */
void fe_4f28c0(Menu *m, bool back)
{
    (void)back;
    uint32_t c = gm32(0x52afc0);
    gw32(0x52afc0, (c & 0x00ffffff) | (c >> 8 & 0xff) << 24);
    for (int i = 0; i < 8; i++) {
        MenuItem *it = &m->items[i];
        it->flags = (uint8_t)((it->flags & ~MIF_ENABLED) | MIF_SELECTABLE);
        it->value = 0;
        int opt = i / 2 + (i % 2 ? 8 : 4);
        if ((gm8(0x52afc0) >> i & 1) || (gm32(0x52e890) >> opt & 1)) {
            it->flags |= MIF_ENABLED;
            if (gm8(0x52afc1) >> i & 1) it->value = 1;
        }
    }
}

/* 0x4f2970: back puts the kept settings back (0x4eaba0); else the rows' values in (0x4eab40) */
void fe_4f2970(Menu *m, bool back)
{
    uint32_t c = gm32(0x52afc0);
    if (back) {
        gw32(0x52afc0, (c & 0xffff00ff) | (c >> 24) << 8);
        return;
    }
    for (int i = 0; i < 8; i++) {
        uint32_t bit = (1u << i) << 8;
        c = m->items[i].value ? c | bit : c & ~bit;
    }
    gw32(0x52afc0, c);
}

/* 0x4f3a50: a row chosen: back to the parent (keeping the settings) */
void fe_4f3a50(Menu *m, MenuItem *it)
{
    (void)it;
    Menu_Goto(m->parent);
}

/* 0x4ded80: the selectable rows (all eight) centred as the Sound page's; a row not found shows the
   unknown mark (0x524dcc) in the third colour, else its name and Off / On (the value's lit) past the
   widest name; the lines between unlit, lit about the chosen, the third colour between two not found */
void fe_4ded80(Menu *m)
{
    int w, h;
    screen(&w, &h);
    int widest = 0;
    for (int i = 0; i < 8; i++) {
        int tw = Font_TextWidth(1, Text_Get(i + 0x149));
        if (widest < tw) widest = tw;
    }
    Fe_Header();
    Fe_Titles(m, w * 0x18 / 0x280, h * 0x26 / 0x1e0, true, 2, NULL, -1);
    int n = 0;
    for (int i = 0; i < m->n; i++) n += m->items[i].flags >> 1 & 1;
    int x = w * 0x7a / 0x280;
    int row = h * 0x24 / 0x1e0;
    int16_t top = (int16_t)((h * 8 / 0x1e0 + h * 0x26 / 0x1e0 + h * 0x180 / 0x1e0) / 2 - row * n / 2);
    uint32_t lo, hi;
    if (m->sel == 0) lo = 0x524968, hi = 0x524974;
    else if (!(m->items[0].flags & MIF_ENABLED)) lo = 0x524970, hi = 0x52497c;
    else lo = 0x52496c, hi = 0x524978;
    int16_t r[4] = {(int16_t)(w * 99 / 0x280), top, (int16_t)(w * 0x11a / 0x280), 1};
    Rect_Fill(r, gm(hi), 1);
    r[1]++;
    Rect_Fill(r, gm(lo), 1);
    Tex *a = g_fe.arrow_a;
    int16_t aw = a ? (int16_t)a->width : 0, ah = a ? (int16_t)a->height : 0;
    for (int i = 0; i < n; i++) {
        MenuItem *it = &m->items[i];
        bool on = m->sel == i, found = it->flags & MIF_ENABLED;
        Tex *t = on ? g_fe.arrow_a : g_fe.arrow_d;
        uint32_t c = on ? 0x524968 : found ? 0x52496c : 0x524970;
        if (t) {
            Rect16 src = {0, 0, t->width, t->height};
            Rect16 dst = {(int16_t)(w * 100 / 0x280), (int16_t)(h * 0x14 / 0x1e0 + top + row * i - ah / 2), aw, ah};
            Sprite_Add(&src, &dst, t, 1, 0, NULL, NULL, gm(c), 8);
        }
        int y = h * 0x18 / 0x1e0 + r[1];
        if (!found)
            Font_Print(1, exe_str(0x524dcc), x, y, gm(c), 0x11);
        else {
            Font_Print(1, Text_Get(it->text), x, y, gm(c), 0x11);
            const char *off = Text_Get(0x133);
            Font_Print(1, off, w * 0xf / 0x280 + widest + x, y, gm(it->value ? 0x52496c : 0x524968), 0x11);
            Font_Print(1, Text_Get(0x134), w * 0x19 / 0x280 + widest + Font_TextWidth(1, off) + x, y,
                       gm(it->value ? 0x524968 : 0x52496c), 0x11);
        }
        if (m->sel == i || m->sel == i + 1) lo = 0x524968, hi = 0x524974;
        else if (!found && !(m->items[i + 1].flags & MIF_ENABLED)) lo = 0x524970, hi = 0x52497c;
        else lo = 0x52496c, hi = 0x524978;
        r[1] = (int16_t)(row * (i + 1) + top);
        Rect_Fill(r, gm(hi), 1);
        r[1]++;
        Rect_Fill(r, gm(lo), 1);
    }
    Strip_DrawMain(&pg_81d6d8, false, NULL);
}

/* ---- the Credits page (0x822958): the credits file's pairs rolling up ---- */

static uint32_t credits_from;   /* DAT_008196e4 */
static int credits_base;        /* DAT_0081987c: always 0 */
static int *credits_y;          /* DAT_0081a72c */

/* 0x4f1bd0: the roll from the bottom again (DAT_00819864 set too: never read) */
void fe_4f1bd0(Menu *m, bool back)
{
    (void)m, (void)back;
    credits_base = 0;
    credits_from = Fe_Now();
}

/* 0x4dec30: each pair 40 of 480 apart, rising a pixel each 30 ms from under the screen; those within
   100 of it drawn either side of the middle; when all are past 10 above the top, again */
void fe_4dec30(Menu *m)
{
    int w, h;
    screen(&w, &h);
    const FeCredit *lines;
    int n = Fe_Credits(&lines);
    static int cap;
    if (n > cap) {
        free(credits_y);
        credits_y = calloc((size_t)n, sizeof *credits_y);
        cap = credits_y ? n : 0;
        if (!credits_y) return;
    }
    bool done = true;
    uint32_t now = Fe_Now();
    for (int i = 0; i < n; i++) {
        credits_y[i] = (int)((uint32_t)(h * 0x28 / 0x1e0 * i) - (now - credits_from) / 0x1e) + 1 + credits_base + h + Font_Height(2);
        if (credits_y[i] < h + 100 && credits_y[i] > -100) {
            Font_Print(2, lines[i].a, w / 2 - 10, (int16_t)credits_y[i], gm(0x524968), 0x14);
            Font_Print(2, lines[i].b, w / 2 + 10, (int16_t)credits_y[i], gm(0x524968), 0x11);
        }
        if (credits_y[i] > -10) done = false;
    }
    if (done) fe_4f1bd0(m, false);
}

/* ---- the opponents' level (0x81ed58; built, and nothing leads to it) ---- */

/* 0x4ef950: the row from the level (0x405dd0: 0x52afb4 bits 20-22) */
void fe_4ef950(Menu *m, bool back)
{
    (void)back;
    m->sel = (int8_t)(gm32(0x52afb4) >> 0x14 & 7);
}

/* 0x4ef960: the row as the level (0x4ea970) */
void fe_4ef960(Menu *m, MenuItem *it)
{
    (void)it;
    gw32(0x52afb4, ((uint32_t)m->sel & 7u) << 0x14 | (gm32(0x52afb4) & 0xff8fffffu));
}

/* 0x4d63e0: the trail, the rows, the main menu's strip unlit */
void fe_4d63e0(Menu *m)
{
    int w, h;
    screen(&w, &h);
    Fe_Header();
    Fe_Titles(m, w * 0x18 / 0x280, h * 0x26 / 0x1e0, true, -1, NULL, -1);
    List_Draw(m, NULL, -1, -1, 0, true);
    Strip_DrawMain(&pg_81d6d8, false, NULL);   /* 0x4f8410 */
}
