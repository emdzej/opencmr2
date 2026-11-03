/* The network pages' draws (0x4dc7b0-0x4dd4b0): the connections, the sessions found, the host's
   set-up and the lobby. Positions are the original's fractions of 640 x 480. */
#include "fe/fe.h"
#include "fe/fe_net.h"
#include "fe/fe_page.h"
#include "fe/fe_res.h"
#include "fe/fe_strip.h"
#include "fe/pages.h"
#include "exe.h"
#include "font.h"
#include "game/gstate.h"
#include "game/mem.h"
#include "game/profile.h"
#include "game/timer.h"
#include "net/records.h"
#include "net/session.h"
#include "pending.h"
#include "render/d3d7.h"
#include "render/twod.h"
#include "sim/car.h"
#include "text.h"
#include "world/tod.h"
#include <stdio.h>
#include <string.h>

static const uint8_t *C(uint32_t va) { return gm(va); }
enum { LIT = 0x524968, PLAIN = 0x52496c, OFF = 0x524970, LINE_LIT = 0x524974, LINE = 0x524978, BUF = 0x663b60 };
static char *buf(void) { return (char *)gm(BUF); }
static const char *mode_name(int k) { return Text_Get(k + 0x1f1); }   /* 0x4f8a70 */
static const char *car_name(uint32_t car) { return Text_Get((int)car + 0xae); }   /* 0x40ee20 (in the front end) */

/* the page's title row: "Main Menu" and the page */
static void titles(int16_t text)
{
    int w, h;
    D3d_ScreenSize(&w, &h);
    const char *names[2] = {Text_Get(0x12), Text_Get(text)};
    Fe_TitleRow(w * 0x18 / 0x280, h * 0x26 / 0x1e0, names, 2);
}

/* 0x4dc7b0: the connections, centred, the chosen lit */
void fe_4dc7b0(Menu *m)
{
    int w, h;
    D3d_ScreenSize(&w, &h);
    Fe_Header();
    titles(0x3c);
    Strip_DrawMain(&pg_81d6d8, false, NULL);   /* 0x4f8410 */
    int n = m->items[0].count, row = h * 0x14 / 0x1e0;
    int y = (h * 8 / 0x1e0 + h * 0x26 / 0x1e0 + h * 0x180 / 0x1e0) / 2 - row * n / 2;
    for (int i = 0; i < n; i++, y += row) {
        const char *s = Net_ConnectionName(i);
        Font_Print(1, s ? s : "", w / 2, y, C(i == m->items[0].value ? LIT : PLAIN), 0x12);
    }
}

/* 0x4dc930: the sessions: the headings (name, mode, players, the most), the ones shown, the items */
void fe_4dc930(Menu *m)
{
    int w, h;
    D3d_ScreenSize(&w, &h);
    Fe_Header();
    titles(0x35);
    bool listing = gm8(0x819014);
    uint32_t head = listing ? LIT : PLAIN, chosen = listing ? LIT : OFF, other = listing ? PLAIN : OFF;
    int hy = h * 0x37 / 0x1e0;
    Font_Print(1, Text_Get(0x7f), w * 100 / 0x280, hy, C(head), 10);
    Font_Print(1, Text_Get(0x1a3), w * 300 / 0x280, hy, C(head), 10);
    Font_Print(1, Text_Get(0x20e), w * 0x1c2 / 0x280, hy, C(head), 10);
    Font_Print(1, Text_Get(0x20f), w * 0x226 / 0x280, hy, C(head), 10);
    int32_t top = gms32(0x81901c), shown = gms32(0x819020);
    if (gm8(0x818ef4))
        for (int i = top, row = 0; i < shown + top; i++, row++) {
            const uint8_t *c = C(gms32(0x525288) == i ? chosen : other);
            int y = h * 100 / 0x1e0 + h * 0x14 / 0x1e0 * row;
            const char *name = Net_FoundName(i);
            Font_Print(1, name ? name : "", w * 100 / 0x280, y, c, 10);
            Font_Print(1, mode_name((int)Net_FoundUser(i, 0) - 8), w * 300 / 0x280, y, c, 10);
            snprintf(buf(), 0x100, exe_str(0x517dd0), (int)Net_FoundPlayers(i));
            Font_Print(1, buf(), w * 0x1c2 / 0x280, y, c, 10);
            snprintf(buf(), 0x100, exe_str(0x517dd0), (int)Net_FoundMaxPlayers(i));
            Font_Print(1, buf(), w * 0x226 / 0x280, y, c, 10);
        }
    List_Draw(m, NULL, h * 0x140 / 0x1e0, -1, 0, true);
    Strip_DrawMain(&pg_81d6d8, false, NULL);
}

/* the blinking cursor after a field being typed into (0x524d60), half of each fifth of a second */
static void cursor(bool on)
{
    if (on && (int)(Timer_Centis() % 0x14) > 9) strcat(buf(), exe_str(0x524d60));
}

/* 0x4dce00's rows: the name, the mode, the time limit, the players, the password, then the items' texts */
static void host_row(Menu *m, MenuItem *it, bool two, int x, int y, uint32_t c, int w)
{
    (void)two, (void)w;
    bool on = &m->items[m->sel] == it;
    switch (it->id) {
    case 0:
        snprintf(buf(), 0x100, exe_str(0x524d38), Text_Get(0x1bf), (const char *)gm(0x818ebc));
        cursor(on);
        break;
    case 1:
        snprintf(buf(), 0x100, exe_str(0x524d90), Text_Get(0x1c0), mode_name(Menu_Item(m, 1)->value));
        break;
    case 2: {
        uint8_t v = Menu_Item(m, 2)->value;
        if (!v) snprintf(buf(), 0x100, exe_str(0x524d78), Text_Get(0x1c1));
        else snprintf(buf(), 0x100, exe_str(0x524d84), Text_Get(0x1c1), v - 1);
        break;
    }
    case 3:
        snprintf(buf(), 0x100, exe_str(0x524d6c), Text_Get(0x1c2), Menu_Item(m, 3)->value + 2);
        break;
    case 4:
        snprintf(buf(), 0x100, exe_str(0x524d64), Text_Get(0x1c3), (const char *)gm(0x818ef8));
        cursor(on);
        break;
    case 5:
        Font_Print(1, Text_Get(0x1c4), x, y, C(c), 0x11);
        return;
    default:
        Font_Print(1, Text_Get(it->text), x, y, C(c), 0x11);
        return;
    }
    Font_Print(1, buf(), x, y, C(c), 0x11);
}

/* 0x4dce00: the host's set-up, as the option pages' rows */
void fe_4dce00(Menu *m)
{
    int w, h;
    D3d_ScreenSize(&w, &h);
    Fe_Header();
    Fe_Titles(m, w * 0x18 / 0x280, h * 0x26 / 0x1e0, true, 2, NULL, -1);
    Rows_Draw(m, host_row, false, true, Rows_Top(m), Rows_ArrowDy());
    Strip_DrawMain(&pg_81d6d8, false, NULL);
}

/* the lobby's line of a player: "name car (box)" (0x524da8; 0x524db8 with the car flag, else 0x524db4) */
static void player_line(const char *name, uint32_t car, bool flag)
{
    snprintf(buf(), 0x100, exe_str(0x524da8), name, car_name(car), exe_str(flag ? 0x524db8 : 0x524db4));
}

/* 0x4dd4b0: the lobby: the titles (the mode, its rally, stage or circuit, host or guest), the chat
   box and its five lines and the line typed, the players at the right (the ready ones lit), the rows
   from item 1 (the car, the box, ready or start, back) */
void fe_4dd4b0(Menu *m)
{
    static const int32_t circuit[8] = {6, 3, 1, 4, 0, 2, 5, 7};
    static const uint32_t stage_names[11] = {0x524dc8, 0x516934, 0x516930, 0x516948, 0x524dc4, 0x51693c,
                                             0x516928, 0x516938, 0x51692c, 0x524dc0, 0x524dbc};
    int w, h;
    D3d_ScreenSize(&w, &h);
    char where[0x100] = "";
    switch (Gs_Mode()) {
    case 8:
        snprintf(where, sizeof where, exe_str(0x516180), Text_Get((int)(Tod_Rally() & 0xff) + 0x27));
        break;
    case 9: case 10:
        snprintf(where, sizeof where, exe_str(0x516940), Text_Get((int)(Tod_Rally() & 0xff) + 0x27),
                 exe_str(stage_names[Tod_Stage() & 0xff]));
        break;
    case 11: case 12: {
        uint32_t loc = gm32(0x52f2ac) >> 10 & 3, race = gm32(0x52f2ac) >> 12 & 3;
        snprintf(where, sizeof where, exe_str(0x516180), Text_Get(circuit[loc * 3 + race] + 0x27));
        break;
    }
    }
    const char *names[4] = {Text_Get(0x12), mode_name((int)(Gs_Mode() & 0xff) - 8), where, Text_Get(Net_IsHost() ? 0x3d : 0x1c5)};
    Fe_TitleRow(w * 0x18 / 0x280, h * 0x26 / 0x1e0, names, 4);
    bool chatting = gm8(0x818ed0);
    uint8_t box[4] = {0xff, 0xff, 0xff, chatting ? 0x20 : 0x10};
    uint32_t chat_c = chatting ? LIT : PLAIN;
    Fe_Header();
    int16_t r[4] = {(int16_t)(w * 0x14 / 0x280), (int16_t)(h * 0x37 / 0x1e0), (int16_t)(w * 0x1a9 / 0x280), (int16_t)(h * 0x6e / 0x1e0)};
    Rect_Fill(r, box, 1);
    r[1] = (int16_t)(r[1] + h * 10 / 0x1e0 + r[3]);
    r[3] = (int16_t)(h * 0x1e / 0x1e0);
    Rect_Fill(r, box, 1);
    /* the players: the local one first (the host's and a ready guest's lit) */
    int px = w - w * 0x14 / 0x280;
    player_line(Player_Name(0), Player_Car(0), Champ_CarFlag(0));
    Font_Print(1, buf(), px, h * 0x32 / 0x1e0, C(Net_IsHost() || gm8(0x818ce4) ? LIT : PLAIN), 0xc);
    for (int i = 0, n = 1; i < 7; i++) {
        if (n >= 8 || !NetRec_Used(i)) continue;
        const char *nm = NetRec_Name(i);
        player_line(nm ? nm : (const char *)gm(0x52ea5c), NetRec_Car(i), NetRec_Bit5(i));
        Font_Print(1, buf(), px, h * 0x32 / 0x1e0 + h * 0x28 / 0x1e0 * n, C(NetRec_Bit6(i) ? LIT : PLAIN), 0xc);
        n++;
    }
    /* the chat lines, the oldest at the top, and the line typed */
    int row = 0;
    for (int k = 4; k >= 0; k--, row++)
        Font_Print(1, (const char *)gm(gm32(0x817c84 + 4u * (uint32_t)k)), w * 0x23 / 0x280, (row + 3) * h * 0x14 / 0x1e0, C(chat_c), 9);
    if (chatting) {
        snprintf(buf(), 0x100, exe_str(Fe_Now() % 0x14 < 10 ? 0x516180 : 0x519fb8), Fe_NetChatLine());
        Font_Print(1, buf(), w * 0x23 / 0x280, (row - 1 + 5) * h * 0x14 / 0x1e0, C(LIT), 9);
    }
    /* the rows, from item 1, under the chat box */
    int top = h * 0x28 / 0x1e0 + r[1], line = h * 0x24 / 0x1e0;
    int16_t lr[4] = {(int16_t)(w * 99 / 0x280), (int16_t)top, (int16_t)(w * 0x11a / 0x280), 1};
    Rect_Fill(lr, C(m->sel == 1 ? LINE_LIT : LINE), 1);
    lr[1]++;
    Rect_Fill(lr, C(m->sel == 1 ? LIT : PLAIN), 1);
    Tex *aa = g_fe.arrow_a;
    int16_t ah = aa ? (int16_t)aa->height : 0, aw = aa ? (int16_t)aa->width : 0;
    for (int k = 0; k < m->n - 1; k++) {
        MenuItem *it = &m->items[k + 1];
        bool on = m->sel == k + 1;
        Tex *t = on ? g_fe.arrow_a : g_fe.arrow_d;
        uint32_t c = on ? LIT : (it->flags & 1) ? PLAIN : OFF;
        if (t) {
            Rect16 src = {0, 0, t->width, t->height};
            Rect16 dst = {(int16_t)(w * 100 / 0x280), (int16_t)(h * 2 / 0x1e0 + h * 0x12 / 0x1e0 + top + line * k - ah / 2), aw, ah};
            Sprite_Add(&src, &dst, t, 1, 0, NULL, NULL, C(c), 8);
        }
        int x = w * 0x7a / 0x280, y = h * 0x18 / 0x1e0 + lr[1];
        switch (it->id) {
        case 0:
            Font_Print(1, Text_Get(0x1c6), x, y, C(c), 0x11);
            break;
        case 1:
            snprintf(buf(), 0x100, exe_str(0x524d9c), Text_Get(0x87), Text_Get((int)gm32(0x818d18 + 4u * Menu_Item(m, 1)->value) + 0x98));
            Font_Print(1, buf(), x, y, C(c), 0x11);
            break;
        case 2:
            snprintf(buf(), 0x100, exe_str(0x524d9c), Text_Get(0x88), Text_Get(Menu_Item(m, 2)->value + 0x131));
            Font_Print(1, buf(), x, y, C(c), 0x11);
            break;
        case 3:
            if (Net_IsHost()) {
                Font_Print(1, Text_Get(0x3f), x, y, C(c), 0x11);
                break;
            } else {
                /* "Ready" then yes and no, the one set lit */
                bool ready = gm8(0x818ce4) != 0;
                int w0 = Font_TextWidth(1, Text_Get(0x1c7)), w1 = Font_TextWidth(1, Text_Get(5));
                Font_Print(1, Text_Get(0x1c7), x, y, C(c), 0x11);
                Font_Print(1, Text_Get(5), w * 10 / 0x280 + x + w0, y, C(ready ? LIT : PLAIN), 0x11);
                Font_Print(1, Text_Get(4), w * 0x14 / 0x280 + x + w0 + w1, y, C(ready ? PLAIN : LIT), 0x11);
            }
            break;
        case 4:
            Font_Print(1, Text_Get(it->text), x, y, C(c), 0x11);
            break;
        }
        bool lit = m->sel == k + 2 || m->sel == k + 1;
        lr[1] = (int16_t)(line * (k + 1) + top);
        Rect_Fill(lr, C(lit ? LINE_LIT : LINE), 1);
        lr[1]++;
        Rect_Fill(lr, C(lit ? LIT : PLAIN), 1);
    }
    Strip_DrawMain(&pg_81d6d8, false, NULL);
}

/* the stage list's labels (0x5250d0 down to 0x5250ac by fours, then 0x524dbc) */
static const char *stage_label(uint32_t k) { return exe_str(k < 10 ? 0x5250d0 - 4 * k : 0x524dbc); }

/* 0x4e77c0's rows: the stages (each lit when on; on the row chosen, the one at the cursor brighter),
   the rally, then the items' texts */
static void rally_row(Menu *m, MenuItem *it, bool two, int x, int y, uint32_t c, int w)
{
    (void)two;
    switch (it->id) {
    case 0: {
        bool on_row = &m->items[m->sel] == it;
        Font_Print(1, Text_Get(0x1f7), x, y, C(on_row ? LIT : PLAIN), 0x11);
        int sx = w * 10 / 0x280 + x + Font_TextWidth(1, Text_Get(0x1f7));
        MenuItem *i0 = Menu_Item(m, 0);
        for (uint32_t k = 0; k < i0->count; k++) {
            uint32_t s = (i0->count == 5 && k == 4) || (i0->count == 9 && k == 8) ? 10 : k;
            bool open = gm8(0x52e898 + s + Menu_Item(m, 1)->value * 0xbu) & 1;   /* 0x406520 */
            uint32_t sc;
            if (on_row) sc = open ? (it->value == k ? LIT : PLAIN) : (it->value == k ? PLAIN : OFF);
            else sc = open ? PLAIN : OFF;
            Font_Print(1, stage_label(s), sx, y, C(sc), 0x11);
            sx += Font_TextWidth(1, stage_label(k));   /* (the k-th label's width, as the original) */
        }
        return;
    }
    case 1:
        snprintf(buf(), 0x100, exe_str(0x524d38), Text_Get(0x30), Text_Get(Menu_Item(m, 1)->value + 0x27));
        Font_Print(1, buf(), x, y, C(c), 0x11);
        return;
    case 2: case 3:
        Font_Print(1, Text_Get(it->text), x, y, C(c), 0x11);
        return;
    }
}

/* the network set-up pages' frame (0x4e77c0-0x4e90f0 each): "Main Menu | Network | <page>", the rows
   centred on their own (a pixel off Rows_Top's and Rows_ArrowDy's at some sizes), the strip */
static void setup_page(Menu *m, int16_t text, RowFn *row_fn)
{
    int w, h;
    D3d_ScreenSize(&w, &h);
    const char *names[3] = {Text_Get(0x12), Text_Get(0x1d9), Text_Get(text)};
    Fe_TitleRow(w * 0x18 / 0x280, h * 0x26 / 0x1e0, names, 3);
    int top = (h * 8 / 0x1e0 + h * 0x26 / 0x1e0 + h * 0x180 / 0x1e0) / 2 - h * 0x24 / 0x1e0 * m->n / 2;
    Fe_Header();
    Rows_Draw(m, row_fn, false, true, top, h * 2 / 0x1e0 + h * 0x12 / 0x1e0);
    Strip_DrawMain(&pg_81d6d8, false, NULL);
}

/* "<label>: <value>" into the buffer (0x524d38) */
static void pair(int16_t label, const char *value) { snprintf(buf(), 0x100, exe_str(0x524d38), Text_Get(label), value); }

/* the damage row: "Off" alone, or "<Damage>: <n>" (0x1e0 formatting the value + 4) */
static void damage(uint8_t v)
{
    if (v == 0) { snprintf(buf(), 0x100, "%s", Text_Get(0x1e1)); return; }   /* (0x4e8500 formats it, 0x4e90f0 copies it: the same, the text has no %) */
    char t[0x40];
    snprintf(t, sizeof t, Text_Get(0x1e0), v + 4);
    pair(0x1de, t);
}

static const char *rally_text(uint8_t k) { return Text_Get(k + 0x27); }

/* 0x4e77c0: the network rally page */
void fe_4e77c0(Menu *m) { setup_page(m, 0x1da, rally_row); }

static void stages_row(Menu *m, MenuItem *it, bool two, int x, int y, uint32_t c, int w)
{
    (void)two, (void)w;
    switch (it->id) {
    case 0: pair(0x30, rally_text(Menu_Item(m, 0)->value)); break;
    case 1: pair(0x31, (const char *)gm(0x818d84 + 3u * Menu_Item(m, 1)->value)); break;
    case 2: {
        MenuItem *i2 = Menu_Item(m, 2);
        pair(0x1dc, Text_Get(i2->flags & 1 ? (int16_t)gm32(0x818d08 + 4u * i2->value) : 0x156));
        break;
    }
    case 3: case 4: Font_Print(1, Text_Get(it->text), x, y, C(c), 0x11); return;
    default: return;
    }
    Font_Print(1, buf(), x, y, C(c), 0x11);
}

/* 0x4e7ed0: the network stage page (rally, stage, weather) */
void fe_4e7ed0(Menu *m) { setup_page(m, 0x1db, stages_row); }

static void damage_row(Menu *m, MenuItem *it, bool two, int x, int y, uint32_t c, int w)
{
    (void)two, (void)w;
    switch (it->id) {
    case 0: pair(0x30, rally_text(Menu_Item(m, 0)->value)); break;
    case 1: pair(0x31, (const char *)gm(0x818d84 + 3u * Menu_Item(m, 1)->value)); break;
    case 2: damage(Menu_Item(m, 2)->value); break;
    case 3: case 4: Font_Print(1, Text_Get(it->text), x, y, C(c), 0x11); return;
    default: return;
    }
    Font_Print(1, buf(), x, y, C(c), 0x11);
}

/* 0x4e8500: the network stage page with damage */
void fe_4e8500(Menu *m) { setup_page(m, 0x1dd, damage_row); }

/* the rally of the stage pages below: the unlocked rallies' list (0x818ed4) */
static const char *open_rally(Menu *m) { return Text_Get((int16_t)gm32(0x818ed4 + 4u * Menu_Item(m, 0)->value) + 0x27); }

static void laps_row(Menu *m, MenuItem *it, bool two, int x, int y, uint32_t c, int w)
{
    (void)two, (void)w;
    switch (it->id) {
    case 0: pair(0x30, open_rally(m)); break;
    case 1: snprintf(buf(), 0x100, exe_str(0x524d40), Text_Get(0x195), Menu_Item(m, 1)->value + 1); break;
    case 2: case 3: Font_Print(1, Text_Get(it->text), x, y, C(c), 0x11); return;
    default: return;
    }
    Font_Print(1, buf(), x, y, C(c), 0x11);
}

/* 0x4e8b60: the network rallycross page (track, laps) */
void fe_4e8b60(Menu *m) { setup_page(m, 0xd7, laps_row); }

static void rx_damage_row(Menu *m, MenuItem *it, bool two, int x, int y, uint32_t c, int w)
{
    (void)two, (void)w;
    switch (it->id) {
    case 0: pair(0x30, open_rally(m)); break;
    case 1: damage(Menu_Item(m, 1)->value); break;
    case 2: case 3: Font_Print(1, Text_Get(it->text), x, y, C(c), 0x11); return;
    default: return;
    }
    Font_Print(1, buf(), x, y, C(c), 0x11);
}

/* 0x4e90f0: the network page of 0xd8 (track, damage) */
void fe_4e90f0(Menu *m) { setup_page(m, 0xd8, rx_damage_row); }

/* 0x4de170: a time in hundredths as mm:ss.hh */
static void print_time(char *b, uint32_t t) { snprintf(b, 0x100, exe_str(0x519f58), (int)t / 6000, (int)t % 6000 / 100, (int)t % 100); }

/* 0x4de1d0: the network game's standings: two faint boxes (the players, the best times); the players'
   names, totals (none: text 0x200) and the leader's mark (0x524dc8) on the players with the first
   row's total, the local player lit, the empty rows to eight dashed; the board's ten names and times
   (0x40e8c0: 0x533900 when 0x535b8c is set; none: text 0x202) */
void fe_4de1d0(Menu *m)
{
    (void)m;
    int w, h;
    D3d_ScreenSize(&w, &h);
    static const uint8_t faint[4] = {0xff, 0xff, 0xff, 0x20};
    int16_t a[4] = {(int16_t)(w * 100 / 0x280), (int16_t)(h * 0x5a / 0x1e0), (int16_t)(w * 300 / 0x280), (int16_t)(h * 0x118 / 0x1e0)};
    int16_t b[4] = {(int16_t)(w * 10 / 0x280 + w * 100 / 0x280 + w * 300 / 0x280), (int16_t)(h * 0x5a / 0x1e0),
                    (int16_t)(w * 0x82 / 0x280), (int16_t)(h * 0x118 / 0x1e0)};
    Rect_Fill(a, faint, 1);
    Rect_Fill(b, faint, 1);
    titles(Gs_Mode() == 10 ? 0x1c9 : 0x1f6);
    Fe_Header();
    int hy = h * 100 / 0x1e0, row = h * 0x14 / 0x1e0, top = h * 0x82 / 0x1e0;
    int x1 = w * 0x96 / 0x280, x2 = w * 0xfa / 0x280, x3 = w * 0x15e / 0x280;
    Font_Print(1, Text_Get(0x7f), x1, hy, C(PLAIN), 10);
    Font_Print(1, Text_Get(0x1ca), x2, hy, C(PLAIN), 10);
    Font_Print(1, Text_Get(0x1f8), x3, hy, C(PLAIN), 10);
    int k = 0;
    for (; k < (int)NetRec_LobbyCount(); k++) {
        const uint8_t *c = C(k == NetRec_LobbyLocal() ? LIT : PLAIN);
        int y = top + row * k;
        const char *name = NetRec_LobbyName(k);
        Font_Print(0, name ? name : "", x1, y, c, 10);
        if (NetRec_LobbyBest(k) == 0xffffffffu) snprintf(buf(), 0x100, "%s", Text_Get(0x200));
        else print_time(buf(), NetRec_LobbyBest(k));
        Font_Print(0, buf(), x2, y, c, 10);
        bool lead = NetRec_LobbyBest(0) != 0xffffffffu && NetRec_LobbyBest(k) == NetRec_LobbyBest(0);
        Font_Print(0, (const char *)C(lead ? 0x524dc8 : 0x519fb0), x3, y, c, 10);
    }
    for (; k < 8; k++) {
        int y = top + row * k;
        Font_Print(0, (const char *)C(0x519fb4), x1, y, C(PLAIN), 10);
        Font_Print(0, Text_Get(0x200), x2, y, C(PLAIN), 10);
        Font_Print(0, (const char *)C(0x519fb0), x3, y, C(PLAIN), 10);
    }
    int bx = b[2] / 2 + b[0];
    Font_Print(1, Text_Get(0x201), bx, hy, C(PLAIN), 10);
    if (!gm8(0x535b8c)) {
        Font_Print(0, Text_Get(0x202), bx, row + top, C(PLAIN), 10);
        return;
    }
    for (uint32_t i = 0; i < 10; i++) {
        uint32_t e = 0x533900 + 8 * i + 4;
        int y = top + row * (int)(i + 1);
        if (!gm8(e)) {
            Font_Print(0, (const char *)C(0x519fb4), w * 0x1c2 / 0x280, y, C(PLAIN), 10);
            Font_Print(0, (const char *)C(0x519fb0), w * 0x1fe / 0x280, y, C(PLAIN), 10);
        } else {
            Font_Print(0, (const char *)C(e), w * 0x1c2 / 0x280, y, C(PLAIN), 10);
            snprintf(buf(), 0x100, exe_str(0x517dd0), (int)gm32(e + 4));
            Font_Print(0, buf(), w * 0x1fe / 0x280, y, C(PLAIN), 10);
        }
    }
}

/* the title row of names[0..n) after the header (0x4d41e0, 0x4d3fa0) */
static void header_titles(const char *const *names, int n)
{
    int w, h;
    D3d_ScreenSize(&w, &h);
    Fe_Header();
    Fe_TitleRow(w * 0x18 / 0x280, h * 0x26 / 0x1e0, names, n);
}

/* 0x4e96d0 (code Ghidra leaves unanalysed): the network error page, one of four messages (text
   0x1e3 on, by 0x4f92f0's code, DAT_00826138) in the middle */
void fe_4e96d0(Menu *m)
{
    (void)m;
    int w, h;
    D3d_ScreenSize(&w, &h);
    const char *names[2] = {Text_Get(0), Text_Get(0x1e2)};
    header_titles(names, 2);
    uint32_t code = gm32(0x826138);
    if (code <= 3) Font_Print(1, Text_Get(0x1e3 + (int)code), w / 2, h / 2, C(LIT), 0x12);   /* 0x40b880 */
    Strip_DrawMain(&pg_81d6d8, false, NULL);   /* 0x4f8410, 0x4d45b0 */
}

/* 0x4e9820: the session's password: the prompt, the label and the password typed (0x818da8), the cursor
   blinking on the second half of every fifth of a second */
void fe_4e9820(Menu *m)
{
    (void)m;
    int w, h;
    D3d_ScreenSize(&w, &h);
    const char *names[3] = {Text_Get(0), Text_Get(0x12), Text_Get(0x1e7)};
    header_titles(names, 3);
    Font_Print(1, Text_Get(0x1e8), w / 2, h / 4, C(LIT), 0x12);
    snprintf(buf(), 0x100, exe_str(0x524d64), Text_Get(0x1c3), (const char *)C(0x818da8));
    if ((int)(Timer_Centis() % 0x14) > 9) strcat(buf(), exe_str(0x524d60));
    Font_Print(1, buf(), w * 0x104 / 0x280, h / 2, C(LIT), 0x11);
    Strip_DrawMain(&pg_81d6d8, false, NULL);
}

/* 0x4e2b40: a waiting room the shipped menus never open (pg_8205b8): a mock-up at fixed 640 x 480
   places with the developers' placeholder chat, set-up and car names (the exe's strings) */
void fe_4e2b40(Menu *m)
{
    (void)m;
    const char *names[2] = {exe_str(0x524e34), exe_str(0x524e24)};
    int w, h;
    D3d_ScreenSize(&w, &h);
    Fe_TitleRow(w * 0x18 / 0x280, h * 0x26 / 0x1e0, names, 2);
    Fe_Header();
    int16_t r[4] = {0x1e, 0x46, 400, 0x96};   /* DAT_008189a8 */
    Rect_Fill(r, C(LINE), 4);
    Font_Print(1, exe_str(0x524f00), r[0] + 10, r[1] + 10, C(LIT), 9);
    Font_Print(1, exe_str(0x524ee8), r[0] + 10, r[1] + 0x1e, C(LIT), 9);
    Font_Print(1, exe_str(0x524ed0), r[0] + 10, r[1] + 0x32, C(LIT), 9);
    r[1] = (int16_t)(r[1] + r[3] + 10), r[3] = 0x14;
    Rect_Fill(r, C(LINE), 4);
    Font_Print(1, exe_str(0x524ec0), r[0] + 10, r[1], C(LIT), 9);
    /* the set-up box (0x4d39a0: its rows at 370 of 480) */
    PENDING_NOTE(0x4d39a0, "the waiting room mock-up's set-up rows (never shown)");
    Font_Print(1, exe_str(0x524e70), 0x1b8, 0x46, C(LIT), 9);
    Font_Print(1, exe_str(0x524e58), 0x1b8, 0x5a, C(LIT), 9);
    Font_Print(1, exe_str(0x524e50), 0x1b8, 0x78, C(LIT), 9);
    Font_Print(1, exe_str(0x524e3c), 0x1b8, 0x8c, C(LIT), 9);
}

/* The stored leaderboards (0x40e4f0-0x40e5e0): 32 boards of 0x104 bytes at 0x533a08 (+0 in use, +4
   the entries: a tag and a total, 8 bytes each), their count DAT_00535b88, the one shown DAT_00516a0c */
enum { BOARDS = 0x533a08, BOARD_SIZE = 0x104, NBOARDS = 0x535b88, CUR_BOARD = 0x516a0c };
static int boards(void) { return gms32(NBOARDS); }                       /* 0x40e540 */
static int cur_board(void) { return gms32(CUR_BOARD); }                  /* 0x40e4f0 */
static void set_board(int k) { gw32(CUR_BOARD, (uint32_t)k); }           /* 0x40e500 */
static uint32_t board(int k)                                             /* 0x40e510 (0 not in use) */
{
    uint32_t b = BOARDS + BOARD_SIZE * (uint32_t)k;
    return gm8(b) ? b : 0;
}

/* 0x40e550: a new board, its entries blank (0x52ea5c) with no totals, named after player 0 */
static void board_add(void)
{
    uint32_t b = BOARDS + BOARD_SIZE * (uint32_t)boards();
    gw8(b, 1);
    for (uint32_t i = 0; i < 0x100; i += 8) {
        snprintf((char *)gm(b + 4 + i), 8, "%s", exe_str(0x52ea5c));
        gw32(b + 8 + i, 0);
    }
    const char *name = Player_Name(0);
    snprintf((char *)gm(b + 4), 0x100, "%s", name ? name : "");   /* (the original's sprintf of the name) */
    gw32(NBOARDS, (uint32_t)(boards() + 1));
}

/* 0x40e5e0: board k out, the later ones moved down (the one past the last marked in use, as the
   original does), the shown one back by one when at or after it */
static void board_delete(int k)
{
    if (k <= cur_board()) set_board(cur_board() - 1);
    gw8(BOARDS + BOARD_SIZE * (uint32_t)boards(), 1);
    if (k < 0x1f)
        for (uint32_t b = BOARDS + BOARD_SIZE * (uint32_t)k; b + BOARD_SIZE < 0x535984 + 4; b += BOARD_SIZE)
            memmove(gm(b), gm(b + BOARD_SIZE), BOARD_SIZE);
    gw32(0x535984, 0);
    gw32(NBOARDS, (uint32_t)(boards() - 1));
}

/* 0x4e9990: a stored board: which of how many (or text 0x1eb without one), its ten tags and totals, the
   arrows when there are boards, the page's other items under it; lit while its list is chosen */
void fe_4e9990(Menu *m)
{
    int w, h;
    D3d_ScreenSize(&w, &h);
    const char *names[3] = {Text_Get(0x12), Text_Get(0x1e9), Text_Get(0x1c4)};
    header_titles(names, 3);
    const uint8_t *c = C(m->items[m->sel].id ? PLAIN : LIT);
    if (boards() == 0)
        Font_Print(1, Text_Get(0x1eb), w / 2, h / 4, c, 0x12);
    else {
        MenuItem *list = Menu_Item(m, 0);
        snprintf(buf(), 0x100, Text_Get(0x1ea), list->value + 1, list->count);
        Font_Print(1, buf(), w / 2, h * 0x46 / 0x1e0, c, 0x12);
        uint32_t b = board(list->value);
        for (int i = 0; b && i < 10; i++) {
            uint32_t e = b + 4 + 8 * (uint32_t)i;
            int y = h * 0x5a / 0x1e0 + h * 0xf / 0x1e0 * i;
            const char *v = (const char *)C(0x519fb0);
            if (!gm8(e)) Font_Print(0, (const char *)C(0x519fb4), w * 0x118 / 0x280, y, c, 0x12);
            else {
                Font_Print(0, (const char *)C(e), w * 0x118 / 0x280, y, c, 0x12);
                snprintf(buf(), 0x100, exe_str(0x517dd0), (int)gm32(e + 4));
                v = buf();
            }
            Font_Print(0, v, w * 0x154 / 0x280, y, c, 0x12);
        }
        if (list->count) {
            Font_Print(1, (const char *)C(0x5250d8), w * 0x118 / 0x280 - w * 0x32 / 0x280, h * 0xa0 / 0x1e0, c, 0x12);
            Font_Print(1, (const char *)C(0x5250d4), w * 0x3c / 0x280 + w * 0x154 / 0x280, h * 0xa0 / 0x1e0, c, 0x12);
        }
    }
    List_Draw(m, NULL, h * 0x140 / 0x1e0, -1, 1, true);   /* 0x4d3360 */
    Strip_DrawMain(&pg_81d6d8, false, NULL);
}

/* 0x4eef30: the page opened: the list's values the boards, on with the shown one (0 when none was) */
void fe_4eef30(Menu *m, bool back)
{
    (void)back;
    MenuItem *list = Menu_Item(m, 0);
    list->count = (uint8_t)boards();
    if (!list->count) {
        list->flags &= (uint8_t)~MIF_ENABLED;
        return;
    }
    list->flags |= MIF_ENABLED;
    if (cur_board() == -1) set_board(0);
    list->value = (uint8_t)cur_board();
}

/* 0x4eefb0: a board chosen: shown from now on, the page left */
void fe_4eefb0(Menu *m, MenuItem *it)
{
    (void)it;
    set_board(Menu_Item(m, 0)->value);
    Menu_Back(m);
}

/* 0x4eefe0: a new board (up to 32), shown */
void fe_4eefe0(Menu *m, MenuItem *it)
{
    (void)m, (void)it;
    if (boards() < 0x20) {
        board_add();
        set_board(boards() - 1);
    }
}

/* 0x4ef000: the board in the list deleted */
void fe_4ef000(Menu *m, MenuItem *it)
{
    (void)it;
    if (boards() > 0) board_delete(Menu_Item(m, 0)->value);
}

/* 0x4ef030: each frame: the list kept to the boards; delete (item 2) only with boards, new (item 1)
   only under 32; without boards new on, delete off */
void fe_4ef030(Menu *m)
{
    MenuItem *list = Menu_Item(m, 0), *add = Menu_Item(m, 1), *del = Menu_Item(m, 2);
    list->count = (uint8_t)boards();
    if (!list->count) {
        del->flags &= (uint8_t)~MIF_ENABLED;
        add->flags |= MIF_ENABLED;
        return;
    }
    del->flags |= MIF_ENABLED;
    if (!(list->flags & MIF_ENABLED)) {
        list->flags |= MIF_ENABLED;
        if (cur_board() == -1) set_board(0);
        list->value = (uint8_t)cur_board();
    }
    if (boards() < 0x20) add->flags |= MIF_ENABLED;
    else add->flags &= (uint8_t)~MIF_ENABLED;
    if (list->value >= list->count) list->value = (uint8_t)(list->count - 1);
}
