/* The stage's results (see results.h), ported from 0x448e60-0x44b7b0 and 0x453c50. */
#include "game/results.h"
#include "fe/menu.h"
#include "exe.h"
#include "font.h"
#include "render/tex.h"
#include "game/gstate.h"
#include "game/input.h"
#include "game/ctlcfg.h"
#include "game/mem.h"
#include "game/pause.h"
#include "game/race.h"
#include "game/hud.h"
#include "game/camera.h"
#include "game/profile.h"
#include "game/sectors.h"
#include "game/opponents.h"
#include "game/records.h"
#include "game/ghost.h"
#include "game/car_load.h"
#include "game/standings.h"
#include "game/challenge.h"
#include "game/tween.h"
#include "world/tod.h"
#include "pending.h"
#include "render/d3d7.h"
#include "render/twod.h"
#include "sim/fade.h"
#include "text.h"
#include <stdio.h>
#include <string.h>

static Menu pg_title, pg_times, pg_splits, pg_board, pg_end;   /* 0x53ea68, 0x541218, 0x53f5b0, 0x53e4b8, 0x541400 */
static Menu pg_rally, pg_restart, pg_quit;   /* 0x53f008, 0x5402f8, 0x5418d8 */
static Menu pg_chal;                         /* 0x53f1e8: the challenge's match */
static uint32_t ch_match;                    /* 0x541cd0: the challenge's match of the results */
static Menu pg_order, pg_points, pg_stand, pg_last;   /* 0x53fb70, 0x53ec48, 0x53e888, 0x53ee28: the arcade's */
static Menu pg_alt;                /* 0x53f3c8: the time trial's players in alternation */
static int32_t alt_time[4];        /* 0x541cdc: each player's time, by Race_Base */
static uint8_t alt_who[4];         /* 0x541cec: and whose it is */
static bool last_stage, last_player;   /* DAT_00541210, DAT_00540898 */
static Menu *page, *next;          /* DAT_00541cc8, DAT_00541ccc */
/* the list pages' scroll (0x448e70): a row's height times 0x5418c0's tween, taken off every row's y
   (0x540c60); moving down or up (0x53f5a8, 0x5413f8) the rows are a place lower and one fewer */
static int scroll_off, scroll_dn, scroll_up;

static int row_count(void) { return 6 - (scroll_dn || scroll_up); }
static int row_pos(int i) { return i + (scroll_dn || scroll_up); }
static int row_idx(const Menu *m, int k) { return (scroll_dn ? m->items[0].value - 1 : m->items[0].value) + k; }
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

/* 0x408400 (in mode 4 the challenge record's) */
static const char *player_name(int n)
{
    if (mode() == 4) return Player_Name(n);
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


/* 0x44b7b0: "replay" and the driver the camera follows: in the shared replay (0x41f3a0) the second
   camera's; one player with computer drivers (bit 26) the first camera's (a computer's when it has
   a driver, 0x408500); a head to head outside the network game its pair's driver (0x455ac0); else
   the first camera's car after the player base, by its driver; in mode 4 the match's names
   (0x4737d0-0x473810). */
static void draw_title(Menu *m)
{
    (void)m;
    int w, h;
    screen(&w, &h);
    int x = title(0x47);
    const char *name = "";
    int c0 = Camera_Car(0);
    if (Gs_SelBits14() > 1 && Race_State(0) == 10) {
        if (mode() == 4) name = Challenge_Tag(ch_match, Camera_Car(1));
        else name = player_name(Camera_Car(1));
    } else if (Gs_SelBit26() && Gs_SelBits14() == 1) {
        name = Player_Driver(c0) == -1 ? player_name(c0) : Cpu_Name(c0);
    } else if (mode() == 4) {
        /* the first camera's side: a computer's full name, a player's (as the tag) */
        int side = Challenge_Side(ch_match, c0);
        name = !Challenge_IsPlayer(ch_match, side) ? Challenge_Name(ch_match, side) : Challenge_Tag(ch_match, side);
    } else if (Gs_SelBit27() && !Gs_ModeBit19()) {
        name = Slot_Name(Opp_PairDriver(Race_Base(), c0));
    } else {
        int d = Player_Driver(c0 + Race_Base());
        name = d == -1 ? player_name(c0 + Race_Base()) : Cpu_Name(d);
    }
    Font_Print(2, name, x, h * 0x43 / 0x1e0, C(0x519ecc), 0x11);
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

static void arcade_title(Menu *m);
static void draw_arcade_times(Menu *m);

static void draw_ch_times(void);

/* 0x44bcd0: crtna640 (0x405610) centred on (564, 48) of 640 x 480; mode 4's times (0x44cdb0); the
   arcade's (modes 5-7 and 11); else a column for the player and one for the stage
   record (or the second player): the driver, each split's time and the total */
static void draw_times(Menu *m)
{
    int w, h;
    screen(&w, &h);
    dim();
    Tex *crt = Pause_Crt();
    if (crt) {
        Rect16 src = {0, 0, crt->width, crt->height};
        Rect16 d = {(int16_t)(w * 0x234 / 0x280 - crt->width / 2), (int16_t)(h * 0x30 / 0x1e0 - crt->height / 2), crt->width, crt->height};
        Sprite_Add(&src, &d, crt, 2, 0, NULL, NULL, C(0x519ecc), 8);
    }
    if (mode() == 4) {
        const char *t[1] = {Text_Get(0x48)};
        titles(w * 0x1e / 0x280, h * 0x43 / 0x1e0, t, 1);
        draw_ch_times();
        Font_SetLayer(2);
        return;
    }
    if (mode() == 5 || mode() == 6 || mode() == 7 || mode() == 11) {
        draw_arcade_times(m);
        Font_SetLayer(2);
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
            int32_t v = record ? gms32(0x541ab8 + 4u * (uint32_t)(i + 1)) - gms32(0x541ab8 + 4u * (uint32_t)i)
                               : Times_Split(col, i + 1) - Times_Split(col, i);
            print_time(b, sizeof b, v);
            Font_Print(1, b, tx, h * 0x91 / 0x1e0 + dy + (h * 0x1e / 0x1e0) * i, C(c), 0x11);
        }
        int32_t total = record ? gms32(0x541ab8 + 4u * (uint32_t)shown) : Times_Finish(col);
        snprintf(b, sizeof b, exe_str(0x519f58), total / 6000, total / 100 % 60, total % 100);
        Font_Print(1, b, tx, h * 0x19 / 0x1e0 + h * 0x91 / 0x1e0 + dy + (h * 0x1e / 0x1e0) * i, C(c), 0x11);
    }
    Font_SetLayer(2);
}

/* 0x44d790: "high score" and "congratulations", "<holder> you have set the fastest time for this
   stage" */
static int finisher(int i);
static void draw_record(Menu *m)
{
    (void)m;
    int w, h;
    screen(&w, &h);
    dim();
    char b[200];
    if (mode() == 5 || mode() == 6) {
        /* the arcade's: "high score" alone, the name the player placed best (0x451850's place in the
           finish, -1 for none, which wins) */
        const char *t[1] = {Text_Get(0x49)};
        titles(w * 0x1e / 0x280, h * 0xf2 / 0x1e0, t, 1);
        int best = 0x10, who = -1;
        for (int p = 0; p < (int)Gs_SelBits14(); p++) {
            int pl = -1;
            for (int i = 0; i < Cars_Racing(); i++)
                if (finisher(i) == p) { pl = i; break; }
            if (pl < best) best = pl, who = p;
        }
        snprintf(b, sizeof b, exe_str(0x519f68), Player_Name(who), Text_Get(0x4a));
    } else {
        const char *t[2] = {Text_Get(0x49), Text_Get(0x46)};
        titles(w * 0x1e / 0x280, h * 0xf2 / 0x1e0, t, 2);
        snprintf(b, sizeof b, exe_str(0x519f68), record_name, Text_Get(0x4a));
    }
    Text_ToFontCase(b);
    Font_Print(0, b, w * 0x1e / 0x280, h * 10 / 0x1e0 + h * 0xf2 / 0x1e0 + Font_Height(0), C(0x519ecc), 0x11);
    Font_SetLayer(2);
}

/* 0x44ec30: a box around row k's place, dy lower (the row's drop 0x53e698 and the caller's), less
   the scroll (nothing passes the closer rows of its third argument) */
static void place_box(int k, int dy)
{
    int w, h;
    screen(&w, &h);
    int x0 = w * 0x20 / 0x280, x1 = w * 0x41 / 0x280, y0 = h * 0x82 / 0x1e0 + k * (h * 0x34 / 0x1e0) + dy - scroll_off,
        y1 = h * 0xa3 / 0x1e0 + k * (h * 0x34 / 0x1e0) + dy - scroll_off;
    int16_t a[4] = {(int16_t)(x0 + 2), (int16_t)y0, (int16_t)(x1 - x0 - 4), 2};
    Rect_Fill(a, C(0x519ecc), 2);
    int16_t b[4] = {(int16_t)(x0 + 2), (int16_t)(y1 - 2), (int16_t)(x1 - x0 - 4), 2};
    Rect_Fill(b, C(0x519ecc), 2);
    int16_t c[4] = {(int16_t)(x1 - 2), (int16_t)y0, 2, (int16_t)(y1 - y0)};
    Rect_Fill(c, C(0x519ecc), 2);
    int16_t d[4] = {(int16_t)x0, (int16_t)y0, 2, (int16_t)(y1 - y0)};
    Rect_Fill(d, C(0x519ecc), 2);
}

/* 0x40ede0: a car's name in the race's text */
static const char *car_name(int car) { return Text_Get(car + 0xc2); }

/* 0x44d960: the rally and stage, then six places from the page's value (scrolled as the lists): the
   name (a player's highlighted), the car, the time and the place (or "=" for a tie) */
static void draw_board(Menu *m)
{
    int w, h;
    screen(&w, &h);
    dim();
    char stage[64];
    uint32_t st = Tod_Stage();
    if (st == 10) snprintf(stage, sizeof stage, "%s", Text_Get(0xba));
    else snprintf(stage, sizeof stage, exe_str(0x516098), Text_Get(0x40), (int)st + 1);
    if (mode() == 0 || mode() == 1) {
        /* a rally's: the rally, "day n" (four stages a day), the stage */
        char day[64];
        snprintf(day, sizeof day, exe_str(0x516098), Text_Get(0x44), (int)(st >> 2) + 1);
        const char *t[3] = {Text_Get((int)Tod_Rally()), day, stage};
        titles(w * 0x1e / 0x280, h * 0x43 / 0x1e0, t, 3);
    } else {
        const char *t[2] = {Text_Get((int)Tod_Rally()), stage};
        titles(w * 0x1e / 0x280, h * 0x43 / 0x1e0, t, 2);
    }
    for (int i = 0, n = row_count(); i < n; i++) {
        int k = row_pos(i), idx = row_idx(m, k), driver = Opp_Order(idx);
        int32_t tm = Opp_Time(idx);
        int who = -1;
        for (int p = 0; p < (int)(gm32(0x52afb4) >> 13 & 0xf); p++)
            if (Opp_Place(p) == idx) { who = p; break; }
        char b[96];
        int y = h * 0x82 / 0x1e0 + (h * 0x34 / 0x1e0) * k - h * 8 / 0x1e0 - scroll_off;
        snprintf(b, sizeof b, "%s", who >= 0 ? player_name(who) : exe_str(0x5166b4 + 0x14u * (uint32_t)(driver < 0 || driver > 15 ? 0 : driver)));
        Font_Print(1, b, w * 0x5c / 0x280, y, C(0x519ecc), 9);
        print_time(b, sizeof b, tm);
        Font_Print(1, b, w * 0x21e / 0x280, y, C(0x519ecc), 9);
        if (who >= 0) {
            snprintf(b, sizeof b, "%s", car_name((int)(gm32(0x531350 + 0x30u * (uint32_t)who) & 0x3f)));
            Text_ToFontCase(b);
            int16_t r[4] = {(int16_t)(w * 0x20 / 0x280), (int16_t)(h * 0x82 / 0x1e0 + (h * 0x34 / 0x1e0) * k - 1 - scroll_off),
                            (int16_t)(w * 0x41 / 0x280 - w * 0x20 / 0x280), (int16_t)(h * 0xa3 / 0x1e0 - h * 0x82 / 0x1e0 + 1)};
            Rect_Fill(r, C(0x519ed8), 2);
        } else {
            int car = driver >= 0 && driver <= 0xe ? gm8(0x51681c + (uint32_t)driver) : 0;   /* 0x407fc0 */
            snprintf(b, sizeof b, "%s", car_name(car));
            Text_ToFontCase(b);
            place_box(k, 0);
        }
        Font_Print(0, b, w * 0x5c / 0x280, h * 0xa3 / 0x1e0 + (h * 0x34 / 0x1e0) * k - scroll_off, C(0x519ecc), 0x21);
        if (Opp_Time(idx - 1) == tm) snprintf(b, sizeof b, "%s", exe_str(0x519f70));
        else snprintf(b, sizeof b, exe_str(0x517dd0), idx + 1);
        int x0 = w * 0x20 / 0x280, y0 = h * 0x82 / 0x1e0;
        Font_Print(1, b, (w * 0x41 / 0x280 - x0) / 2 + x0, h * 8 / 0x1e0 + (h * 0xa3 / 0x1e0 - y0) / 2 + (h * 0x34 / 0x1e0) * k - scroll_off + y0, C(0x519ecc), 0x12);
    }
    Font_SetLayer(2);
}

/* 0x449ba0 for the board: the list starts five above the best player's place (at most 10) */
static void board_enter(Menu *m, bool back)
{
    (void)back;
    int best = 0x10;
    for (int p = 0; p < (int)(gm32(0x52afb4) >> 13 & 0xf); p++)
        if (Opp_Place(p) < best) best = Opp_Place(p);
    if (best > 5) m->items[0].value = (uint8_t)(best - 5);
    if (m->items[0].value > 10) m->items[0].value = 10;
}

/* 0x44b270: the players' times so far in order, the shortest first (a 0 is left out; when only
   0s are left the swap is with the last place picked, 0 at first, as in the original) */
static void alt_sort(void)
{
    int pick = 0;
    for (int i = 0; i < Race_Base() + 1; i++) {
        int32_t best = 0xffff;
        for (int j = i; j < Race_Base() + 1; j++)
            if (alt_time[j] && alt_time[j] < best) pick = j, best = alt_time[j];
        int32_t t = alt_time[i];
        alt_time[i] = alt_time[pick], alt_time[pick] = t;
        uint8_t a = alt_who[pick], b = alt_who[i];
        alt_who[i] = a, alt_who[pick] = b;
    }
}

/* 0x44d260: the rally, the stage and 0x9a; a row for each player so far (lower the fewer: 80, 50, 20
   of 480 for the second to the fourth; the first's offset is the original's uninitialised local, 0
   here): the name, the car, the time, the place in its box */
static void draw_alt(Menu *m)
{
    (void)m;
    int w, h;
    screen(&w, &h);
    dim();
    char stage[64];
    uint32_t st = Tod_Stage();
    if (st == 10) snprintf(stage, sizeof stage, "%s", Text_Get(0xba));
    else snprintf(stage, sizeof stage, exe_str(0x516098), Text_Get(0x40), (int)st + 1);
    const char *t[3] = {Text_Get((int)Tod_Rally()), stage, Text_Get(0x9a)};
    titles(w * 0x1e / 0x280, h * 0x43 / 0x1e0, t, 3);
    int base = Race_Base(), dy = 0;
    if (base == 1) dy = h * 0x50 / 0x1e0;
    else if (base == 2) dy = h * 0x32 / 0x1e0;
    else if (base == 3) dy = h * 0x14 / 0x1e0;
    for (int k = 0; k < base + 1 && k < 4; k++) {
        char b[96];
        int row = h * 0x82 / 0x1e0 + (h * 0x34 / 0x1e0) * k - h * 8 / 0x1e0 + dy;
        snprintf(b, sizeof b, "%s", player_name(alt_who[k]));
        Text_Lower(b);
        Font_Print(1, b, w * 0x5c / 0x280, row, C(0x519ecc), 9);
        snprintf(b, sizeof b, "%s", car_name((int)(gm32(0x531350 + 0x30u * alt_who[k]) & 0x3f)));
        Text_ToFontCase(b);
        Font_Print(0, b, w * 0x5c / 0x280, h * 0xa3 / 0x1e0 + dy + (h * 0x34 / 0x1e0) * k, C(0x519ecc), 0x11);
        print_time(b, sizeof b, alt_time[k]);
        Font_Print(1, b, w * 0x21e / 0x280, h * 6 / 0x1e0 + row, C(0x519ecc), 9);
        place_box(k, dy);
        snprintf(b, sizeof b, exe_str(0x517dd0), k + 1);
        int x0 = w * 0x20 / 0x280, y0 = h * 0x82 / 0x1e0;
        Font_Print(1, b, (w * 0x41 / 0x280 - x0) / 2 + x0, h * 8 / 0x1e0 + (h * 0x34 / 0x1e0) * k + y0 + (h * 0xa3 / 0x1e0 - y0) / 2 + dy, C(0x519ecc), 0x12);
    }
}

/* The arcade's pages */
static uint32_t location(void) { return gm32(0x52f2ac) >> 10 & 3; }   /* 0x406940 */
static uint32_t race_no(void) { return gm32(0x52f2ac) >> 12 & 3; }    /* 0x406950 */
static bool arcade(void) { return mode() == 5 || mode() == 6 || mode() == 7; }
static uint32_t ch_kind(void) { return gm32(0x52f2b0) & 7; }          /* 0x4082b0 */
static bool challenge_kinds(void) { return (gm32(0x52f2b0) >> 0xb & 1) && (ch_kind() == 1 || ch_kind() == 2); }   /* 0x4082e0 */

/* 0x448390: the car in place i of the finish (0x53e17c), -1 outside the eight */
static int finisher(int i) { return i >= 0 && i < 8 ? (int8_t)gm8(0x53e17c + (uint32_t)i) : -1; }

/* 0x407fc0: a computer car's model: in the arcade the loaded car's (0x456c00: 0x54260c, cars 0-5),
   else its driver's (0x51681c, 0-14) */
static int cpu_car(int car)
{
    if (arcade()) return car >= 0 && car < 6 ? (uint8_t)gm32(0x54260c + 4u * (uint32_t)car) : 0;
    return car >= 0 && car <= 0xe ? gm8(0x51681c + (uint32_t)car) : 0;
}

/* in the big font at (x, 67), then a separator; the x after it */
static int big_sep(const char *s, int x)
{
    int w, h;
    screen(&w, &h);
    Font_Print(2, s, x, h * 0x43 / 0x1e0, C(0x519ecc), 0x11);
    x = w * 8 / 0x280 + x + Font_TextWidth(2, s);
    int16_t r[4] = {(int16_t)x, (int16_t)(h * 0x25 / 0x1e0), 2, (int16_t)(h * 0x29 / 0x1e0)};
    Rect_Fill(r, C(0x519ecc), 2);
    return x + 2 + w * 8 / 0x280;
}

/* 0x451890: the arcade pages' title: on the times page "times" (0xac) first; at the first two
   locations "location <'b' less the location>" (0x43) and the race's rally (0x451ce0: by location x
   3 + race in the table {6, 3, 1, 4, 0, 2, 5, 7}, lower case), at the third the race's own text (5
   the first, else 7, lower case); on the points page (0x53ec48) "points" (0x8d) after it */
static void arcade_title(Menu *m)
{
    int w, h;
    screen(&w, &h);
    int x = w * 0x1e / 0x280;
    if (m == &pg_times) x = big_sep(Text_Get(0xac), x);
    char b[128];
    bool two = location() < 2;
    if (two) snprintf(b, sizeof b, Text_Get(0x43), 0x62 - (int)location());
    else {
        snprintf(b, sizeof b, "%s", Text_Get(race_no() == 0 ? 5 : 7));
        Text_Lower(b);
    }
    if (two) {
        x = big_sep(b, x);
        static const int8_t rally[8] = {6, 3, 1, 4, 0, 2, 5, 7};
        uint32_t k = location() * 3 + race_no();
        snprintf(b, sizeof b, "%s", k < 8 ? Text_Get(rally[k]) : exe_str(0x519f98));
        Text_Lower(b);
    }
    Font_Print(2, b, x, h * 0x43 / 0x1e0, C(0x519ecc), 0x11);
    if (page == &pg_points) {
        x = big_sep(b, x);
        Font_Print(2, Text_Get(0x8d), x, h * 0x43 / 0x1e0, C(0x519ecc), 0x11);
    }
}

/* the rows' common parts: the name (row k), the car (a player's row filled, another boxed), the place
   (or "=") */
static void row_name(int k, const char *name)
{
    int w, h;
    screen(&w, &h);
    Font_Print(1, name, w * 0x5c / 0x280, h * 0x82 / 0x1e0 + (h * 0x34 / 0x1e0) * k - h * 8 / 0x1e0, C(0x519ecc), 9);
}
static void row_value(int k, const char *s, int flags)
{
    int w, h;
    screen(&w, &h);
    Font_Print(1, s, w * 0x21e / 0x280, h * 0x82 / 0x1e0 + (h * 0x34 / 0x1e0) * k - h * 8 / 0x1e0, C(0x519ecc), flags);
}
static void row_car(int k, int car, bool player)
{
    int w, h;
    screen(&w, &h);
    char b[96];
    snprintf(b, sizeof b, "%s", car_name(car));
    Text_ToFontCase(b);
    if (player) {
        int16_t r[4] = {(int16_t)(w * 0x20 / 0x280), (int16_t)(h * 0x82 / 0x1e0 + (h * 0x34 / 0x1e0) * k - 1),
                        (int16_t)(w * 0x41 / 0x280 - w * 0x20 / 0x280), (int16_t)(h * 0xa3 / 0x1e0 - h * 0x82 / 0x1e0 + 1)};
        Rect_Fill(r, C(0x519ed8), 2);
    } else
        place_box(k, 0);
    Font_Print(0, b, w * 0x5c / 0x280, h * 0xa3 / 0x1e0 + (h * 0x34 / 0x1e0) * k, C(0x519ecc), 0x21);
}
static void row_place(int k, bool tie)
{
    int w, h;
    screen(&w, &h);
    char b[16];
    if (tie) snprintf(b, sizeof b, "%s", exe_str(0x519f70));
    else snprintf(b, sizeof b, exe_str(0x517dd0), k + 1);
    int x0 = w * 0x20 / 0x280, y0 = h * 0x82 / 0x1e0;
    Font_Print(1, b, (w * 0x41 / 0x280 - x0) / 2 + x0, h * 8 / 0x1e0 + (h * 0x34 / 0x1e0) * k + y0 + (h * 0xa3 / 0x1e0 - y0) / 2, C(0x519ecc), 0x12);
}

/* 0x44bcd0's arcade part: the title, "record, <holder>, <time>" of the location's race (0x52b044 +
   0x1210, 8 a race: the name, the time in bits 7-22), then per player a column (360 apart; the rows
   moved down by ten less the laps): "laps" (0xac, not with ten), the driver, each lap's time
   (0x4481f0) and their total ("total", 0x8a). The network game's (the lap counter 0x539dcc and
   Times_Finish) is not ported. */
/* 0x44cdb0: the match's two columns, as the stage's (the driver, each split's time and the total):
   the two players' names, or the player's first and the computer's (0x407ec0) second */
static void draw_ch_times(void)
{
    int w, h;
    screen(&w, &h);
    int a = (int)(gm32(ch_match) & 0x1f), b = (int)(gm32(ch_match) >> 5 & 0x1f), base = Race_Base();
    char n[2][64], t[160];
    if (Player_Driver(a) == -1 && Player_Driver(b) == -1) {
        snprintf(n[0], sizeof n[0], "%s", player_name(base + a));
        snprintf(n[1], sizeof n[1], "%s", player_name(base + b));
    } else if (Player_Driver(a) == -1) {
        snprintf(n[0], sizeof n[0], "%s", player_name(base + a));
        snprintf(n[1], sizeof n[1], "%s", Cpu_Name(Player_Driver(b)));
    } else {
        snprintf(n[0], sizeof n[0], "%s", player_name(base + b));
        snprintf(n[1], sizeof n[1], "%s", Cpu_Name(Player_Driver(a)));
    }
    int shown = Sectors_Shown(), y = h * 0xe6 / 0x1e0, line = h * 0x1e / 0x1e0;
    for (int col = 0; col < 2; col++) {
        int x = w * 0x1e / 0x280 + (w * 0x166 / 0x280) * col;
        Font_Print(1, Text_Get(0x89), x, y, C(0x519ecc), 0x11);
        snprintf(t, sizeof t, Text_Get(0x9b), n[col]);
        Font_Print(1, t, x, y - line, C(0x519ecc), 0x11);
        Font_Print(1, Text_Get(0x8a), x, h * 0x19 / 0x1e0 + y + shown * h * 0x1e / 0x1e0, C(0x519ecc), 0x11);
        int tx = w * 0xb1 / 0x280 + (w * 0x166 / 0x280) * col, i = 0;
        for (; i < shown; i++) {
            print_time(t, sizeof t, Times_Split(col, i + 1) - Times_Split(col, i));
            Font_Print(1, t, tx, y + line * i, C(0x519ecc), 0x11);
        }
        int32_t total = Times_Finish(col);
        snprintf(t, sizeof t, exe_str(0x519f58), total / 6000, total / 100 % 60, total % 100);
        Font_Print(1, t, tx, h * 0x19 / 0x1e0 + y + line * i, C(0x519ecc), 0x11);
    }
}

/* 0x4529c0: the match's result: lost, "0x88 <the first side's name>" (whoever it is) and out of the
   challenge (0x91 before the final unless the depth is 1, else 0x92); won, 0x49 and the faster side's
   name in 0x7e (0x7d in the final) */
static void draw_chal(Menu *m)
{
    (void)m;
    int w, h;
    screen(&w, &h);
    int x = w * 0x1e / 0x280, y = h * 0xf2 / 0x1e0;
    uint32_t r = gm32(0x52f2b4), mw = gm32(ch_match);
    char nm[64], b[256];
    dim();
    if (!Challenge_PlayerWon(ch_match)) {
        snprintf(nm, sizeof nm, "%s", player_name(Race_Base() + (int)(mw & 0x1f)));
        snprintf(b, sizeof b, exe_str(0x516940), Text_Get(0x88), nm);
        Font_Print(2, b, x, y, C(0x519ecc), 0x11);
        snprintf(b, sizeof b, "%s", Text_Get((r & 0x38) < 0x20 && (r & 7) != 1 ? 0x91 : 0x92));
    } else {
        Font_Print(2, Text_Get(0x49), x, y, C(0x519ecc), 0x11);
        uint32_t d = gm32(ch_match + 8) < gm32(ch_match + 4) ? mw >> 5 : mw;
        snprintf(nm, sizeof nm, "%s", player_name(Race_Base() + (int)(d & 0x1f)));
        snprintf(b, sizeof b, Text_Get((r & 0x38) < 0x20 ? 0x7e : 0x7d), nm);
    }
    Text_ToFontCase(b);
    Font_Print(0, b, x, h * 5 / 0x1e0 + y + Font_Height(0), C(0x519ecc), 0x11);
}

static void draw_arcade_times(Menu *m)
{
    int w, h;
    screen(&w, &h);
    arcade_title(m);
    int x = w * 0x1e / 0x280, y = h * 0x91 / 0x1e0 - h * 0x32 / 0x1e0;
    char b[160];
    snprintf(b, sizeof b, exe_str(0x519f50), Text_Get(0x6d));
    Text_ToFontCase(b);
    Font_Print(0, b, x, y, C(0x519ecc), 0x11);
    x += Font_TextWidth(0, b);
    uint32_t rec = 0x52b044 + 0x1210 + (location() * 3 + race_no()) * 8;
    snprintf(b, sizeof b, exe_str(0x519f48), (const char *)gm(rec));
    Font_Print(0, b, x, y, C(0x519ecc), 0x11);
    int wid = Font_TextWidth(0, b);
    print_time(b, sizeof b, (int32_t)(gm32(rec + 4) >> 7 & 0xffff));
    Font_Print(0, b, x + wid, y, C(0x519ecc), 0x11);
    int laps = (int)(gm32(0x52f2ac) >> 0x10 & 0xf);   /* Gs_Laps 0x406990 */
    int dy = (10 - laps) * (h * 10 / 0x1e0) - h * 0x12 / 0x1e0;
    if (laps == 10) dy += h * -10 / 0x1e0;
    int top = h * 0x91 / 0x1e0, row = h * 0x1e / 0x1e0;
    for (int p = 0; p < (int)Gs_SelBits14(); p++) {
        int cx = w * 0x1e / 0x280 + (w * 0x166 / 0x280) * p;
        if (laps != 10) Font_Print(1, Text_Get(0xac), cx, top + dy, C(0x519ecc), 0x11);
        snprintf(b, sizeof b, Text_Get(0x9b), player_name(Race_Base() + p));
        Font_Print(1, b, cx, row + dy + top, C(0x519ecc), 0x11);
        Font_Print(1, Text_Get(0x8a), cx, top + dy + (laps + 1) * row, C(0x519ecc), 0x11);
        int tx = w * 0xb1 / 0x280 + (w * 0x166 / 0x280) * p, i = 1;
        int32_t total = 0;
        if (Gs_ModeBit19()) PENDING_NOTE(0x44bcd0, "the network game's lap times");
        else
            for (; i < laps + 1; i++) {
                int32_t v = gms32(0x53d9b4 + 4u * (uint32_t)(i + p * 0x22));   /* 0x4481f0 */
                print_time(b, sizeof b, v);
                Font_Print(1, b, tx, top + dy + row * i, C(0x519ecc), 0x11);
                total += v;
            }
        print_time(b, sizeof b, total);
        Font_Print(1, b, tx, top + dy + row * i, C(0x519ecc), 0x11);
    }
}

/* 0x451df0: the finish (0x53fb70) and the race's points (0x53ec48): each car racing in its place,
   its time (a challenge of kind 1: the winner's less the given seconds, 0x4082d0, the others "+" the
   seconds; kind 2: the laps' count, 0x448c60, in points) or on the points page the place's points
   (0x40ce40) */
static void draw_order(Menu *m)
{
    dim();
    arcade_title(m);
    for (int i = 0; i < Cars_Racing(); i++) {
        int k = finisher(i);
        int32_t t = Times_Finish(k);
        bool player = k < (int)Gs_SelBits14();
        row_name(i, player ? player_name(k) : Cpu_Name(k));
        char b[96];
        int flags = 9;
        if (page == &pg_points) snprintf(b, sizeof b, exe_str(0x517dd0), Stand_ArcadePoints(i));
        else if (!challenge_kinds()) print_time(b, sizeof b, t);
        else if (ch_kind() == 1) {
            flags = 0xc;
            int secs = (int)(gm32(0x52f2b0) >> 3 & 0xf);   /* 0x4082d0 */
            if (i == 0) print_time(b, sizeof b, t - secs * 100);
            else {
                char s[48];
                print_time(s, sizeof s, secs * 100);
                snprintf(b, sizeof b, exe_str(0x519fa8), s);
            }
        } else
            snprintf(b, sizeof b, exe_str(0x519fa0), (int)(int8_t)gm8(0x53d1a4 + (uint32_t)k));   /* 0x448c60 */
        row_value(i, b, flags);
        row_car(i, player ? (int)(gm32(0x531350 + 0x30u * (uint32_t)k) & 0x3f) : cpu_car(k), player);
        bool tie = (ch_kind() == 1 || ch_kind() == 2) ? false : Times_Finish(finisher(i)) == Times_Finish(finisher(i - 1));
        row_place(i, tie);
    }
    Font_SetLayer(2);
}

/* 0x4530e0: the location's standings: "location <x>", the standings (0x42), "points" (0x8d); each car
   by its order (0x533610) with its points (0x5335b8) and, when another has as many (and not none),
   its wins (0xb8 one, else 0xb6; 0x533628), the place "=" when the one above has as many of both */
static void draw_standings(Menu *m)
{
    (void)m;
    int w, h;
    screen(&w, &h);
    dim();
    char loc[64];
    snprintf(loc, sizeof loc, Text_Get(0x43), 0x62 - (int)location());   /* 0x4ab600 */
    const char *t[3] = {loc, Text_Get(0x42), Text_Get(0x8d)};
    titles(w * 0x1e / 0x280, h * 0x43 / 0x1e0, t, 3);
    int n = Cars_Racing();
    uint8_t pts[8] = {0};   /* 0x5418c4 */
    for (int i = 0; i < n && i < 8; i++) pts[i] = (uint8_t)(gms32(0x5335b8 + 4u * (uint32_t)(int8_t)gm8(0x533610 + (uint32_t)i)) >> 16);
    for (int i = 0; i < n && i < 8; i++) {
        int k = (int8_t)gm8(0x533610 + (uint32_t)i);   /* 0x40cea0 */
        bool player = k < (int)Gs_SelBits14(), tie = false;
        row_name(i, player ? player_name(k) : Cpu_Name(k));
        char b[96];
        snprintf(b, sizeof b, exe_str(0x517dd0), gms32(0x5335b8 + 4u * (uint32_t)k) >> 16);   /* 0x40ced0 */
        row_value(i, b, 9);
        if (pts[i])
            for (int j = 0; j < n && j < 8; j++) {
                if (j == i || pts[i] != pts[j]) continue;
                int wins = (int8_t)gm8(0x533628 + (uint32_t)k);   /* 0x40ce30 */
                snprintf(b, sizeof b, Text_Get(wins == 1 ? 0xb8 : 0xb6), wins);
                Font_Print(1, b, w * 0x1fe / 0x280, h * 0x82 / 0x1e0 + (h * 0x34 / 0x1e0) * i - h * 8 / 0x1e0, C(0x519ecc), 0xc);
                if (i > 0 && (int8_t)gm8(0x533628 + (uint32_t)(int8_t)gm8(0x533610 + (uint32_t)(i - 1))) == wins && pts[i - 1] == pts[i]) tie = true;
                break;
            }
        row_car(i, player ? (int)(gm32(0x531350 + 0x30u * (uint32_t)k) & 0x3f) : cpu_car(k), player);
        row_place(i, tie);
    }
    Font_SetLayer(2);
}

/* 0x453830: the location's end: "location <x>"; once a player is in the first three, the level's
   (0x8e on) and "congratulations" (0x49) with separators; each player's name and place (0x51-0x53, else
   0x54 with the place) */
static void draw_last(Menu *m)
{
    (void)m;
    int w, h;
    screen(&w, &h);
    dim();
    int x = w * 0x1e / 0x280, y = h * 0xf2 / 0x1e0;
    char b[160];
    snprintf(b, sizeof b, Text_Get(0x43), 0x62 - (int)location());
    Font_Print(2, b, x, y, C(0x519ecc), 0x11);
    x += Font_TextWidth(2, b);
    bool shown = false;
    for (int p = 0; p < (int)Gs_SelBits14(); p++) {
        int place = (int8_t)gm8(0x533618 + (uint32_t)p);   /* 0x40ceb0 */
        if (!shown && place < 3) {
            int16_t r[4] = {0, (int16_t)(y + h * 4 / 0x1e0 - Font_Height(2)), 2, (int16_t)(h * 0x29 / 0x1e0)};
            x += w * 8 / 0x280;
            r[0] = (int16_t)x;
            Rect_Fill(r, C(0x519ecc), 2);
            x += w * 8 / 0x280;
            const char *lv = Text_Get((int)Gs_Mode2() + 0x8e);
            Font_Print(2, lv, x, y, C(0x519ecc), 0x11);
            x += Font_TextWidth(2, lv) + w * 8 / 0x280;
            r[0] = (int16_t)x;
            Rect_Fill(r, C(0x519ecc), 2);
            x += w * 8 / 0x280;
            Font_Print(2, Text_Get(0x49), x, y, C(0x519ecc), 0x11);
            shown = true;
        }
        snprintf(b, sizeof b, exe_str(0x517dd4), player_name(p));
        size_t l = strlen(b);
        if (place < 3) snprintf(b + l, sizeof b - l, "%s", Text_Get(0x51 + place));
        else snprintf(b + l, sizeof b - l, Text_Get(0x54), place + 1);
        Text_ToFontCase(b);
        Font_Print(0, b, w * 0x1e / 0x280, y + (h * 10 / 0x1e0 + Font_Height(0)) * (p + 1), C(0x519ecc), 0x11);
    }
    Font_SetLayer(2);
}

/* 0x44f680: row k's place box filled (0x519ed4, opaque) */
static void place_fill(int k, int dy)
{
    int w, h;
    screen(&w, &h);
    uint8_t c[4];
    memcpy(c, C(0x519ed4), 4);
    c[3] = 0xff;
    int16_t r[4] = {(int16_t)(w * 0x20 / 0x280), (int16_t)(h * 0x82 / 0x1e0 + (h * 0x34 / 0x1e0) * k - 1 + dy - scroll_off),
                    (int16_t)(w * 0x41 / 0x280 - w * 0x20 / 0x280), (int16_t)(h * 0xa3 / 0x1e0 - h * 0x82 / 0x1e0 + 1)};
    Rect_Fill(r, c, 2);
}

/* 0x44f7b0: the line under row k (after the sixth place) */
static void place_line(int k)
{
    int w, h;
    screen(&w, &h);
    int16_t r[4] = {(int16_t)(w * 0x20 / 0x280), (int16_t)(h * 0x82 / 0x1e0 + (h * 0x34 / 0x1e0) * k + h * 0x2f / 0x1e0 - scroll_off), (int16_t)(w * 0x240 / 0x280), 1};
    Rect_Fill(r, C(0x519ecc), 2);
}

/* 0x40d480: a championship time (16.16 s) to 1/100 s */
static int32_t champ_cs(int32_t t) { return (int32_t)(((int64_t)(t + 0x147) << 16) / 0x28f5c28); }

enum { L_TIMES, L_POINTS, L_CHAMP };

static int8_t sb(uint32_t va) { return (int8_t)gm8(va); }

/* the six rows of the rally's lists from the page's value: 0x44fea0 (L_TIMES: the championship's
   times, by their order 0x533638, a player found by the times' place of 15 down), 0x44efa0 (L_POINTS:
   the rally's points 0x533598 by the times' order, a player by its slot's times place) and 0x450ef0
   (L_CHAMP: the championship's points by their order 0x533538, a player by its slot, equal points
   with the wins, 0xb9 or 0xb7, and "=" when the wins are equal too). Each row: the name, the value,
   the car, the place; the first two with the places after the sixth shown 10 lower when the list
   starts in the first six (0x53e698, the place box lowered too), the computers' first six places
   filled, a line after the sixth; all less the scroll. */
static void list_rows(Menu *m, int kind)
{
    int w, h;
    screen(&w, &h);
    int np = (int)Gs_Players();
    uint8_t pts[16];   /* 0x5418c4: the points by their order */
    for (uint32_t k = 0; k < 16; k++) pts[k] = (uint8_t)(gms32(0x533558 + 4u * (uint32_t)sb(0x533538 + k)) >> 16);
    for (int i = 0, n = row_count(); i < n; i++) {
        int k = row_pos(i), idx = row_idx(m, k);
        int dy = kind == L_CHAMP || idx < 6 || m->items[0].value > 5 ? 0 : h * 10 / 0x1e0;
        int driver = kind == L_CHAMP ? sb(0x533538 + (uint32_t)idx) : sb(0x533638 + (uint32_t)idx);   /* 0x40d050, 0x40d390 */
        int who = -1;
        for (int p = 0; p < np; p++) {
            bool hit = kind == L_TIMES ? sb(0x533648 + (uint32_t)(0xf - p)) == idx
                       : kind == L_POINTS ? sb(0x533648 + (uint32_t)Opp_PlayerSlot(p)) == idx : Opp_PlayerSlot(p) == driver;
            if (hit) { who = p; break; }
        }
        char b[96];
        int y = h * 0x82 / 0x1e0 + (h * 0x34 / 0x1e0) * k - h * 8 / 0x1e0 + dy - scroll_off;
        snprintf(b, sizeof b, "%s", who >= 0 ? player_name(who) : exe_str(0x5166b4 + 0x14u * (uint32_t)(driver < 0 || driver > 15 ? 0 : driver)));
        Font_Print(1, b, w * 0x5c / 0x280, y, C(0x519ecc), 9);
        if (kind == L_TIMES) print_time(b, sizeof b, champ_cs(gms32(0x533658 + 4u * (uint32_t)driver)));   /* 0x40d3b0 */
        else if (kind == L_POINTS) snprintf(b, sizeof b, exe_str(0x517dd0), sb(0x533598 + (uint32_t)driver));   /* 0x40d410 */
        else snprintf(b, sizeof b, exe_str(0x517dd0), gms32(0x533558 + 4u * (uint32_t)driver) >> 16);   /* 0x40d070 */
        Font_Print(1, b, w * 0x21e / 0x280, y, C(0x519ecc), 9);
        if (who >= 0) {
            snprintf(b, sizeof b, "%s", car_name((int)(gm32(0x531350 + 0x30u * (uint32_t)who) & 0x3f)));
            Text_ToFontCase(b);
            int16_t r[4] = {(int16_t)(w * 0x20 / 0x280), (int16_t)(h * 0x82 / 0x1e0 + (h * 0x34 / 0x1e0) * k - 1 + dy - scroll_off),
                            (int16_t)(w * 0x41 / 0x280 - w * 0x20 / 0x280), (int16_t)(h * 0xa3 / 0x1e0 - h * 0x82 / 0x1e0 + 1)};
            Rect_Fill(r, C(0x519ed8), 2);
        } else {
            if (kind != L_CHAMP && idx < 6) place_fill(k, 0);
            int car = driver >= 0 && driver <= 0xe ? gm8(0x51681c + (uint32_t)driver) : 0;   /* 0x407fc0 */
            snprintf(b, sizeof b, "%s", car_name(car));
            Text_ToFontCase(b);
            place_box(k, dy);
        }
        Font_Print(0, b, w * 0x5c / 0x280, h * 0xa3 / 0x1e0 + (h * 0x34 / 0x1e0) * k + dy - scroll_off, C(0x519ecc), 0x21);
        snprintf(b, sizeof b, exe_str(0x517dd0), idx + 1);
        if (kind == L_CHAMP && pts[idx])
            for (int j = 0; j < 16; j++) {
                if (j == idx || pts[j] != pts[idx]) continue;
                int wins = sb(0x5335a8 + (uint32_t)driver);   /* 0x40cfe0 */
                char wb[64];
                snprintf(wb, sizeof wb, Text_Get(wins == 1 ? 0xb9 : 0xb7), wins);
                Font_Print(1, wb, w * 0x1fe / 0x280, y, C(0x519ecc), 0xc);
                if (idx > 0 && sb(0x5335a8 + (uint32_t)sb(0x533538 + (uint32_t)(idx - 1))) == wins && pts[idx - 1] == pts[idx])
                    snprintf(b, sizeof b, "%s", exe_str(0x519f70));
                break;
            }
        int x0 = w * 0x20 / 0x280, y0 = h * 0x82 / 0x1e0;
        Font_Print(1, b, (w * 0x41 / 0x280 - x0) / 2 + x0, h * 8 / 0x1e0 + (h * 0xa3 / 0x1e0 - y0) / 2 + (h * 0x34 / 0x1e0) * k - scroll_off + y0 + dy, C(0x519ecc), 0x12);
        if (kind != L_CHAMP && idx == 5 && m->items[0].value) place_line(k);
    }
    Font_SetLayer(2);
}

static void draw_rally(Menu *m)
{
    int w, h;
    screen(&w, &h);
    dim();
    const char *t[3] = {Text_Get((int)Tod_Rally()), Text_Get(0x41), Text_Get(0xef)};
    titles(w * 0x1e / 0x280, h * 0x43 / 0x1e0, t, last_stage && last_player ? 3 : 2);
    list_rows(m, L_TIMES);
}

static void draw_rally_points(Menu *m)   /* 0x44efa0 */
{
    int w, h;
    screen(&w, &h);
    dim();
    const char *t[3] = {Text_Get((int)Tod_Rally()), Text_Get(0x41), Text_Get(0x8d)};
    titles(w * 0x1e / 0x280, h * 0x43 / 0x1e0, t, 3);
    list_rows(m, L_POINTS);
}

static void draw_champ(Menu *m)   /* 0x450ef0 */
{
    int w, h;
    screen(&w, &h);
    dim();
    const char *t[3] = {Text_Get((int)Tod_Rally()), Text_Get(0x42), Text_Get(0x8d)};
    titles(w * 0x1e / 0x280, h * 0x43 / 0x1e0, t, 3);
    list_rows(m, L_CHAMP);
}

/* "<name> - <place>": 1st, 2nd, 3rd (0x51-0x53) or the place in 0x54's form */
static void place_line_text(char *b, size_t n, int p, int place)
{
    char pl[64];
    if (place >= 0 && place < 3) snprintf(pl, sizeof pl, "%s", Text_Get(0x51 + place));
    else snprintf(pl, sizeof pl, Text_Get(0x54), place + 1);
    snprintf(b, n, exe_str(0x517dd8), player_name(p), pl);
    Text_ToFontCase(b);
}

/* 0x450c10: the rally's result: the rally, its "result" and well done (0x49) when a player is in the
   first six by times, else 0x88; each player's times place; under six none: the message, the
   championship's end (0xee) in the last rally, else 0xbb */
static void draw_rally_result(Menu *m)
{
    (void)m;
    int w, h;
    screen(&w, &h);
    dim();
    int x = w * 0x1e / 0x280, y = h * 0xf2 / 0x1e0, best = 99, np = (int)Gs_Players();
    for (int p = 0; p < np; p++)
        if (sb(0x533648 + (uint32_t)(0xf - p)) < best) best = sb(0x533648 + (uint32_t)(0xf - p));
    const char *t[3] = {Text_Get((int)Tod_Rally()), Text_Get(0x41), Text_Get(np && best < 6 ? 0x49 : 0x88)};
    titles(x, y, t, 3);
    char b[160];
    int i = 0;
    for (; i < np; i++) {
        place_line_text(b, sizeof b, i, sb(0x533648 + (uint32_t)Opp_PlayerSlot(i)));
        Font_Print(0, b, x, h * 10 / 0x1e0 + (h * 0x14 / 0x1e0) * i + y + Font_Height(0), C(0x519ecc), 0x11);
    }
    if (best > 5) {
        snprintf(b, sizeof b, "%s", Text_Get(Tod_Rally() == 7 ? 0xee : 0xbb));
        Text_ToFontCase(b);
        Font_Print(0, b, x, h * 10 / 0x1e0 + (h * 0x14 / 0x1e0) * i + y + Font_Height(0), C(0x519ecc), 0x11);
    }
    Font_SetLayer(2);
}

static Menu pg_rpoints, pg_result, pg_champ, pg_final, pg_final2, pg_opened;   /* 0x53e6a0, 0x540118, 0x541ae0, 0x53f790, 0x53f970, 0x540e50 */

/* 0x452be0: what the stage opened, for each player: a car (0x7f with the car's name, flag 0x80) or
   else a bonus stage (0xad's form of 0xad + its number, flag 0x2000), the arcade's next level (0xbf
   with 5 or 7, flags 0x1800), its cars (0xc1, 0x300) and its other track (0xc0, 0x400). The first
   line is placed by the player alone, the others by the lines so far and the player (as the
   original). */
static void draw_opened(Menu *m)
{
    (void)m;
    int w, h;
    screen(&w, &h);
    dim();
    int x = w * 0x1e / 0x280, y0 = h * 0xf2 / 0x1e0, lines = 0;
    Font_Print(2, Text_Get(0x49), x, y0, C(0x519ecc), 0x11);
    int fh = Font_Height(0), step = h * 10 / 0x1e0 + fh;
    char b[160];
    for (int p = 0; p < (int)Gs_SelBits14(); p++) {
        uint32_t f = gm32(0x537f68 + 4u * (uint32_t)p);
        bool first = false;
        if (f & 0x80) {
            snprintf(b, sizeof b, Text_Get(0x7f), car_name((int)gm32(0x537f78 + 4u * (uint32_t)p)));
            first = true;
        } else if (f & 0x2000) {
            snprintf(b, sizeof b, Text_Get(0xad), Text_Get(0xad + (int)gm32(0x537f0c + 4u * (uint32_t)p)));
            first = true;
        }
        if (first) {
            Text_ToFontCase(b);
            Font_Print(0, b, x, h * 5 / 0x1e0 + fh + y0 + step * p, C(0x519ecc), 0x11);
            lines++;
        }
        static const struct { uint32_t mask; int16_t text; } more[3] = {{0x1800, 0xbf}, {0x300, 0xc1}, {0x400, 0xc0}};
        for (int k = 0; k < 3; k++) {
            if (!(f & more[k].mask)) continue;
            if (k == 0) snprintf(b, sizeof b, Text_Get(0xbf), Text_Get(f & 0x800 ? 5 : 7));
            else snprintf(b, sizeof b, "%s", Text_Get(more[k].text));
            Text_ToFontCase(b);
            Font_Print(0, b, x, h * 5 / 0x1e0 + fh + y0 + step * lines + step * p, C(0x519ecc), 0x11);
            lines++;
        }
    }
}

/* 0x451690: the championship's final: on 0x53f790 the level's championship (0x8e + level) and each
   player's points place; on 0x53f970 only the screen dimmed (not in the super special) */
static void draw_final(Menu *m)
{
    int w, h;
    screen(&w, &h);
    if (m != &pg_final) {
        if (!Gs_SuperSpecial()) dim();
        return;
    }
    dim();
    int x = w * 0x1e / 0x280, y = h * 0xf2 / 0x1e0;
    const char *t[2] = {Text_Get(0x42), Text_Get(0x8e + (int)Gs_Mode2())};
    titles(x, y, t, 2);
    char b[160];
    for (int i = 0; i < (int)Gs_Players(); i++) {
        place_line_text(b, sizeof b, i, sb(0x533548 + (uint32_t)Opp_PlayerSlot(i)));
        Font_Print(0, b, x, y + (h * 10 / 0x1e0 + Font_Height(0)) * (i + 1), C(0x519ecc), 0x11);
    }
}

/* 0x449ba0 for the rally's lists: the list starts five above the best player's times place (15
   down); on the championship's table (0x541ae0) at the times place of the player with the most
   points (as the original) */
static void rally_enter(Menu *m, bool back)
{
    (void)back;
    int best = 0x10, most = 0;
    for (int p = 0; p < (int)Gs_Players(); p++) {
        if (m == &pg_champ) {
            int pt = gms32(0x533558 + 4u * (uint32_t)Opp_PlayerSlot(p)) >> 16;
            if (most < pt) most = pt, best = sb(0x533648 + (uint32_t)Opp_PlayerSlot(p));
            continue;
        }
        int pl = sb(0x533648 + (uint32_t)(0xf - p));
        if (pl < best) best = pl;
    }
    if (best > 5) m->items[0].value = (uint8_t)(best - 5);
    if (m->items[0].value > 10) m->items[0].value = 10;
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

/* 0x449fe0, the continue fade's end: every process on from the replay, out of the menus */
void Results_ContinueDone(int ch)
{
    (void)ch;
    Race_EndReplay();
    gw32(0x538118, 0);   /* 0x41f390 */
    Input_SetMenuMode(false);
}

/* 0x44a000 (not the network game's): every car's fade, the first calling back */
static void continue_fn(Menu *m, MenuItem *it)
{
    (void)m, (void)it;
    for (int i = 0; i < Race_Cars(); i++) Fade_OutIn(i, i ? NULL : Results_ContinueDone, 1, 0, (int32_t)gm32(0x519ec8), true);
}

/* 0x449b80: the championship's end goes on at once */
static void final_enter(Menu *m, bool back) { (void)back; continue_fn(m, NULL); }

/* 0x449b90: the last item first */
static void last_enter(Menu *m, bool back) { (void)back; m->sel = (int8_t)(m->n - 1); }

/* 0x44af70: the choices after `parent`. Param 0 (single stage, time trial): restart, at the service
   area in those, quit. Param 1 (a rally): on (0x1f; 0x80 at a championship rally's end once every
   player has driven; 0x95 in the arcade; with the next player's name, 0x21, in alternation before the
   last), restart (the question 0x5402f8) and quit (the question 0x5418d8). The restart items' text is
   the mode's (Results_Start). */
static void build_end(int param, Menu *parent, int16_t restart_text)
{
    Menu_Init(&pg_end, NULL, 0, parent, false, NULL, 1, 0, 1);
    if (param == 0) {
        Menu_AddPage(&pg_end, NULL, restart_text, NULL, restart_fn, 1000);
        if (mode() == 2 || mode() == 3) Menu_AddPage(&pg_end, NULL, 0x8b, NULL, service_fn, 0x3e9);
        Menu_AddPage(&pg_end, NULL, 0xa8, NULL, quit_fn, -1);
    } else {
        int16_t text = 0x1f, id = -1;
        if (Gs_ModeBit17() && Race_Base() != (int)Gs_Players() - 1) text = 0x21, id = 0x3ea;
        else if (last_stage && last_player && mode() == 0) text = 0x80;
        else if (mode() == 5) text = 0x95;
        Menu_AddAction(&pg_end, NULL, text, continue_fn, id);
        Menu_AddPage(&pg_end, NULL, restart_text, &pg_restart, NULL, 1000);
        Menu_AddPage(&pg_end, NULL, 0x2d, &pg_quit, NULL, -1);
    }
    Menu_SetCallbacks(&pg_end, NULL, NULL, draw_end, NULL);
    Menu_Reset(&pg_end);
}

/* 0x449100's questions: restart (0x5402f8: restart, at the service area in the single stage and the
   time trial, back) and quit (0x5418d8: quit, back), each from its last item */
static void build_questions(int16_t restart_text)
{
    Menu_Init(&pg_restart, NULL, 0, &pg_end, false, NULL, 1, 0, 1);
    Menu_AddPage(&pg_restart, NULL, restart_text, NULL, restart_fn, 1000);
    if (mode() == 2 || mode() == 3) Menu_AddPage(&pg_restart, NULL, 0x8b, NULL, service_fn, 0x3e9);
    Menu_AddBack(&pg_restart, NULL, 0xa6, NULL, -1);
    Menu_SetCallbacks(&pg_restart, last_enter, NULL, draw_end, NULL);
    Menu_Reset(&pg_restart);
    Menu_Init(&pg_quit, NULL, 0, &pg_end, false, NULL, 1, 0, 1);
    Menu_AddPage(&pg_quit, NULL, 0xa8, NULL, quit_fn, 1000);
    Menu_AddBack(&pg_quit, NULL, 0xaa, NULL, -1);
    Menu_SetCallbacks(&pg_quit, last_enter, NULL, draw_end, NULL);
    Menu_Reset(&pg_quit);
}

/* 0x449100's end-of-rally pages and 0x44a1b0's way through them in a rally: at a stage before the
   last the rally's times on to the choices; at the last (once every player has driven) in the
   championship the rally's points, its result and the championship's table, the choices to go on
   when a player was in the first six (else only restart or quit), after the last rally the
   championship's final (0x53f790) on to its end (0x53f970, which goes on at once); in a single rally
   the result when a player was in the first six, then restart or quit */
static void champ_end(int best);

/* 0x44a1b0's first loop in the arcade (as for the record: not in a challenge, without the cheats'
   options or a recovery's penalty, nor for "cmr"): a player with a time who opened something (0x41bf50:
   bits 7, 13, 11-12, 8-9, 10) */
static bool arcade_opened(void)
{
    if (mode() == 4 || gm8(0x52afc2) || (gm32(0x52afbc) >> 0xb & 1) || !strncmp(player_name(0), exe_str(0x519ec4), 3)) return false;
    bool any = false;
    for (int p = 0; p < (int)Gs_SelBits14(); p++)
        if (Times_Finish(p) > 0 && (gm32(0x537f68 + 4u * (uint32_t)p) & 0x3f80)) any = true;
    return any;
}

/* 0x44a1b0's end: the page of what was opened (0x540e50) put after `after` */
static void insert_opened(Menu *after)
{
    Menu_Init(&pg_opened, NULL, 0, after, false, NULL, 1, 0, 0);
    Menu_AddPage(&pg_opened, NULL, 0, after->items[0].target, NULL, 0);
    Menu_SetCallbacks(&pg_opened, NULL, NULL, draw_opened, NULL);
    Menu_Reset(&pg_opened);
    if (after->items[0].target) after->items[0].target->parent = &pg_opened;
    after->items[0].target = &pg_opened;
}
static void build_rally_end(void)
{
    Menu_Init(&pg_rpoints, NULL, 0, &pg_rally, false, NULL, 1, 0, 0);
    Menu_AddList(&pg_rpoints, NULL, 0, 0xb, 0, false, to_target, 0);
    Menu_SetCallbacks(&pg_rpoints, rally_enter, NULL, draw_rally_points, NULL);
    Menu_Reset(&pg_rpoints);
    Menu_Init(&pg_result, NULL, 0, &pg_rpoints, false, NULL, 1, 0, 0);
    Menu_AddPage(&pg_result, NULL, 0, NULL, NULL, 0);
    Menu_SetCallbacks(&pg_result, NULL, NULL, draw_rally_result, NULL);
    Menu_Reset(&pg_result);
    Menu_Init(&pg_champ, NULL, 0, &pg_result, false, NULL, 1, 0, 0);
    Menu_AddList(&pg_champ, NULL, 0, 0xb, 0, false, to_target, 0);
    Menu_SetCallbacks(&pg_champ, rally_enter, NULL, draw_champ, NULL);
    Menu_Reset(&pg_champ);
    Menu_Init(&pg_final2, NULL, 0, &pg_final, false, NULL, 1, 0, 0);
    Menu_AddPage(&pg_final2, NULL, 0, NULL, continue_fn, 0);
    Menu_SetCallbacks(&pg_final2, final_enter, NULL, draw_final, NULL);
    Menu_Reset(&pg_final2);
    Menu_Init(&pg_final, NULL, 0, &pg_champ, false, NULL, 1, 0, 0);
    Menu_AddPage(&pg_final, NULL, 0, &pg_final2, NULL, 0);
    Menu_SetCallbacks(&pg_final, NULL, NULL, draw_final, NULL);
    Menu_Reset(&pg_final);
    pg_rally.items[0].target = &pg_end;
    if (!last_stage || !last_player) {
        /* in alternation before the last player the times (or the record's page) go straight on to
           the choices, without the board and the rally's times */
        if (Gs_ModeBit17() && !last_player) {
            Menu *after = pg_times.items[0].target == &pg_splits ? &pg_splits : &pg_times;
            after->items[0].target = &pg_end;
            build_end(1, after, 0x25);
        } else
            build_end(1, &pg_rally, 0x25);
        return;
    }
    int best = 99;
    for (int p = 0; p < (int)Gs_Players(); p++)
        if (sb(0x533648 + (uint32_t)(0xf - p)) < best) best = sb(0x533648 + (uint32_t)(0xf - p));
    if (mode() == 1) {
        if (Gs_Players() && best <= 5) pg_rally.items[0].target = &pg_result, pg_result.parent = &pg_rally, pg_result.items[0].target = &pg_end;
        build_end(0, &pg_rally, 0x25);
    } else
        champ_end(best);
    /* a car or bonus stage opened: its page after the rally's result (the arcade's after 0x53e888) */
    bool opened = false;
    for (int p = 0; p < (int)Gs_SelBits14(); p++) opened |= (gm32(0x537f68 + 4u * (uint32_t)p) & 0x2080) != 0;
    if (opened) insert_opened(&pg_result);
}

/* the championship's way from the rally's times at its last stage */
static void champ_end(int best)
{
    pg_rally.items[0].target = &pg_rpoints;
    pg_rpoints.items[0].target = &pg_result;
    pg_result.items[0].target = &pg_champ;
    if (Tod_Rally() == 7 && best < 6) {
        gw32(0x541cf0, 0);   /* the super special's reveal */
        pg_champ.items[0].target = &pg_final;
        build_end(0, &pg_final, 0x25);
    } else if (best > 5) {
        pg_result.items[0].target = &pg_end;
        build_end(0, &pg_result, 0x25);
    } else {
        pg_champ.items[0].target = &pg_end;
        build_end(1, &pg_champ, 0x25);
    }
}

/* 0x44a1b0's records (not in a challenge, with the cheats' options, after a recovery's penalty, nor
   for the "cmr" driver; 0x4481b0 the lap clock's cap): for each player with a time, outside a circuit
   the stage's time, a rally's stage wins, the stage's best, the stage's record (the last such player's
   answer is kept) and the session's best (the ghost's, into *best); at the arcade's last race its
   result, best and table. Then, once the last player has driven, at a rally's last stage the rally's
   result (in the championship its sums), best and table, after the championship's last rally its
   result, best and table. */
static bool take_records(bool *best)
{
    bool record = false;
    *best = false;
    if (mode() == 4 || gm8(0x52afc2) || (gm32(0x52afbc) >> 0xb & 1) || Hud_LapCapped() || !strncmp(player_name(0), exe_str(0x519ec4), 3))
        return false;
    for (int i = 0; i < (int)Gs_SelBits14(); i++) {
        int id = Race_Base() + i;
        int32_t t = Times_Finish(i);
        if (t <= 0) continue;
        if (!Gs_SelBit24()) {
            int place = Opp_Place(id);
            Rec_SetTime(id, t);
            if (mode() == 0 || mode() == 1) {
                if (Tod_Stage() == 0) Rec_RallyStart(id);
                Rec_StageWin(id, place);
            }
            Rec_BestStage(id);
            record = Rec_StageRecord(i, id, Player_Name(id));
            *best = Rec_SessionStage(id);
        }
        if (mode() == 5 && race_no() == 2) {
            Rec_SetArcade(id, (uint32_t)(int8_t)gm8(0x533618 + (uint32_t)i), (uint32_t)(gms32(0x5335b8 + 4u * (uint32_t)i) >> 16), gm32(0x5335f0 + 4u * (uint32_t)i));   /* 0x40ceb0, 0x40ced0, 0x40cef0 */
            Rec_BestArcade(id);
            Rec_ArcadeTable(id, Player_Name(id));
        }
    }
    if (!last_player) return record;
    for (int i = 0; i < (int)Gs_Players(); i++) {
        bool last = (int)Tod_Stage() == Rally_LastStage(Tod_Rally(), Gs_Mode2());
        if ((mode() == 0 || mode() == 1) && last) {
            int slot = Opp_PlayerSlot(i);
            uint32_t t = Stand_Time(slot), place = (uint32_t)Stand_TimePlace(slot);
            Rec_SetRally(i, t, place, (uint32_t)Stand_Points(slot));
            if (mode() == 0) {
                if (Tod_Rally() == 0) Rec_ChampStart(i);
                Rec_ChampAdd(i, (int32_t)t, (int)place);
            }
            Rec_BestRally(i);
            Rec_RallyTable(i, Player_Name(i));
        }
        if (mode() == 0 && Tod_Rally() == 7 && last) {
            int slot = Opp_PlayerSlot(i);
            Rec_SetChamp(i, (uint32_t)Stand_Place(slot), (uint32_t)Stand_Points(slot));
            Rec_BestChamp(i);
            Rec_ChampTable(i, Player_Name(i));
        }
    }
    return record;
}

void Results_Start(bool again)
{
    /* the stage record's holder (0x52b044 + 0x654), lower case */
    snprintf(holder, sizeof holder, "%s", (const char *)gm(0x52b044 + 0x654 + ((gm32(0x52f2ac) & 0x1f) * 0xb + (gm32(0x52f2ac) >> 5 & 0x1f)) * 8));
    Text_Lower(holder);
    /* the record's splits as they stood (0x541ab8, ten; the last the record's time) */
    for (int i = 0; i < 10; i++) gw32(0x541ab8 + 4u * (uint32_t)i, (uint32_t)Rec_Split(i));
    gw32(0x541adc, gm32(0x52b044 + 0x658 + ((gm32(0x52f2ac) & 0x1f) * 0xb + (gm32(0x52f2ac) >> 5 & 0x1f)) * 8) >> 7 & 0xffff);
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
    Menu_Init(&pg_board, NULL, 0, NULL, false, NULL, 1, 0, 0);
    Menu_AddList(&pg_board, NULL, 0, 0xb, 0, false, to_target, 0);
    Menu_SetCallbacks(&pg_board, board_enter, NULL, draw_board, NULL);
    Menu_SetCallbacks(&pg_splits, NULL, NULL, draw_record, NULL);
    /* the arcade's: the finish, the race's points, the standings and the location's end */
    Menu_Init(&pg_order, NULL, 0, NULL, false, NULL, 1, 0, 0);
    Menu_AddPage(&pg_order, NULL, 0, NULL, NULL, 0);
    Menu_SetCallbacks(&pg_order, NULL, NULL, draw_order, NULL);
    Menu_Init(&pg_points, NULL, 0, &pg_order, false, NULL, 1, 0, 0);
    Menu_AddPage(&pg_points, NULL, 0, NULL, NULL, 0);
    Menu_SetCallbacks(&pg_points, NULL, NULL, draw_order, NULL);
    Menu_Init(&pg_stand, NULL, 0, &pg_points, false, NULL, 1, 0, 0);
    Menu_AddPage(&pg_stand, NULL, 0, NULL, NULL, 0);
    Menu_SetCallbacks(&pg_stand, NULL, NULL, draw_standings, NULL);
    Menu_Init(&pg_last, NULL, 0, &pg_stand, false, NULL, 1, 0, 0);
    Menu_AddPage(&pg_last, NULL, 0, &pg_end, NULL, 0);
    Menu_SetCallbacks(&pg_last, NULL, NULL, draw_last, NULL);
    Menu_Init(&pg_alt, NULL, 0, &pg_title, false, NULL, 1, 0, 0);
    Menu_AddPage(&pg_alt, NULL, 0, &pg_end, NULL, 0);
    Menu_SetCallbacks(&pg_alt, NULL, NULL, draw_alt, NULL);
    if (mode() == 4) {
        Menu_Init(&pg_chal, NULL, 0, &pg_times, false, NULL, 1, 0, 0);
        Menu_AddPage(&pg_chal, NULL, 0, &pg_end, NULL, 0);
        Menu_SetCallbacks(&pg_chal, NULL, NULL, draw_chal, NULL);
        Menu_Reset(&pg_chal);
    }
    if (mode() >= 8) PENDING_NOTE(0x44a1b0, "the results' pages of the other modes");
    last_player = !Gs_ModeBit17() || Race_Base() == (int)Gs_Players() - 1;
    bool best, record = take_records(&best);
    if (Gs_SelBit28() && Gs_Bit30() && best) Ghost_TakeFrom(0);   /* the session's best into the ghost */
    snprintf(record_name, sizeof record_name, "%s", (const char *)gm(0x52b044 + 0x654 + ((gm32(0x52f2ac) & 0x1f) * 0xb + (gm32(0x52f2ac) >> 5 & 0x1f)) * 8));
    /* with a new record the times go on to the congratulations, else to the board */
    if (record) pg_times.items[0].target = &pg_splits, pg_board.parent = &pg_splits;
    else pg_times.items[0].target = &pg_board, pg_board.parent = &pg_times;
    /* round a circuit (bit 24, not networked; no record is taken there) on to the finish (0x53fb70),
       from the title itself in a challenge of kind 1 or 2 */
    if (Gs_SelBit24() && !Gs_ModeBit19()) {
        if (ch_kind() == 1 || ch_kind() == 2) pg_title.items[0].target = &pg_order, pg_order.parent = &pg_title;
        else pg_times.items[0].target = &pg_order, pg_order.parent = &pg_times;
    }
    last_stage = false;
    if (mode() == 0 || mode() == 1) {
        /* a rally: the board on to the rally's times (0x53e4d8), then the choices (0x53f028) */
        Menu_Init(&pg_rally, NULL, 0, &pg_board, false, NULL, 1, 0, 0);
        Menu_AddList(&pg_rally, NULL, 0, 0xb, 0, false, to_target, 0);
        Menu_SetCallbacks(&pg_rally, rally_enter, NULL, draw_rally, NULL);
        Menu_Reset(&pg_rally);
        pg_board.items[0].target = &pg_rally;
        uint32_t l = Gs_Mode2(), r = Tod_Rally();
        int lastst = (r & 1) ? 10 : l == 0 ? 3 : l == 1 ? 7 : l == 2 ? 9 : 0;   /* 0x4071c0 */
        last_stage = (int)Tod_Stage() == lastst;
        build_rally_end();
        build_questions(0x25);
    } else if (mode() == 5) {
        /* the arcade: at the third location the finish on to the choices (restart, quit); else the
           race's points and the standings, then at the location's last race its end and the choices,
           before it the choices with "next race" (0x95); the restart's text 0x2b (0x29 at the third) */
        int16_t text = location() != 2 ? 0x2b : 0x29;
        build_end(0, NULL, text);
        if (location() == 2) pg_order.items[0].target = &pg_end, pg_end.parent = &pg_order;
        else {
            pg_order.items[0].target = &pg_points;
            pg_points.items[0].target = &pg_stand;
            if (race_no() == 2) pg_stand.items[0].target = &pg_last, pg_end.parent = &pg_last;
            else {
                pg_stand.items[0].target = &pg_end;
                build_end(1, &pg_stand, text);
            }
        }
        if (arcade_opened()) insert_opened(&pg_stand);
        build_questions(text);
    } else if (mode() == 4) {
        /* the challenge: the match noted and ended (0x472a30), the title on to its times, then its
           result, then the choices: on while a player is still in the round (0x4728e0), unless the
           final's matches have all been run */
        ch_match = Challenge_Match();
        Challenge_MatchEnd();
        pg_title.items[0].target = &pg_times;
        pg_times.parent = &pg_title;
        pg_times.items[0].target = &pg_chal;
        uint32_t r = gm32(0x52f2b4);
        build_end(!(r & 0x400000) || (r & 0x38) < 0x20 ? Challenge_PlayersLeft() : 0, &pg_chal, 0x27);
        build_questions(0x27);
    } else if (mode() == 6 || mode() == 7) {
        build_end(0, &pg_order, 0x29);
        pg_order.items[0].target = &pg_end;
        build_questions(0x29);
    } else if (mode() == 2 || mode() == 3) {
        /* the single stage: the board on to the choices (0x53e4d8); the time trial: the times (or the
           record's page) on to them. In alternation before the last player the choices go on to the
           next; at the last the single stage's board goes to the choices without building them again
           (the last player's keep the one before's, as in the original), the time trial's to the
           players' times in order (0x53f3c8) first. */
        Menu *after = pg_times.items[0].target == &pg_splits ? &pg_splits : &pg_times;
        if (mode() == 3 && Gs_ModeBit17()) {
            alt_time[Race_Base() & 3] = Times_Split(0, Sectors_Shown());
            alt_who[Race_Base() & 3] = (uint8_t)Race_Base();
        }
        if (!Gs_ModeBit17()) {
            Menu *p = mode() == 2 ? &pg_board : after;
            p->items[0].target = &pg_end;
            build_end(0, p, 0x23);
        } else if (!last_player) {
            after->items[0].target = &pg_end;
            build_end(1, after, 0x23);
        } else if (mode() == 2) {
            pg_board.items[0].target = &pg_end;
            pg_end.parent = &pg_board;
        } else {
            alt_sort();
            after->items[0].target = &pg_alt;
            pg_alt.parent = after;
            build_end(0, &pg_alt, 0x23);
        }
        build_questions(0x23);
    } else {
        build_end(0, &pg_board, 0x23);
        build_questions(0x23);
        pg_board.items[0].target = &pg_end;
    }
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
    /* (the race's step polled the records, 0x49eab0) */
    Ctl_AxisPresses();   /* 0x40bad0 */
    Ctl_Merge(0, Input_Record(0));
    uint32_t b = Input_Record(0)->pressed;
    if (Gs_Mode() == 0 && (gm8(0x52f2ac) & 0x1f) == 8 && (gm32(0x52f2ac) & 0x3e0) == 0x160) b &= 0x10;   /* 0x407270 */
    if (page == &pg_title) b &= 0xffdf;
    /* the list pages: only the keys 0x3c pass; up (4) or down (8) not at that end of the list starts
       the scroll (curve 2 over 7 steps), its value (0x541cf4) the share of a row. (0x448e70 also
       notes the time of a page change in 0x53fd50, never read.) */
    if (page == &pg_rally || page == &pg_board || page == &pg_rpoints || page == &pg_champ) {
        uint32_t keys = b;
        b &= 0x3c;
        MenuItem *it = &page->items[0];
        if ((keys & 4) && it->value) {
            Tween_Start(0x5418c0, 2, 7, 0, 0x10000, 0, false);   /* 0x4bc290 */
            scroll_up = 1;
        } else if ((keys & 8) && it->value < it->count - 1) {
            Tween_Start(0x5418c0, 2, 7, 0, 0, 0x10000, false);
            scroll_dn = 1;
        }
        if (!Tween_Active(0x5418c0)) scroll_dn = scroll_up = scroll_off = 0;   /* 0x4bc0c0 */
        else {
            int32_t v = Tween_Value(gm8(0x5418c0));   /* 0x4bc110 */
            int sw, sh;
            screen(&sw, &sh);
            scroll_off = (int32_t)(((int64_t)((sh * 0x34 / 0x1e0) * 0x10000) * v) >> 16) >> 16;
        }
    } else
        scroll_dn = scroll_up = scroll_off = 0;
    Menu *to = Menu_Input(page, b);
    if (to) next = to;
}

/* 0x44a150 (state 10 of the super special, once its replay has run): the reveal (0x541cf0, from
   0x53e880's tween, 0 to 1 over 200 steps) started once and followed */
void Results_SuperSpecialTick(void)
{
    if (!Tween_Active(0x53e880) && !gm32(0x541cf0)) Tween_Start(0x53e880, 0, 200, 0, 0, 0x10000, true);   /* 0x4bc290 */
    if (Tween_Active(0x53e880)) gw32(0x541cf0, (uint32_t)Tween_Value(gm8(0x53e880)));   /* 0x4bc0c0, 0x4bc110 */
}

/* 0x44b3a0: the super special's lines over the page as the reveal passes a quarter, a half and three
   quarters (or once it is over): the title by the difficulty (text 0x8e + 0x52afb4 bits 10-12); a
   2-pixel bar and text 0x93 after it; each player's name (the full one when it has one under 30
   letters) with " - " and the place (text 0x51-0x53, else 0x54 with the place), in capitals, a line
   apart below */
static void super_special(void)
{
    int w, h;
    screen(&w, &h);
    int x = w * 0x1e / 0x280, y = h * 0xf2 / 0x1e0;
    int32_t t = gms32(0x541cf0);
    if (t < 1) return;
    bool on = Tween_Active(0x53e880);
    static char b[256];   /* 0x663b60 (the original prints each text as its format) */
    if (t > 0x4000 || !on) {
        snprintf(b, sizeof b, "%s", Text_Get((int16_t)((gm32(0x52afb4) >> 10 & 7) + 0x8e)));   /* 0x405d90 */
        Font_Print(2, b, x, y, C(0x519ecc), 0x11);
    }
    if (t > 0x8000 || !on) {
        int bx = x + Font_TextWidth(2, b) + (w << 3) / 0x280;
        int16_t r[4] = {(int16_t)bx, (int16_t)(h * 4 / 0x1e0 + h * 0xf2 / 0x1e0 - Font_Height(2)), 2, (int16_t)(h * 0x29 / 0x1e0)};
        Rect_Fill(r, C(0x519ecc), 2);   /* (at 0x541cc0) */
        snprintf(b, sizeof b, "%s", Text_Get(0x93));
        Font_Print(2, b, bx + (w << 3) / 0x280, y, C(0x519ecc), 0x11);
    }
    if (t < 0xc001 && on) return;
    for (int i = 0; i < (int)Gs_Players(); i++) {
        int place = Stand_Place(Opp_PlayerSlot(i));   /* 0x455de0, 0x40d060 */
        /* (without a profile 0x408470 gives NULL, which the original's length reads) */
        const char *full = Player_FullName(i), *n = full && *full && strlen(full) <= 0x1d ? full : Player_Name(i);
        snprintf(b, sizeof b, "%s", n);
        size_t l = strlen(b);
        snprintf(b + l, sizeof b - l, "%s", exe_str(0x519f44));
        l = strlen(b);
        if (place < 3) snprintf(b + l, sizeof b - l, "%s", Text_Get((int16_t)(0x51 + place)));
        else snprintf(b + l, sizeof b - l, Text_Get(0x54), place + 1);
        Text_ToFontCase(b);
        Font_Print(0, b, x, y + (h * 10 / 0x1e0 + Font_Height(0)) * (i + 1), C(0x519ecc), 0x11);
    }
}

/* 0x44b330: the page, dimmed alone for two frames after a change */
void Results_Draw(void)
{
    if (!active) return;
    if (shown == page || page == &pg_title) {
        if (settle < 1) {
            Menu_Draw(page);   /* 0x4a0550 */
            if (Gs_SuperSpecial()) super_special();
            return;
        }
    } else {
        shown = page;
        settle = 2;
    }
    dim();
    settle--;
}
