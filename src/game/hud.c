/* The race times and the HUD (see hud.h). */
#include "game/hud.h"
#include "text.h"
#include "exe.h"
#include "game/pause.h"
#include "game/opponents.h"
#include <string.h>
#include "game/sectors.h"
#include "game/gstate.h"
#include "game/mem.h"
#include "game/race.h"
#include "font.h"
#include "render/d3d7.h"
#include "render/twod.h"
#include "sim/sim.h"
#include "pending.h"
#include <stdio.h>
#include <stdlib.h>

static int32_t shown, clock_;          /* DAT_0053d1b0, DAT_0053d1b4 */
static bool capped;                    /* DAT_0053d1d8 */
static int32_t finish[CAR_MAX];        /* 0x53d1b8 */
static uint8_t done[CAR_MAX];          /* 0x53d1da */
static int32_t hud_time[4];            /* 0x536e0c + 0x48 n */
static int32_t splits[4][9];           /* 0x53e190: the players' times at each split */
static bool hud_ready;                 /* DAT_00536bfc: the reference splits taken */
static int32_t ref_split[12];          /* 0x536e94: the leader's (or record's) time at each split */
static int split_idx[4];               /* 0x536e00 + 0x48 n: the last split passed */
static int hud_place[4];               /* 0x536dfc + 0x48 n */
static int rows[4][3];                 /* 0x536c94: the standings' three places */
static int32_t my_split[4][12];        /* 0x536e10 + 0x48 n */
static uint8_t show_for[4];            /* DAT_00536c20 */

void Times_Clear(void)
{
    shown = clock_ = 0;
    for (int i = 0; i < CAR_MAX; i++) done[i] = 0;
    memset(splits, 0, sizeof splits);
    hud_ready = false;
    memset(split_idx, 0, sizeof split_idx);
    memset(my_split, 0, sizeof my_split);
    memset(show_for, 0, sizeof show_for);
    for (int i = 0; i < 4; i++) rows[i][0] = rows[i][1] = rows[i][2] = -1, hud_place[i] = 0xf;
}

int32_t Times_Race(void) { return shown; }
int32_t Times_Finish(int car) { return car == -1 ? -1 : finish[car]; }
int32_t Times_Split(int p, int k) { return p >= 0 && p < 4 && k >= 0 && k < 9 ? splits[p][k] : 0; }

/* 0x448de0: the clock 4 a step; the time shown off by -1 to 2 (rand() & 3, less one); at an hour the
   clock stops at 59:59.99 (unless 0x53d1d9) */
static void tick(void)
{
    if (!gm8(0x53d1d9) && 0x57e3b < clock_) {
        capped = true;
        clock_ = 360000;
        shown = 359999;
        return;
    }
    clock_ += 4;
    shown = clock_ + (rand() % 4 - 1);   /* (C's remainder of a non-negative value, as the original's mask) */
    if (gm32(0x52f2b0) >> 0xb & 1) PENDING_NOTE(0x448de0, "Times_Tick (the clock at 0x53d1a0)");
}

void Times_Step(void)
{
    tick();
    if (Gs_SelBit24() || Gs_SelBit25()) PENDING_NOTE(0x448120, "Times_Step (the arcade cases)");
    /* 0x448730 per car not yet done whose sectors changed: the splits (0x4487a0, 0x448920, pending) and
       at the finish 0x448cf0, the time kept */
    for (int i = 0; i < g_all.n; i++) {
        int k = g_all.car[i];
        if (done[k] || !Sectors_Moved(k)) continue;
        /* 0x4487a0's rally part: a player's time at the split */
        if (Sectors_Split(k) && k < (int)Gs_SelBits14() && Sectors_SplitIndex(k) >= 0 && Sectors_SplitIndex(k) < 9)
            splits[k][Sectors_SplitIndex(k)] = shown;
        if (!Race_Finished(k)) continue;
        done[k] = 1;   /* 0x448cf0 */
        finish[k] = shown;
    }
}

/* 0x413330's rally part: at a split its index, the time there, the place at it */
static void rally_split(int p)
{
    if (!Sectors_Split(p)) return;
    int idx = Sectors_Last(p);
    if (!Sectors_LapFirst(p) && idx != 0 && idx < 12) my_split[p][idx] = hud_time[p];
    PENDING_NOTE(0x413330, "Hud_Split (the delta's colour and bar, 0x411ab0)");
    split_idx[p] = idx;
    uint32_t m = Gs_Mode();
    if (m != 5 && m != 6 && idx != 0) show_for[p] = 0x4b;
    if (m != 5 && m != 6 && m != 7 && m != 4) hud_place[p] = Opp_Loaded() ? Opp_PlaceAt(Race_Base() + p, idx) : 0xf;
}

/* 0x414720: the three places around the player's at its last split */
static void standings_rows(int p)
{
    int s = split_idx[p];
    if (s == 0) return;
    int pl = 0xf;
    if (Opp_Loaded()) {
        pl = Opp_PlaceAt(Race_Base() + p, s);
        if (pl < 1) {
            rows[p][0] = pl, rows[p][1] = pl + 1, rows[p][2] = pl + 2;
            return;
        }
    }
    if (Opp_Entries(s) - 1 <= pl) rows[p][0] = pl - 2, rows[p][1] = pl - 1, rows[p][2] = pl;
    else rows[p][0] = pl - 1, rows[p][1] = pl, rows[p][2] = pl + 1;
}

void Hud_Update(int p, bool on_line)
{
    /* 0x411f70's first frame: the reference splits, the leader's (0x455d40(0, i)) */
    if (!hud_ready && Opp_Loaded() && Gs_Mode() != 3 && Gs_Mode() != 7) {
        hud_ready = true;
        for (int i = 0; i < 12; i++) ref_split[i] = Opp_TimeAt(0, i);
    }
    PENDING_NOTE(0x411f70, "Hud_Update (the gauges, the splits' bar, the messages)");
    if (on_line) hud_time[p] = 0;
    else hud_time[p] = Race_Finished(p) ? Times_Finish(p) : Times_Race();
    rally_split(p);   /* 0x413160 */
    standings_rows(p);   /* 0x414720 */
    if (show_for[p]) show_for[p]--;
}

/* 0x415bd0: a time as MM:SS:cc (or --:--:--) in a font at a fraction of the screen */
static void draw_time(int32_t t, int font, int32_t fx, int32_t fy, const uint8_t *rgba, uint32_t flags)
{
    char s[32];
    if (t == -1) snprintf(s, sizeof s, "--:--:--");
    else snprintf(s, sizeof s, "%02d:%02d:%02d", (t / 6000) % 100, (t % 6000) / 100, (t % 6000) % 100);
    int w, h;
    D3d_ScreenSize(&w, &h);
    Font_Print(font, s, (int)(((int64_t)w * fx) >> 16), (int)(((int64_t)h * fy) >> 16), rgba, flags);
}

static const uint8_t *colour(uint32_t a)
{
    static uint8_t c[8][4];
    static int n;
    uint8_t *r = c[n++ & 7];
    for (int i = 0; i < 4; i++) r[i] = gm8(a + (uint32_t)i);
    return r;
}

/* 0x415750: the name at place e of split s (a player's if one is there, else the driver's, short
   when asked), in the font's case */
static void place_name(char *b, size_t n, int e, int s, bool shrt)
{
    b[0] = 0;
    int drv = Opp_DriverAt(e, s);
    if (drv == -1) return;
    const char *name = NULL;
    for (int i = 0; i < (int)(gm32(0x52afb4) >> 13 & 0xf); i++)
        if (Opp_PlaceAt(i, s) == e) {
            uint32_t pr = gm32(0x531350 + 0x30u * (uint32_t)i) >> 0x12 & 0xf;
            name = pr == 0xf ? "" : (const char *)gm(0x52fa18 + pr * 0x650);
        }
    if (!name) name = shrt ? exe_str(0x5167e0 + 4u * (uint32_t)(drv < 0 || drv > 15 ? 0 : drv)) : exe_str(0x5166b4 + 0x14u * (uint32_t)(drv < 0 || drv > 15 ? 0 : drv));
    snprintf(b, n, "%s", name);
    Text_ToFontCase(b);
}

/* 0x414ed0 for one view: three boxes (the player's lit), each with the place, the name and the time
   at the last split */
static void standings(int p)
{
    if (rows[p][0] == -1 && !Pause_Active()) return;
    int w, h;
    D3d_ScreenSize(&w, &h);
    int ox = 0, oy = 0;   /* 0x464b10: the view's corner */
    int step = ((h * 0xccc) >> 16) - 2, ty = ((h * 0x1b32) >> 16) - 2 + oy;
    int16_t r[4] = {(int16_t)(((w * 0xc00) >> 16) + ox), (int16_t)(((h << 0xc) >> 16) + oy), (int16_t)((w * 0x47ae) >> 16), (int16_t)step};
    for (int k = 0; k < 3; k++, r[1] = (int16_t)(r[1] + step), ty += step) {
        int e = rows[p][k];
        Rect_Fill(r, colour(e == hud_place[p] ? 0x5170d8 : 0x5170d4), 2);
        if (e == -1) continue;
        char b[64];
        snprintf(b, sizeof b, exe_str(0x517df0), e + 1);
        Font_Print(0, b, ((w * 0xf95) >> 16) + ox, ty, colour(0x5170d0), 0x22);
        place_name(b, sizeof b, e, split_idx[p], false);
        if (Font_TextWidth(0, b) > w * 0x59 / 0x280) place_name(b, sizeof b, e, split_idx[p], true);
        int32_t t = 3000;
        if (!Opp_Loaded()) snprintf(b, sizeof b, "%s", exe_str(0x517de0));
        else t = Opp_TimeAt(e, split_idx[p]);
        Font_Print(0, b, ((w * 0x132a) >> 16) + ox, ty, colour(0x5170d0), 0x21);
        draw_time(t, 5, (int32_t)(((int64_t)ox << 16) / w) + 0x51e4, (int32_t)(((int64_t)(ty - 1) << 16) / h), colour(0x5170d0), 0x24);
    }
}

/* 0x413fe0 for one player: a box (0x5170bc) and the time in lcd_640 (slot 4) at its top right */
static void time_panel(int p)
{
    int w, h;
    D3d_ScreenSize(&w, &h);
    PENDING_NOTE(0x4143f0, "Hud_SplitBar");
    int16_t r[4] = {(int16_t)((w * 0xab9b) >> 16), (int16_t)((h * 0x1aac) >> 16), (int16_t)(((w * 0x4865) >> 16) + 1),
                    (int16_t)((h * 0x1554) >> 16)};
    if (gm32(0x543098)) PENDING_NOTE(0x459390, "Hud_TimePanel (laps)");
    Rect_Fill(r, colour(0x5170bc), 2);
    /* the viewport's corner (0, 0 for one player) over the screen, plus the offsets */
    draw_time(hud_time[p], 4, 0xaf30, 0x2e66, colour(0x5170f4), 0x21);
}

void Hud_Draw(int p)
{
    PENDING_NOTE(0x405d60, "Hud_Draw (the debug position line)");
    uint32_t t = gm32(0x531350 + 0x30u * (uint32_t)p) >> 0x12 & 0xf;
    uint8_t flags = Gs_Mode() == 4 ? gm8(0x52f3ed + 0xc4u * (uint32_t)p) : t != 0xf ? gm8(0x52ff90 + t * 0x650) : 0;
    uint32_t m = Gs_Mode();
    if (flags & 4) {
        if (m == 0 || m == 1 || m == 2) standings(p);
        else PENDING_NOTE(0x4147f0, "Hud_Standings (the other modes)");
    }
    if (flags & 8) time_panel(p);
    if (flags & 0x20) PENDING_NOTE(0x4137e0, "Hud_SplitDelta");
}
