/* The country (0x81eb78), stage (0x81e3f8) and weather (0x81f4d8) pages, the stage maps (dmd) and the
   start (0x8214b8): 0x4d5ca0-0x4da630, 0x4ef590, 0x4efb50-0x4f0050, 0x4f3010-0x4f3120. */
#include "fe/fe.h"
#include "fe/fe_page.h"
#include "fe/fe_res.h"
#include "pending.h"
#include "fe/fe_strip.h"
#include "fe/pages.h"
#include "exe.h"
#include "font.h"
#include "game/gstate.h"
#include "game/mem.h"
#include "game/paths.h"
#include "game/profile.h"
#include "game/select.h"
#include "render/d3d7.h"
#include "render/twod.h"
#include "text.h"
#include "world/tod.h"
#include <stdio.h>
#include <string.h>

static const uint8_t *maps[9][11];   /* DAT_008185bc: the stage maps by rally (8 the challenge) */
static uint8_t open[11];             /* DAT_00819130: the stages open to choose, 0xff none */

static void screen(int *w, int *h) { D3d_ScreenSize(w, h); }
static uint32_t mode(void) { return Gs_Mode(); }
static bool option(int n) { return gm32(0x52e890) >> n & 1; }
static uint32_t rally(void) { return Tod_Rally(); }
static const uint8_t *C(uint32_t va) { return gm(va); }

/* 0x4d5ca0: <rally><nn>.dmd from the front end's Common.bfl, 10 or 11 a rally (the odd ones have a
   super special), and the challenge's eight cha<nn>.dmd. The original formats the challenge's name
   without its number argument; the port gives the number. */
void Fe_LoadDmd(void)
{
    static const uint32_t names[8] = {0x524cd8, 0x524cdc, 0x524ce0, 0x524cd0, 0x524cd4, 0x524ccc, 0x524cc8, 0x524cc4};
    static const int count[9] = {10, 11, 10, 11, 10, 11, 10, 11, 8};
    for (int r = 0; r < 9; r++)
        for (int s = 1; s <= count[r]; s++) {
            char p[32];
            if (r == 8) snprintf(p, sizeof p, exe_str(0x524c94), s);
            else snprintf(p, sizeof p, exe_str(0x524c88), exe_str(names[r]), s);
            maps[r][s - 1] = Bfl_FindFile(Fe_Common(), p, NULL, NULL, false);
        }
}

/* 0x4d5de0: a stage's map, 36 rows of 9 bytes, four 2-bit pixels each (the palette 0x524b90), as
   small dots at (60, 75) of 640 x 480 */
static void stage_map(int stage, bool challenge)
{
    Tex *t = g_fe.sm_matrix;
    const uint8_t *p = maps[challenge ? 8 : rally()][stage];
    if (!t || !p) return;
    int w, h;
    screen(&w, &h);
    Rect16 src = {0, 0, t->width, t->height}, d = {0, (int16_t)(h * 0x4b / 0x1e0), t->width, t->height};
    for (int row = 0; row < 0x24; row++) {
        d.x = (int16_t)(w * 0x3c / 0x280);
        for (int b = 0; b < 9; b++) {
            uint8_t v = *p++;
            for (int k = 0; k < 4; k++) {
                uint8_t px = (uint8_t)(v >> (2 * k) & 3);
                if (px) Sprite_Add(&src, &d, t, 4, 0, NULL, NULL, C(0x524b90 + 4u * px), 8);
                d.x = (int16_t)(d.x + w * 8 / 0x280);
            }
        }
        d.y = (int16_t)(d.y + h * 8 / 0x1e0);
    }
}

static int title_x(void) { int w, h; screen(&w, &h); return w * 0x18 / 0x280; }
static int title_y(void) { int w, h; screen(&w, &h); return h * 0x26 / 0x1e0; }
static bool rally_names(const char *names[2])
{
    uint32_t m = mode();
    if (m < 1 || m > 3) return false;
    names[0] = Text_Get(0xe7);
    names[1] = Text_Get(m == 1 ? 0xd : m == 2 ? 0xf : 0x10);
    return true;
}

/* 0x4f36e0: the rallies' strip (scroller 5), measured from the country names (0x27 on) */
void fe_4f36e0(Menu *m, bool back)
{
    (void)back;
    Scroll_Measure(Fe_Scroll(5), m);
}

/* 0x4efb50: the rally follows the selection */
void fe_4efb50(Menu *m)
{
    gw32(0x52f2ac, (gm32(0x52f2ac) & ~0x1fu) | ((uint32_t)m->sel & 0x1f));
    Scroll_Update(Fe_Scroll(5));
}

/* 0x4d9ad0: the trail, the strip, the country's flag in dots */
void fe_4d9ad0(Menu *m)
{
    const char *names[2] = {0};
    rally_names(names);
    Fe_Header();
    Fe_Titles(m, title_x(), title_y(), true, 3, names, 2);
    Strip_Draw(Fe_Scroll(5), true);
    int w, h;
    screen(&w, &h);
    int cell = w * 0x12 / 0x280;
    Fe_DotPicture((int16_t)(((cell - w * 0xb / 0x280) / 2 - cell * 0x12 / 2) + w / 2), (int16_t)(h * 100 / 0x1e0),
                  gm(0x52401c + (uint32_t)m->sel * 0xd8));
    Fe_Hint(Text_Get(0x57), true);
}

static void weather_page(bool shown);

/* 0x4efb70: a rally chosen; a single rally starts, else the stages open by the unlocks (1; to 4, 8
   or 10 as the difficulties' counts pass the rally; in single stage only the difficulty's), the super
   special by its bits (0x52b040) */
void fe_4efb70(Menu *m, MenuItem *it)
{
    (void)it;
    gw32(0x52f2ac, (gm32(0x52f2ac) & ~0x1fu) | ((uint32_t)m->sel & 0x1f));
    if (mode() == 2) weather_page(false);
    if (mode() == 1) {
        gw32(0x52f2ac, gm32(0x52f2ac) & 0xfffffc1fu);
        Sel_ModeFlags();
        Menu_Goto(&pg_8214b8);
        Fe_Stamp();
        return;
    }
    memset(open, 1, sizeof open);
    uint32_t u = gm32(0x52b03c), r = rally();
    if (!option(0xd)) {
        uint32_t k = 1;
        if (r + 1 <= (u >> 8 & 0xf)) k = 4;
        if (r + 1 <= (u >> 12 & 0xf)) k = 8;
        if (r + 1 <= (u >> 16 & 0xf) && (u & 1)) k = 10;
        if (k < 0xb) memset(open + k, 0, 0xb - k);
        if (mode() == 2) {
            uint32_t d = Gs_Mode2();
            if (d == 0) memset(open + 4, 0, 7);
            else if (d == 1) memset(open + 8, 0, 3);
        }
        if (r & 1) {
            uint32_t b = gm32(0x52b040), bit = 1u << (((r + 1) / 2 - 1) & 0x1f);
            open[10] = (bit & b & 0x1f) || ((b >> 5) & 0x1f & bit) || ((b >> 10) & 0x1f & bit) ? 1 : 0;
            pg_81e3f8.n = 0xb;
        } else {
            open[10] = 0xff;
            pg_81e3f8.n = 10;
        }
    } else if (r & 1)
        pg_81e3f8.n = 0xb;
    else {
        open[10] = 0xff;
        pg_81e3f8.n = 10;
    }
    if (pg_81e3f8.sel >= pg_81e3f8.n) pg_81e3f8.sel = (int8_t)(pg_81e3f8.n - 1);
    Menu_Goto(&pg_81e3f8);
}

/* 0x4f3010: the stages' strip (scroller 9), the open ones enabled */
void fe_4f3010(Menu *m, bool back)
{
    (void)back;
    for (int i = 0; i < 0xc; i++) {
        if (i < 11 && open[i] == 1) m->items[i].flags |= MIF_ENABLED;
        else m->items[i].flags &= (uint8_t)~MIF_ENABLED;
    }
    Scroll_Measure(Fe_Scroll(9), m);
}

void fe_4efdc0(Menu *m) { (void)m; Scroll_Update(Fe_Scroll(9)); }

static void print_time(char *b, size_t n, uint32_t cs) { snprintf(b, n, exe_str(0x516e3c), cs / 6000, (int)(cs / 100 % 60), cs % 100); }

/* 0x4d9c40: the strip, the trail, the rally's banner, the stage's map, its name, length and
   surfaces, the best time and its holder, and the players' own bests */
void fe_4d9c40(Menu *m)
{
    int w, h;
    screen(&w, &h);
    uint32_t r = rally();
    int s = m->sel;
    const char *names[2] = {0};
    if (mode() == 2 || mode() == 3) names[0] = Text_Get(0xe7), names[1] = Text_Get(mode() == 2 ? 0xf : 0x10);
    Strip_Draw(Fe_Scroll(9), true);
    Fe_Header();
    Fe_Titles(m, title_x(), title_y(), true, 3, names, 2);
    Tex *b = r < 8 ? g_fe.banner[r] : NULL;
    if (b) {
        Rect16 src = {0, 0, b->width, b->height}, d = {(int16_t)(w * 0x19f / 0x280), (int16_t)(h * 0x4b / 0x1e0), b->width, b->height};
        Sprite_Add(&src, &d, b, 1, 0, NULL, NULL, C(0x524968), 8);
    }
    stage_map(s, false);
    int x = w * 0x19f / 0x280, y = h * 0x4b / 0x1e0 + h * 0x50 / 0x1e0, line = h * 0x10 / 0x1e0;
    uint32_t idx = (uint32_t)s + r * 0xb;
    char t[160];
    snprintf(t, sizeof t, exe_str(0x524d38), Text_Get(0x18b), exe_str(gm32(0x5269d0 + 4 * idx)));
    Font_Print(1, t, x, y, C(0x52496c), 0x21);
    y += line;
    uint32_t info = 0x525d50 + idx * 0x24;
    if (!gm8(info + 1)) snprintf(t, sizeof t, exe_str(0x524d2c), Text_Get(0x18c), gm8(info));
    else snprintf(t, sizeof t, exe_str(0x524d20), Text_Get(0x18c), gm8(info), gm8(info + 1));
    strncat(t, exe_str(0x524d1c), sizeof t - strlen(t) - 1);
    Font_Print(1, t, x, y, C(0x52496c), 0x21);
    y += line;
    snprintf(t, sizeof t, exe_str(0x524d14), Text_Get(0x18d));
    Font_Print(1, t, x, y, C(0x52496c), 0x21);
    y += line;
    uint32_t nsurf = gm8(info + 2);
    if (nsurf > 1) nsurf = 2;
    for (uint32_t i = 0; i < nsurf; i++) {
        uint32_t e = info + 4 + 8 * i;
        snprintf(t, sizeof t, exe_str(0x524d0c), gm8(e + 4), Text_Get((int)gm32(e) + 0x18e));
        Font_Print(1, t, x, y, C(0x52496c), 0x21);
        y += line;
    }
    /* the record (0x52b044 + 0x654: the holder, + 0x658: the time in bits 7-22) */
    int ty = h * 0x4b / 0x1e0 + (h * 8 / 0x1e0) * 0x24;
    uint32_t rec = 0x52b044 + 0x654 + idx * 8;
    print_time(t, sizeof t, gm32(rec + 4) >> 7 & 0xffff);
    Font_Print(1, t, w * 0x3a / 0x280 + x, ty, C(0x52496c), 0x21);
    snprintf(t, sizeof t, "%s", (const char *)gm(rec));
    Font_Print(1, t, x, ty, C(0x52496c), 0x21);
    Font_Print(1, Text_Get(0x193), x, ty - line, C(0x52496c), 0x21);
    /* the players' own: those set up without a guest profile (0x4085a0 false) */
    int shown = 0;
    for (int p = 0; p < (int)(gm32(0x52afb4) >> 13 & 0xf); p++) {
        if (Player_HasProfile(p)) continue;
        shown++;
        uint32_t pw = Player_Word(p);
        if ((pw & 0x3c0000) == 0x3c0000) continue;   /* 0x408cb0: no record without a profile */
        uint32_t pr = 0x52fa8c + (pw >> 0x12 & 0xf) * 0x650 + 0x150 + ((uint32_t)s + r * 0xc) * 8;
        if (!(gm8(pr) & 0x80)) snprintf(t, sizeof t, "%s", exe_str(0x516e30));
        else print_time(t, sizeof t, gm32(pr + 4));
        Font_Print(1, t, w * 0x3a / 0x280 + x, ty - line * (shown + 2), C(0x52496c), 0x21);
        const char *nm = (const char *)gm(0x52fa18 + (pw >> 0x12 & 0xf) * 0x650);
        Font_Print(1, nm, x, ty - line * (shown + 2), C(0x52496c), 0x21);
    }
    if (shown > 0) Font_Print(1, Text_Get(0x194), x, ty - line * (shown + 3), C(0x52496c), 0x21);
    Fe_Hint(Text_Get(0x57), true);
}

/* 0x4dbd80 (the arcade's locations, 0x4dc710): as 0x4d9c40 for the location chosen: the banner of its
   rally (the order 6, 3, 1, 4, 0, 2, 5, 7), its map (the challenge's), name (0x5269b0), length and
   surfaces (0x525c30), the record (0x52b044 + 0x1210) and the players' own (their profiles' + 0x4bc);
   the index is the selection (the original's (s / 3) * 3 + s % 3) */
void Fe_ArcadeLocation(Menu *m)
{
    static const int order[8] = {6, 3, 1, 4, 0, 2, 5, 7};
    int w, h;
    screen(&w, &h);
    int s = m->sel;
    if (s < 0 || s > 7) return;
    Tex *b = g_fe.banner[order[s]];
    if (b) {
        Rect16 src = {0, 0, b->width, b->height}, d = {(int16_t)(w * 0x19f / 0x280), (int16_t)(h * 0x4b / 0x1e0), b->width, b->height};
        Sprite_Add(&src, &d, b, 1, 0, NULL, NULL, C(0x524968), 8);
    }
    stage_map(s, true);
    int x = w * 0x19f / 0x280, y = h * 0x4b / 0x1e0 + h * 0x50 / 0x1e0, line = h * 0x10 / 0x1e0;
    char t[160];
    snprintf(t, sizeof t, exe_str(0x524d38), Text_Get(0x18b), exe_str(gm32(0x5269b0 + 4u * (uint32_t)s)));
    Font_Print(1, t, x, y, C(0x52496c), 0x21);
    y += line;
    uint32_t info = 0x525c30 + (uint32_t)s * 0x24;
    if (!gm8(info + 1)) snprintf(t, sizeof t, exe_str(0x524d2c), Text_Get(0x18c), gm8(info));
    else snprintf(t, sizeof t, exe_str(0x524d20), Text_Get(0x18c), gm8(info), gm8(info + 1));
    strncat(t, exe_str(0x524d1c), sizeof t - strlen(t) - 1);
    Font_Print(1, t, x, y, C(0x52496c), 0x21);
    y += line;
    snprintf(t, sizeof t, exe_str(0x524d14), Text_Get(0x18d));
    Font_Print(1, t, x, y, C(0x52496c), 0x21);
    y += line;
    uint32_t nsurf = gm8(info + 2);
    if (nsurf > 1) nsurf = 2;
    for (uint32_t i = 0; i < nsurf; i++) {
        uint32_t e = info + 4 + 8 * i;
        snprintf(t, sizeof t, exe_str(0x524d0c), gm8(e + 4), Text_Get((int)gm32(e) + 0x18e));
        Font_Print(1, t, x, y, C(0x52496c), 0x21);
        y += line;
    }
    int ty = h * 0x4b / 0x1e0 + (h * 8 / 0x1e0) * 0x24;
    uint32_t rec = 0x52b044 + 0x1210 + (uint32_t)s * 8;   /* 0x405fe0 */
    print_time(t, sizeof t, gm32(rec + 4) >> 7 & 0xffff);
    Font_Print(1, t, w * 0x3a / 0x280 + x, ty, C(0x52496c), 0x21);
    snprintf(t, sizeof t, "%s", (const char *)gm(rec));
    Font_Print(1, t, x, ty, C(0x52496c), 0x21);
    Font_Print(1, Text_Get(0x193), x, ty - line, C(0x52496c), 0x21);
    int shown = 0;
    for (int p = 0; p < (int)(gm32(0x52afb4) >> 13 & 0xf); p++) {
        if (Player_HasProfile(p)) continue;
        shown++;
        uint32_t pw = Player_Word(p);
        if ((pw & 0x3c0000) == 0x3c0000) continue;   /* 0x408cb0: no record without a profile (the original reads at 0) */
        uint32_t pr = 0x52fa8c + (pw >> 0x12 & 0xf) * 0x650 + 0x4bc + (uint32_t)s * 8;
        if (!(gm8(pr) & 0x80)) snprintf(t, sizeof t, "%s", exe_str(0x516e30));
        else print_time(t, sizeof t, gm32(pr + 4));
        Font_Print(1, t, w * 0x3a / 0x280 + x, ty - line * (shown + 2), C(0x52496c), 0x21);
        const char *nm = (const char *)gm(0x52fa18 + (pw >> 0x12 & 0xf) * 0x650);
        Font_Print(1, nm, x, ty - line * (shown + 2), C(0x52496c), 0x21);
    }
    if (shown > 0) Font_Print(1, Text_Get(0x194), x, ty - line * (shown + 3), C(0x52496c), 0x21);
    Fe_Hint(Text_Get(0x57), true);
}

/* 0x4efe60: the weather page (0x81f4d8) from the rally's weathers (0x4081d0: the distinct ones of
   the rally's 30, 0x52ea74, their count 0x52eab8): "random" and up to three (with four or more the
   first, middle and last); none at night or on stage 11 (straight to the start then) */
/* 0x4081d0 */
int Fe_RallyWeathers(void)
{
    uint32_t src = 0x51627c + (gm32(0x52f2ac) & 0x1f) * 0x78;
    int n = 1;
    gw32(0x52ea74, gm32(src));
    for (int k = 1; k < 0x1e; k++) {
        int32_t v = gms32(src + 4u * (uint32_t)k), at = -1;
        for (int i = 0; i < n; i++)
            if (v <= gms32(0x52ea74 + 4u * (uint32_t)i)) { at = i; break; }
        if (at == -1) gw32(0x52ea74 + 4u * (uint32_t)n++, (uint32_t)v);
        else if (gms32(0x52ea74 + 4u * (uint32_t)at) != v) {
            if (at < 8)
                for (uint32_t a = 0x52ea94; a > 0x52ea74 + 4u * (uint32_t)at; a -= 4) gw32(a, gm32(a - 4));
            gw32(0x52ea74 + 4u * (uint32_t)at, (uint32_t)v);
            n++;
        }
    }
    gw32(0x52eab8, (uint32_t)n);
    return n;
}

static void weather_page(bool shown)
{
    int n = Fe_RallyWeathers();
    Menu *w = &pg_81f4d8;
    gw32(0x52f290, 0), gw32(0x52f0fc, 0);   /* 0x40df60 */
    w->items[0].text = 0x156;
    w->items[0].id = -1;
    w->items[0].flags = (uint8_t)((w->items[0].flags & ~MIF_ENABLED) | shown);
    if (Tod_StartTime() < 0x834 && Tod_Stage() != 10) {
        Menu_SoundsOn(shown);
        Menu_Goto(w);
        Menu_SoundsOn(true);
        uint32_t list = 0x52ea74;
        int pick[3], np;
        if (n < 4) {
            np = n;
            for (int i = 0; i < n; i++) pick[i] = i;
        } else {
            np = 3;
            pick[0] = 0, pick[1] = n / 2, pick[2] = n - 1;
        }
        w->n = (int8_t)(np + 1);
        for (int i = 0; i < np; i++) {
            int16_t v = (int16_t)gm16(list + 4u * (uint32_t)pick[i]);
            w->items[1 + i].text = (int16_t)(v + 0x157);
            w->items[1 + i].id = v;
            w->items[1 + i].flags = (uint8_t)((w->items[1 + i].flags & ~MIF_ENABLED) | shown);
        }
        return;
    }
    gw32(0x52f290, 1), gw32(0x52f0fc, Tod_StartTime() < 0x834 ? 0 : 2);
    w->n = 0;
    Menu_SoundsOn(false);
    Menu_Goto(&pg_8214b8);
    Menu_SoundsOn(true);
}

/* 0x4efde0: a stage chosen (open ones only): single stage to the weather, else the start */
void fe_4efde0(Menu *m, MenuItem *it)
{
    (void)it;
    gw32(0x52f2ac, (gm32(0x52f2ac) & 0xfffffc1fu) | ((uint32_t)m->sel & 0x1f) << 5);
    Sel_ModeFlags();
    if (open[m->sel] != 1) return;
    if (mode() == 2) {
        weather_page(true);
        return;
    }
    if (mode() == 6) Menu_Goto((gm32(0x52afb4) >> 13 & 0xf) == 2 ? &pg_828e60 : &pg_827aa0);   /* 0x4fa4c0, 0x4fa360 */
    else Menu_Goto(&pg_8214b8);
}

/* 0x4f3120: the weather strip's scroller (11), every item enabled */
void fe_4f3120(Menu *m, bool back)
{
    (void)back;
    for (int i = 0; i < m->n; i++) m->items[i].flags |= MIF_ENABLED;
    FeScroll *s = Fe_Scroll(11);
    int cur = s->cur, prev = s->prev;
    Scroll_Measure(s, m);
    s->cur = cur, s->prev = prev;
}

void fe_4efdd0(Menu *m) { (void)m; Scroll_Update(Fe_Scroll(11)); }

/* 0x4da630 */
void fe_4da630(Menu *m)
{
    const char *names[2] = {0};
    rally_names(names);
    Fe_Header();
    Fe_Titles(m, title_x(), title_y(), true, 3, names, 2);
    List_Draw(&pg_81f4d8, NULL, -1, -1, 0, true);
    Fe_Hint(Text_Get(0x57), true);
}

/* 0x4f0050: random (id -1) or a weather */
void fe_4f0050(Menu *m, MenuItem *it)
{
    (void)m;
    if (it->id == -1) gw32(0x52f290, 0), gw32(0x52f0fc, 0);
    else gw32(0x52f290, 1), gw32(0x52f0fc, (uint32_t)it->id);
    Menu_Goto(&pg_8214b8);
}

/* 0x4ef590: the start: the two-player arcade flag, then out of the front end, to the race (the demo
   to the stage set-up) */
void fe_4ef590(Menu *m)
{
    (void)m;
    bool two_arcade = (gm32(0x52afb4) >> 13 & 0xf) == 2 && mode() == 6;
    gw32(0x52f2b0, (gm32(0x52f2b0) & ~0x800u) | (two_arcade ? 0x800u : 0));   /* 0x40e330 */
    if (gm8(0x52ea52)) {
        Fe_DemoSetup();   /* 0x4e9e40 */
        Fe_Leave(false, false, true);
        return;
    }
    Fe_Leave(true, false, false);
}
