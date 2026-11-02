/* The car select (0x821e18) and the transmission (0x81cf58): 0x4d7c00-0x4d8480, 0x4f17d0-0x4f1b90. */
#include "fe/fe.h"
#include "fe/fe_page.h"
#include "fe/fe_res.h"
#include "fe/fe_strip.h"
#include "fe/pages.h"
#include "crt.h"
#include "font.h"
#include "game/gstate.h"
#include "game/mem.h"
#include "game/profile.h"
#include "game/select.h"
#include "render/d3d7.h"
#include "render/twod.h"
#include "text.h"
#include <stdio.h>
#include <string.h>

static uint8_t car_of[22];   /* DAT_008196e8: the strip's cars */

static void screen(int *w, int *h) { D3d_ScreenSize(w, h); }
static uint32_t mode(void) { return Gs_Mode(); }
static bool option(int n) { return gm32(0x52e890) >> n & 1; }   /* 0x406410 */

/* 0x408e30: the car is the player's to choose: the cheats' options open some (all with 12), else
   the profile's unlocked cars (+0x38) */
static bool car_open(int p, int car, bool cheats)
{
    if ((Player_Word(p) & 0x3c0000) == 0x3c0000) return false;
    if (cheats) {
        if (option(0xc)) return true;
        if (car == 7 && (option(0) || option(0x12))) return true;
        if (car == 0x10 && option(1)) return true;
        if (car == 0xf && option(2)) return true;
        if (car == 0x14 && option(3)) return true;
    }
    return gm32(0x52fa54 + (Player_Word(p) >> 0x12 & 0xf) * 0x650) >> (car & 0x1f) & 1;
}

/* 0x4086b0: the player's car (in mode 4 the challenge record's) */
static int player_car(int p)
{
    if (mode() == 4) return gm8(0x52f3e8 + 0xc4u * (uint32_t)p);
    return (int)(Player_Word(p) & 0x3f);
}

/* 0x408600 */
static void set_car(int p, int car)
{
    if (mode() == 4) {
        gw32(0x52f3e8 + 0xc4u * (uint32_t)p, (uint32_t)car & 0xff);
        gw8(0x52f3ec + 0xc4u * (uint32_t)p, gm8(0x52f3ec + 0xc4u * (uint32_t)p) | 2);
        return;
    }
    uint32_t w = Player_Word(p), prof = w >> 0x12 & 0xf;
    if (prof == 0xf) return;
    gw8(0x52fa00 + prof, 1);   /* the profile changed (a byte each) */
    gw32(0x531350 + 0x30u * (uint32_t)p, (w & ~0x3fu) | ((uint32_t)car & 0x3f));
    uint32_t a = 0x52fa5c + prof * 0x650;
    gw32(a, (gm32(a) & ~0x1fu) | ((uint32_t)car & 0x1f));
}

/* 0x4086f0: the automatic box (the profile's +0x40 bit 5; 1 without a profile) */
static uint8_t auto_box(int p)
{
    if (mode() == 4) return gm8(0x52f3ec + 0xc4u * (uint32_t)p) & 1;
    uint32_t prof = Player_Word(p) >> 0x12 & 0xf;
    return prof == 0xf ? 1 : (uint8_t)(gm32(0x52fa5c + prof * 0x650) >> 5 & 1);
}

/* 0x4eb0c0 */
static void set_auto_box(int p, uint8_t v)
{
    if (mode() == 4) {
        uint32_t a = 0x52f3ec + 0xc4u * (uint32_t)p;
        gw8(a, (uint8_t)((gm8(a) & ~1) | (v & 1) | 2));
        return;
    }
    uint32_t prof = Player_Word(p) >> 0x12 & 0xf;
    if (prof == 0xf) return;
    gw8(0x52fa00 + prof, 1);   /* the profile changed (a byte each) */
    uint32_t a = 0x52fa5c + prof * 0x650;
    gw32(a, (gm32(a) & ~0x20u) | (uint32_t)(v & 1) << 5);
}

/* 0x4f17d0: the strip of the player's open cars, at the player's car */
void fe_4f17d0(Menu *m, bool back)
{
    (void)back;
    int p = Fe_SetupPlayer();
    Fe_SetPlayer(p);
    FeScroll *s = Fe_Scroll(2);
    s->page = m;
    m->sel = 0;
    int k = 0;
    for (int car = 0; car < 0x16; car++)
        if (car_open(p, car, true)) {
            car_of[k] = (uint8_t)car;
            m->items[k].text = (int16_t)(car + 0x98);
            if (car == player_car(p)) m->sel = (int8_t)k;
            k++;
        }
    m->n = (int8_t)k;
    Scroll_Measure(s, m);
}

void fe_4f39d0(Menu *m) { (void)m; Scroll_Update(Fe_Scroll(2)); }
void fe_4f1b90(Menu *m, bool back) { (void)m, (void)back; Fe_SetPlayer(0); }
void fe_4f1a10(Menu *m, MenuItem *it) { (void)it; set_car(Fe_SetupPlayer(), car_of[m->sel]); }

/* 0x4d7c00: a class's power and handling bars (of 11) and its drive (0 four-wheel, 1 front, 2 rear);
   classes beyond 13 random */
static void class_stats(uint32_t cls, int *power, int *handling, int *drive)
{
    int32_t pw, hd, dr;
    switch (cls) {
    case 0: case 3: case 4: pw = 0x570a, hd = 1000, dr = 0; break;
    case 1: pw = 0x570a, hd = 0x400, dr = 0; break;
    case 2: pw = 0x5687, hd = 1000, dr = 0; break;
    case 5: pw = 0x5c28, hd = 1000, dr = 0; break;
    case 6: pw = 0x63d7, hd = 0x3a6, dr = 0; break;
    case 7: pw = 0x4a3d, hd = 0x3c7, dr = 2; break;
    case 8: pw = 0x3ae1, hd = 0x1bf, dr = 1; break;
    case 9: pw = 0x6666, hd = 0x345, dr = 0; break;
    case 10: pw = 0x570a, hd = 0x2dc, dr = 2; break;
    case 11: pw = 0x75c2, hd = 0x2fc, dr = 0; break;
    case 12: pw = 0x570a, hd = 0x34e, dr = 0; break;
    case 13: pw = 0x3ae1, hd = 0x1bf, dr = 2; break;
    default:
        pw = crt_rand() % 0xffff;
        hd = crt_rand() % 1000;
        dr = crt_rand() % 3;
    }
    int32_t f = (int32_t)(((int64_t)pw << 16) / 0x75c2);
    *power = (int32_t)(((int64_t)f * 0xb0000) >> 16) >> 16;
    *handling = hd * 0xb / 1024;
    *drive = dr;
}

/* 0x4d8330: n squares from x, the first v lit, 10 apart at 640; the label right of them; the x
   after */
static int bars(int x, int y, int n, int v, const char *label)
{
    int w, h;
    screen(&w, &h);
    int16_t r[4] = {(int16_t)x, (int16_t)y, (int16_t)(w * 8 / 0x280), (int16_t)(h * 8 / 0x1e0)};
    int32_t fx = x << 16, step = (int32_t)(((int64_t)(w << 16) * 0x400) >> 16);
    for (int i = 0; i < n; i++) {
        Rect_Fill(r, gm(i < v ? 0x524968 : 0x52496c), 1);
        fx += step;
        r[0] = (int16_t)(fx >> 16);
    }
    if (!label) return r[0];
    Font_Print(1, label, r[0], h * 0x18 / 0x1e0 + y, gm(0x52496c), 0x14);
    return r[0];
}

static void picture(Tex *t, int16_t x, int16_t y, int16_t hh)
{
    Rect16 src = {0, 0, t->width, hh}, dst = {(int16_t)(x - t->width / 2), (int16_t)(y - hh), t->width, hh};
    Sprite_Add(&src, &dst, t, 1, 0, NULL, NULL, gm(0x524968), 8);
}

/* 0x4d7db0: the trail, the strip, the car from the front and the back (232 rows at 640), its livery
   (the rectangles of 0x524a28 by the livery table 0x5249cc), the power and handling bars, the drive */
void fe_4d7db0(Menu *m)
{
    int w, h;
    screen(&w, &h);
    uint32_t rects = 0x524a28;   /* 0x524ad8 at 1,024 with the texture memory */
    Fe_Header();
    char who[64], name[16] = "";
    Fe_PlayerTitle(who, sizeof who);
    const char *pn = (const char *)gm(0x52fa18 + (Player_Word(Fe_SetupPlayer()) >> 0x12 & 0xf) * 0x650);
    if ((Player_Word(Fe_SetupPlayer()) >> 0x12 & 0xf) != 0xf) snprintf(name, sizeof name, "%s", pn);
    const char *names[2] = {who, name};
    Fe_Titles(m, w * 0x18 / 0x280, h * 0x26 / 0x1e0, true, 3, names, 2);
    Strip_Draw(Fe_Scroll(2), true);
    int16_t hh = 0xe8, base = (int16_t)(h * 0x147 / 0x1e0);
    int car = car_of[m->sel];
    if (g_fe.car_front[car]) picture(g_fe.car_front[car], (int16_t)(w * 0x8e / 0x280), base, hh);
    if (g_fe.car_back[car]) picture(g_fe.car_back[car], (int16_t)(w * 0x165 / 0x280), base, hh);
    uint32_t liv = gm32(0x5249cc + 4u * (uint32_t)car);
    Tex *lt = liv < 3 ? g_fe.livery[liv] : NULL;
    if (lt) {
        uint32_t r = rects + 8u * (uint32_t)car;
        Rect16 src = {gms16(r), gms16(r + 2), gms16(r + 4), gms16(r + 6)};
        Rect16 dst = {(int16_t)(w * 0x22c / 0x280 - src.w / 2), (int16_t)(h * 0xab / 0x1e0 - src.h / 2), src.w, src.h};
        Sprite_Add(&src, &dst, lt, 1, 0, NULL, NULL, gm(0x524968), 8);
    }
    int power, handling, drive;
    class_stats(gm32(0x516b40 + 4u * (uint32_t)car), &power, &handling, &drive);   /* 0x40ee90 */
    bars(w * 500 / 0x280, h * 0x10c / 0x1e0, 0xb, power, Text_Get(0x186));
    int x = bars(w * 500 / 0x280, h * 300 / 0x1e0, 0xb, handling, Text_Get(0x187));
    char b[128];
    snprintf(b, sizeof b, "%s", Text_Get(drive + 0x183));
    Font_Print(1, b, x, h * 0xf5 / 0x1e0, gm(0x52496c), 0x14);
    Fe_Hint(Text_Get(0x57), true);
}

/* 0x4f1960: the box's item at the player's choice; back here (not in a challenge) the car strip
   measured again */
void fe_4f1960(Menu *m, bool back)
{
    m->items[0].value = auto_box(Fe_SetupPlayer());
    Fe_SetPlayer(Fe_SetupPlayer());
    if (back && mode() != 4) fe_4f17d0(Fe_Scroll(2)->page, false);
}

/* 0x4d8480: the trail (player, name), the transmission row with its two values (the chosen lit), the
   car strip unlit */
void fe_4d8480(Menu *m)
{
    int w, h;
    screen(&w, &h);
    Fe_Header();
    char who[64], name[16] = "";
    snprintf(who, sizeof who, Text_Get(0xdc), (int)(gm32(0x52afb4) >> 13 & 0xf) - Fe_ToSet());
    uint32_t prof = Player_Word(Fe_SetupPlayer()) >> 0x12 & 0xf;
    if (prof != 0xf) snprintf(name, sizeof name, "%s", (const char *)gm(0x52fa18 + prof * 0x650));
    const char *names[2] = {who, name};
    Fe_Titles(m, w * 0x18 / 0x280, h * 0x26 / 0x1e0, true, 3, names, 2);
    int16_t top = (int16_t)((h * 0x38 / 0x1e0 + h * 0x176 / 0x1e0) / 2 - (h * 0x24 / 0x1e0) * m->n / 2);
    int16_t r[4] = {(int16_t)(w * 99 / 0x280), top, (int16_t)(w * 0x11a / 0x280), 1};
    Rect_Fill(r, gm(0x524974), 1);
    r[1]++;
    Rect_Fill(r, gm(0x524968), 1);
    Tex *a = g_fe.arrow_a;
    if (a) {
        Rect16 src = {0, 0, a->width, a->height};
        Rect16 dst = {(int16_t)(w * 100 / 0x280), (int16_t)(h * 0x14 / 0x1e0 - a->height / 2 + top), a->width, a->height};
        Sprite_Add(&src, &dst, a, 1, 0, NULL, NULL, gm(0x524968), 8);
    }
    char prev[128];
    snprintf(prev, sizeof prev, "%s", Text_Get(m->items[0].text));
    int x = w * 0x7a / 0x280, y = h * 0x18 / 0x1e0 + r[1];
    Font_Print(1, prev, x, y, gm(0x524968), 0x11);
    for (int i = 0; i < m->items[0].count; i++) {
        x = w * 10 / 0x280 + x + Font_TextWidth(1, prev);
        Font_Print(1, Text_Get(i + 0x131), x, y, gm(m->items[0].value == i ? 0x524968 : 0x52496c), 0x11);
        snprintf(prev, sizeof prev, "%s", Text_Get(i + 0x131));
    }
    r[1] = (int16_t)(h * 0x24 / 0x1e0 + top);
    Rect_Fill(r, gm(0x524974), 1);
    r[1]++;
    Rect_Fill(r, gm(0x524968), 1);
    if (mode() != 4) Strip_Draw(Fe_Scroll(2), false);
    Fe_Hint(Text_Get(0x57), true);
}

/* 0x4f02e0: the rallies open to choose: the difficulty's count of the unlocks word (0x52b03c bits
   8-11, 12-15, 16-19; time trial and single stage the most of the three; 8 with the cheat option 13),
   and in single stage and time trial the first four anyway; the country and stage pages at the
   selection kept in the mode word (bits 23-26, 27-30) */
static void open_rallies(void)
{
    uint32_t u = gm32(0x52b03c), n;
    if (option(0xd)) n = 8;
    else if (mode() == 3 || mode() == 2) {
        n = u >> 8 & 0xf;
        if (n < (u >> 12 & 0xf)) n = u >> 12 & 0xf;
        if (n < (u >> 16 & 0xf)) n = u >> 16 & 0xf;
    } else {
        uint32_t d = Gs_Mode2();
        n = d == 0 ? u >> 8 & 0xf : d == 1 ? u >> 12 & 0xf : d == 2 ? u >> 16 & 0xf : 0;
    }
    for (uint32_t i = 0; i < 8; i++) {
        uint8_t *f = &pg_81eb78.items[i].flags;
        if (i < n || (mode() != 1 && i < 4)) *f |= MIF_ENABLED;
        else *f &= (uint8_t)~MIF_ENABLED;
    }
    pg_81eb78.sel = (int8_t)(gm32(0x52afb4) >> 0x17 & 0xf);
    pg_81e3f8.sel = (int8_t)(gm32(0x52afb4) >> 0x1b & 0xf);
}

/* 0x4f1a40: the box chosen; with every player set, on to the mode's next page (a championship
   straight to its start 0x8214b8, a single rally, stage or time trial the country select, a
   challenge 0x823858), else the next player's driver select */
void fe_4f1a40(Menu *m, MenuItem *it)
{
    Fe_SetPlayer(0);
    set_auto_box(Fe_SetupPlayer(), m->items[0].value);
    if (Fe_ToSet() == 0) {
        switch (mode()) {
        case 0:
            Menu_Goto(&pg_8214b8);
            Fe_Stamp();
            return;
        case 1:
        case 2:
        case 3:
            open_rallies();
            Menu_SetParent(&pg_81eb78, m);
            Menu_Goto(&pg_81eb78);
            return;
        case 4:
            Menu_SetParent(&pg_823858, m);
            Menu_Goto(&pg_823858);
            return;
        }
        return;
    }
    Menu_SetParent(&pg_8241b8, m);
    if (mode() != 4) {
        Menu_Goto(&pg_8241b8);
        return;
    }
    fe_4f0ac0(m, it);
    Menu_Goto(&pg_824a38);
}

/* ---- the arcade's car select (0x826f60) and transmission (0x826d80) ---- */

/* 0x4faa50: the strip of the player's open cars in the arcade modes (5-7; none in the others), at the
   player's car; the slide at rest, but the selection and its time not reset (unlike 0x4f17d0) */
void fe_4faa50(Menu *m, bool back)
{
    (void)back;
    int p = Fe_SetupPlayer();
    Fe_SetPlayer(p);
    FeScroll *s = Fe_Scroll(2);
    s->page = m;
    m->sel = 0;
    int k = 0;
    for (int car = 0; car < 0x16; car++) {
        if (mode() != 7 && mode() != 5 && mode() != 6) continue;
        if (!car_open(p, car, true)) continue;
        car_of[k] = (uint8_t)car;
        m->items[k].text = (int16_t)(car + 0x98);
        if (car == player_car(p)) m->sel = (int8_t)k;
        k++;
    }
    m->n = (int8_t)k;
    int w, h;
    screen(&w, &h);
    s->gap = w * 0x18 / 0x280;
    s->n = m->n;
    s->off = s->off0 = 0;
    for (int i = 0; i < m->n; i++) {
        char b[256];
        snprintf(b, sizeof b, "%s", Text_Get(m->items[i].text));
        s->width[i] = Font_TextWidth(2, Text_Lower(b));
    }
}

/* 0x4d8950: the same code as 0x4d7db0 */
void fe_4d8950(Menu *m) { fe_4d7db0(m); }

/* 0x4facd0: the car chosen, the CPU cars by it (0x40dfa0); the item's page (the transmission) follows,
   as the menu takes it after this (the original's own goto, to 0x826ba0 or the driver select, is
   overridden) */
void fe_4facd0(Menu *m, MenuItem *it)
{
    (void)it;
    set_car(Fe_SetupPlayer(), car_of[m->sel]);
    Sel_CpuCars();
    if (Fe_ToSet() == 0) Menu_Goto(&pg_826ba0);
    else {
        Menu_SetParent(&pg_8241b8, m);
        Menu_Goto(&pg_8241b8);
    }
}

/* 0x4fac70: the box's item at the player's choice; back here the car strip measured again */
void fe_4fac70(Menu *m, bool back)
{
    m->items[0].value = auto_box(Fe_SetupPlayer());
    Fe_SetPlayer(Fe_SetupPlayer());
    if (back) fe_4faa50(Fe_Scroll(2)->page, false);
}

/* 0x4fad40: the box chosen: on to 0x826ba0 with every player set, else the next driver select */
void fe_4fad40(Menu *m, MenuItem *it)
{
    (void)it;
    set_auto_box(Fe_SetupPlayer(), m->items[0].value);
    Menu_Goto(Fe_ToSet() == 0 ? &pg_826ba0 : &pg_8241b8);
}

/* 0x4d8ed0: as 0x4d8480 for every item: a row each between lines (the lines by the selected row lit),
   its arrow (Ar_640A lit, Ar_640D not, placed by Ar_640A's size), and for the transmission item (0x88)
   its label and values (the chosen lit) */
void fe_4d8ed0(Menu *m)
{
    int w, h;
    screen(&w, &h);
    Fe_Header();
    char who[64], name[16] = "";
    snprintf(who, sizeof who, Text_Get(0xdc), (int)(gm32(0x52afb4) >> 13 & 0xf) - Fe_ToSet());
    uint32_t prof = Player_Word(Fe_SetupPlayer()) >> 0x12 & 0xf;
    if (prof != 0xf) snprintf(name, sizeof name, "%s", (const char *)gm(0x52fa18 + prof * 0x650));
    const char *names[2] = {who, name};
    Fe_Titles(m, w * 0x18 / 0x280, h * 0x26 / 0x1e0, true, 3, names, 2);
    int row = h * 0x24 / 0x1e0;
    int16_t top = (int16_t)((h * 0x38 / 0x1e0 + h * 0x176 / 0x1e0) / 2 - row * m->n / 2);
    int16_t r[4] = {(int16_t)(w * 99 / 0x280), top, (int16_t)(w * 0x11a / 0x280), 1};
    Rect_Fill(r, gm(m->sel == 0 ? 0x524974 : 0x524978), 1);
    r[1]++;
    Rect_Fill(r, gm(m->sel == 0 ? 0x524968 : 0x52496c), 1);
    Tex *a = g_fe.arrow_a;
    int16_t aw = a ? (int16_t)a->width : 0, ah = a ? (int16_t)a->height : 0;
    for (int i = 0; i < m->n; i++) {
        bool on = m->sel == i;
        Tex *t = on ? g_fe.arrow_a : g_fe.arrow_d;
        uint32_t c = on ? 0x524968 : 0x52496c;
        if (t) {
            Rect16 src = {0, 0, t->width, t->height};
            Rect16 dst = {(int16_t)(w * 100 / 0x280), (int16_t)(h * 0x14 / 0x1e0 + top + row * i - ah / 2), aw, ah};
            Sprite_Add(&src, &dst, t, 1, 0, NULL, NULL, gm(c), 8);
        }
        MenuItem *it = &m->items[i];
        if (it->id == 0x88) {
            char prev[128];
            snprintf(prev, sizeof prev, "%s", Text_Get(it->text));
            int x = w * 0x7a / 0x280, y = h * 0x18 / 0x1e0 + r[1];
            Font_Print(1, prev, x, y, gm(c), 0x11);
            for (int v = 0; v < it->count; v++) {
                x = w * 10 / 0x280 + x + Font_TextWidth(1, prev);
                Font_Print(1, Text_Get(v + 0x131), x, y, gm(it->value == v ? 0x524968 : 0x52496c), 0x11);
                snprintf(prev, sizeof prev, "%s", Text_Get(v + 0x131));
            }
        }
        bool lit = m->sel == i || m->sel == i + 1;
        r[1] = (int16_t)(row * (i + 1) + top);
        Rect_Fill(r, gm(lit ? 0x524974 : 0x524978), 1);
        r[1]++;
        Rect_Fill(r, gm(lit ? 0x524968 : 0x52496c), 1);
    }
    Strip_Draw(Fe_Scroll(2), false);
    Fe_Hint(Text_Get(0x57), true);
}

/* 0x4fad90 (mode 6, the menus back to the first player's input) and 0x4fae70 (mode 7): the car
   chosen, the CPU cars by it */
void fe_4fad90(Menu *m, MenuItem *it)
{
    (void)it;
    Fe_SetPlayer(0);
    set_car(Fe_SetupPlayer(), car_of[m->sel]);
    Sel_CpuCars();
}

void fe_4fae70(Menu *m, MenuItem *it)
{
    (void)it;
    set_car(Fe_SetupPlayer(), car_of[m->sel]);
    Sel_CpuCars();
}

/* 0x4fadd0 (mode 6): the box chosen; players left: the driver select (back to here for two); else
   with two players 0x827320 (back to here), with one 0x828220 (back to here, and 0x827320 back to
   0x828220) */
void fe_4fadd0(Menu *m, MenuItem *it)
{
    (void)it;
    set_auto_box(Fe_SetupPlayer(), m->items[0].value);
    bool two = (gm32(0x52afb4) >> 13 & 0xf) == 2;
    if (Fe_ToSet() != 0) {
        if (two) Menu_SetParent(&pg_8241b8, m);
        Menu_Goto(&pg_8241b8);
        return;
    }
    if (two) {
        Menu_SetParent(&pg_8241b8, m);
        Menu_SetParent(&pg_827320, m);
        Menu_Goto(&pg_827320);
        return;
    }
    Menu_SetParent(&pg_827320, &pg_828220);
    Menu_SetParent(&pg_828220, m);
    Menu_Goto(&pg_828220);
}

/* 0x4faea0 (mode 7): the box chosen; on to 0x826420 with every player set, else the driver select */
void fe_4faea0(Menu *m, MenuItem *it)
{
    (void)it;
    set_auto_box(Fe_SetupPlayer(), m->items[0].value);
    Menu_Goto(Fe_ToSet() == 0 ? &pg_826420 : &pg_8241b8);
}
