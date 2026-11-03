/* The front end's network game (0x4ec2b0-0x4eee60, docs/re/frontend.md#network): the connection
   page (0x822598), the sessions (0x824c18: search, join, host), the host's set-up (0x821c38), the
   session's lobby (0x81e218: the car, the box, ready, the chat line, the start), the password page
   (0x81b158), the network stage pages (rally 0x824fd8, single stage 0x81fc58, the others 0x81b338,
   0x81e998, 0x81d138), and the messages the front end takes (0x4ec8d0): the host's settings (types
   2 and 3, 0x1c4 bytes), a chat line (0), the leaderboard (0x11), and DirectPlay's own.

   The state is the original's in the game state: the search's flags and selection (0x818ef4,
   0x819014, 0x819024, 0x525288, 0x52528c, 0x81901c, 0x819020, the instance chosen 0x818cf0), the
   session's name and password as typed (0x818ebc, 0x818ef8), the password to join with (0x818da8),
   ready (0x818ce4), the lobby's cars (0x818d18) and its chat line (0x818f14, 0x819028).

   Where it can't be the original: DirectPlay's dialogs (it flips to the GDI surface and shows the
   cursor around EnumSessions and Open) are not shown; a join the host hasn't answered yet goes on
   from the page's frame (net/session.h). */
#include "fe/fe.h"
#include "fe/fe_net.h"
#include "fe/fe_page.h"
#include "fe/menu.h"
#include "fe/pages.h"
#include "exe.h"
#include "font.h"
#include "game/gstate.h"
#include "game/input.h"
#include "game/mem.h"
#include "game/profile.h"
#include "game/select.h"
#include "game/unlocks.h"
#include "game/weather.h"
#include "net/records.h"
#include "net/session.h"
#include "render/d3d7.h"
#include "sim/car.h"
#include "game/timer.h"
#include "world/tod.h"
#include <stdio.h>
#include <string.h>

enum {
    CARS = 0x818d18, READY = 0x818ce4, SEARCHING = 0x818ef4, LISTING = 0x819014, CHOSEN = 0x819024,
    SEL = 0x525288, LAST_COUNT = 0x52528c, TOP = 0x81901c, SHOWN = 0x819020, COUNT = 0x819018,
    INSTANCE = 0x818cf0, NAME = 0x818ebc, PASSWORD = 0x818ef8, JOIN_PASSWORD = 0x818da8,
    NO_DIALOG = 0x818d04, HAVE_DP = 0x818f10, EDIT = 0x663b60, CHATTING = 0x818ed0, LAST_CAR = 0x818ce8,
    LAST_BOX = 0x818ce0, LAST_SEL = 0x818d00, CHAT = 0x818f14, CHAT_LEN = 0x819028, ERROR = 0x826138,
    UNLOCKS = 0x52b03c, STAGE_FLAGS = 0x52ea68, EMPTY = 0x52ea5c,
};

static MenuItem *item(Menu *m, int16_t id) { return Menu_Item(m, id); }
static uint32_t mode(void) { return Gs_Mode(); }
static bool option(int n) { return gm32(0x52e890) >> n & 1; }   /* 0x406410 */
static uint8_t stage_flags(uint32_t r, uint32_t k) { return gm8(0x52e898 + k + r * 0xb); }   /* 0x406520 */
static void set_stage_flags(uint32_t r, uint32_t k, uint8_t v) { gw8(0x52e898 + k + r * 0xb, v); }   /* 0x406540 */
static void gcpy(uint32_t dst, uint32_t src) { memmove(gm(dst), gm(src), strlen((const char *)gm(src)) + 1); }
static void set_rally(uint32_t r) { gw32(0x52f2ac, (gm32(0x52f2ac) & ~0x1fu) | (r & 0x1f)); }   /* 0x4068b0 */
static void set_stage(uint32_t s)   /* 0x4068e0 */
{
    gw32(0x52f2ac, (gm32(0x52f2ac) & 0xfffffc1fu) | (s & 0x1f) << 5);
    Sel_ModeFlags();
}
static void set_location(uint32_t v) { gw32(0x52f2ac, (gm32(0x52f2ac) & 0xfffff3ffu) | (v & 3) << 10); }   /* 0x40d600 */
static void set_race(uint32_t v) { gw32(0x52f2ac, (gm32(0x52f2ac) & 0xffffcfffu) | (v & 3) << 12); Sel_ModeFlags(); }   /* 0x406960 */
static void set_laps(uint32_t v) { gw32(0x52f2ac, (gm32(0x52f2ac) & 0xfff0ffffu) | (v & 0xf) << 16); }   /* 0x40d620 */
static uint32_t location(void) { return gm32(0x52f2ac) >> 10 & 3; }   /* 0x406940 */
static uint32_t race(void) { return gm32(0x52f2ac) >> 12 & 3; }       /* 0x406950 */
static uint32_t laps(void) { return gm32(0x52f2ac) >> 16 & 0xf; }     /* Gs_Laps */
static int text_limit(int small, int large)   /* the original's widths at 640 and at 1,024 or more */
{
    int w, h;
    D3d_ScreenSize(&w, &h);
    return w < 0x400 ? small : large;
}

/* ---- the messages ---- */

/* 0x4ec2b0: the local player's data: the id, the car, the car flag (bit 5), ready (bit 6), in use */
void Fe_NetSendMe(void)
{
    uint8_t d[16] = {0};
    uint32_t id = Net_LocalPlayer(), f = (Player_Car(0) & 0x1f) | ((uint32_t)((gm8(READY) & 1) | 2) << 1 | (Champ_CarFlag(0) & 1)) << 5;
    memcpy(d, &id, 4), memcpy(d + 4, &f, 4);
    NetRec_SendData(d);
}

/* 0x4ec320: the host's settings' tail: the weather chosen (0x40dc30), then the profile block's
   (0x52ebc0) twenty stage records: +0x540/+0x544 pairs, +0x5e0, +0x630, +0x680, the bytes +0x6d4,
   and +0x53c, +0x6d0 */
static void pack(uint8_t *msg)
{
    Weather_Choose();
    uint32_t p = 0x52ebc0;
    for (uint32_t k = 0; k < 20; k++) {
        memcpy(msg + 0x18 + 8 * k, gm(p + 0x540 + 8 * k), 8);
        memcpy(msg + 0xb8 + 4 * k, gm(p + 0x5e0 + 4 * k), 4);
        memcpy(msg + 0x108 + 4 * k, gm(p + 0x630 + 4 * k), 4);
        memcpy(msg + 0x158 + 4 * k, gm(p + 0x680 + 4 * k), 4);
        msg[0x1a8 + k] = gm8(p + 0x6d4 + k);
        memcpy(msg + 0x1bc, gm(p + 0x53c), 4);
        memcpy(msg + 0x1c0, gm(p + 0x6d0), 4);
    }
}

/* 0x4ec3c0 */
static void unpack(const uint8_t *msg)
{
    uint32_t p = 0x52ebc0;
    for (uint32_t k = 0; k < 20; k++) {
        memcpy(gm(p + 0x540 + 8 * k), msg + 0x18 + 8 * k, 8);
        memcpy(gm(p + 0x5e0 + 4 * k), msg + 0xb8 + 4 * k, 4);
        memcpy(gm(p + 0x630 + 4 * k), msg + 0x108 + 4 * k, 4);
        memcpy(gm(p + 0x680 + 4 * k), msg + 0x158 + 4 * k, 4);
        gw8(p + 0x6d4 + k, msg[0x1a8 + k]);
        memcpy(gm(p + 0x53c), msg + 0x1bc, 4);
        memcpy(gm(p + 0x6d0), msg + 0x1c0, 4);
    }
}

/* the settings' word: the mode, and the rally and stage (8-10) or the location, race and laps (11, 12) */
static uint32_t settings_word(void)
{
    uint32_t w = mode() & 0xff;
    switch (mode()) {
    case 8: case 9: case 10:
        w |= (Tod_Rally() & 0xf) << 8 | (Tod_Stage() & 0xf) << 12;
        break;
    case 11: case 12:
        w |= (location() & 3) << 16 | (race() & 3) << 18 | (laps() & 0xf) << 20;
        break;
    }
    return w;
}

/* 0x4ec460: message 3, the host's settings as they change. (The original sends the stage flags at
   +8 from the stack unset; the port sends the current ones.) */
void Fe_NetSendSettings(void)
{
    uint8_t msg[0x1c4] = {3};
    uint32_t w = settings_word(), t = gm32(0x52e91c);   /* 0x406710 */
    memcpy(msg + 4, &w, 4);
    memcpy(msg + 8, gm(STAGE_FLAGS), 0xb);
    memcpy(msg + 0x14, &t, 4);
    pack(msg);
    Net_Send(0, true, msg, sizeof msg);
}

/* 0x4ec560: DirectPlay's messages in the front end */
static void sys_message(const DpSysMsg *s)
{
    switch (s->type) {
    case DPSYS_SETPLAYERORGROUPDATA: {
        uint8_t d[16] = {0};
        memcpy(d, s->data, s->data_len < 16 ? s->data_len : 16);
        NetRec_Take(s->player, d, true);
        return;
    }
    case DPSYS_SETSESSIONDESC:
        Net_TakeSessionDesc(&s->session);
        Fe_SetMode(Net_User(0));
        gw32(0x52e92c, Net_User(2));   /* 0x406780 */
        return;
    case DPSYS_CREATEPLAYERORGROUP: {
        uint8_t d[16] = {0};
        memcpy(d, s->data, s->data_len < 16 ? s->data_len : 16);
        NetRec_Take(s->player, d, true);
        Fe_NetSendMe();
        Fe_NetSendSettings();
        return;
    }
    case DPSYS_DESTROYPLAYERORGROUP:
        Net_RemovePlayer(s->player);
        NetRec_Remove(s->player);
        return;
    case DPSYS_HOST: {
        Net_SetHost(1);
        if (Net_SessionName()) gcpy(NAME, Net_SessionName());
        if (Net_SessionPassword()) gcpy(PASSWORD, Net_SessionPassword());
        gw8(READY, 1);
        Fe_NetSendMe();
        static Menu *const pages[5] = {&pg_824fd8, &pg_81fc58, &pg_81b338, &pg_81e998, &pg_81d138};
        if (mode() >= 8 && mode() <= 12) pg_81e218.parent = pages[mode() - 8];
        Net_SetJoinable(Fe_Page() != &pg_821c38);
        return;
    }
    }
}

/* 0x4ec6f0: the players' messages in the front end */
static void message(uint32_t from, const uint8_t *msg)
{
    switch (msg[0]) {
    case 0:
        Fe_NetChat(from, (const char *)msg + 1, false);
        return;
    case 2: case 3: {
        memcpy(gm(STAGE_FLAGS), msg + 8, 0xb);
        Fe_SetMode(msg[4]);
        uint32_t w;
        memcpy(&w, msg + 4, 4);
        switch (mode()) {
        case 8:
            set_rally(w >> 8 & 0xffffff0f);
            for (uint32_t k = 0; k < 0xb; k++) {
                uint8_t f = gm8(STAGE_FLAGS + k);
                if (f & 1 && (!(f & 2) || option(0xd)) && !(f & 4)) {
                    set_stage(k);
                    break;
                }
            }
            break;
        case 9: case 10:
            set_rally(w >> 8 & 0xffffff0f);
            set_stage(w >> 12 & 0xffffff0f);
            break;
        case 11: case 12:
            set_stage(0);
            set_location(w >> 16);
            set_race(w >> 18);
            set_laps(w >> 20);
            break;
        }
        uint32_t t;
        memcpy(&t, msg + 0x14, 4);
        gw32(0x52e91c, t);   /* 0x406720 */
        Fe_SetCar(0, (int)gm8(CARS + 4u * item(&pg_81e218, 1)->value));
        Fe_SetAutoBox(0, item(&pg_81e218, 2)->value);
        unpack(msg);
        gw32(0x818f0c, Timer_Centis());
        NetRec_Clear(false, true);
        if (msg[0] == 2) {
            Menu_Goto(&pg_8214b8);   /* 0x4f8330: the start */
            NetRec_ResetRace(true, true);
            NetRec_ClearBests();
            NetRec_Order();
        }
        return;
    }
    case 0x11:
        NetRec_TakeBoard(msg[1] != 0, msg + 4);
        NetRec_Lobby();
        return;
    }
}

/* 0x4ec8d0: the messages waiting */
void Fe_NetMessages(void)
{
    uint32_t from;
    const uint8_t *msg;
    while (Net_Receive(&from, &msg)) {
        if (from == DPID_SYSMSG) sys_message((const DpSysMsg *)msg);
        else message(from, msg);
    }
}

/* 0x4d05f0: the chat lines emptied (five of 0x100 at 0x817784, their order at 0x817c84) */
void Fe_NetChatClear(void)
{
    for (uint32_t i = 0; i < 5; i++) {
        gw8(0x817784 + 0x100 * i, 0);
        gw32(0x817c84 + 4 * i, 0x817784 + 0x100 * i);
    }
    gw32(0x817780, 0xffffffff);
}

/* 0x4d0620: a chat line "name> text" (local: the local player's name), the newest first */
bool Fe_NetChat(uint32_t from, const char *text, bool local)
{
    const char *who;
    if (!local) {
        uint8_t slot;
        if (!Net_PlayerSlot(from, &slot)) return false;
        who = Net_PlayerShortName(slot);
    } else
        who = Player_Name(0);
    int32_t at = gms32(0x817780) + 1;
    if (at > 4) at = 0;
    gw32(0x817780, (uint32_t)at);
    snprintf((char *)gm(0x817784 + 0x100u * (uint32_t)at), 0x100, exe_str(0x523bbc), who ? who : "", text);
    for (int k = 0, n = at + 5; k < 5; k++, n--) gw32(0x817c84 + 4u * (uint32_t)k, 0x817784 + 0x100u * (uint32_t)(n % 5));
    return true;
}

/* 0x4d0700: the local player's chat line sent (message 0, 0x101 bytes) and shown */
static void say(const char *text)
{
    uint8_t msg[0x101] = {0};
    snprintf((char *)msg + 1, 0x100, "%s", text);
    Net_Send(0, false, msg, sizeof msg);
    Fe_NetChat(0, text, true);
}
void Fe_NetSay(const char *text) { say(text); }
const char *Fe_NetChatRow(int i) { return (const char *)gm(gm32(0x817c84 + 4u * (uint32_t)i)); }   /* 0x4d06f0 */

/* ---- the connection and session pages ---- */

/* 0x4ec930: the main menu's network item: one player, the network bit, not alternate, a rally by default */
void fe_4ec930(Menu *m, MenuItem *it)
{
    (void)it;
    gw32(0x52afb4, (gm32(0x52afb4) & 0xfffe1fffu) | 1u << 13);   /* 0x4ea8c0 */
    gw32(0x52afb4, gm32(0x52afb4) | 1u << 19);                    /* 0x405de0 */
    gw32(0x52afb4, gm32(0x52afb4) & ~0x20000u);                   /* 0x4ea950 */
    Fe_SetPlayersPage(m);                                         /* 0x4f1bc0 */
    Menu_SetParent(&pg_8241b8, m);                                /* 0x4f83a0 */
    Fe_SetToSet((int)Gs_Players());                               /* 0x4f1bb0 */
    if (mode() < 8) {
        Fe_SetMode(8);
        set_rally(0);
        set_stage(0);
        gw32(0x52afb4, gm32(0x52afb4) & 0x807fffffu);             /* 0x4ea9c0(0, 0) */
    }
}

/* 0x4ec9a0: the connection page: DirectPlay, the lobby object, the connections listed */
void fe_4ec9a0(Menu *m, bool back)
{
    (void)back;
    gw8(HAVE_DP, 0);
    if (!Net_CreateDirectPlay()) return;
    if (!Net_CreateLobby()) {
        Net_ReleaseDirectPlay();
        return;
    }
    if (!Net_EnumConnections()) {
        Net_ReleaseLobby();
        Net_ReleaseDirectPlay();
        Menu_Goto(m->parent);
        return;
    }
    pg_822598.items[0].count = (uint8_t)Net_Connections();
    pg_822598.items[0].value = 0;
    gw8(HAVE_DP, 1);
}

/* 0x4eca10: the local player made (its data: the id it had, the car, in use; the original's other
   eight bytes are the stack's) */
static bool create_player(void)
{
    uint8_t d[16] = {0};
    uint32_t id = Net_LocalPlayer(), f = (Player_Car(0) & 0x1f) | 0x80;
    memcpy(d, &id, 4), memcpy(d + 4, &f, 4);
    return Net_CreatePlayer(Player_Name(0), Player_Name(0), d, 0x10);
}

/* 0x4eca60: the sessions page */
void fe_4eca60(Menu *m, bool back)
{
    Menu_SetFlags(m, true, true, true, true);
    gw8(SEARCHING, 0), gw8(CHOSEN, 0), gw8(LISTING, 0);
    gw32(LAST_COUNT, 0xffffffff), gw32(SEL, 0xffffffff);
    Input_FlushChars();
    gw8(JOIN_PASSWORD, 0);
    gw32(TOP, 0), gw32(SHOWN, 0);
    m->items[0].text = 0x1ff;
    gw8(NO_DIALOG, back);
    if (back) Input_FlushChars();
}

static bool join_pending;   /* the port's: a join waiting for the host (from 0x4ecd60 or 0x4eee60) */
static bool join_from_password;

/* the join's answer: 0x4ecd60's and 0x4eee60's rest */
static void joined(int r, bool bad_password, bool from_password)
{
    if (r == 0) {
        if (from_password) {
            if (bad_password) {
                Menu_Goto(&pg_81b158);
                return;
            }
            Net_Close();
            gw32(ERROR, 1);
            Menu_Goto(&pg_826140);
            return;
        }
        if (Net_FoundPassworded(gms32(SEL))) {
            Menu_Goto(&pg_81b158);
            return;
        }
        Net_Close();
        gw32(ERROR, 1);
        Menu_Goto(&pg_826140);
        return;
    }
    Fe_SetMode(Net_User(0));
    gw32(0x52e92c, Net_User(2));   /* 0x406780 */
    if (create_player()) {
        Menu_SetParent(&pg_81e218, &pg_824c18);
        Menu_Goto(&pg_81e218);
        return;
    }
    Net_Close();
    gw32(ERROR, 3);
    Menu_Goto(&pg_826140);
}

static void join(bool from_password)
{
    bool bad = false;
    int r = Net_Join(gms32(SEL), JOIN_PASSWORD, &bad);
    join_pending = r == NET_PENDING, join_from_password = from_password;
    if (!join_pending) joined(r, bad, from_password);
}

/* 0x4ecaf0: the sessions page's frame: the search's sessions, the selection moved by up and down,
   back stopping the search */
void fe_4ecaf0(Menu *m)
{
    InputRecord *in = Input_Record(0);
    if (join_pending) {
        join(join_from_password);
        return;
    }
    if (!gm8(SEARCHING)) return;
    int r = Net_EnumSessions(PASSWORD);
    if (r != 1) {
        if (r == -2) {
            gw32(0x593cac, 1);   /* 0x49c140 */
            Menu_Goto(m->parent);
        }
        return;
    }
    gw8(NO_DIALOG, 1);
    int32_t n = Net_Sessions(), sel = gms32(SEL);
    gw32(COUNT, (uint32_t)n);
    if (!gm8(LISTING)) {
        gw32(SEL, 0), gw32(LAST_COUNT, 0xffffffff), gw8(CHOSEN, 0);
        Menu_SetFlags(m, true, true, true, true);
    } else {
        bool top_down = false;
        if (n < 1) {
            gw8(LISTING, 0), gw32(SEL, 0xffffffff), gw32(LAST_COUNT, 0xffffffff), gw8(CHOSEN, 0);
            gw32(TOP, 0);
            top_down = true;
        } else {
            if (gms32(LAST_COUNT) != n) {
                if (!gm8(CHOSEN)) sel = -1;
                else {
                    uint32_t g[4];
                    for (int i = 0; i < n; i++) {
                        Net_SessionInstance(i, g);
                        if (!memcmp(g, gm(INSTANCE), 16)) {
                            sel = i;
                            break;
                        }
                    }
                }
            }
            gw32(SEL, (uint32_t)sel), gw32(LAST_COUNT, (uint32_t)n);
            bool moved = false;
            if (sel == -1) {
                gw32(SEL, 0);
                uint32_t g[4];
                Net_SessionInstance(0, g);
                memcpy(gm(INSTANCE), g, 16);
                gw8(CHOSEN, 1);
            } else if (!(in->pressed & 0x20)) {
                Menu_SetFlags(m, false, false, true, false);
                if (in->pressed & 4 && sel > 0) gw32(SEL, (uint32_t)(sel - 1)), moved = true;
                else if (in->pressed & 8 && sel < n - 1) gw32(SEL, (uint32_t)(sel + 1)), moved = true;
                if (moved) {
                    uint32_t g[4];
                    Net_SessionInstance(gms32(SEL), g);
                    memcpy(gm(INSTANCE), g, 16);
                    gw8(CHOSEN, 1);
                }
            } else {
                gw8(LISTING, 0), gw32(SEL, 0xffffffff), gw8(CHOSEN, 0);
                Net_StopEnumSessions();
                gw8(SEARCHING, 0);
            }
            if (gms32(SEL) < gms32(TOP)) top_down = true;
        }
        if (top_down) {
            int32_t t = gms32(TOP) - 1;
            gw32(TOP, (uint32_t)(t < 0 ? 0 : t));
        }
        if (gms32(TOP) + 5 <= gms32(SEL)) gw32(TOP, gm32(TOP) + 1);
    }
    gw32(SHOWN, (uint32_t)(n > 5 ? 5 : n));
}

/* 0x4ecd60: the search item: searching, then (a session chosen) joining it, or to the password page */
void fe_4ecd60(Menu *m, MenuItem *it)
{
    (void)it;
    if (!gm8(SEARCHING)) {
        m->items[0].text = 0x1ec;
        gw8(SEARCHING, 1);
        return;
    }
    int32_t sel = gms32(SEL);
    if (gm8(LISTING) && gm8(CHOSEN) && sel >= 0 && sel < Net_Sessions()) {
        gw8(READY, 0);
        Sel_Reset();         /* 0x409a30 */
        Fe_NetChatClear();   /* 0x4d05f0 */
        gw8(SEARCHING, 0);
        if (!Net_FoundPassworded(sel)) {
            join(false);
            return;
        }
        Menu_Goto(&pg_81b158);   /* 0x4f88d0 */
        return;
    }
    if (Net_Sessions() > 0) gw8(LISTING, 1);
}

/* 0x4ecea0: host a session with the last settings (the name 0x52e8f0, the password 0x52e905, the mode
   0x52e920, eight players), then the host's page */
void fe_4ecea0(Menu *m, MenuItem *it)
{
    (void)m, (void)it;
    gw8(SEARCHING, 0);
    gw8(READY, 1);
    Sel_Reset();
    Fe_NetChatClear();
    Fe_SetMode(gm8(0x52e920));   /* 0x4ea920 */
    if (!Net_Host(0x52e8f0, 0x52e905, mode() & 0xff, 0, 0, 0, 8)) return;
    gw8(NO_DIALOG, 1);
    if (create_player()) Menu_Goto(&pg_821c38);
}

/* 0x4ecf80: a connection chosen */
void fe_4ecf80(Menu *m, MenuItem *it)
{
    (void)it;
    if ((uint8_t)Net_InitializeConnection(m->items[0].value)) Menu_Goto(&pg_824c18);
}

/* 0x4ecfa0: going back from the connection and session pages lets DirectPlay go */
void fe_4ecfa0(Menu *m, bool back)
{
    (void)m;
    if (!back || !gm8(HAVE_DP)) return;
    Net_ReleaseLobby();
    Net_ReleaseDirectPlay();
    gw8(HAVE_DP, 0);
}

/* ---- the host's page ---- */

/* the mode list's length by the players (5, 3 with seven or more) and its value kept below it */
static void mode_count(Menu *m)
{
    MenuItem *i1 = item(m, 1);
    i1->count = Net_FoundPlayers(-1) < 7 ? 5 : 3;
    if (i1->count <= i1->value) i1->value = (uint8_t)(i1->count - 1);
}

/* 0x4ecfd0 */
void fe_4ecfd0(Menu *m, bool back)
{
    Input_FlushChars();
    Net_SetJoinable(false);
    mode_count(m);
    if (!back) {
        gcpy(NAME, 0x52e8f0);       /* 0x406690 */
        gcpy(PASSWORD, 0x52e905);   /* 0x4066d0 */
        item(m, 2)->value = (uint8_t)(gm32(0x52e92c) + 1);   /* 0x406770 */
        item(m, 3)->value = (uint8_t)(gm32(0x52e930) - 2);   /* 0x406790 */
    }
    item(m, 1)->value = (uint8_t)(gm8(0x52e920) - 8);
}

/* the typing of a name (0x4ed100, 0x4eed80): backspace, or a character of the set while it fits
   (19 characters, the width) */
static void type_into(uint32_t target, uint32_t charset, bool width)
{
    gcpy(EDIT, target);
    int c;
    if (!Input_NextChar(&c)) return;
    char *e = (char *)gm(EDIT);
    size_t n = strlen(e);
    if (c == 8) {
        if (!e[0]) {
            gcpy(target, EDIT);
            return;
        }
        e[n - 1] = 0;
        Menu_Sound(2);
    } else if (n < 0x13 && (!width || Font_TextWidth(1, e) < text_limit(0x8c, 0xe0)) && c &&
               strchr((const char *)gm(charset), c)) {
        e[n] = (char)c, e[n + 1] = 0;
        Menu_Sound(1);
    }
    gcpy(target, EDIT);
}

/* 0x4ed100: the host page's frame: the players item shown for some modes, the lists' lengths, the
   name or password typed into while chosen */
void fe_4ed100(Menu *m)
{
    MenuItem *i1 = item(m, 1), *i2 = item(m, 2), *i3;
    if (i1->value == 2 || i1->value == 4) i2->flags &= (uint8_t)~MIF_ENABLED;
    else i2->flags |= MIF_ENABLED;
    mode_count(m);
    i3 = item(m, 3);
    i3->count = i1->value < 3 ? 7 : 5;
    if (i3->count <= i3->value) i3->value = (uint8_t)(i3->count - 1);
    int16_t id = m->items[m->sel].id;
    if (id == 0) type_into(NAME, 0x525290, true);
    else if (id == 4) type_into(PASSWORD, 0x525290, true);
    Fe_NetMessages();
}

/* 0x4ed340: the host page's go: a name needed; the settings into the session (the players' limit
   refused: back to it), the mode's page */
void fe_4ed340(Menu *m, MenuItem *it)
{
    (void)it;
    if (!gm8(NAME)) {
        m->sel = 0;
        Menu_Sound(3);
        return;
    }
    Net_SetUser(2, item(m, 2)->value - 1u);
    gw32(0x52e92c, item(m, 2)->value - 1u);   /* 0x406780 */
    if (!Net_SetMaxPlayers(item(m, 3)->value + 2u)) {
        Menu_Sound(3);
        Menu_SelectId(m, 3);
        return;
    }
    gw32(0x52e930, item(m, 3)->value + 2u);   /* 0x4067a0 */
    Net_SetSessionName(NAME);
    gcpy(0x52e8f0, NAME);       /* 0x4066a0 */
    Net_SetPassword(PASSWORD);
    gcpy(0x52e905, PASSWORD);   /* 0x4066e0 */
    Fe_SetMode(item(m, 1)->value + 8u);
    Net_SetUser(0, item(m, 1)->value + 8u);
    Net_SetJoinable(true);
    static Menu *const pages[5] = {&pg_824fd8, &pg_81fc58, &pg_81b338, &pg_81e998, &pg_81d138};
    Menu_Goto(pages[item(m, 1)->value < 4 ? item(m, 1)->value : 4]);
    Fe_NetSendSettings();
}

/* 0x4ed500: leaving the host's page: back closes the session */
void fe_4ed500(Menu *m, bool back)
{
    if (back) {
        Net_DestroyPlayer();
        Net_Close();
        return;
    }
    Fe_SetMode(item(m, 1)->value + 8u);
}

/* ---- the lobby ---- */

/* 0x4ed530: the lobby: the cars player 0 has (0x408e30), the box, ready */
void fe_4ed530(Menu *m, bool back)
{
    (void)back;
    gw8(CHATTING, 1);
    gw32(LAST_CAR, 0xffffffff), gw32(LAST_BOX, 0xffffffff);
    gw8(0x818ac4, 0);   /* 0x4ea5c0 */
    Menu_SetFlags(m, true, true, true, true);
    m->sel = 0;
    gw32(LAST_SEL, 0);
    uint8_t n = 0;
    for (uint32_t car = 0; car < 0x16; car++)
        if (Player_HasDriven(0, (int)car, true)) {
            gw32(CARS + 4u * n, car);
            if (car == (Player_Car(0) & 0xff)) item(m, 1)->value = n;
            n++;
        }
    item(m, 2)->value = Champ_CarFlag(0) ? 1 : 0;
    item(m, 3)->value = gm8(READY) ? 0 : 1;
    item(m, 1)->count = n;
}

/* 0x4ed610: the host's start: joining closed, the stages' flags (0x52ea68, the cheat's bit 1 off),
   the first open stage of a rally, the car and box, message 2 with the settings, the records reset,
   the start */
void fe_4ed610(Menu *m, MenuItem *it)
{
    (void)it;
    if (!Net_IsHost()) return;
    Net_SetJoinable(false);
    uint8_t msg[0x1c4] = {2};
    for (uint32_t k = 0; k < 0xb; k++) {
        uint8_t f = stage_flags(Tod_Rally(), k);
        if (option(0xd)) f &= 0xfd;
        msg[8 + k] = f;
        gw8(STAGE_FLAGS + k, f);
    }
    if (mode() == 8)
        for (uint32_t k = 0; k < 0xb; k++) {
            uint8_t f = stage_flags(Tod_Rally(), k);
            if (f & 1 && (!(f & 2) || option(0xd)) && !(f & 4)) {
                set_stage(k);
                break;
            }
        }
    Fe_SetCar(0, (int)gm8(CARS + 4u * item(m, 1)->value));
    Fe_SetAutoBox(0, item(m, 2)->value);
    uint32_t w = settings_word(), t = gm32(0x52e91c);
    memcpy(msg + 4, &w, 4);
    memcpy(msg + 0x14, &t, 4);
    pack(msg);
    Net_Send(0, true, msg, sizeof msg);
    NetRec_ResetRace(true, true);
    NetRec_ClearBests();
    NetRec_Clear(false, true);
    gw8(0x531768, 1);   /* 0x4083e0 */
    Menu_Goto(&pg_8214b8);
    NetRec_Order();
}

/* 0x4ed840: the lobby's frame: the chat line (item 0) typed into while chosen, the car and box sent
   when changed, the players, ready (the host: the start only when everyone is ready) */
void fe_4ed840(Menu *m)
{
    bool all_ready = true;
    if (gm8(CHATTING)) {
        InputRecord *in = Input_Record(0);
        if (in->pressed & 8) m->sel = 1;
        else if (in->pressed & 4) m->sel = (int8_t)(m->n - 1);
    }
    if (gm32(LAST_CAR) != item(m, 1)->value) {
        Fe_SetCar(0, (int)gm8(CARS + 4u * item(m, 1)->value));
        Fe_NetSendMe();
        gw32(LAST_CAR, item(m, 1)->value);
    }
    if (gm32(LAST_BOX) != item(m, 2)->value) {
        Fe_SetAutoBox(0, item(m, 2)->value);
        Fe_NetSendMe();
        gw32(LAST_BOX, item(m, 2)->value);
    }
    if (gms32(LAST_SEL) != m->sel) {
        if (m->items[m->sel].id == 0) {
            Input_FlushChars();
            gw8(CHAT, 0);
            gw32(CHAT_LEN, 0);
            gw8(CHATTING, 1);
            gw8(0x818ac4, 1);
            Menu_SetFlags(m, false, false, false, false);
        } else {
            if (m->items[gms32(LAST_SEL)].id == 0) {
                gw8(0x818ac4, 0);
                Menu_SetFlags(m, true, true, true, true);
            }
            gw8(CHATTING, 0);
        }
        gw32(LAST_SEL, (uint32_t)m->sel);
    }
    Net_EnumPlayers();
    int c;
    if (gm8(CHATTING) && Input_NextChar(&c)) {
        char *chat = (char *)gm(CHAT);
        int32_t len = gms32(CHAT_LEN);
        if (c == 8) {
            size_t n = strlen(chat);
            if (n > 0) chat[n - 1] = 0, gw32(CHAT_LEN, (uint32_t)(len - 1));
        } else if (c == 0xd) {
            if (chat[0]) {
                say(chat);
                gw32(CHAT_LEN, 0);
                chat[0] = 0;
            }
        } else if (c == 0x1b) {
            gw8(CHATTING, 0);
            gw8(0x818ac4, 0);
            Menu_SetFlags(m, true, true, true, true);
            m->sel = 1;
        } else if (c && strchr((const char *)gm(0x5252d4), c)) {
            if (len < 0xff && Font_TextWidth(1, chat) < text_limit(0x14a, 0x210)) {
                chat[len] = (char)c, chat[len + 1] = 0;
                gw32(CHAT_LEN, (uint32_t)(len + 1));
            }
        }
    }
    if (!Net_IsHost()) {
        bool decided = false;
        if (gm8(READY)) {
            if (item(m, 3)->value == 1) {
                gw8(READY, 0);
                Fe_NetSendMe();
            }
            decided = gm8(READY) != 0;
        }
        if (!decided && item(m, 3)->value == 0) {
            gw8(READY, 1);
            Fe_NetSendMe();
        }
    } else
        for (int i = 0; i < 7; i++)
            if (NetRec_Used(i) && !NetRec_Bit6(i)) {
                all_ready = false;
                break;
            }
    MenuItem *i3 = item(m, 3);
    i3->flags = (uint8_t)((i3->flags & ~1u) | all_ready);
    Fe_NetMessages();
}

/* 0x4edb30: leaving the lobby: back leaves the session (not the host's) */
void fe_4edb30(Menu *m, bool back)
{
    (void)m;
    if (back && !Net_IsHost()) {
        Net_DestroyPlayer();
        Net_Close();
    }
}

const char *Fe_NetChatLine(void) { return (const char *)gm(CHAT); }   /* 0x4edb50 */

/* 0x4edb60 */
void fe_4edb60(Menu *m)
{
    (void)m;
    Fe_NetMessages();
    Input_FlushChars();
}

/* ---- the network stage pages ---- */

/* the stages a rally has open by the unlocks (0x52b03c): 1, 4, 8, or (with bit 0) 10 */
static int open_stages(uint32_t r, bool cheat)
{
    if (cheat) return 10;
    uint32_t w = gm32(UNLOCKS), n = r + 1;
    int c = 1;
    if (n <= (w >> 8 & 0xf)) c = 4;
    if (n <= (w >> 12 & 0xf)) c = 8;
    if (w & 1 && n <= (w >> 16 & 0xf)) c = 10;
    return c;
}

/* the extra stage an odd rally has open (the unlocks' second word's three 5-bit fields) */
static bool odd_extra(uint32_t r)
{
    uint32_t w2 = gm32(UNLOCKS + 4), bit = 1u << (((int)(r + 1) / 2 - 1) & 0x1f);
    return (w2 & bit & 0x1f) || ((uint8_t)bit & (uint8_t)(w2 >> 5) & 0x1f) || ((uint8_t)bit & (uint8_t)(w2 >> 10) & 0x1f);
}

/* the rallies open: at least 4, the unlocks' three counts (the last with bit 0); 8 with the cheat */
static uint8_t open_rallies(void)
{
    if (option(0xd)) return 8;
    uint32_t w = gm32(UNLOCKS), c = 4;
    if ((w >> 8 & 0xf) > c) c = w >> 8 & 0xf;
    if ((w >> 12 & 0xf) > c) c = w >> 12 & 0xf;
    if (w & 1 && (w >> 16 & 0xf) > c) c = w >> 16 & 0xf;
    return (uint8_t)c;
}

/* 0x4edb70: the rally page (mode 8): the rallies open, the selection's rally, its stages */
void fe_4edb70(Menu *m, bool back)
{
    (void)back;
    uint32_t r = gm32(0x52afb4) >> 23 & 0xf;   /* 0x4ea990 */
    MenuItem *i0 = item(m, 0), *i1 = item(m, 1);
    i1->count = option(0xd) ? 8 : (uint8_t)(gm32(UNLOCKS) >> 8 & 0xf);
    i1->value = (uint8_t)r;
    if (i1->count < i1->value) i1->value = 0;
    if (option(0xd)) {
        i0->count = (uint8_t)((r & 1) + 10);
        return;
    }
    int k = 0;
    for (; k < 0xb; k++)
        if (stage_flags(r, (uint32_t)k) & 2) {
            i0->count = (uint8_t)k;
            break;
        }
    if (r & 1 && (k == 4 || k == 8) && !(stage_flags(r, 10) & 2)) i0->count++;
}

/* 0x4ede10: its frame: the stages of the rally chosen */
void fe_4ede10(Menu *m)
{
    MenuItem *i0 = item(m, 0), *i1 = item(m, 1);
    if (option(0xd)) {
        i0->count = (uint8_t)((i1->value & 1) + 10);
        Fe_NetMessages();
        return;
    }
    int k = 0;
    for (; k < 0xb; k++)
        if (stage_flags(i1->value, (uint32_t)k) & 2) {
            i0->count = (uint8_t)k;
            break;
        }
    if (i1->value & 1 && (k == 4 || k == 8) && !(stage_flags(i1->value, 10) & 2)) i0->count++;
    Fe_NetMessages();
}

/* the stage behind a rally page row: the last row of five or nine is stage 11 (index 10) */
static uint32_t row_stage(uint8_t count, uint32_t row) { return (count == 5 && row == 4) || (count == 9 && row == 8) ? 10 : row; }

/* 0x4edca0: a stage of the rally switched on or off */
void fe_4edca0(Menu *m, MenuItem *it)
{
    (void)it;
    MenuItem *i0 = item(m, 0);
    uint32_t k = row_stage(i0->count, i0->value), r = item(m, 1)->value;
    set_stage_flags(r, k, stage_flags(r, k) ^ 1);
}

/* 0x4edd50: the rally page's go: with a stage on, the rally into the selection and the lobby */
void fe_4edd50(Menu *m, MenuItem *it)
{
    (void)it;
    MenuItem *i0 = item(m, 0);
    if (i0->count) {
        for (uint32_t row = 0;; row++) {
            if (row >= i0->count) {
                Fe_NetSendSettings();
                return;
            }
            if (stage_flags(item(m, 1)->value, row_stage(i0->count, row)) & 1) break;
        }
        set_stage(0);
        set_rally(item(m, 1)->value);
        pg_81e218.parent = m;
        Menu_Goto(&pg_81e218);
    }
    Fe_NetSendSettings();
}

/* the stage list's last row (5, 9 or 11 rows: stage 11) */
static bool last_row_is_11(MenuItem *s) { return (s->count == 5 && s->value == 4) || (s->count == 9 && s->value == 8) || (s->count == 11 && s->value == 10); }

/* 0x4edef0: the single stage page (mode 9): the rallies open, the rally and stage (from the selection
   on going back, else the mode word's), the stages, the weather list from the start */
void fe_4edef0(Menu *m, bool back)
{
    MenuItem *i0 = item(m, 0), *i1 = item(m, 1);
    i0->count = open_rallies();
    uint32_t r, s;
    if (!back) {
        r = gm32(0x52afb4) >> 23 & 0xf, s = gm32(0x52afb4) >> 27 & 0xf;   /* 0x4ea990 */
        i0->value = (uint8_t)r, i1->value = (uint8_t)s;
    } else
        r = Tod_Rally() & 0xff, s = Tod_Stage() & 0xff;
    (void)s;
    uint8_t c;
    if (!option(0xd)) {
        /* (the original's count here: 4, the back flag when the rally is within the first count, 8
           or 10 past the others; 0x4ee170 sets it again straight after) */
        uint32_t w = gm32(UNLOCKS), n = r + 1;
        c = 4;
        if ((int)n <= (int)(w >> 8 & 0xf)) c = back;
        if ((int)(w >> 12 & 0xf) < (int)n) c = 8;
        if (w & 1 && (int)(w >> 16 & 0xf) < (int)n) c = 10;
        if (r & 1 && odd_extra(r)) c++;
    } else
        c = (uint8_t)((r & 1) + 10);
    i1->count = c;
    if (!back) item(m, 2)->value = 0;
    gw32(0x818d70, r);
    fe_4ee170(m);
}

/* 0x4ee090: the single stage's go: the stage (the last row stage 11), the rally, the weather chosen
   (none on stage 11), the lobby */
void fe_4ee090(Menu *m, MenuItem *it)
{
    (void)it;
    MenuItem *i1 = item(m, 1);
    set_stage(last_row_is_11(i1) ? 10 : i1->value);
    set_rally(item(m, 0)->value);
    uint32_t wv = 0;
    if (Tod_Stage() != 10) {
        int32_t w = gms32(0x818d74 + 4u * item(m, 2)->value);
        if (w != -1) wv = (uint32_t)w;
    }
    gw32(0x52f290, 1), gw32(0x52f0fc, wv);   /* 0x40df60 */
    pg_81e218.parent = m;
    Menu_Goto(&pg_81e218);
    Fe_NetSendSettings();
}

/* the stage list's labels "1".."n" and the end (0x818d84, 3 bytes each; 0x517dd0, 0x524dbc) and its
   length with the odd rally's extra stage */
static void stage_labels(Menu *m, uint32_t r)
{
    int c = open_stages(r, option(0xd)), i = 0;
    while (i < c) {
        i++;
        snprintf((char *)gm(0x818d84 + 3u * (uint32_t)(i - 1)), 3, exe_str(0x517dd0), i);
    }
    snprintf((char *)gm(0x818d84 + 3u * (uint32_t)i), 3, "%s", exe_str(0x524dbc));
    if (!option(0xd)) {
        if (r & 1 && odd_extra(r)) c++;
    } else
        c = (int)((r & 1) + 10);
    MenuItem *i1 = item(m, 1);
    i1->count = (uint8_t)c;
    if (i1->count <= i1->value) i1->value = (uint8_t)(i1->count - 1);
}

/* 0x4ee170: the single stage page's frame: the stages of the rally chosen, the weather item on all but
   stage 11, the weathers of the rally (0x4081d0) as the weather list (0x818d08 the texts, 0x818d74 the
   values: "random" and up to three) */
void fe_4ee170(Menu *m)
{
    uint32_t r = item(m, 0)->value;
    if (r != gm32(0x818d70)) {
        item(m, 2)->value = 0;
        gw32(0x818d70, r);
    }
    stage_labels(m, r);
    MenuItem *i2 = item(m, 2);
    if (last_row_is_11(item(m, 1))) i2->flags &= (uint8_t)~MIF_ENABLED;
    else i2->flags |= MIF_ENABLED;
    set_rally(r);
    int n = Fe_RallyWeathers();   /* 0x4081d0, 0x408280 */
    uint32_t list = 0x52ea74;     /* 0x408270 */
    gw32(0x818d08, 0x156);
    gw32(0x818d74, 0xffffffff);
    if (n < 4) {
        i2->count = (uint8_t)(n + 1);
        for (int k = 0; k < n; k++) {
            gw32(0x818d0c + 4u * (uint32_t)k, gm32(list + 4u * (uint32_t)k) + 0x157);
            gw32(0x818d78 + 4u * (uint32_t)k, gm32(list + 4u * (uint32_t)k));
        }
        Fe_NetMessages();
        return;
    }
    i2->count = 4;
    gw32(0x818d0c, gm32(list) + 0x157), gw32(0x818d78, gm32(list));
    gw32(0x818d10, gm32(list + 4u * (uint32_t)(n / 2)) + 0x157), gw32(0x818d7c, gm32(list + 4u * (uint32_t)(n / 2)));
    gw32(0x818d14, gm32(list + 4u * (uint32_t)(n - 1)) + 0x157), gw32(0x818d80, gm32(list + 4u * (uint32_t)(n - 1)));
    Fe_NetMessages();
}

/* 0x4ee460: the special stage page (mode 10): as 0x4edef0, the time of day (0x406710, past 4 shown
   less 4) */
void fe_4ee460(Menu *m, bool back)
{
    MenuItem *i0 = item(m, 0), *i1 = item(m, 1);
    i0->count = open_rallies();
    uint32_t r = gm32(0x52afb4) >> 23 & 0xf, s = gm32(0x52afb4) >> 27 & 0xf;   /* 0x4ea990 */
    i0->value = (uint8_t)r, i1->value = (uint8_t)s;
    uint8_t c;
    if (!option(0xd)) {
        uint32_t w = gm32(UNLOCKS), n = r + 1;
        c = 4;
        if ((int)n <= (int)(w >> 8 & 0xf)) c = back;   /* (as 0x4edef0) */
        if ((int)(w >> 12 & 0xf) < (int)n) c = 8;
        if (w & 1 && (int)(w >> 16 & 0xf) < (int)n) c = 10;
        if (r & 1 && odd_extra(r)) c++;
    } else
        c = (uint8_t)((r & 1) + 10);
    i1->count = c;
    if (!back) {
        MenuItem *i2 = item(m, 2);
        i2->value = (uint8_t)gm32(0x52e91c);   /* 0x406710 */
        if (i2->value) i2->value -= 4;
    }
    fe_4ee6e0(m);
}

/* 0x4ee600: its go: the stage, the rally, the time of day (a value past 0 plus 4), no weather, the lobby */
void fe_4ee600(Menu *m, MenuItem *it)
{
    (void)it;
    MenuItem *i1 = item(m, 1), *i2 = item(m, 2);
    set_stage(last_row_is_11(i1) ? 10 : i1->value);
    set_rally(item(m, 0)->value);
    gw32(0x52e91c, i2->value ? i2->value + 4u : 0);   /* 0x406720 */
    gw32(0x52f290, 1), gw32(0x52f0fc, 0);              /* 0x40df60 */
    pg_81e218.parent = m;
    Menu_Goto(&pg_81e218);
    Fe_NetSendSettings();
}

/* 0x4ee6e0: its frame */
void fe_4ee6e0(Menu *m)
{
    stage_labels(m, item(m, 0)->value);
    Fe_NetMessages();
}

/* the arcade locations in the network's order (the stack table of 0x4ee850: six, then 5 and 7 with
   the unlocks' bits 20 and 21) into 0x818ed4; the count */
static uint32_t locations(void)
{
    static const int32_t order[8] = {6, 3, 1, 4, 0, 2, 5, 7};
    uint32_t w = gm32(UNLOCKS), n = 6;
    if (!option(0xd)) {
        n = (w >> 2 & 3) * 3;
        if ((w >> 4 & 3) * 3 > n) n = (w >> 4 & 3) * 3;
        if ((w >> 6 & 3) * 3 > n) n = (w >> 6 & 3) * 3;
    }
    for (uint32_t i = 0; i < 6; i++)
        if (i < n || option(0xd)) gw32(0x818ed4 + 4 * i, (uint32_t)order[i]);
    if (w & 0x100000 || option(0xd)) n++, gw32(0x818ed4 + 4 * 6, 5);
    if (w & 0x200000 || option(0xd)) n++, gw32(0x818ed8 + 4 * 6, 7);
    return n;
}

/* the location list's value at the one chosen last (0x406730) */
static void location_value(Menu *m, uint32_t n)
{
    MenuItem *i0 = item(m, 0);
    i0->count = (uint8_t)n;
    i0->value = 0;
    for (uint32_t k = 0; k < n; k++)
        if (gm32(0x818ed4 + 4 * k) == gm32(0x52e924)) {
            item(m, 0)->value = (uint8_t)k;
            break;
        }
}

/* the location's place in the order: class (/ 3) and race (% 3) */
static void set_place(Menu *m)
{
    static const int32_t order[8] = {6, 3, 1, 4, 0, 2, 5, 7};
    int k = 0;
    while (k < 8 && order[k] != gms32(0x818ed4 + 4u * item(m, 0)->value)) k++;
    set_stage(0);
    set_location((uint32_t)(k / 3));
    set_race((uint32_t)(k % 3));
}

/* 0x4ee850: the circuit page (mode 11): the locations, the laps (0x406750) */
void fe_4ee850(Menu *m, bool back)
{
    (void)back;
    location_value(m, locations());
    item(m, 1)->value = (uint8_t)(gm32(0x52e928) - 1);
}

/* 0x4ee9b0: its go */
void fe_4ee9b0(Menu *m, MenuItem *it)
{
    (void)it;
    set_place(m);
    set_laps(item(m, 1)->value + 1u);
    gw32(0x52e928, item(m, 1)->value + 1u);                       /* 0x406760 */
    gw32(0x52e924, gm32(0x818ed4 + 4u * item(m, 0)->value));     /* 0x406740 */
    pg_81e218.parent = m;
    Menu_Goto(&pg_81e218);
    Fe_NetSendSettings();
}

/* 0x4eeab0: the time trial page (mode 12): the locations, the time (0x406710, past 4 shown less 4) */
void fe_4eeab0(Menu *m, bool back)
{
    location_value(m, locations());
    if (!back) {
        MenuItem *i1 = item(m, 1);
        i1->value = (uint8_t)gm32(0x52e91c);
        if (i1->value) i1->value -= 4;
    }
}

void fe_4eec30(Menu *m) { (void)m; Fe_NetMessages(); }   /* 0x4eec30 */

/* 0x4eec40: its go */
void fe_4eec40(Menu *m, MenuItem *it)
{
    (void)it;
    set_place(m);
    gw32(0x52e924, gm32(0x818ed4 + 4u * item(m, 0)->value));   /* 0x406740 */
    MenuItem *i1 = item(m, 1);
    gw32(0x52e91c, i1->value ? i1->value + 4u : 0);             /* 0x406720 */
    pg_81e218.parent = m;
    Menu_Goto(&pg_81e218);
    Fe_NetSendSettings();
}

/* ---- the password page ---- */

/* 0x4eed50 */
void fe_4eed50(Menu *m, bool back)
{
    (void)back;
    Menu_SetFlags(m, false, false, true, true);
    Input_FlushChars();
    gw8(JOIN_PASSWORD, 0);
}

/* 0x4eed80: the password typed (19 characters of its set, no width limit) */
void fe_4eed80(Menu *m)
{
    (void)m;
    if (join_pending) {
        join(true);
        return;
    }
    type_into(JOIN_PASSWORD, 0x525330, false);
}

/* 0x4eee60: the password's join */
void fe_4eee60(Menu *m, MenuItem *it)
{
    (void)m, (void)it;
    gw8(READY, 0);
    Sel_Reset();
    Fe_NetChatClear();
    gw8(SEARCHING, 0);
    join(true);
}
