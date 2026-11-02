/* The rally pages from the mode select to the driver: the mode select (0x823a38), the difficulties,
   the number of players, the driver select (0x8241b8) and the tag entry (0x824a38); 0x4d3fa0-0x4d9880,
   0x4ef5f0-0x4f1040. */
#include "fe/fe.h"
#include "fe/fe_page.h"
#include "fe/fe_res.h"
#include "fe/fe_strip.h"
#include "fe/pages.h"
#include "crt.h"
#include "exe.h"
#include "fixmath.h"
#include "font.h"
#include "game/gstate.h"
#include "game/input.h"
#include "game/mem.h"
#include "game/profile.h"
#include "game/select.h"
#include "game/timer.h"
#include "pending.h"
#include "render/d3d7.h"
#include "render/twod.h"
#include "text.h"
#include <stdio.h>
#include <string.h>

static uint8_t to_set;          /* DAT_00819048: the players still to set up, counting down */
static int guests;              /* DAT_00819744 */
static Menu *players_page;      /* DAT_00819870 */
static bool from_tag;           /* DAT_00819879 */
static int tag_player;          /* DAT_00819880 */
static Menu *after_tag;         /* DAT_00819030 */
static int shown_player;        /* DAT_008182b4: the n of "Player n" */
static char player_title[64];   /* DAT_00818274 */
static bool tag_new;            /* DAT_008189a4: the tag page names a new driver */
static bool typed;              /* DAT_0081986c */
static uint32_t typed_at;       /* DAT_00819040 */
static char profile_names[4][0x28];   /* DAT_00819054 */

static void screen(int *w, int *h) { D3d_ScreenSize(w, h); }
static uint32_t mode(void) { return Gs_Mode(); }
static uint32_t players(void) { return gm32(0x52afb4) >> 13 & 0xf; }   /* 0x405d70 */
static int title_x(void) { int w, h; screen(&w, &h); return w * 0x18 / 0x280; }
static int title_y(void) { int w, h; screen(&w, &h); return h * 0x26 / 0x1e0; }

/* the pages' trail names by mode: rally (0xe7) or arcade (0x94), then the mode's */
static bool mode_names(const char *names[2], bool champ_e)
{
    static const int16_t sub[8] = {0xc, 0xd, 0xf, 0x10, 0x11, 0xc, 0xe2, 0x10};
    uint32_t m = mode();
    if (m > 7) return false;
    names[0] = Text_Get(m < 5 ? 0xe7 : 0x94);
    names[1] = Text_Get(m == 4 && champ_e ? 0xe : sub[m]);
    return true;
}

/* 0x4ef5f0: one lap (the selection's bits 16-19) */
void fe_4ef5f0(Menu *m, bool back)
{
    (void)m, (void)back;
    gw32(0x52f2ac, (gm32(0x52f2ac) & 0xfff0ffffu) | 1u << 16);
}

void fe_4f3ae0(Menu *m) { (void)m; Scroll_Update(Fe_Scroll(0)); }

/* 0x4e1fb0 */
void fe_4e1fb0(Menu *m)
{
    const char *names[1] = {Text_Get(0xd9)};
    Fe_Header();
    Fe_Titles(m, title_x(), title_y(), true, 2, names, 1);
    List_Draw(m, NULL, -1, -1, 0, true);
    Strip_DrawMain(&pg_81d6d8, false, NULL);
}

static void set_rally(uint32_t r) { gw32(0x52f2ac, (gm32(0x52f2ac) & ~0x1fu) | (r & 0x1f)); }   /* 0x4068b0 */
static void set_stage(uint32_t s)   /* 0x4068e0 */
{
    gw32(0x52f2ac, (gm32(0x52f2ac) & 0xfffffc1fu) | (s & 0x1f) << 5);
    Sel_ModeFlags();
}

/* 0x4ef600: the mode from the item (on going back too); the players page starts at the count */
void fe_4ef600(Menu *m, bool back)
{
    (void)back;
    static Menu *const pages[5] = {&pg_81d318, &pg_820978, &pg_81f2f8, &pg_824678, &pg_81e5d8};
    int s = m->sel;
    if (s < 0 || s > 4) return;
    Fe_SetMode((uint32_t)s);
    if (s == 0) set_rally(0);
    if (s <= 1) set_stage(0);
    uint32_t n = players(), lim = s == 4 ? 8 : 4;
    /* the first item's value, as Pages_Build's players pages (see there) */
    pages[s]->items[0].value = (uint8_t)(n > lim ? 0 : n - 1);
}

/* 0x4ef8f0: Expert only when unlocked (0x52b03c bit 0 or the option 0x52e890 bit 13); the
   selection the current difficulty */
void fe_4ef8f0(Menu *m, bool back)
{
    (void)back;
    if (!(gm8(0x52b03c) & 1) && !(gm32(0x52e890) >> 13 & 1)) m->items[2].flags &= (uint8_t)~MIF_ENABLED;
    else m->items[2].flags |= MIF_ENABLED;
    m->sel = (int8_t)Gs_Mode2();
}

/* 0x4ef930: the difficulty (the mode word's bits 10-12) */
void fe_4ef930(Menu *m, MenuItem *it)
{
    (void)it;
    gw32(0x52afb4, (gm32(0x52afb4) & 0xffffe3ffu) | ((uint32_t)m->sel & 7) << 10);
    if (mode() == 5) PENDING_NOTE(0x4fa970, "the arcade's difficulty");
}

/* 0x4d6290 */
void fe_4d6290(Menu *m)
{
    const char *names[2] = {0};
    Fe_Header();
    mode_names(names, false);
    Fe_Titles(m, title_x(), title_y(), true, 3, names, 2);
    List_Draw(m, NULL, -1, -1, 0, true);
    Strip_DrawMain(&pg_81d6d8, false, NULL);
}

/* 0x4f3610: the players strip (scroller 4) */
void fe_4f3610(Menu *m, bool back)
{
    (void)back;
    Scroll_Measure(Fe_Scroll(4), m);
}

void fe_4f39f0(Menu *m) { (void)m; Scroll_Update(Fe_Scroll(4)); }

/* 0x4d5ad0: a driver figure, 7 x 15 cells of the small matrix (0x523f30), shaded by a ripple */
static void figure(int px, int py, uint16_t phase)
{
    Tex *t = g_fe.sm_matrix;
    if (!t) return;
    int w, h;
    screen(&w, &h);
    static const int32_t o[2] = {0, 0};
    Rect16 src = {0, 0, t->width, t->height}, d = {(int16_t)px, 0, t->width, t->height};
    for (int c = 0; c < 7; c++) {
        int32_t fx = (int32_t)(((int64_t)((w * 0x12 / 0x280) * c + px) << 16) / w);
        d.y = (int16_t)py;
        for (int r = 0; r < 0xf; r++) {
            if (gm8(0x523f30 + (uint32_t)(c + r * 7))) {
                int32_t fy = (int32_t)(((int64_t)((h * 0x12 / 0x1e0) * r + py) << 16) / w);
                int32_t dx = fx - o[0], dy = fy - o[1];
                int32_t dist = (int32_t)Fx_Sqrt((uint32_t)fx_mul(dx, dx) + (uint32_t)fx_mul(dy, dy)) % 0x140000;
                int32_t v = g_sin[((int32_t)(((int64_t)dist << 16) / 0x140000) + phase) & 0xfff];
                int32_t k = v / 4 + 0xc000;
                uint8_t g = (uint8_t)(((int64_t)k * 0xff0000) >> 32);
                uint8_t rgba[4] = {g, g, g, 0xff};
                Sprite_Add(&src, &d, t, 1, 0, NULL, NULL, rgba, 8);
            }
            d.y = (int16_t)(d.y + h * 8 / 0x1e0);
        }
        d.x = (int16_t)(d.x + w * 8 / 0x280);
    }
}

/* 0x4d5fb0: the trail, the strip, a figure for each player up to the selection */
void fe_4d5fb0(Menu *m)
{
    uint16_t phase = (uint16_t)((uint32_t)((Timer_Centis() + 1) * (uint32_t)-0x2000) / 360);
    const char *names[2] = {0};
    mode_names(names, false);
    Fe_Header();
    Fe_Titles(m, title_x(), title_y(), true, 3, names, 2);
    Strip_Draw(Fe_Scroll(4), true);
    int w, h;
    screen(&w, &h);
    int a = h * 8 / 0x1e0;
    int y = (a - h * 6 / 0x1e0) / 2 - a * 15 / 2 + h / 2;
    int d = w * 8 / 0x280, e = w * 6 / 0x280, f = w * 10 / 0x280, n = m->sel + 1;
    int x = w / 2 - ((e + 6 * d) * n + f * m->sel) / 2;
    for (int k = 0; k < n; k++) {
        figure(x, y, phase);
        x += f + e + 6 * d;
    }
    Fe_Hint(Text_Get(0x57), true);
}

/* 0x4ef7c0: the number of players; one player straight to the driver select, two the split screen
   or alternate page (not in time trial), more alternate */
void fe_4ef7c0(Menu *m, MenuItem *it)
{
    gw32(0x52afb4, (gm32(0x52afb4) & 0xfffe1fffu) | ((uint32_t)(m->sel + 1) & 0xf) << 13);   /* 0x4ea8c0 */
    Players_DropAll();
    if (mode() == 4) {
        PENDING_NOTE(0x4eae40, "the challenge's players");
        gw32(0x52afb4, gm32(0x52afb4) & ~0x20000u);
        pg_81cf58.frame = NULL;
        pg_81cf58.parent = &pg_824a38;
        Menu_SetParent(&pg_824a38, m);
        to_set = (uint8_t)players();
        fe_4f0ac0(m, it);
        return;
    }
    pg_81cf58.frame = fe_4f39d0;
    pg_81cf58.parent = &pg_821e18;
    bool alternate;
    if (players() == 1) alternate = false;
    else {
        if (mode() != 3 && players() == 2) {
            switch (mode()) {
            case 0: Menu_Goto(&pg_8230d8); return;
            case 1: Menu_Goto(&pg_824858); return;
            case 2: Menu_Goto(&pg_8251b8); return;
            default: return;
            }
        }
        alternate = true;
    }
    gw32(0x52afb4, (gm32(0x52afb4) & ~0x20000u) | (uint32_t)alternate << 17);   /* 0x4ea950 */
    players_page = m;
    Menu_SetParent(&pg_8241b8, m);
    to_set = (uint8_t)players();
    Menu_Goto(&pg_8241b8);
}

/* 0x4f03f0: the driver select for the next player; back here, the last player's set-up undone */
void fe_4f03f0(Menu *m, bool back)
{
    if (back) {
        int p = (int)players() - 1 - to_set;
        if (Player_HasProfile(p) || from_tag) {
            if (Player_HasProfile(p)) {
                guests--;
                Player_SetGuest(p, false);
            }
            Profile_Create(p);
        }
        Player_DropProfile(p);
        to_set++;
    }
    from_tag = false;
    shown_player = (int)players() - to_set + 1;
    Fe_SetPlayer((int)players() - to_set);
    /* the free profiles as "Load <name>" items 3-6 */
    for (int i = 0; i < 4; i++) {
        MenuItem *it = &m->items[3 + i];
        if (i < Profiles_Free()) {
            snprintf(profile_names[i], sizeof profile_names[i], Text_Get(0x17e), Profile_FreeName(i));
            it->flags |= MIF_ENABLED | MIF_SELECTABLE;
            it->label = profile_names[i];
        } else
            it->flags &= (uint8_t)~(MIF_ENABLED | MIF_SELECTABLE);
    }
    m->sel = Profiles_Free() > 0 ? 3 : 2;
}

/* 0x4d9880: the trail (mode, "Player n", the page) and the list */
void fe_4d9880(Menu *m)
{
    char who[64];
    snprintf(who, sizeof who, Text_Get(0xdc), shown_player);
    const char *names[4] = {0, 0, who, Text_Get(m->text)};
    if (gm32(0x52afb4) >> 19 & 1) names[0] = Text_Get(0x12), names[1] = NULL, names[2] = NULL;
    else mode_names(names, true);
    Fe_Header();
    Fe_TitleRow(title_x(), title_y(), names, 4);
    List_Draw(m, NULL, -1, -1, 0, true);
    Fe_Hint(Text_Get(0x57), true);
    if (gm32(0x52afb4) >> 19 & 1) Strip_DrawMain(&pg_81d6d8, false, NULL);
}

/* 0x4f07e0: back past the first player returns to the players page */
void fe_4f07e0(Menu *m, bool back)
{
    (void)m;
    Fe_SetPlayer(0);
    if (back && to_set == players()) {
        to_set = (uint8_t)players();
        Menu_Goto(players_page);
    }
}

/* the page after the drivers: the car select, or the arcade's (0x4fa300-0x4fa320) */
static Menu *next_after_driver(void)
{
    switch (mode()) {
    case 4: return &pg_81bc98;   /* 0x4f8430 */
    case 5: return &pg_826f60;   /* 0x4fa300 */
    case 6: return &pg_828500;   /* 0x4fa310 */
    case 7: return &pg_827e60;   /* 0x4fa320 */
    default: return &pg_821e18;  /* 0x4f83f0 */
    }
}

/* 0x4f0ac0: play as a guest: a profile for the player, then the tag entry */
void fe_4f0ac0(Menu *m, MenuItem *it)
{
    (void)it;
    guests++;
    tag_player = (int)players() - to_set;   /* 0x4f2bf0 */
    if (mode() == 4) {
        Menu_SetParent(&pg_824a38, m);
        after_tag = &pg_81cf58;
        PENDING_NOTE(0x4f8430, "the challenge's driver");
    } else {
        Menu *next = next_after_driver();
        int p = (int)players() - to_set;
        Player_AssignProfile(p, -1);
        Profile_Create(p);
        Player_SetGuest((int)players() - to_set, false);
        Menu_SetParent(&pg_824a38, m);
        Player_SetGuest((int)players() - to_set, true);
        if (!(gm32(0x52afb4) >> 19 & 1)) {
            after_tag = next;
            Menu_SetParent(next, m);
        } else
            after_tag = &pg_822598;
    }
    snprintf(player_title, sizeof player_title, Text_Get(0xdc), (int)players() - to_set + 1);
    to_set--;
    Menu_Goto(&pg_824a38);
}

/* 0x4f0c50: a saved driver's profile */
void fe_4f0c50(Menu *m, MenuItem *it)
{
    (void)it;
    int prof = Profile_FreeIndex(m->sel - 3);
    Menu *next = mode() == 4 ? &pg_81bc98 : next_after_driver();
    Player_AssignProfile((int)players() - to_set, prof);
    Player_SetGuest((int)players() - to_set, false);
    to_set--;
    if (!(gm32(0x52afb4) >> 19 & 1)) {
        Menu_SetParent(next, m);
        Menu_Goto(next);
    } else {
        Menu_SetParent(&pg_822598, m);
        Menu_Goto(&pg_822598);
    }
}

/* 0x408400: the player's name (the profile's first bytes; in mode 4 the challenge record's) */
static char *player_name(int n)
{
    if (mode() == 4) return (char *)gm(0x52f3e0 + 0xc4u * (uint32_t)n);
    uint32_t p = Player_Word(n) >> 0x12 & 0xf;
    return p == 0xf ? NULL : (char *)gm(0x52fa18 + p * 0x650);
}

/* 0x4eae90 */
static void set_player_name(int n, const char *s)
{
    if (mode() == 4) return;
    uint32_t p = Player_Word(n) >> 0x12 & 0xf;
    if (p != 0xf) snprintf((char *)gm(0x52fa18 + p * 0x650), 0x10, "%s", s);
}

/* 0x4f0d30: typing on, the cursor at "end" when back, else the name cleared */
void fe_4f0d30(Menu *m, bool back)
{
    Fe_SetPlayer(tag_player);
    Menu_AllowDiagonal(true);
    tag_new = true;
    if (back) {
        m->sel = 2;
        m->items[2].value = (uint8_t)(m->items[2].count - 1);
    } else {
        m->sel = 0;
        m->items[0].value = 0;
        set_player_name(tag_player, "");
    }
    Input_FlushChars();
    typed = false;
}

/* the rows' letters: abcdefghij, klmnopqrst, uvwxyz. <_ (0x525374, '<' rub out, '_' done) */
static const char *row_letters(int row) { return exe_str(0x525374 + 0xcu * (uint32_t)row); }

/* 0x4f0e80: a typed letter goes in (to three) and moves the cursor to it; backspace rubs out; the
   cursor goes to "end" a tenth of a second after typing */
void fe_4f0e80(Menu *m)
{
    char name[16] = "";
    const char *p = player_name(tag_player);
    if (p) snprintf(name, sizeof name, "%s", p);
    int c, snd = 0;
    if (!Input_NextChar(&c)) {
        if (!typed || Timer_Centis() - typed_at < 0xb) goto done;
        typed_at = (uint32_t)-1;
        m->sel = 2;
        m->items[2].value = 9;
        typed = false;
    } else if (c == 8) {
        if (name[0]) {
            name[strlen(name) - 1] = 0;
            Menu_Sound(2);
        }
        m->sel = 2;
        m->items[2].value = 8;
        typed_at = Timer_Centis();
        typed = true;
        goto done;
    } else {
        size_t l = strlen(name);
        if (l > 2 || !strchr(exe_str(0x5253a0), c)) goto done;
        if (strchr(exe_str(0x5253d8), c)) c += 0x20;
        name[l] = (char)c;
        name[l + 1] = 0;
        if (c > '`' && c < 'k') m->sel = 0, m->items[0].value = (uint8_t)(c - 'a');
        if (c > 'j' && c < 'u') m->sel = 1, m->items[1].value = (uint8_t)(c - 'k');
        if (c > 't' && c < '{') m->sel = 2, m->items[2].value = (uint8_t)(c - 'u');
        if (c == '.') m->sel = 2, m->items[2].value = 7;
        else if (c == ' ') m->sel = 2, m->items[2].value = 8;
        typed_at = Timer_Centis();
        typed = true;
        snd = 1;
    }
    Menu_Sound(snd);
done:
    set_player_name(tag_player, name);
}

/* 0x4f1040: a letter chosen with the keys: in, '<' rubs out, '_' ends (with a name) */
void fe_4f1040(Menu *m, MenuItem *it)
{
    (void)it;
    char name[16] = "";
    const char *p = player_name(tag_player);
    if (p) snprintf(name, sizeof name, "%s", p);
    size_t l = strlen(name);
    int16_t row = m->items[m->sel].id;
    int snd;
    const char *u = row_letters(2);
    if (row == 2 && u[m->items[2].value] == '<') {
        if (!name[0]) goto store;
        name[l - 1] = 0;
        snd = 2;
    } else if (row == 2 && u[m->items[2].value] == '_') {
        if ((int)l < 1) return;
        Menu_Goto(after_tag);
        return;
    } else if (l < 3) {
        name[l] = row_letters(row)[m->items[m->sel].value];
        name[l + 1] = 0;
        if (l == 2) m->sel = 2, m->items[2].value = 9;
        snd = 1;
    } else
        snd = 3;
    Menu_Sound(snd);
store:
    set_player_name(tag_player, name);
}

/* 0x4f0da0: typing off; back in a challenge to the previous guest */
void fe_4f0da0(Menu *m, bool back)
{
    (void)m;
    Fe_SetPlayer(0);
    Menu_AllowDiagonal(false);
    if (!back) return;
    tag_new = false;
    if (mode() == 4 && !gm8(0x818848)) {
        if (--guests > 0) {
            to_set++;
            tag_player = (int)players() - to_set - 1;
            snprintf(player_title, sizeof player_title, Text_Get(0xdc), (int)players() - to_set);
            return;
        }
        Menu_Goto(&pg_81e5d8);
    }
}

/* 0x4d27e0: a rectangle's outline */
static void outline(const int16_t r[4], const uint8_t *c)
{
    int16_t a[4] = {r[0], (int16_t)(r[1] + 1), 1, (int16_t)(r[3] - 2)};
    Rect_Fill(a, c, 4);
    a[0] = (int16_t)(r[2] + r[0] - 1);
    Rect_Fill(a, c, 4);
    int16_t b[4] = {r[0], r[1], r[2], 1};
    Rect_Fill(b, c, 4);
    b[1] = (int16_t)(r[3] + r[1] - 1);
    Rect_Fill(b, c, 4);
}

/* 0x4d6f10: the trail, the three rows of letters (the cursor's lit, the space boxed), the name in
   big letters padded with random ones */
void fe_4d6f10(Menu *m)
{
    int w, h;
    screen(&w, &h);
    const char *names[2] = {player_title, NULL};
    if (tag_new) names[1] = Text_Get(Player_HasProfile(tag_player) ? 0xe5 : 0x17b);
    Fe_Titles(m, title_x(), title_y(), true, 3, names, 2);
    Fe_Header();
    for (int row = 0; row < 3; row++)
        for (int i = 0; i < 10; i++) {
            int font = 2;
            char s[64];
            char ch = exe_str(0x5249a8 + 0xcu * (uint32_t)row)[i];
            if (row == 2 && ch == '<') font = 1, snprintf(s, sizeof s, "%s", Text_Get(0x13f));
            else if (row == 2 && ch == '_') font = 1, snprintf(s, sizeof s, "%s", Text_Get(0x140));
            else snprintf(s, sizeof s, "%c", ch);
            MenuItem *it = &m->items[m->sel];
            uint32_t c = it->value == i && it->id == row ? 0x524968 : 0x52496c;
            if (row == 2 && ch == ' ') {
                int th = Font_TextHeight(font, "");
                int16_t r[4] = {(int16_t)((i + 2) * w / 0xe), (int16_t)(h * 6 / 8 - th / 2), (int16_t)(w / 0xe), (int16_t)(th / 2)};
                outline(r, gm(c));
            }
            Font_Print(font, s, w / 0x1c + (i + 2) * w / 0xe, h * (row + 4) / 8, gm(c), 0x12);
        }
    char cur[16] = "", shown[16] = "";
    if (tag_new) {
        const char *p = player_name(tag_player);
        if (p) snprintf(cur, sizeof cur, "%s", p), snprintf(shown, sizeof shown, "%s", p);
    }
    while (strlen(shown) < 3) {
        size_t l = strlen(shown);
        shown[l] = (char)(crt_rand() % 0x1a + 'a');
        shown[l + 1] = 0;
    }
    for (int i = -1, k = 0; i <= 1; i++, k++) {
        char s[2] = {shown[i + 1], 0};
        Font_Print(2, s, i * w / 0xe + w / 2, h * 2 / 8, gm((int)strlen(cur) <= k ? 0x52496c : 0x524968), 0x12);
    }
    Fe_Hint(Text_Get(0x57), true);
    if (gm32(0x52afb4) >> 19 & 1) Strip_DrawMain(&pg_81d6d8, false, NULL);
}

int Fe_ToSet(void) { return to_set; }
int Fe_SetupPlayer(void) { return (int)players() - to_set - 1; }
void Fe_PlayerTitle(char *dst, size_t cap) { snprintf(dst, cap, Text_Get(0xdc), (int)players() - to_set); }
void Fe_ClearGuests(void) { guests = 0; }

/* ---- the arcade (page 0x8267e0 and its players page 0x8276e0) ---- */

/* 0x4e2500: the arcade's title, its list, the main strip */
void fe_4e2500(Menu *m)
{
    const char *names[1] = {Text_Get(0x94)};
    Fe_Header();
    Fe_Titles(m, title_x(), title_y(), true, 2, names, 1);
    List_Draw(m, NULL, -1, -1, 0, true);
    Strip_DrawMain(&pg_81d6d8, false, NULL);
}

/* 0x4ef740: going on from the arcade page: the mode by the item (5 the first, 6 the second; the third,
   7, also one player), the rally and stage back to the first */
void fe_4ef740(Menu *m, bool back)
{
    if (back) return;
    gw32(0x52afb4, gm32(0x52afb4) & ~0x20000u);   /* 0x4ea950 */
    switch (m->sel) {
    case 0: Fe_SetMode(5); break;
    case 1: Fe_SetMode(6); break;
    case 2:
        Fe_SetMode(7);
        gw32(0x52afb4, (gm32(0x52afb4) & 0xfffe1fffu) | 1u << 13);   /* 0x4ea8c0 */
        to_set = (uint8_t)players();
        gw32(0x52afb4, gm32(0x52afb4) & ~0x20000u);
        break;
    default: break;
    }
    set_rally(0);
    set_stage(0);
}

/* 0x4fa890: the players strip (scroller 4) for the arcade's players page: the items' widths as they
   are (not lowered, unlike 0x4f3610), its slide and selection kept; the selection at the players
   (the first beyond two) */
void fe_4fa890(Menu *m, bool back)
{
    (void)back;
    Sel_SetCpuCount(5);   /* 0x40d640 */
    FeScroll *s = Fe_Scroll(4);
    s->t = Fe_Now();
    s->page = m;
    s->n = m->n;
    for (int i = 0; i < m->n; i++) s->width[i] = Font_TextWidth(2, Text_Get(m->items[i].text));
    m->sel = (int8_t)(players() > 2 ? 0 : (int)players() - 1);
}

void fe_4f9520(Menu *m) { fe_4f39f0(m); }

/* 0x4fa910: the players chosen: straight to the driver select (no split screen choice) */
void fe_4fa910(Menu *m, MenuItem *it)
{
    (void)it;
    gw32(0x52afb4, (gm32(0x52afb4) & 0xfffe1fffu) | ((uint32_t)(m->sel + 1) & 0xf) << 13);   /* 0x4ea8c0 */
    Sel_SetCpuCount(players() == 1 ? 5 : 0);
    to_set = (uint8_t)players();
    gw32(0x52afb4, gm32(0x52afb4) & ~0x20000u);   /* 0x4ea950 */
    Menu_SetParent(&pg_8241b8, m);
    players_page = m;
    Menu_Goto(&pg_8241b8);
}

/* 0x4fa9b0: the arcade's third item: one player, to the driver select */
void fe_4fa9b0(Menu *m, MenuItem *it)
{
    (void)it;
    gw32(0x52afb4, (gm32(0x52afb4) & 0xfffe1fffu) | 1u << 13);
    to_set = (uint8_t)players();
    gw32(0x52afb4, gm32(0x52afb4) & ~0x20000u);
    Menu_SetParent(&pg_8241b8, m);
    players_page = m;
    Menu_Goto(&pg_8241b8);
}

/* ---- the arcade's difficulty (0x826600, 0x828220) and locations (0x827320, 0x826420) ---- */

static bool option(int n) { return gm32(0x52e890) >> n & 1; }   /* 0x406410 */
static void item_on(Menu *m, int i, bool on) { m->items[i].flags = (uint8_t)(on ? m->items[i].flags | 1 : m->items[i].flags & ~1); }

/* 0x4faa00: the third difficulty open with the unlocks' bit 1 (0x52b03c) or option 13; the selection
   at the difficulty (0x405d90) */
void fe_4faa00(Menu *m, bool back)
{
    (void)back;
    item_on(m, 2, (gm32(0x52b03c) & 2) || option(0xd));
    int d = (int)Gs_Mode2();
    m->sel = (int8_t)(d < m->n ? d : m->n - 1);
}

/* 0x4fb010: the locations open by the unlocks word (2 bits a difficulty at bits 2, 4, 6: three
   locations a step), on both location pages; in mode 7 or with two players the best of the three,
   one player by the difficulty. Single race novice for one player shows three; otherwise eight, the
   seventh and eighth with the unlocks' bits 20 and 21 (or option 13). The strip (scroller 10). */
void fe_4fb010(Menu *m, bool back)
{
    (void)back;
    Fe_SetPlayer(0);
    FeScroll *s = Fe_Scroll(10);
    s->t = Fe_Now();
    s->n = m->n;
    s->page = m;
    for (int i = 0; i < 8; i++) item_on(&pg_827320, i, false), item_on(&pg_826420, i, false);
    uint32_t u = gm32(0x52b03c);
    int a = (int)(u >> 2 & 3) * 3, b = (int)(u >> 4 & 3) * 3, c = (int)(u >> 6 & 3) * 3, k;
    if (mode() == 7 || players() != 1) k = a > b ? (a > c ? a : c) : (b > c ? b : c);
    else switch (Gs_Mode2()) {
    case 0: k = a; break;
    case 1: k = b; break;
    case 2: k = c; break;
    default: k = 0x7fffffff; break;   /* the original takes the page's address here: all open */
    }
    for (int i = 0; i < 6; i++) {
        bool on = i < k || option(0xd);
        item_on(&pg_827320, i, on), item_on(&pg_826420, i, on);
    }
    Menu *last = mode() == 7 ? &pg_826420 : &pg_827320;
    if (mode() != 7 && Gs_Mode2() == 0 && players() == 1) {
        s->n = 3, m->n = 3;
        for (int i = k; i < 3; i++) item_on(&pg_827320, i, false);
    } else {
        s->n = 8, m->n = 8;
        if ((u & 0x100000) || option(0xd)) item_on(last, 6, true);
        if ((u & 0x200000) || option(0xd)) item_on(last, 7, true);
    }
    int w, h;
    screen(&w, &h);
    s->gap = w * 0x18 / 0x280;
    s->n = m->n;
    s->off = s->off0 = 0;
    for (int i = 0; i < m->n; i++) {
        char t[256];
        snprintf(t, sizeof t, "%s", Text_Get(m->items[i].text));
        s->width[i] = Font_TextWidth(2, Text_Lower(t));
    }
}

void fe_4fb360(Menu *m) { (void)m; Scroll_Update(Fe_Scroll(10)); }

/* 0x4fb370: the location (three stages each: the rally and stage index), the CPU cars (none with two
   players); single race on to its next page (0x828040, two players 0x827aa0), otherwise three laps and
   the start */
void fe_4fb370(Menu *m, MenuItem *it)
{
    (void)it;
    gw32(0x52f2ac, (gm32(0x52f2ac) & 0xfffff3ffu) | ((uint32_t)(m->sel / 3) & 3) << 10);   /* 0x40d600 */
    gw32(0x52f2ac, (gm32(0x52f2ac) & 0xffffcfffu) | ((uint32_t)(m->sel % 3) & 3) << 12);   /* 0x406960 */
    Sel_ModeFlags();
    Sel_SetCpuCount(players() == 2 ? 0 : 5);
    if (mode() == 6) {
        Menu_Goto(players() == 2 ? &pg_827aa0 : &pg_828040);
        return;
    }
    gw32(0x52f2ac, (gm32(0x52f2ac) & 0xfff0ffffu) | 3u << 16);   /* 0x40d620 */
    Menu_Goto(&pg_8214b8);
}

/* 0x4dc710: the trail (arcade, single race), the strip (scroller 10), the location's picture */
void fe_4dc710(Menu *m)
{
    const char *names[2] = {Text_Get(0x94), Text_Get(0xe2)};
    Fe_Titles(m, title_x(), title_y(), true, 3, names, 2);
    Fe_Header();
    Strip_Draw(Fe_Scroll(10), true);
    PENDING_NOTE(0x4dbd80, "the arcade location's picture and records");
}

/* ---- the single race's options (0x828040): laps, and the CPU cars (two players: an on/off) ---- */

static uint32_t opts(void) { return gm32(0x52afbc); }

/* 0x4f3220: the laps (0x52afbc bits 3-7) into the first list; the second: with two players an on/off
   (bit 11, id 2), else the CPU cars, 1-5 (bits 8-10, id 1); the start item selected */
void fe_4f3220(Menu *m, bool back)
{
    (void)back;
    m->items[0].value = (uint8_t)((opts() >> 3 & 0x1f) - 1);
    if (players() == 2) {
        m->items[1].id = 2;
        m->items[1].value = 0;
        m->items[1].count = 2;
        m->items[1].value = (opts() >> 0xb & 1) != 0;
    } else {
        m->items[1].id = 1;
        m->items[1].count = 5;
        m->items[1].value = (uint8_t)((opts() >> 8 & 7) - 1);
    }
    m->sel = 2;
}

/* 0x4f0090: the laps and the CPU cars (none with two players, the on/off kept instead) kept, the
   start */
void fe_4f0090(Menu *m, MenuItem *it)
{
    (void)it;
    uint32_t laps = (uint32_t)m->items[0].value + 1;
    gw32(0x52f2ac, (gm32(0x52f2ac) & 0xfff0ffffu) | (laps & 0xf) << 16);   /* 0x40d620 */
    gw32(0x52afbc, (opts() & 0xffffff07u) | (laps & 0x1f) << 3);           /* 0x4eaa90 */
    if (players() == 2) {
        Sel_SetCpuCount(0);
        gw32(0x52afbc, (opts() & 0xfffff7ffu) | (m->items[1].value & 1u) << 0xb);   /* 0x4eac80 */
    } else {
        uint32_t n = (uint32_t)m->items[1].value + 1;
        Sel_SetCpuCount((int)n);
        gw32(0x52afbc, (opts() & 0xfffff8ffu) | (n & 7) << 8);   /* 0x4eaa60 */
        gw32(0x52afbc, opts() & 0xfffff7ffu);
    }
    Menu_Goto(&pg_8214b8);
}

/* a row's label for 0x4da710 (one player) and 0x4daf90 (two): one player's id 0 "Number Of Laps: n" and
   id 1 "Number Of CPU Drivers: n" (the first and second lists' values, whatever the row); with two
   players id 3 "<0x1a3>: <0x1a4 + value>", ids 4, 5, 6 "<0x195, 0x1a2, 0x1a1>: <value + 1>" (the row's
   own value); id 2 "Catch Up:" with Off and On (the chosen lit); any other its own text */
static void setup_row(Menu *m, MenuItem *it, bool two, int x, int y, uint32_t c, int w)
{
    char b[256];
    int id = it->id;
    if (!two && (id == 0 || id == 1))
        snprintf(b, sizeof b, "%s: %d", Text_Get(id == 0 ? 0x195 : 0x196), (id == 0 ? m->items[0].value : m->items[1].value) + 1);
    else if (two && id == 3)
        snprintf(b, sizeof b, "%s: %s", Text_Get(0x1a3), Text_Get(it->value + 0x1a4));
    else if (two && id >= 4 && id <= 6)
        snprintf(b, sizeof b, "%s: %d", Text_Get(id == 4 ? 0x195 : id == 5 ? 0x1a2 : 0x1a1), it->value + 1);
    else if (id == 2) {
        snprintf(b, sizeof b, "%s:", Text_Get(0x19e));
        Font_Print(1, b, x, y, gm(c), 0x11);
        int x1 = w * 10 / 0x280 + x + Font_TextWidth(1, b);
        int x2 = w * 0x14 / 0x280 + x + Font_TextWidth(1, Text_Get(0x133)) + Font_TextWidth(1, b);
        Font_Print(1, Text_Get(0x133), x1, y, gm(it->value == 0 ? 0x524968 : 0x52496c), 0x11);
        Font_Print(1, Text_Get(0x134), x2, y, gm(it->value == 0 ? 0x52496c : 0x524968), 0x11);
        return;
    } else
        snprintf(b, sizeof b, "%s", Text_Get(it->text));
    Font_Print(1, b, x, y, gm(c), 0x11);
}

/* 0x4da710 / 0x4daf90: the single race's options as 0x4d8ed0's rows (the arrows Ar_640A lit, Ar_640D
   or, when the item is off, the third colour), the labels by setup_row; the trail Arcade, Single
   Race; the hint twice and the locations' strip (scroller 10) unlit */
static void setup_draw(Menu *m, bool two)
{
    int w, h;
    screen(&w, &h);
    const char *names[2] = {Text_Get(0x94), Text_Get(0xe2)};
    Fe_Header();
    Fe_Titles(m, w * 0x18 / 0x280, h * 0x26 / 0x1e0, true, 3, names, 2);
    Fe_Hint(Text_Get(0x57), true);
    int row = h * 0x24 / 0x1e0;
    int16_t top = (int16_t)((h * 0x38 / 0x1e0 + h * 0x176 / 0x1e0) / 2 - row * m->n / 2);
    int16_t r[4] = {(int16_t)(w * 99 / 0x280), top, (int16_t)(w * 0x11a / 0x280), 1};
    Rect_Fill(r, gm(m->sel == 0 ? 0x524974 : 0x524978), 1);
    r[1]++;
    Rect_Fill(r, gm(m->sel == 0 ? 0x524968 : 0x52496c), 1);
    Tex *a = g_fe.arrow_a;
    int16_t aw = a ? (int16_t)a->width : 0, ah = a ? (int16_t)a->height : 0;
    for (int i = 0; i < m->n; i++) {
        MenuItem *it = &m->items[i];
        bool on = m->sel == i;
        Tex *t = on ? g_fe.arrow_a : g_fe.arrow_d;
        uint32_t c = on ? 0x524968 : (it->flags & 1) ? 0x52496c : 0x524970;
        if (t) {
            Rect16 src = {0, 0, t->width, t->height};
            Rect16 dst = {(int16_t)(w * 100 / 0x280), (int16_t)(h * 0x14 / 0x1e0 + top + row * i - ah / 2), aw, ah};
            Sprite_Add(&src, &dst, t, 1, 0, NULL, NULL, gm(c), 8);
        }
        setup_row(m, it, two, w * 0x7a / 0x280, h * 0x18 / 0x1e0 + r[1], c, w);
        bool lit = m->sel == i || m->sel == i + 1;
        r[1] = (int16_t)(row * (i + 1) + top);
        Rect_Fill(r, gm(lit ? 0x524974 : 0x524978), 1);
        r[1]++;
        Rect_Fill(r, gm(lit ? 0x524968 : 0x52496c), 1);
    }
    Fe_Hint(Text_Get(0x57), true);
    Strip_Draw(Fe_Scroll(10), false);
}

void fe_4da710(Menu *m) { setup_draw(m, false); }
void fe_4daf90(Menu *m) { setup_draw(m, true); }

/* ---- the two players' single race (0x827aa0): the kind (0x52f2b0 bits 0-2), its count, the
   duel's on/off (0x52afbc bit 11) ---- */

static int duel_kind(void) { return (int)(gm32(0x52f2b0) & 7); }   /* 0x4082b0 */
static int kind_shown;                                             /* DAT_00829320 */

/* the second list by the kind: 0 the laps (1-10, 0x52afbc bits 3-7, id 4), 1 bits 12-15 (id 6), 2
   bits 16-19 (id 5) */
static void duel_count(Menu *m, int kind)
{
    MenuItem *it = &m->items[1];
    int shift = kind == 0 ? 3 : kind == 1 ? 0xc : 0x10;
    if (kind < 0 || kind > 2) return;
    it->count = 10;
    it->value = (uint8_t)((opts() >> shift & (kind == 0 ? 0x1f : 0xf)) - 1);
    it->id = (int16_t)(kind == 0 ? 4 : kind == 1 ? 6 : 5);
}

/* 0x4f3280: the kind into the first list, its count into the second, the on/off into the third; the
   start item selected */
void fe_4f3280(Menu *m, bool back)
{
    (void)back;
    kind_shown = duel_kind();
    m->items[0].value = (uint8_t)duel_kind();
    duel_count(m, duel_kind());
    m->items[2].value = (opts() >> 0xb & 1) != 0;   /* 0x406440 */
    m->sel = 3;
}

/* the second list's value kept by the kind: the laps (0x40d620, 0x4eaa90), 0x52f2b0 bits 3-6 with
   0x52afbc bits 12-15 (0x40e3a0, 0x4eacb0) or bits 7-10 with bits 16-19 (0x40e380, 0x4eacd0) */
static void duel_keep(int kind, uint32_t n)
{
    if (kind == 0) {
        gw32(0x52f2ac, (gm32(0x52f2ac) & 0xfff0ffffu) | (n & 0xf) << 16);
        gw32(0x52afbc, (opts() & 0xffffff07u) | (n & 0x1f) << 3);
    } else if (kind == 1) {
        gw32(0x52f2b0, (gm32(0x52f2b0) & 0xffffff87u) | (n & 0xf) << 3);
        gw32(0x52afbc, (opts() & 0xffff0fffu) | (n & 0xf) << 0xc);
    } else if (kind == 2) {
        gw32(0x52f2b0, (gm32(0x52f2b0) & 0xfffff87fu) | (n & 0xf) << 7);
        gw32(0x52afbc, (opts() & 0xfff0ffffu) | (n & 0xf) << 0x10);
    }
}

/* 0x4f3310: a new kind reloads the second list; its value kept by the shown kind; the strip
   (scroller 10) */
void fe_4f3310(Menu *m)
{
    int k = m->items[0].value;
    if (kind_shown != k) {
        duel_count(m, k);
        kind_shown = k;
    }
    duel_keep(k, (uint32_t)m->items[1].value + 1);
    Scroll_Update(Fe_Scroll(10));
}

/* 0x4f0110: no CPU cars (0x40d640), the kind (0x40e360) and the on/off (0x4eac80) kept, the count by
   the kind kept so far (0x4082b0, the kind just written), the start */
void fe_4f0110(Menu *m, MenuItem *it)
{
    (void)it;
    Sel_SetCpuCount(0);
    gw32(0x52f2b0, (gm32(0x52f2b0) & ~7u) | (m->items[0].value & 7u));
    gw32(0x52afbc, (opts() & 0xfffff7ffu) | (m->items[2].value & 1u) << 0xb);
    duel_keep(duel_kind(), (uint32_t)m->items[1].value + 1);
    Menu_Goto(&pg_8214b8);
}
