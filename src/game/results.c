/* The stage's results (see results.h), ported from 0x448e60-0x44b7b0 and 0x453c50. */
#include "game/results.h"
#include "fe/menu.h"
#include "exe.h"
#include "font.h"
#include "render/tex.h"
#include "game/gstate.h"
#include "game/input.h"
#include "game/mem.h"
#include "game/pause.h"
#include "game/race.h"
#include "game/hud.h"
#include "game/sectors.h"
#include "pending.h"
#include "render/d3d7.h"
#include "render/twod.h"
#include "sim/fade.h"
#include "text.h"
#include <stdio.h>
#include <string.h>

static Menu pg_title, pg_times, pg_splits, pg_board, pg_end;   /* 0x53ea68, 0x541218, 0x53f5b0, 0x53e4b8, 0x541400 */
static Menu *page, *next;          /* DAT_00541cc8, DAT_00541ccc */
static Menu *shown;                /* DAT_00541dfc */
static int settle;                 /* DAT_00541e00 */
static bool active;
static char holder[32];            /* DAT_00519ee0: the stage record's holder, lower case */
static char record_name[32];       /* DAT_00519edc: the holder after the update */

static void screen(int *w, int *h) { D3d_ScreenSize(w, h); }
static const uint8_t *C(uint32_t va) { return gm(va); }   /* 0x519ecc, 0x519ed0, 0x519ed4 */
static uint32_t mode(void) { return Gs_Mode(); }

/* 0x44b760: the screen dimmed */
static void dim(void)
{
    int w, h;
    screen(&w, &h);
    int16_t r[4] = {0, 0, (int16_t)w, (int16_t)h};
    Font_SetLayer(2);
    Rect_Fill(r, C(0x519ed4), 2);
}

/* 0x408400 */
static const char *player_name(int n)
{
    uint32_t p = gm32(0x531350 + 0x30u * (uint32_t)n) >> 0x12 & 0xf;
    return p == 0xf ? "" : (const char *)gm(0x52fa18 + p * 0x650);
}

/* a page's title at (30, 67) of 640 x 480 with a separator after it; the x after */
static int title(int16_t text)
{
    int w, h;
    screen(&w, &h);
    Font_Print(2, Text_Get(text), w * 0x1e / 0x280, h * 0x43 / 0x1e0, C(0x519ecc), 0x11);
    int x = w * 0x1e / 0x280 + Font_TextWidth(2, Text_Get(text)) + w * 8 / 0x280;
    int16_t r[4] = {(int16_t)x, (int16_t)(h * 0x25 / 0x1e0), 2, (int16_t)(h * 0x29 / 0x1e0)};
    Rect_Fill(r, C(0x519ecc), 2);
    return x + 2 + w * 8 / 0x280;
}

/* 0x44b7b0: the replay's title and the driver followed (the one-player rally's path; the
   championship's and the alternate players' names not ported) */
static void draw_title(Menu *m)
{
    (void)m;
    int w, h;
    screen(&w, &h);
    int x = title(0x47);
    if (mode() == 4 || Gs_SelBit26() || Gs_SelBit27()) PENDING_NOTE(0x44b7b0, "the results title's other modes");
    Font_Print(2, player_name(0), x, h * 0x43 / 0x1e0, C(0x519ecc), 0x11);
    Font_SetLayer(2);
}


/* 0x454df0: titles in a row in the big font, lower case, with separators */
static void titles(int x, int y, const char *const *t, int n)
{
    int w, h;
    screen(&w, &h);
    for (int i = 0; i < n; i++) {
        char b[128];
        snprintf(b, sizeof b, "%s", t[i]);
        Text_Lower(b);
        Font_Print(2, b, x, y, C(0x519ecc), 0x11);
        if (i == n - 1) break;
        x = w * 8 / 0x280 + x + Font_TextWidth(2, b);
        int16_t r[4] = {(int16_t)x, (int16_t)(y - h * 0x1e / 0x1e0), 2, (int16_t)(h * 0x29 / 0x1e0)};
        Rect_Fill(r, C(0x519ecc), 2);
        x = x + 2 + w * 8 / 0x280;
    }
}

static void print_time(char *b, size_t n, int32_t t) { snprintf(b, n, exe_str(0x519f58), t / 6000, t % 6000 / 100, t % 100); }   /* 0x4de170 */

/* 0x44bcd0's rally part: a column for the player and one for the stage record (or the second
   player): the driver, each split's time and the total */
static void draw_times(Menu *m)
{
    (void)m;
    int w, h;
    screen(&w, &h);
    dim();
    if (mode() >= 4 && mode() != 8 && mode() != 9 && mode() != 10) {
        PENDING_NOTE(0x44bcd0, "the results' times in the championship and arcade");
        return;
    }
    const char *t[1] = {Text_Get(0x48)};
    titles(w * 0x1e / 0x280, h * 0x43 / 0x1e0, t, 1);
    int cols = Gs_ModeBit19() ? 1 : 2, shown = Sectors_Shown();
    for (int col = 0; col < cols; col++) {
        bool record = Gs_SelBits14() == 1 && col == 1;
        uint32_t c = record ? 0x519ed0 : 0x519ecc;
        int x = w * 0x1e / 0x280 + (w * 0x166 / 0x280) * col, dy = 0, top = 0;
        if (shown == 2) top = -(h * 0x1e / 0x1e0), dy = h * 0xe6 / 0x1e0 - h * 0x91 / 0x1e0;
        int y0 = h * 0x91 / 0x1e0 + dy + top;
        char b[160];
        const char *name;
        if (record) {
            snprintf(b, sizeof b, "%s", Text_Get(0x6d));
            Text_Lower(b);
            Font_Print(1, b, x, y0, C(c), 0x11);
            name = holder;
        } else {
            Font_Print(1, Text_Get(0x89), x, y0, C(c), 0x11);
            name = player_name(Race_Base() + col);
        }
        snprintf(b, sizeof b, Text_Get(0x9b), name);
        Font_Print(1, b, x, h * 0x1e / 0x1e0 + y0, C(c), 0x11);
        Font_Print(1, Text_Get(0x8a), x, h * 0x19 / 0x1e0 + h * 0x91 / 0x1e0 + shown * h * 0x1e / 0x1e0 + dy, C(c), 0x11);
        int tx = w * 0xb1 / 0x280 + (w * 0x166 / 0x280) * col, i = 0;
        for (; i < shown; i++) {
            int32_t v = record ? gms32(0x817448 + 4u * (uint32_t)(i + 1)) - gms32(0x817448 + 4u * (uint32_t)i)
                               : Times_Split(col, i + 1) - Times_Split(col, i);
            print_time(b, sizeof b, v);
            Font_Print(1, b, tx, h * 0x91 / 0x1e0 + dy + (h * 0x1e / 0x1e0) * i, C(c), 0x11);
        }
        int32_t total = record ? gms32(0x817448 + 4u * (uint32_t)shown) : Times_Finish(col);
        snprintf(b, sizeof b, exe_str(0x519f58), total / 6000, total / 100 % 60, total % 100);
        Font_Print(1, b, tx, h * 0x19 / 0x1e0 + h * 0x91 / 0x1e0 + dy + (h * 0x1e / 0x1e0) * i, C(c), 0x11);
    }
    Font_SetLayer(2);
}

/* 0x44d790: "high score" and "congratulations", "<holder> you have set the fastest time for this
   stage" */
static void draw_record(Menu *m)
{
    (void)m;
    int w, h;
    screen(&w, &h);
    dim();
    if (mode() == 5 || mode() == 6) PENDING_NOTE(0x44d790, "the arcade's record page");
    const char *t[2] = {Text_Get(0x49), Text_Get(0x46)};
    titles(w * 0x1e / 0x280, h * 0xf2 / 0x1e0, t, 2);
    char b[200];
    snprintf(b, sizeof b, exe_str(0x519f68), record_name, Text_Get(0x4a));
    Text_ToFontCase(b);
    Font_Print(0, b, w * 0x1e / 0x280, h * 10 / 0x1e0 + h * 0xf2 / 0x1e0 + Font_Height(0), C(0x519ecc), 0x11);
    Font_SetLayer(2);
}

/* 0x4cfa10: a player's stage time into the stage's record when faster (the holder, the car, the box,
   the time in bits 7-22, the ten split times at +0x914 and 0x817448; the original reads the tenth from
   the next player's row); true if it was */
static bool stage_record(int p, int id, const char *name)
{
    uint32_t idx = gm32(0x8173fc) + gm32(0x8173f8) * 0xb, e = 0x52b044 + 0x654 + idx * 8;
    int32_t t = Times_Finish(p);   /* 0x4d0280: the champion tables' time */
    if (t < 0 || (int32_t)(gm32(e + 4) >> 7 & 0xffff) <= t) return false;
    snprintf((char *)gm(e), 4, "%s", name);
    uint32_t v = gm32(e + 4);
    uint32_t prof = gm32(0x531350 + 0x30u * (uint32_t)id) >> 0x12 & 0xf;
    uint32_t car = gm32(0x531350 + 0x30u * (uint32_t)id) & 0x3f, box = prof == 0xf ? 1 : gm32(0x52fa5c + prof * 0x650) >> 5 & 1;
    v = (v & 0xff80003fu) | ((uint32_t)t & 0xffff) << 7 | box << 6 | car;
    gw32(e + 4, v);
    for (int i = 0; i < 10; i++) {
        int32_t sp = Times_Split(p + i / 9, i % 9);
        gw16(0x52b044 + 0x914 + idx * 0x14 + 2u * (uint32_t)i, (uint16_t)sp);
        gw32(0x817448 + 4u * (uint32_t)i, (uint32_t)(uint16_t)sp);
    }
    gw8(0x817410, 1);
    return true;
}

/* the board page (0x44d960): the stage's standings against the computer drivers, not ported yet */
static void draw_pending(Menu *m)
{
    (void)m;
    PENDING_NOTE(0x44d790, "the results' splits and standings (0x44d790, 0x44d960)");
    dim();
    title(0x47);
    Font_SetLayer(2);
}

/* 0x453c50: the choices: each item's name (with the next player's for id 0x3ea) and its description,
   the pause menu's arrows */
static void draw_end(Menu *m)
{
    int w, h;
    screen(&w, &h);
    dim();
    Font_Print(2, Text_Get(0x56), w * 0x1e / 0x280, h * 0x43 / 0x1e0, C(0x519ecc), 0x11);
    Tex *on = Pause_ArrowOn(), *off = Pause_ArrowOff();
    for (int i = 0; i < m->n; i++) {
        MenuItem *it = &m->items[i];
        bool lit = m->sel == i;
        uint32_t c = lit ? 0x519ecc : 0x519ed0;
        int y = h * 0xde / 0x1e0 + (h * 0x36 / 0x1e0) * i;
        char b[160];
        if (it->id == 0x3ea) snprintf(b, sizeof b, exe_str(0x517dd8), Text_Get(it->text), player_name(Race_Base() + 1));
        else snprintf(b, sizeof b, "%s", Text_Get(it->text));
        Font_Print(1, b, w * 0x86 / 0x280, y, C(c), 0x11);
        Font_Print(0, Text_Get(it->text + 1), w * 0x86 / 0x280, h * 0xf / 0x1e0 + y, C(c), 0x11);
        Tex *t = lit ? on : off;
        if (t && on) {
            Rect16 src = {0, 0, t->width, t->height};
            Rect16 d = {(int16_t)(w * 0x70 / 0x280), (int16_t)(y - h * 0xd / 0x1e0), on->width, on->height};
            Sprite_Add(&src, &d, t, 2, 0, NULL, NULL, C(c), 8);
        }
    }
    Font_SetLayer(2);
}

/* the fades' ends (0x449ea0, 0x449ee0, 0x449020): the race to the results state with the flag */
static void restart_done(int ch) { (void)ch; Race_EventAll(1, 3); Pause_Ask(true, false, false); }
static void service_done(int ch) { (void)ch; Race_EventAll(1, 3); Pause_Ask(true, true, false); }
static void quit_done(int ch)
{
    (void)ch;
    Race_EventAll(1, 3);
    if (mode() == 4) gw32(0x52f2b0, gm32(0x52f2b0) | 0x800000);
    Pause_Ask(false, false, true);
}

static int32_t fade_colour(void)
{
    uint32_t m = mode();
    return (int32_t)(m == 8 || m == 1 || m == 0 ? gm32(0x519ec8) : gm32(0x541cd4));
}

/* 0x449f30, 0x44a0a0, 0x449b00: every car's fade, the first calling back */
static void restart_fn(Menu *m, MenuItem *it)
{
    (void)m, (void)it;
    for (int i = 0; i < Race_Cars(); i++) Fade_OutIn(i, i ? NULL : restart_done, 1, 0, fade_colour(), true);
}
static void service_fn(Menu *m, MenuItem *it)
{
    (void)m, (void)it;
    for (int i = 0; i < Race_Cars(); i++) Fade_OutIn(i, i ? NULL : service_done, 1, 0, (int32_t)gm32(0x519ec8), true);
}
static void quit_fn(Menu *m, MenuItem *it)
{
    (void)m, (void)it;
    for (int i = 0; i < Race_Cars(); i++) Fade_OutIn(i, i ? NULL : quit_done, 1, 0, (int32_t)gm32(0x519ec8), true);
}

static void to_target(Menu *m, MenuItem *it) { (void)it; Menu_Goto(m->items[0].target); }   /* 0x449e90 */

/* 0x44af70 with param 0: restart (the text by the mode), at the service area in single stage and time
   trial, quit */
static void build_end(Menu *parent, int16_t restart_text)
{
    Menu_Init(&pg_end, NULL, 0, parent, false, NULL, 1, 0, 1);
    Menu_AddPage(&pg_end, NULL, restart_text, NULL, restart_fn, 1000);
    if (mode() == 2 || mode() == 3) Menu_AddPage(&pg_end, NULL, 0x8b, NULL, service_fn, 0x3e9);
    Menu_AddPage(&pg_end, NULL, 0xa8, NULL, quit_fn, -1);
    Menu_SetCallbacks(&pg_end, NULL, NULL, draw_end, NULL);
    Menu_Reset(&pg_end);
}

void Results_Start(bool again)
{
    PENDING_NOTE(0x44a1b0, "the results' records and championship tables (0x4cf3b0-0x4cfff0)");
    /* the stage record's holder (0x52b044 + 0x654), lower case */
    snprintf(holder, sizeof holder, "%s", (const char *)gm(0x52b044 + 0x654 + ((gm32(0x52f2ac) & 0x1f) * 0xb + (gm32(0x52f2ac) >> 5 & 0x1f)) * 8));
    Text_Lower(holder);
    if (!again) Input_SetMenuMode(true);
    Menu_Start();
    /* 0x449100's pages on the one-player stage's way */
    Menu_Init(&pg_title, NULL, 0, NULL, false, NULL, 1, 0, 0);
    Menu_AddPage(&pg_title, NULL, 0, &pg_times, NULL, 0);
    Menu_SetCallbacks(&pg_title, NULL, NULL, draw_title, NULL);
    Menu_Init(&pg_times, NULL, 0, &pg_title, false, NULL, 1, 0, 0);
    Menu_AddPage(&pg_times, NULL, 0, &pg_splits, NULL, 0);
    Menu_SetCallbacks(&pg_times, NULL, NULL, draw_times, NULL);
    Menu_Init(&pg_splits, NULL, 0, &pg_times, false, NULL, 1, 0, 0);
    Menu_AddPage(&pg_splits, NULL, 0, &pg_board, NULL, 0);
    Menu_SetCallbacks(&pg_splits, NULL, NULL, draw_pending, NULL);
    Menu_Init(&pg_board, NULL, 0, NULL, false, NULL, 1, 0, 0);
    Menu_AddList(&pg_board, NULL, 0, 0xb, 0, false, to_target, 0);
    Menu_SetCallbacks(&pg_board, NULL, NULL, draw_pending, NULL);
    Menu_SetCallbacks(&pg_splits, NULL, NULL, draw_record, NULL);
    if (mode() != 2 && mode() != 3) PENDING_NOTE(0x44a1b0, "the results' pages of the other modes");
    /* the stage record (not in a challenge, with the cheats' options, after a recovery's penalty, or
       for the "cmr" driver) */
    bool record = false;
    const char *pn = player_name(0);
    if (mode() != 4 && !gm8(0x52afc2) && !(gm32(0x52afbc) >> 0xb & 1) && strncmp(pn, exe_str(0x519ec4), 3))
        for (int i = 0; i < (int)Gs_SelBits14(); i++)
            if (Times_Finish(i) > 0 && !Gs_SelBit24() && stage_record(i, Race_Base() + i, player_name(Race_Base() + i))) record = true;
    snprintf(record_name, sizeof record_name, "%s", (const char *)gm(0x52b044 + 0x654 + ((gm32(0x52f2ac) & 0x1f) * 0xb + (gm32(0x52f2ac) >> 5 & 0x1f)) * 8));
    /* with a new record the times go on to the congratulations, else to the board */
    if (record) pg_times.items[0].target = &pg_splits, pg_board.parent = &pg_splits;
    else pg_times.items[0].target = &pg_board, pg_board.parent = &pg_times;
    /* single stage and time trial: the board on to the choices (0x53e4d8) */
    build_end(&pg_board, 0x23);
    pg_board.items[0].target = &pg_end;
    page = NULL;
    shown = NULL;
    settle = 0;
    next = gm8(0x817574) ? &pg_end : &pg_title;
    active = true;
}

bool Results_Active(void) { return active; }
bool Results_AtChoices(void) { return active && page == &pg_end; }
void Results_Stop(void) { active = false; }

/* 0x448e70: not while a fade runs; the select only in the super special; no back on the first page */
void Results_Step(void)
{
    if (!active) return;
    for (int i = 0; i < (int)Gs_SelBits14(); i++)
        if (Fade_Active(i)) return;
    page = next;   /* 0x448e60 */
    Input_PollAll();
    uint32_t b = Input_Record(0)->pressed;
    if (Gs_Mode() == 0 && (gm8(0x52f2ac) & 0x1f) == 8 && (gm32(0x52f2ac) & 0x3e0) == 0x160) b &= 0x10;   /* 0x407270 */
    if (page == &pg_title) b &= 0xffdf;
    /* the list pages' scrolling (0x4bc290's tweens) not ported */
    Menu *to = Menu_Input(page, b);
    if (to) next = to;
}

/* 0x44b330: the page, dimmed alone for two frames after a change */
void Results_Draw(void)
{
    if (!active) return;
    if (shown == page || page == &pg_title) {
        if (settle < 1) {
            Menu_Draw(page);
            return;
        }
    } else {
        shown = page;
        settle = 2;
    }
    dim();
    settle--;
}
