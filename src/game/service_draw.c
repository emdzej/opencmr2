/* The service screens' draws (see service.h): the textures (0x50a080), the typewriter prints and the
   boxes' tweens (0x501d50-0x5020a0), the top bar (0x50c420), the list panel (0x50b1c0), the information
   panel (0x50bfd0) and the pages' draws (0x50e1c0-0x50ee10). Coordinates are the original's 640 x 480,
   scaled to the screen; the colours are the exe's at 0x527378-0x5273a8. */
#include "game/service_int.h"
#include "exe.h"
#include "font.h"
#include "game/gstate.h"
#include "game/mem.h"
#include "game/paths.h"
#include "game/profile.h"
#include "game/timer.h"
#include "pending.h"
#include "render/d3d7.h"
#include "render/tex.h"
#include "render/twod.h"
#include "text.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define C_BLINK gm(0x527378)   /* red */
#define C_LIT gm(0x52737c)     /* white */
#define C_TEXT gm(0x527380)    /* dark grey */
#define C_ICON gm(0x527384)
#define C_LINE gm(0x527388)
#define C_NEXT gm(0x52738c)    /* white: the typewriter's next letter */
#define C_FAINT gm(0x527390)
#define C_OFF2 gm(0x527394)
#define C_OFF gm(0x527398)     /* a part the car's class has not */
#define C_WHITE gm(0x5273a8)

static Tex *t_red, *t_orange, *t_yellow;   /* DAT_00831360, 64, 68: the damage marks */
static Tex *t_banner;                       /* DAT_00831668: the rally's banner */
static Tex *t_arrow, *t_arrow_off;          /* DAT_0083166c, 70: the list's arrows */
static Tex *t_tick, *t_cross, *t_box, *t_circle;   /* DAT_008313ac, 0x831648, 0x8313b0, 0x831674 */
static Tex *t_parts[12];                    /* DAT_0083137c */

static int sw, sh;
static int X(int x) { return sw * x / 0x280; }
static int Y(int y) { return sh * y / 0x1e0; }

static Tex *find(BflSlot *a, uint32_t fmt, ...)
{
    char p[260];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(p, sizeof p, exe_str(fmt), ap);
    va_end(ap);
    char *q = p;
    while (*q == '\\') q++;
    return Bfl_FindTexture(a, q, NULL, false, 0);
}

/* 0x50a080: the marks, the banner (b<country>, from the day's archive), the arrows and the parts */
void Sv_Textures(void)
{
    static const uint32_t banners[8] = {0x519280, 0x51927c, 0x519278, 0x519274, 0x519270, 0x51926c, 0x519268, 0x519264};
    static const uint32_t parts[12] = {0x5295e8, 0x5295e0, 0x5295d8, 0x5295d0, 0x5295c8, 0x5295c0,
                                       0x5295b8, 0x5295b0, 0x5295a8, 0x5295a0, 0x529598, 0x529590};
    BflSlot *c = Sv_Common();
    t_red = find(c, 0x52956c, Path_SetupRep(), 0x280);
    t_orange = find(c, 0x529548, Path_SetupRep(), 0x280);
    t_yellow = find(c, 0x529524, Path_SetupRep(), 0x280);
    uint32_t r = gm32(0x52f2ac) & 0xff & 0x1f;
    t_banner = find(Sv_Day(), 0x529508, Path_SetupRep(), r < 8 ? exe_str(banners[r]) : "");
    t_arrow = find(c, 0x5294f0, Path_FrontEnd());
    t_arrow_off = find(c, 0x5294d8, Path_FrontEnd());
    t_tick = find(c, 0x5294b8, Path_SetupRep(), 0x280);
    t_cross = find(c, 0x529494, Path_SetupRep(), 0x280);
    t_box = find(c, 0x529474, Path_SetupRep(), 0x280);
    t_circle = find(c, 0x529450, Path_SetupRep(), 0x280);
    for (int i = 0; i < 12; i++) t_parts[i] = find(c, 0x529434, Path_SetupRep(), 0x280, exe_str(parts[i]));
}

/* ---- the tweens on text and boxes ---- */

static int16_t scale(int16_t v, int32_t k) { return (int16_t)(((int64_t)((int32_t)v << 16) * k) >> 32); }
static int32_t fmul(int32_t a, int32_t b) { return (int32_t)(((int64_t)a * b) >> 16); }

/* 0x501d50: a box's width (wide) or height grown by tween i; nothing while it waits */
static void tw_grow(int i, int16_t r[4], bool wide)
{
    const SvTween *t = &S.tw[i];
    if (t->state == 0) r[2] = r[3] = 0;
    else if (t->state == 1) {
        if (wide) r[2] = scale(r[2], t->v);
        else r[3] = scale(r[3], t->v);
    }
}

/* 0x501f00: both */
static void tw_grow2(int i, int16_t r[4])
{
    const SvTween *t = &S.tw[i];
    if (t->state == 1) r[2] = scale(r[2], t->v), r[3] = scale(r[3], t->v);
    else if (t->state == 0) r[2] = r[3] = 0;
}

/* 0x501de0: a box opening, a line two pixels wide growing to its height in the first half, then to its
   width, both squared */
static void tw_open(int i, int16_t r[4])
{
    const SvTween *t = &S.tw[i];
    if (t->state != 1) {
        if (t->state == 0) r[2] = r[3] = 0;
        return;
    }
    int32_t v = t->v;
    if (v < 0x8000) {
        int16_t c = (int16_t)(r[2] / 2 + r[0]);
        r[0] = (int16_t)(c - 1);
        r[2] = 2;
        int32_t u = (int32_t)(((int64_t)v * 0x20000) >> 16);
        int32_t d = r[3] - (fmul((int32_t)r[3] << 16, fmul(u, u)) >> 16);
        r[1] = (int16_t)(r[1] + d / 2);
        r[3] = (int16_t)(r[3] - d);
        return;
    }
    int32_t u = (int32_t)(((int64_t)(v - 0x8000) * 0x20000) >> 16);
    int32_t d = r[2] - (fmul((int32_t)r[2] << 16, fmul(u, u)) >> 16);
    r[0] = (int16_t)(r[0] + d / 2);
    r[2] = (int16_t)(r[2] - d);
}

/* the letters of s tween i has reached (0x501f80, 0x5020a0) */
static int typed(int i, const char *s, char *buf, size_t cap)
{
    int n = (int)strlen(s);
    int k = (int)(((int64_t)(n * 0x10000) * S.tw[i].v) >> 32);
    if (k >= (int)cap) k = (int)cap - 1;
    memcpy(buf, s, (size_t)k);
    buf[k] = 0;
    return k;
}

/* 0x501f80: s typed out by tween i in font f; with flags 9, 0x11 or 0x21 the next letter in font g's
   colour c2; whole once the tween is done, nothing before it starts */
static void type1(int i, int f, int g, const char *s, int x, int y, const uint8_t *c, const uint8_t *c2, uint32_t flags)
{
    if (S.tw[i].state == 2) {
        Font_Print(f, s, x, y, c, flags);
        return;
    }
    if (S.tw[i].state != 1) return;
    char buf[256];
    int k = typed(i, s, buf, sizeof buf);
    Font_Print(f, buf, x, y, c, flags);
    if ((flags == 9 || flags == 0x11 || flags == 0x21) && k < (int)strlen(s)) {
        int wx = Font_TextWidth(f, buf);
        char one[2] = {s[k], 0};
        Font_Print(g, one, wx + x, y, c2, flags);
    }
}

/* 0x5020a0: the same with the rest of s in c2 */
static void type2(int i, int f, int g, const char *s, int x, int y, const uint8_t *c, const uint8_t *c2, uint32_t flags)
{
    if (S.tw[i].state == 2) {
        Font_Print(f, s, x, y, c, flags);
        return;
    }
    if (S.tw[i].state != 1) return;
    char buf[256];
    int k = typed(i, s, buf, sizeof buf);
    Font_Print(f, buf, x, y, c, flags);
    if (k < (int)strlen(s)) {
        int wx = Font_TextWidth(f, buf);
        Font_Print(g, s + k, x + wx, y, c2, flags);
    }
}

static void sprite(Tex *t, const int16_t dst[4], const uint8_t *c)
{
    if (!t) return;
    Rect16 src = {0, 0, t->width, t->height}, d = {dst[0], dst[1], dst[2], dst[3]};
    Sprite_Add(&src, &d, t, 1, 0, NULL, NULL, c, 8);
}

/* 0x4ff550: the item whose panel is up (in modes without Save, index 4 is Quit, 5) */
int Sv_Active(void)
{
    for (int i = 0; i < sv_main.n; i++)
        if (S.panel[i] != 3) return Gs_Mode() != 0 && i == 4 ? 5 : i;
    return 0;
}

/* 0x50c420: the items across the top, the shown one typed in the pulse (white on a set-up page), the
   lines between them lit beside it; the player and the name; in a network game the time left */
static void top_bar(bool lit)
{
    const uint8_t *c = lit ? C_LIT : S.pulse;
    int a = Sv_Active(), x = X(0x1c), y = Y(0x2c);
    int16_t r[4] = {0, (int16_t)Y(0x1c), 1, (int16_t)Y(0x18)};
    for (int i = 0; i < sv_main.n; i++) {
        int on = a, before = a;
        if (Gs_Mode() != 0 && i == 4) on = a - 1;
        if (Gs_Mode() != 0 && i == 3) before = a - 1;
        const char *s = Text_Get(sv_main.items[i].text);
        if (on == i) type2(6, 0, 0, s, x, y, c, C_TEXT, 0x11);
        else Font_Print(0, s, x, y, C_TEXT, 0x11);
        int rx = x + Font_TextWidth(0, s) + Font_TextWidth(0, exe_str(0x516870));
        r[0] = (int16_t)rx;
        if (i != sv_main.n - 1) Rect_Fill(r, on == i || before == i + 1 ? c : C_LINE, 3);
        x = rx + 2 + Font_TextWidth(0, exe_str(0x516870));
    }
    char b[128];
    x = (int)gm32(0x5293a0) * sw / 0x280;
    snprintf(b, sizeof b, exe_str(0x516098), Text_Get(0x4a), Sv_Player() + 1);
    Font_Print(0, b, x, y, C_TEXT, 0x11);
    int rx = x + Font_TextWidth(0, b) + Font_TextWidth(0, exe_str(0x516870));
    r[0] = (int16_t)rx;
    Rect_Fill(r, C_LINE, 3);
    x = rx + 2 + Font_TextWidth(0, exe_str(0x516870));
    const char *name = Player_Name(Sv_Player());
    Font_Print(0, name, x, y, C_TEXT, 0x11);
    if (!Gs_ModeBit19()) return;
    uint32_t left;
    if (Gs_Mode() == 10) {
        PENDING_NOTE(0x406710, "the network time trial's clock");
        return;
    }
    int32_t t = (int32_t)(S.turn_at - Timer_Centis()) + 0x17d5;   /* 0x5012c0 */
    left = t < 0 ? 0 : (uint32_t)t;
    snprintf(b, sizeof b, exe_str(0x51a904), left / 100 / 0x3c, left / 100 % 0x3c);
    Font_Print(2, b, Font_TextWidth(0, name) + X(0x1e) + x, y, C_TEXT, 0x11);
}

/* 0x407150: the super special's stage number by the rally and the difficulty */
static int ss_number(uint32_t r, uint32_t d) { return (int)(r & 1) + (d == 0 ? 4 : d == 1 ? 8 : 10); }

static const uint8_t *hi(const uint8_t *c) { return Sv_BlinkLit() ? C_BLINK : c; }

/* 0x50b1c0: a list of n under a line from (x, y) (640 x 480), each row with a line under it; the
   selected row typed, with the arrow, its lines in the pulse (white when lit, red in the warning blink).
   Kinds: 0 the stages, 1 the repairs (the damage mark, a box ticked when repaired, crossed without the
   time), 2 the set-up (ticked when changed, crossed without the time), 3 Yes / No, 4 the save slots
   (twice as wide). The panel opens with tween 0 (the lines) and 1 (the marks); the rows type with 3,
   then 7. */
static void list_panel(int n, int sel, int kind, int x, int y, bool lit)
{
    const uint8_t *c = lit ? C_LIT : S.pulse;
    int16_t arrow[4] = {(int16_t)(x * sw / 0x280), 0, 0, 0};
    int16_t mark[4] = {(int16_t)X(0x23a), 0, 0, 0};
    int16_t tick[4] = {(int16_t)X(0x254), 0, 0, 0};
    if (t_arrow) arrow[2] = t_arrow->width, arrow[3] = t_arrow->height, tw_grow2(1, arrow);
    if (t_red) mark[2] = t_red->width, mark[3] = t_red->height, tw_grow2(1, mark);
    if (t_tick) tick[2] = t_tick->width, tick[3] = t_tick->height, tw_grow2(1, tick);
    int16_t r[4] = {(int16_t)(x * sw / 0x280), (int16_t)(sh * y / 0x1e0), (int16_t)X(0xa2), 1};
    if (kind == 4) r[2] = (int16_t)(r[2] + X(200));
    tw_grow(0, r, true);
    Rect_Fill(r, sel == 0 ? hi(c) : C_LINE, 1);
    int p = Sv_Player(), cls = Sv_CarClass();
    for (int i = 0; i < n; i++) {
        if (t_arrow) {
            /* the 640 textures (the 1,024 ones with the texture memory and both 0x4b7560 and 0x4b7590:
               10, 14, 13) */
            int16_t base = (int16_t)(sh * 0x18 / 0x3c0 + r[1]);
            arrow[1] = (int16_t)(base - 6), tick[1] = (int16_t)(base - 9), mark[1] = (int16_t)(base - 8);
        }
        bool off = false;
        char b[160];
        const uint8_t *ic = i == sel ? hi(c) : C_ICON;
        uint32_t st = gm32(0x52f2ac) >> 5 & 0x1f;
        switch (kind) {
        case 0:
            if ((Gs_Mode() == 0 || Gs_Mode() == 1) && (st & 0xff) + (uint32_t)i == 10)
                snprintf(b, sizeof b, exe_str(0x529600), Text_Get(0x9b), ss_number(gm32(0x52f2ac) & 0x1f, Gs_Mode2()), Text_Get(0x13d));
            else if ((st & 0xff) + (uint32_t)i == 10)
                snprintf(b, sizeof b, exe_str(0x516940), Text_Get(0x9b), Text_Get(0x13d));
            else
                snprintf(b, sizeof b, exe_str(0x516098), Text_Get(0x9b), (int)(st & 0xff) + 1 + i);
            break;
        case 1: {
            int part = Sv_RepairView(i);
            snprintf(b, sizeof b, "%s", Text_Get(part));
            off = !Sv_ViewAllowed(part, cls);
            int d = Sv_Damage(p, part, false);
            if (d > 0 || Sv_Repaired(p, part)) {
                sprite(t_box, tick, ic);
                if (Sv_Repaired(p, part)) sprite(t_tick, tick, ic);
                else if (!Sv_CanRepair(p, part)) sprite(t_cross, tick, ic);
            }
            /* the mark's size is the red one's for all three */
            Tex *m = d >= 0x43 ? t_red : d >= 0x22 ? t_orange : d > 0 ? t_yellow : NULL;
            if (m && t_red) {
                Rect16 src = {0, 0, t_red->width, t_red->height}, dst = {mark[0], mark[1], mark[2], mark[3]};
                Sprite_Add(&src, &dst, m, 1, 0, NULL, NULL, C_WHITE, 8);
            }
            break;
        }
        case 2:
            snprintf(b, sizeof b, "%s", Text_Get(0xa6 + i));
            off = !Sv_ViewAllowed(Sv_SetupView(i), cls);
            if (off) break;
            sprite(t_box, tick, ic);
            if (!Sv_SetupKept(p, i)) sprite(t_tick, tick, ic);
            else if (!Sv_CanSet(p, Sv_SetupPart(i))) sprite(t_cross, tick, ic);
            break;
        case 3: snprintf(b, sizeof b, "%s", Text_Get(0x100 + i)); break;
        case 4:
            if (i == 0) {
                snprintf(b, sizeof b, "%s", Text_Get(0x141));
                break;
            }
            snprintf(b, sizeof b, exe_str(Timer_Centis() % 0x14 < 10 || sel != i ? 0x524d38 : 0x5295f8), Text_Get(0x142),
                     (const char *)gm(0x82aa44));
            break;
        default: snprintf(b, sizeof b, "%s", exe_str(0x516870)); break;
        }
        int tx = X(0x14) + x * sw / 0x280, ty = (int16_t)(Y(0x12) + r[1]);
        if (i == sel) {
            bool bl = Sv_BlinkLit();
            if (Sv_TweenState(3) == 2) type2(7, 0, 0, b, tx, ty, bl ? C_BLINK : c, C_TEXT, 0x11);
            else type1(3, 0, 0, b, tx, ty, bl ? C_BLINK : c, bl ? C_BLINK : C_FAINT, 0x11);
            sprite(t_arrow, arrow, bl ? C_BLINK : c);
        } else if (off) {
            type1(3, 0, 0, b, tx, ty, C_OFF, C_OFF2, 0x11);
            sprite(t_arrow, arrow, C_OFF);
        } else {
            type1(3, 0, 0, b, tx, ty, C_TEXT, C_NEXT, 0x11);
            sprite(t_arrow_off, arrow, C_TEXT);
        }
        r[1] = (int16_t)((int16_t)(sh * y / 0x1e0) + (int16_t)Y(0x18) * (int16_t)(i + 1));
        Rect_Fill(r, i == sel || i + 1 == sel ? hi(c) : C_LINE, 1);
    }
}

static int value_of(int16_t id) { return sv_main.items[Menu_IndexOf(&sv_main, id)].value; }

/* 0x50bfd0: the stages the information pages are on, the rally's banner opening (tween 2); the
   page's map and notes (0x50a880) */
static void info_panel(void)
{
    int16_t x = (int16_t)gm16(0x5293a0);
    int v = value_of(0);
    list_panel(Sv_InfoPages(), v, 0, x, 0x124, false);
    if (t_banner) {
        int16_t r[4] = {(int16_t)(sw * x / 0x280), (int16_t)Y(0x6f), (int16_t)X(0xa1), (int16_t)Y(0x37)};
        /* the source is the screen size (the banner's pixels at 640 x 480) */
        Rect16 src = {0, 0, r[2], r[3]};
        tw_open(2, r);
        Rect16 dst = {r[0], r[1], r[2], r[3]};
        Sprite_Add(&src, &dst, t_banner, 1, 0, NULL, NULL, C_WHITE, 8);
    }
    PENDING_NOTE(0x50a880, "the information page's map and notes (0x5059d0, 0x5043b0)");
}

/* 0x50e1c0: Race: the prompt centred */
static void race_draw(void)
{
    char b[256];
    snprintf(b, sizeof b, "%s", Text_Get(0x131));
    PENDING_NOTE(0x40bac0, "the key names in the race prompt");
    int w = Font_TextWidth(0, b);
    type1(4, 0, 0, b, X(0x140) - w / 2, Y(0xf0), C_TEXT, C_NEXT, 0x11);
}

/* 0x50e280: Save: the saved championships (none yet: the message), the slots */
static void save_draw(Menu *m)
{
    (void)m;
    const uint8_t *c = gm8(0x82ab44) ? C_TEXT : C_FAINT;   /* 0x82ab44 the list in use */
    if (gms32(0x82ac48) < 1) type1(4, 0, 0, Text_Get(0x140), sw / 2, sh / 2, c, C_NEXT, 0x12);
    else PENDING_NOTE(0x50e280, "the saved championships' rows (0x4f4dc0-0x4f4e70)");
    list_panel(2, value_of(4), 4, 0x32, 0x17c, true);
}

/* 0x50e6a0: Quit: the question, Yes / No */
static void quit_draw(void)
{
    type1(4, 0, 0, Text_Get(0x133), X(0xf0), Y(200), C_TEXT, C_NEXT, 0x11);
    list_panel(2, value_of(5), 3, 0xf0, 0xd7, false);
}

static int last_view = -1;   /* DAT_00529430 */

/* 0x50eba0: the top bar and the shown item's panel; with the set-up or repair list on a new part the
   car turned to it (the view at 0x5273c0, the tyres' at 0x8313c8) */
void sv_main_draw(Menu *m)
{
    (void)m;
    D3d_ScreenSize(&sw, &sh);
    top_bar(false);
    int v;
    switch (Sv_Active()) {
    case 0: info_panel(); break;
    case 1:
        PENDING_NOTE(0x50a3f0, "the set-up panel (0x50a3f0, 0x50a680, 0x50a8a0, 0x50cc10, 0x50a920, 0x500550)");
        v = Sv_SetupView(value_of(1));
        if (last_view != v) {
            Sv_CarView(Sv_Player(), v, false);
            Sv_CarTyres(Sv_Player(), v, false);
            last_view = v;
        }
        break;
    case 2:
        PENDING_NOTE(0x50a8e0, "the repair panel (0x50a3f0, 0x50a8e0, 0x50a920, 0x500550)");
        v = Sv_RepairView(value_of(2));
        if (last_view != v) {
            Sv_CarView(Sv_Player(), v, false);
            Sv_CarTyres(Sv_Player(), v, false);
            last_view = v;
        }
        break;
    case 3: race_draw(); break;
    case 4: save_draw(&sv_main); break;
    case 5: quit_draw(); break;
    }
}

/* 0x50ee10: a set-up value: the top bar lit */
void sv_setup_draw(Menu *m)
{
    (void)m;
    D3d_ScreenSize(&sw, &sh);
    top_bar(true);
    PENDING_NOTE(0x50a3f0, "the set-up value page (0x50a3f0, 0x50a680, 0x50a8a0, 0x50cc10, 0x50a920, 0x500550)");
}

/* 0x50edf0 */
void sv_info_draw(Menu *m)
{
    (void)m;
    D3d_ScreenSize(&sw, &sh);
    top_bar(false);
    PENDING_NOTE(0x50c130, "the information pages");
}

/* 0x50e740: the message (0x82a93c) centred */
void sv_msg_draw(Menu *m)
{
    (void)m;
    D3d_ScreenSize(&sw, &sh);
    type1(4, 0, 0, (const char *)gm(0x82a93c), sw / 2, sh / 2, C_TEXT, C_NEXT, 0x12);
}

/* 0x50e780: save the championship? Yes / No, white. The arrow's box is scaled twice and has no size
   (as the original: nothing shows) */
void sv_save_draw(Menu *m)
{
    D3d_ScreenSize(&sw, &sh);
    int x1 = X(0xf0), y1 = Y(0xd7);
    const uint8_t *c = C_LIT;
    int16_t arrow[4] = {(int16_t)(x1 * sw / 0x280), 0, 0, 0};
    type1(4, 0, 0, Text_Get(0x13f), X(0xf0), Y(200), C_TEXT, C_NEXT, 0x11);
    int16_t r[4] = {(int16_t)x1, (int16_t)y1, (int16_t)X(0xa2), 1};
    tw_grow(0, r, true);
    int sel = m->sel;
    Rect_Fill(r, sel == 0 ? c : C_LINE, 1);
    for (int i = 0; i < m->n; i++) {
        if (t_arrow) arrow[1] = (int16_t)(sh * 0x18 / 0x3c0 + r[1] - 6);
        char b[64];
        snprintf(b, sizeof b, "%s", Text_Get(0x100 + i));
        int tx = X(0x14) + x1, ty = (int16_t)(Y(0x12) + r[1]);
        if (i == sel) {
            type2(7, 0, 0, b, tx, ty, c, C_TEXT, 0x11);
            sprite(t_arrow, arrow, c);
        } else {
            type1(3, 0, 0, b, tx, ty, C_TEXT, C_NEXT, 0x11);
            sprite(t_arrow_off, arrow, C_TEXT);
        }
        r[1] = (int16_t)((int16_t)Y(0x18) * (int16_t)(i + 1) + (int16_t)y1);
        Rect_Fill(r, i == sel || i + 1 == sel ? hi(c) : C_LINE, 1);
    }
}
