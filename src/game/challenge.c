/* The challenge (see challenge.h). */
#include "game/challenge.h"
#include "audio/crash.h"
#include "audio/engine.h"
#include "audio/surface.h"
#include "fe/menu.h"
#include "game/ctlcfg.h"
#include "game/gstate.h"
#include "game/hud.h"
#include "game/input.h"
#include "game/mem.h"
#include "game/opponents.h"
#include "game/pause.h"
#include "game/profile.h"
#include "game/race.h"
#include "game/replay.h"
#include "game/select.h"
#include "fe/fe.h"
#include "game/tween.h"
#include "render/d3d7.h"
#include "render/rstate.h"
#include "render/tex.h"
#include "render/twod.h"
#include "sim/fade.h"
#include "world/frame.h"
#include "exe.h"
#include "font.h"
#include "text.h"
#include <stdio.h>
#include <string.h>

enum { REC = 0x52f2b4 };

/* state 6's variables, kept where the original has them */
enum {
    T_GROW = 0x58ca80,     /* the boxes growing in (the round's first frames) */
    T_NEXT = 0x58ca8c,     /* the winners moving on to the next round */
    T_ROUND = 0x58cc70,    /* the round's end, its boxes fading */
    T_LINES = 0x58cf60,    /* the items' lines and text */
    V_GROW = 0x58cc74,     /* T_GROW's or T_ROUND's value (16.16) */
    V_LINES = 0x58ce58,    /* T_LINES's value */
    V_MATCH = 0x58ce5c,    /* the match raced (set, not read) */
    F_GO = 0x58cf64,       /* "continue" chosen, or the times counted up */
    F_KEEP = 0x58cf68,
    F_PAGES = 0x58cf6c,    /* the pages built */
    F_THROUGH = 0x58cf70,  /* a player through the last round */
    V_TIME_A = 0x58cf74,   /* the times counting up on the result */
    V_TIME_B = 0x58cf78,
    V_SUB = 0x58cf7c,      /* the step: 0 set-up, 1 the draw shown, 2 a match next, 3 computers' match,
                              4 the players' match, 5 its result, 6 the round's end, 7 given up */
};

static Menu pg_go, pg_quit;   /* 0x58ca90 (continue, give up), 0x58cc78 (give up? yes, no) */
static Menu *page;            /* PTR_DAT_0051c97c */
static ProcGroup *group;      /* DAT_0058ca88 */

static void screen(int *w, int *h) { D3d_ScreenSize(w, h); }
static uint8_t *C(uint32_t va) { return gm(va); }   /* the colours at 0x51c984-0x51c9a8 */
static uint32_t rec(void) { return gm32(REC); }
static uint32_t round_kind(void) { return rec() >> 3 & 7; }
static uint32_t match_no(void) { return rec() >> 0xc & 0xf; }
static bool player(uint32_t d) { return Player_Driver((int)(d & 0x1f)) == -1; }   /* 0x408500 */
static uint32_t side_a(uint32_t m) { return gm32(m) & 0x1f; }
static uint32_t side_b(uint32_t m) { return gm32(m) >> 5 & 0x1f; }

/* the original's tests for a 1,024-wide mode with the texture memory (0x405c10, 0x4b7560, 0x4b7590):
   the port's device has one mode */
static bool hires(void) { return false; }

/* a round's matches and their count */
static uint32_t round_at(uint32_t k, int *n)
{
    switch (k) {
    case 1: *n = 8; return 0x52f30c;
    case 2: *n = 4; return 0x52f2dc;
    case 3: *n = 2; return 0x52f2c4;
    case 4: *n = 1; return 0x52f2b8;
    default: *n = 0; return 0;
    }
}

uint32_t Challenge_Match(void)
{
    int n;
    uint32_t a = round_at(round_kind(), &n);
    return a && round_kind() != 4 ? a + 12 * match_no() : a;
}

/* 0x473680: a bye (a side without a driver) */
static bool bye(uint32_t m) { return side_a(m) == 0x1f || side_b(m) == 0x1f; }

/* 0x472990: a player won the match */
bool Challenge_PlayerWon(uint32_t m)
{
    uint32_t w = gm32(m) & 0x1800;
    return (player(side_a(m)) && w == 0x800) || (player(side_b(m)) && w == 0x1000);
}

bool Challenge_PlayersLeft(void)
{
    int n;
    uint32_t m = round_at(round_kind(), &n);
    for (int i = 0; i < n; i++, m += 12) {
        if ((player(side_a(m)) && !(gm32(m) & 0x400)) || (player(side_b(m)) && !(gm32(m) & 0x400))) return true;
        if (Challenge_PlayerWon(m)) return true;
    }
    return false;
}

/* 0x473290: a player won a match of the round */
static bool player_through(void)
{
    int n;
    uint32_t m = round_at(round_kind(), &n);
    for (int i = 0; i < n; i++, m += 12)
        if (Challenge_PlayerWon(m)) return true;
    return false;
}

/* 0x473310: both drivers of the match computers */
static bool computers(void)
{
    uint32_t m = Challenge_Match();
    return !player(side_a(m)) && !player(side_b(m));
}

/* 0x407940: the match's times, the faster the winner (the second at a tie) */
static void set_times(int32_t a, int32_t b)
{
    uint32_t m = Challenge_Match();
    if (!m) return;
    gw32(m + 4, (uint32_t)a);
    gw32(m + 8, (uint32_t)b);
    uint32_t w = gm32(m);
    gw32(m, (uint32_t)b <= (uint32_t)a ? (w & ~0x800u) | 0x1000 : (w & ~0x1000u) | 0x800);
}

/* 0x407b10: each round's winners into the next round's matches (the even match's into the first
   side), the next round's sides then in order when both are players */
static void advance(void)
{
    static const uint32_t from[3] = {0x52f30c, 0x52f2dc, 0x52f2c4}, to[3] = {0x52f2dc, 0x52f2c4, 0x52f2b8};
    static const int count[3] = {8, 4, 2};
    for (int r = 0; r < 3; r++) {
        for (int i = 0; i < count[r]; i++) {
            uint32_t w = gm32(from[r] + 12u * (uint32_t)i), res = w & 0x1800;
            if (!res) continue;
            uint32_t d = res == 0x800 ? w & 0x1f : w >> 5 & 0x1f, at = to[r] + 12u * (uint32_t)(i / 2), v = gm32(at);
            gw32(at, i % 2 == 0 ? (v & ~0x1fu) | d : (v & ~0x3e0u) | d << 5);
        }
        for (int i = 0; i < count[r] / 2; i++) {
            uint32_t at = to[r] + 12u * (uint32_t)i, v = gm32(at), a = v & 0x1f, b = v >> 5 & 0x1f, n = Gs_Players();
            if (a < n && b < n && b < a) gw32(at, (v & 0xfffffc00u) | b | a << 5);
        }
    }
}

void Challenge_MatchEnd(void)
{
    uint32_t m = Challenge_Match(), a = side_a(m), b = side_b(m);
    int32_t ta, tb;
    if (player(a) || player(b)) {
        if (player(a)) ta = Times_Finish(0), tb = Times_Finish(1);
        else tb = Times_Finish(0), ta = Times_Finish(1);
    } else
        Opp_PairTimes((int)a, (int)b, &ta, &tb);   /* 0x455df0 */
    if (!bye(m))
        set_times(ta, tb);
    else {
        gw32(m + 4, 0), gw32(m + 8, 0);
        gw32(m, side_a(m) == 0x1f ? (gm32(m) & ~0x800u) | 0x1000 : (gm32(m) & ~0x1000u) | 0x800);
    }
    /* the head to head won by touching (0x41f380: the winner's car), with one player the other side */
    int win = Race_Winner();
    if (win != -1) {
        if (Gs_Players() == 2) gw32(m, (gm32(m) & ~0x1800u) | ((uint32_t)(-0x800 * win - 0x1000) & 0x1800));
        else gw32(m, player(side_a(m)) ? (gm32(m) & ~0x800u) | 0x1000 : (gm32(m) & ~0x1000u) | 0x800);
    }
    uint32_t r = rec(), k = match_no();
    r = (r & ~0xf000u) | ((r + 0x1000) & 0xf000);
    gw32(REC, r);
    int n;
    uint32_t at = round_at(r >> 3 & 7, &n);
    if ((r >> 3 & 7) == 4) gw32(REC, r | 0x400000), gw32(at, gm32(at) | 0x400);
    else {
        gw32(at + 12 * k, gm32(at + 12 * k) | 0x400);
        if ((int)(r >> 0xc & 0xf) >= n) gw32(REC, r | 0x400000);
    }
    advance();
}

bool Challenge_TwoPlayers(void)
{
    uint32_t m = Challenge_Match();
    return m && player(side_a(m)) && player(side_b(m));
}

void Challenge_Players(void)
{
    Gs_SetRacePlayers(Challenge_TwoPlayers() ? 2 : 1);   /* 0x407500 */
    for (int i = 0; i < 2; i++) {
        gw8(0x53acc8 + (uint32_t)i, (uint8_t)Ctl_Device(i));   /* 0x42b720 */
        if ((int)Gs_SelBits14() <= i) gw8(0x53acc8 + (uint32_t)i, 0xff);
    }
}

bool Challenge_Over(void)
{
    if (!(rec() & 0x800000) && gm32(F_THROUGH)) return false;
    gw32(REC, rec() & ~0x800000u);
    gw32(V_SUB, 0);
    Game_SetMode(0);
    return true;
}

void Challenge_Reset(void) { gw32(V_SUB, 0); }
void Challenge_KeepRestart(bool on) { gw32(F_KEEP, on); }
bool Challenge_KeptRestart(void) { return gm32(F_KEEP) != 0; }

/* the driver of a side: nobody's "", a player's name, else a computer's (0x407ec0) or its tag
   (0x407f20: the three letters at 0x5167e0) */
static const char *name_of(uint32_t m, int side, bool tag)
{
    static char b[64];   /* DAT_00663b60 */
    uint32_t d;
    if (side == 0) d = side_a(m);
    else if (side == 1) d = side_b(m);
    else return b;
    if (d == 0x1f) return (const char *)gm(0x52ea5c);
    int drv = Player_Driver((int)d);
    if (drv == -1) snprintf(b, sizeof b, "%s", Player_Name((int)d));
    else if (!tag) snprintf(b, sizeof b, "%s", exe_str(0x5166b4 + 0x14u * (uint32_t)(drv < 0 || drv > 15 ? 0 : drv)));
    else snprintf(b, sizeof b, "%s", (const char *)gm(0x5167e0 + 4u * (uint32_t)(drv < 0 || drv > 15 ? 0 : drv)));
    return b;
}

const char *Challenge_Name(uint32_t match, int side) { return name_of(match, side, false); }
const char *Challenge_Tag(uint32_t match, int side) { return name_of(match, side, true); }

bool Challenge_IsPlayer(uint32_t match, int side)
{
    uint32_t d = side ? side_b(match) : side_a(match);
    return d != 0x1f && player(d);
}

bool Challenge_Side(uint32_t match, int side)
{
    if (side_a(match) != 0x1f && player(side_a(match))) return side != 0;
    return side == 0;
}

/* 0x4752f0: a side's time ("" for nobody); counting up (0x27 a frame from 0x58cf74 / 0x58cf78) on
   the result, the go flag set once both have arrived */
static const char *time_of(uint32_t m, int side, bool count)
{
    static char b[32];
    uint32_t ta = gm32(m + 4), tb = gm32(m + 8);
    if (count) {
        if (gm32(V_TIME_A) <= ta - 0x27) ta = gm32(V_TIME_A) + 0x27;
        if (gm32(V_TIME_B) <= tb - 0x27) tb = gm32(V_TIME_B) + 0x27;
        gw32(V_TIME_A, ta), gw32(V_TIME_B, tb);
        if (ta == gm32(m + 4) && tb == gm32(m + 8)) gw32(F_GO, 1);
    }
    uint32_t t;
    if (side == 0) {
        if (side_a(m) == 0x1f) return (const char *)gm(0x52ea5c);
        t = ta;
    } else if (side == 1) {
        if (side_b(m) == 0x1f) return (const char *)gm(0x52ea5c);
        t = tb;
    } else return b;
    snprintf(b, sizeof b, exe_str(0x519f58), (int)t / 6000, (int)t % 6000 / 100, (int)t % 100);
    return b;
}

/* 0x474420: the first frac (16.16) of s, a space not counting, then the next letter */
static void type_in(const char *s, int32_t frac, int font, int x, int y, const uint8_t *c, uint32_t flags)
{
    char b[128];
    int n = (int)strlen(s), k = (int)((int64_t)(n << 16) * frac >> 32);
    if (n > (int)sizeof b - 1) n = (int)sizeof b - 1;
    int j = 0;
    for (; j < k && j < n; j++) {
        if (s[j] == ' ' && k < n) k++;
        b[j] = s[j];
    }
    if (k > n) k = n;
    b[k] = 0;
    Font_Print(font, b, x, y, c, flags);
    if (k < n) {
        int x2 = x + Font_TextWidth(font, b);
        char one[2] = {s[k], 0};
        uint8_t copy[4];
        memcpy(copy, c, 4);
        Font_Print(font, one, x2, y, copy, flags);
    }
}

/* 0x475970: a rectangle frac (16.16) of its width */
static void part_rect(int32_t frac, const int16_t r[4], const uint8_t *c, int layer)
{
    int16_t q[4] = {r[0], r[1], (int16_t)((int64_t)(r[2] << 16) * frac >> 32), r[3]};
    Rect_Fill(q, c, layer);
}

static void arrow(Tex *t, int16_t x, int16_t y, const uint8_t *c)
{
    if (!t) return;
    Rect16 src = {0, 0, t->width, t->height}, d = {x, y, t->width, t->height};
    Sprite_Add(&src, &d, t, 2, 0, NULL, NULL, c, 8);
}

/* 0x475740: a box: its fill, its 1-pixel edges, and for a driver out the RoCx_640 cross over it */
static void box(const int16_t r[4], const uint8_t *fill, const uint8_t *edge, bool out)
{
    if (fill) Rect_Fill(r, fill, 2);
    if (edge) {
        int16_t t[4] = {r[0], r[1], r[2], 1}, rr[4] = {(int16_t)(r[0] + r[2]), r[1], 1, (int16_t)(r[3] + 1)},
                b[4] = {r[0], (int16_t)(r[1] + r[3]), r[2], 1}, l[4] = {r[0], r[1], 1, r[3]};
        Rect_Fill(t, edge, 2), Rect_Fill(rr, edge, 2), Rect_Fill(b, edge, 2), Rect_Fill(l, edge, 2);
    }
    Tex *x = Pause_Roc();
    if (out && x) {
        int w, h;
        screen(&w, &h);
        Rect16 src = {0, 0, x->width, x->height};
        Rect16 d = {r[0], r[1], (int16_t)(x->width * r[2] * w / 0x280 / (w * 0x55 / 0x280)),
                    (int16_t)(x->height * r[3] * h / 0x1e0 / (h * 0x26 / 0x1e0))};
        Sprite_Add(&src, &d, x, 2, 0, NULL, NULL, edge, 8);
    }
}

/* 0x475430: a side's box (grown in by 0x58cc74 while T_GROW runs, or when forced) with the name and
   the time */
static void side_box(int side, uint32_t m, const int16_t r[4], const uint8_t *fill, const uint8_t *edge, bool out,
                     bool count, const uint8_t *text, bool force)
{
    int w, h;
    screen(&w, &h);
    if (Tween_Active(T_GROW) || force) {
        int32_t g = gms32(V_GROW);
        int16_t q[4] = {r[0], r[1], (int16_t)((int64_t)(r[2] << 16) * g >> 32), (int16_t)((int64_t)(r[3] << 16) * g >> 32)};
        box(q, fill, edge, out);
        return;
    }
    box(r, fill, edge, out);
    char b[128];
    snprintf(b, sizeof b, "%s", Challenge_Name(m, side));
    Text_ToFontCase(b);
    int ny = hires() ? h * 0x10 / 0x1e0 - h * 2 / 0x1e0 : h * 0x10 / 0x1e0, ty = hires() ? h * 7 / 0x1e0 + h * 0x1c / 0x1e0 : h * 0x1c / 0x1e0;
    Font_Print(0, b, r[0] + w * 0x54 / 0x280, r[1] + ny, text, 0x14);
    snprintf(b, sizeof b, "%s", time_of(m, side, count));
    Text_ToFontCase(b);
    Font_Print((int)gm32(0x51c980), b, r[0] + w * 0x54 / 0x280, r[1] + ty, text, 0x14);
}

/* 0x474fe0: a winner's box moving (by 0x58cc74) from its place to the next round's */
static void move_box(int x, int y, int tx, int ty, int side, uint32_t m)
{
    int w, h;
    screen(&w, &h);
    int32_t g = gms32(V_GROW);
    x += (tx - x) * g / 0x10000;
    y += (ty - y) * g / 0x10000;
    int16_t r[4] = {(int16_t)x, (int16_t)y, (int16_t)(w * 0x56 / 0x280), (int16_t)(h * 0x26 / 0x1e0)};
    box(r, C(0x51c9a4), C(0x51c990), false);
    char b[128];
    snprintf(b, sizeof b, "%s", Challenge_Name(m, side));
    Text_ToFontCase(b);
    int ny = hires() ? h * 0x10 / 0x1e0 - h * 2 / 0x1e0 : h * 0x10 / 0x1e0, ty2 = hires() ? h * 7 / 0x1e0 + h * 0x1c / 0x1e0 : h * 0x1c / 0x1e0;
    Font_Print(0, b, x + w * 0x54 / 0x280, y + ny, C(0x51c990), 0x14);
    snprintf(b, sizeof b, "%s", time_of(m, side, false));
    Text_ToFontCase(b);
    Font_Print((int)gm32(0x51c980), b, x + w * 0x54 / 0x280, y + ty2, C(0x51c990), 0x14);
}

static void set_alpha(uint8_t *c, uint8_t a) { if (c) c[3] = a; }

/* 0x4744f0: the player's car, then the round's matches: a number over each, the two sides' boxes
   (the match next or just run highlighted with "vs." beside it, a winner filled, the side out
   crossed); at the round's end the winners' boxes moving on to their next places */
static void draw_round(void)
{
    int w, h;
    screen(&w, &h);
    char b[128];
    snprintf(b, sizeof b, "%s", Text_Get((int)gm8(0x52f3e8) + 0xc2));   /* 0x40ede0(Player_Car(0)): mode 4's, the record's */
    Text_Lower(b);
    Font_Print(0, b, w * 0x1e / 0x280, h * 5 / 0x1e0 + h * 0x39 / 0x1e0 + Font_Height(0), C(0x51c990), 0x11);
    uint32_t k = round_kind(), sub = gm32(V_SUB);
    bool moving = Tween_Active(T_NEXT);
    bool fading = Tween_Active(T_ROUND) || (!Tween_Active(T_NEXT) && sub == 6);
    int n = 0, x0 = 0, y0 = 0;
    uint32_t at = round_at(k, &n);
    switch (k) {
    case 1: x0 = w * 0x56 / 0x280, y0 = h * 0x8e / 0x1e0; break;
    case 2: x0 = w * 0x93 / 0x280, y0 = h * 0x8e / 0x1e0; break;
    case 3: x0 = w * 0x93 / 0x280, y0 = h * 0xcf / 0x1e0; break;
    case 4: x0 = w * 0x115 / 0x280, y0 = h * 0xcf / 0x1e0; break;
    }
    int tx = 0, ty = 0;   /* (the final's are the original's locals left from before) */
    for (int i = 0; i < n; i++) {
        bool hl = false, cur = false;
        int dy = 0, dx = 0;
        if (moving) {
            if (k == 1) tx = w * 0x93 / 0x280, ty = h * 0x8e / 0x1e0;
            else if (k == 2) tx = w * 0x93 / 0x280, ty = h * 0xcf / 0x1e0;
            else if (k == 3) tx = w * 0x115 / 0x280, ty = h * 0xcf / 0x1e0;
        }
        if (k == 1) {
            if (i >= 4) dy = h * 0x82 / 0x1e0;
            if (i == 2 || i == 3 || i == 6 || i == 7) dx = w * 0x108 / 0x280;
            if (moving) {
                if (i / 2 == 2 || i / 2 == 3) ty += h * 0x82 / 0x1e0;
                if (i / 2 == 1 || i / 2 == 3) tx += w * 0x108 / 0x280;
            }
        } else if (k == 2) {
            if (i == 2 || i == 3) dy = h * 0x82 / 0x1e0;
            if (i == 1 || i == 3) dx = w * 0x108 / 0x280;
            if (moving && i / 2 == 1) tx += w * 0x108 / 0x280;
        } else if (k == 3 && i == 1)
            dx = w * 0x108 / 0x280;
        dy += y0;
        dx += k == 1 && i % 2 ? w * 0x78 / 0x280 + x0 : x0;
        if (sub == 3 && i == (int)match_no() - 1) hl = true;
        uint32_t m = at + 12u * (uint32_t)i, word = gm32(m);
        bool out_b = (word & 0x3e0) == 0x3e0, out_a = (word & 0x1f) == 0x1f;
        uint8_t *fa = NULL, *fb = NULL, *col;
        if (((sub == 2 || sub == 4 || sub == 5) && i == (int)match_no()) || (sub == 3 && i == (int)match_no() - 1))
            cur = true, col = C(0x51c990);
        else if (!(word & 0x400))
            col = C(0x51c994);
        else if ((word & 0x1800) == 0x800)
            out_b = true, fa = C(0x51c9a4), col = C(0x51c994);
        else
            out_a = true, fb = C(0x51c9a4), col = C(0x51c994);
        uint8_t *col2 = col;
        uint8_t fadeout = (uint8_t)((int64_t)gms32(V_GROW) * 0xff0000 >> 32);
        if (!moving) {
            snprintf(b, sizeof b, Text_Get(0x4f), i + 1);
            if (!Tween_Active(T_GROW) && !fading) {
                Font_Print(0, b, w / 0x280 + dx, dy - h * 5 / 0x1e0, col, 0x11);
                set_alpha(fa, 0xff), set_alpha(col, 0xff), set_alpha(col2, 0xff);
            } else {
                type_in(b, gms32(V_GROW), 0, w / 0x280 + dx, dy - h * 5 / 0x1e0, col, 0x11);
                if (!fading || !out_a) set_alpha(fa, 0xff), set_alpha(col, 0xff), set_alpha(col2, 0xff);
                else set_alpha(fa, fadeout), set_alpha(col, fadeout), set_alpha(col2, fadeout);
            }
            int16_t r[4] = {(int16_t)dx, (int16_t)dy, (int16_t)(w * 0x56 / 0x280), (int16_t)(h * 0x26 / 0x1e0)};
            side_box(0, m, r, fa, col, out_a, hl, col2, false);
        } else if (out_b) {
            if (i % 2 == 1) ty += h * 0x2d / 0x1e0;
            move_box(dx, dy, tx, ty, 0, m);
        }
        if (cur) Font_Print(0, (const char *)gm(0x51c9ac), dx - w * 2 / 0x280, h * 0x2f / 0x1e0 + dy, C(0x51c990), 0x14);
        if (!moving) {
            if (!fading || !out_b) set_alpha(fb, 0xff), set_alpha(col, 0xff), set_alpha(col2, 0xff);
            else set_alpha(fb, fadeout), set_alpha(col, fadeout), set_alpha(col2, fadeout);
            int16_t r[4] = {(int16_t)dx, (int16_t)(h * 0x2d / 0x1e0 + dy), (int16_t)(w * 0x56 / 0x280), (int16_t)(h * 0x26 / 0x1e0)};
            side_box(1, m, r, fb, col, out_b, hl, col2, false);
        } else if (out_a) {
            if (i % 2 == 1) ty += h * 0x2d / 0x1e0;
            move_box(dx, h * 0x2d / 0x1e0 + dy, tx, ty, 1, m);
        }
        gw8(0x51c993, 0xff);
    }
}

/* 0x473d60: the continue page: each item between its lines with the arrow (the first's text by the
   step: 0x77 at the draw, 0x79 the match, 0x7a its result, the others' 0x78), typed in while the
   lines' tween runs; then the round */
static void draw_go(Menu *mn)
{
    int w, h;
    screen(&w, &h);
    Tex *on = Pause_ArrowOn(), *off = Pause_ArrowOff();
    int base = h * 0x17c / 0x1e0, ah = on ? on->height : 0;
    uint32_t sub = gm32(V_SUB);
    for (int i = 0; i < mn->n; i++) {
        int sy = h * 0x14 / 0x1e0 + h * 0x24 / 0x1e0 * i - ah / 2 + base;
        int ty = h * 0xe / 0x1e0 + h * 0x14 / 0x1e0 + base + (h * 0x24 / 0x1e0 * i - ah / 2 * h / 0x1e0);
        if (hires()) ty += h * 6 / 0x1e0;
        bool sel = mn->sel == i;
        const uint8_t *col = C(sel ? 0x51c990 : 0x51c994);
        Tex *t = sel ? on : off;
        char b[128];
        bool top;
        if (sub == 1) {
            snprintf(b, sizeof b, "%s", Text_Get(i == 0 ? 0x77 : 0x78));
            top = i == 0 || sel;
        } else if (sub == 2) {
            if (i == 0) snprintf(b, sizeof b, Text_Get(0x79), (int)match_no() + 1);
            else snprintf(b, sizeof b, "%s", Text_Get(0x78));
            top = i == 0 || sel;
        } else if (sub == 5) {
            if (i == 0) snprintf(b, sizeof b, Text_Get(0x7a), (int)match_no() + 1);
            else snprintf(b, sizeof b, "%s", Text_Get(0x78));
            if (!(rec() & 0x400000) && round_kind() != 4) continue;
            top = i == 0 || sel;
        } else
            continue;
        bool typing = Tween_Active(T_LINES);
        int16_t up[4] = {(int16_t)(w * 99 / 0x280), (int16_t)(h * i * 0x24 / 0x1e0 + base), (int16_t)(w * 0x11a / 0x280), 1};
        if (top) {
            if (!typing) Rect_Fill(up, col, 1);
            else part_rect(gms32(V_LINES), up, col, 1);
        }
        arrow(t, (int16_t)(w * 100 / 0x280), (int16_t)sy, col);
        if (!typing) Font_Print(0, b, w * 0x7a / 0x280, ty, col, 0x21);
        else type_in(b, gms32(V_LINES), 0, w * 0x7a / 0x280, ty, col, 0x21);
        int16_t lo[4] = {(int16_t)(w * 99 / 0x280), (int16_t)(h * 0x24 / 0x1e0 + base + h * i * 0x24 / 0x1e0), (int16_t)(w * 0x11a / 0x280), 1};
        if (!typing) Rect_Fill(lo, col, 1);
        else part_rect(gms32(V_LINES), lo, col, 2);
    }
    Font_SetLayer(2);
    draw_round();
}

/* 0x4738f0: give up? 0x76 in a box, the items between their lines with the arrow */
static void draw_quit(Menu *mn)
{
    int w, h;
    screen(&w, &h);
    Tex *on = Pause_ArrowOn(), *off = Pause_ArrowOff();
    int16_t r[4] = {(int16_t)(w * 99 / 0x280), (int16_t)(h * 0xa0 / 0x1e0), (int16_t)(w * 0x11a / 0x280), (int16_t)(h * 0x26 / 0x1e0)};
    Rect_Fill(r, C(0x51c984), 2);
    Font_Print(0, Text_Get(0x76), w * 0x7a / 0x280, h * 0xb8 / 0x1e0, C(0x51c990), 0x11);
    for (int i = 0; i < mn->n; i++) {
        bool sel = mn->sel == i;
        const uint8_t *col = C(sel ? 0x51c990 : 0x51c994);
        Font_Print(0, Text_Get(mn->items[i].text), w * 0x78 / 0x280, h * 0xdd / 0x1e0 + h * i * 0x24 / 0x1e0, col, 0x11);
        arrow(sel ? on : off, (int16_t)(w * 100 / 0x280), (int16_t)(h * 0xd1 / 0x1e0 + h * i * 0x24 / 0x1e0), col);
        if (i == 0 || sel) {
            int16_t up[4] = {(int16_t)(w * 99 / 0x280), (int16_t)(h * 0xc6 / 0x1e0 + h * i * 0x24 / 0x1e0), (int16_t)(w * 0x11a / 0x280), 1};
            Rect_Fill(up, col, 1);
        }
        int16_t lo[4] = {(int16_t)(w * 99 / 0x280), (int16_t)(h * 0xea / 0x1e0 + h * i * 0x24 / 0x1e0), (int16_t)(w * 0x11a / 0x280), 1};
        Rect_Fill(lo, col, 1);
    }
}

static void go_fn(Menu *m, MenuItem *it) { (void)m, (void)it; gw32(F_GO, 1); }        /* 0x473450 */
static void quit_fn(Menu *m, MenuItem *it) { (void)m, (void)it; gw32(V_SUB, 7); }     /* 0x473460 */

/* 0x473360 */
static void build(void)
{
    page = &pg_go;
    Menu_Init(&pg_go, NULL, 0, NULL, false, NULL, 1, 0, 1);
    Menu_AddAction(&pg_go, NULL, 0x13, go_fn, 0);
    Menu_AddPage(&pg_go, NULL, 99, &pg_quit, NULL, 1);
    Menu_SetCallbacks(&pg_go, NULL, NULL, draw_go, NULL);
    Menu_Reset(&pg_go);
    Menu_Init(&pg_quit, NULL, 0, NULL, false, NULL, 1, 0, 1);
    Menu_AddAction(&pg_quit, NULL, 0x72, quit_fn, 0);
    Menu_AddPage(&pg_quit, NULL, 0x73, &pg_go, NULL, 1);
    Menu_SetCallbacks(&pg_quit, NULL, NULL, draw_quit, NULL);
    Menu_Reset(&pg_quit);
    gw32(F_PAGES, 1);
}

/* 0x473470: the first player's keys to the page, none while the round's end moves */
static void input(void)
{
    Input_PollAll();     /* 0x49eab0 */
    Ctl_AxisPresses();   /* 0x40bad0 */
    uint32_t b = 0;
    if (!Tween_Active(T_ROUND) && !Tween_Active(T_NEXT)) {
        InputRecord *r = Input_Record(0);
        Ctl_Merge(0, r);
        b = r->pressed;
    }
    Menu *to = Menu_Input(page, b);
    if (to) {
        pg_go.sel = 0;
        pg_quit.sel = 1;
        page = to;
    }
}

/* 0x4734f0, the give-up fade's end: the challenge left, through the results (event 1) */
static void quit_done(int ch)
{
    (void)ch;
    gw32(REC, rec() | 0x800000);
    gw32(F_PAGES, 0);
    Pause_Ask(false, false, true);   /* 0x41f2a0 */
    for (uint32_t i = 0; i < group->n; i++) Proc_Event(group, i, 1, 2);
}

/* 0x473540, the match's fade: on to the countdown */
static void race_done(int ch)
{
    (void)ch;
    for (uint32_t i = 0; i < group->n; i++) Proc_Event(group, i, 0, 2);
    Input_SetMenuMode(false);
    Race_ClearRestarted();   /* 0x41b330 */
    gw32(V_SUB, 5);
    Tween_Start(T_LINES, 2, 7, 0, 0, 0x10000, false);
}

void Challenge_Step(ProcGroup *g, uint32_t slot)
{
    (void)slot;
    group = g;
    Engine_Pause();     /* 0x478c40 */
    Surface_Silence();  /* 0x418ee0 */
    Crash_StopSounds();
    for (int i = 0; i < 2; i++) Fade_Tick(i, false), Fade_Level(i, 0x10000, false);
    if (Fade_Active(0)) return;
    if (gm32(F_PAGES)) input();
    if (Tween_Active(T_GROW)) gw32(V_GROW, (uint32_t)Tween_Value(gm8(T_GROW)));
    gw32(V_LINES, Tween_Active(T_LINES) ? (uint32_t)Tween_Value(gm8(T_LINES)) : 0);
    uint32_t sub = gm32(V_SUB);
    switch (sub) {
    case 0:
        Tween_ClearAll();
        gw32(V_GROW, 0);
        if (round_kind() == 1) {
            Tween_Start(T_GROW, 2, 0xd, 0, 0, 0x10000, false);
            Tween_Start(T_LINES, 2, 7, 0, 0, 0x10000, false);
        }
        gw32(REC, rec() & 0xff1fffffu);
        build();
        gw32(V_SUB, 1);
        for (int i = 0; i < 2; i++)
            if ((int)Gs_SelBits14() <= i) gw8(0x53acc8 + (uint32_t)i, 0xff);
        gw32(F_GO, 0);
        return;
    case 1:
        if (gm32(F_GO)) {
            Opp_Times(true);   /* 0x455470(1) */
            gw32(F_GO, 0);
            gw32(V_SUB, 2);
            return;
        }
        break;
    case 2:
        if (bye(Challenge_Match())) {
            gw32(V_SUB, 5);
            Challenge_MatchEnd();
            gw32(V_TIME_B, 0), gw32(V_TIME_A, 0);
        }
        if (gm32(F_GO)) {
            Challenge_Players();
            Replay_FreeAll();   /* 0x46c500: the replays' buffers */
            Replay_Setup();     /* 0x41c260 */
            if (!computers()) {
                gw32(V_MATCH, Challenge_Match());
                gw32(F_GO, 0);
                gw32(V_SUB, 4);
                return;
            }
            gw32(V_SUB, 3);
            Challenge_MatchEnd();
            gw32(V_TIME_B, 0), gw32(V_TIME_A, 0), gw32(F_GO, 0);
            return;
        }
        break;
    case 3:
        if (gm32(F_GO)) {
            gw32(V_SUB, 5);
            Tween_Start(T_LINES, 2, 7, 0, 0, 0x10000, false);
            gw32(F_GO, 0);
            return;
        }
        break;
    case 4:
        /* the race built again at the line for the match (0x41b310), then its fade to the countdown */
        if (!Race_Restarted()) {
            Race_RequestRestart();
            gw32(F_GO, 0);
            return;
        }
        Fade_OutIn(0, race_done, 1, 0, (int32_t)gm32(0x51c9a8), false);
        if (Gs_SelBits14() == 2) Fade_OutIn(1, NULL, 1, 0, (int32_t)gm32(0x51c9a8), false);
        break;
    case 5:
        if (!(rec() & 0x400000)) {
            gw32(F_GO, 0);
            gw32(V_SUB, 2);
            return;
        }
        if (round_kind() == 4) {
            if (gm32(F_GO)) {
                gw32(F_GO, 0);
                gw32(V_SUB, 7);
                return;
            }
        } else if (gm32(F_GO)) {
            gw32(V_GROW, 0x10000);
            Tween_Start(T_ROUND, 2, 0xd, 0, 0, 0x10000, false);
            gw32(F_GO, 0);
            gw32(V_SUB, 6);
            return;
        }
        break;
    case 6:
        if (Tween_Active(T_ROUND)) {
            gw32(V_GROW, (uint32_t)Tween_Value(gm8(T_ROUND)));
            gw32(F_GO, 0);
            return;
        }
        if (!Tween_Active(T_NEXT)) {
            Tween_Stop(T_ROUND);
            gw32(V_GROW, 0);
            Tween_Start(T_NEXT, 2, 0xd, 0, 0, 0x10000, true);
            gw32(F_GO, 0);
            return;
        }
        gw32(V_GROW, (uint32_t)Tween_Value(gm8(T_NEXT)));
        if (!Tween_Active(T_NEXT)) gw32(V_GROW, 0);
        if (gm32(F_GO) || !Tween_Active(T_NEXT)) {
            /* the next round: on the next stage (0x4070c0), through the results */
            Tween_ClearAll();
            gw32(V_GROW, 0), gw32(V_LINES, 0);
            gw32(F_THROUGH, player_through());
            uint32_t r = rec();
            r = (r & ~0x38u) | ((r + 8) & 0x38);   /* the next round */
            gw32(REC, (r & 0xffbf0fffu) | 0x200000);
            Sel_ChallengeStage();   /* 0x4070c0 */
            Pause_Ask(false, false, true);   /* 0x41f2a0 */
            for (uint32_t i = 0; i < group->n; i++) Proc_Event(group, i, 1, 2);
            gw32(V_SUB, 0);
            gw32(F_GO, 0);
            return;
        }
        break;
    case 7:
        Fade_OutIn(0, quit_done, 1, 0, (int32_t)gm32(0x51c9a8), false);
        if (Gs_SelBits14() == 2) Fade_OutIn(1, NULL, 1, 0, (int32_t)gm32(0x51c9a8), false);
        break;
    }
    gw32(F_GO, 0);
}

/* 0x475a50: the page's background, "challenge", the round's name, and on the continue page from the
   draw to the round's end the match ("match <n>", the one just run on its result), on the give-up
   page 0x7b */
static void header(void)
{
    int w, h;
    screen(&w, &h);
    int16_t all[4] = {0, 0, (int16_t)w, (int16_t)h};
    Rect_Fill(all, C(0x51c988), 4);
    char b[128];
    snprintf(b, sizeof b, "%s", Text_Get(0x7c));
    Text_Lower(b);
    int x = w * 0x1e / 0x280;
    Font_Print(2, b, x, h * 0x39 / 0x1e0, C(0x51c990), 0x11);
    x = w * 5 / 0x280 + x + Font_TextWidth(2, b);
    int16_t bar[4] = {(int16_t)x, (int16_t)(h * 0x1b / 0x1e0), 2, (int16_t)(h * 0x29 / 0x1e0)};
    Rect_Fill(bar, C(0x51c990), 1);
    x += 2 + w * 5 / 0x280;
    static char name[128];   /* (DAT_00663b60: an unknown round keeps what it held) */
    switch (round_kind()) {
    case 1: snprintf(name, sizeof name, Text_Get(0x4b), 1); break;
    case 2: snprintf(name, sizeof name, "%s", Text_Get(0x4c)); break;
    case 3: snprintf(name, sizeof name, "%s", Text_Get(0x4d)); break;
    case 4: snprintf(name, sizeof name, "%s", Text_Get(0x4e)); break;
    }
    Text_Lower(name);
    Font_Print(2, name, x, h * 0x39 / 0x1e0, C(0x51c990), 0x11);
    uint32_t sub = gm32(V_SUB);
    if (page == &pg_go && !(sub > 1 && sub < 6)) return;
    int x2 = Font_TextWidth(2, name) + w * 5 / 0x280 + x;
    int16_t bar2[4] = {(int16_t)x2, (int16_t)(h * 0x1b / 0x1e0), 2, (int16_t)(h * 0x29 / 0x1e0)};
    Rect_Fill(bar2, C(0x51c990), 1);
    if (page == &pg_go) {
        int n = sub == 3 || (rec() & 0x400000) ? (int)match_no() : (int)match_no() + 1;
        snprintf(b, sizeof b, Text_Get(0x4f), n);
        Font_Print(2, b, x2 + 2 + w * 5 / 0x280, h * 0x39 / 0x1e0, C(0x51c990), 0x11);
    } else
        Font_Print(2, Text_Get(0x7b), x2 + 2 + w * 5 / 0x280, h * 0x39 / 0x1e0, C(0x51c990), 0x11);
}

void Challenge_Draw(ProcGroup *g, uint32_t slot)
{
    (void)g;
    Render_ClearTarget();
    Render_ClearZ(false);
    header();
    if (page) Menu_Draw(page);   /* 0x4a0550(0x475a40) */
    for (int i = 0; i < (int)Gs_SelBits14(); i++) Race_FadeOverlay(i, false);
    int w, h;
    D3d_ScreenSize(&w, &h);
    int16_t v[4] = {0, 0, (int16_t)w, (int16_t)h};
    World_Render(Race_Camera((int)slot), v, slot, false);
}
