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

/* 0x4e77c0: the network rally page */
void fe_4e77c0(Menu *m)
{
    int w, h;
    D3d_ScreenSize(&w, &h);
    const char *names[3] = {Text_Get(0x12), Text_Get(0x1d9), Text_Get(0x1da)};
    Fe_TitleRow(w * 0x18 / 0x280, h * 0x26 / 0x1e0, names, 3);
    Fe_Header();
    /* (its own centring and arrow offset, a pixel off Rows_Top's and Rows_ArrowDy's at some sizes) */
    int top = (h * 8 / 0x1e0 + h * 0x26 / 0x1e0 + h * 0x180 / 0x1e0) / 2 - h * 0x24 / 0x1e0 * m->n / 2;
    Rows_Draw(m, rally_row, false, true, top, h * 2 / 0x1e0 + h * 0x12 / 0x1e0);
    Strip_DrawMain(&pg_81d6d8, false, NULL);
}
