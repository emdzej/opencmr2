/* The language page and the main menu's callbacks (0x4ef270-0x4ef420, 0x4f25d0, 0x4f3400, 0x4e2da0,
   0x4d4ba0). */
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
#include "text.h"
#include <string.h>

static uint32_t idle_since;   /* DAT_0081904c */

static void screen(int *w, int *h) { D3d_ScreenSize(w, h); }

/* 0x4ef190: every scroller's widths again (after a language change) */
static void remeasure_all(void)
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
    remeasure_all();
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
