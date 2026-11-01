/* The race times and the HUD (see hud.h). */
#include "game/hud.h"
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

void Times_Clear(void)
{
    shown = clock_ = 0;
    for (int i = 0; i < CAR_MAX; i++) done[i] = 0;
}

int32_t Times_Race(void) { return shown; }
int32_t Times_Finish(int car) { return car == -1 ? -1 : finish[car]; }

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
        if (done[k] || !Race_Finished(k)) continue;
        done[k] = 1;
        finish[k] = shown;
    }
}

void Hud_Update(int p, bool on_line)
{
    PENDING_NOTE(0x411f70, "Hud_Update (the gauges, the splits' bar, the messages)");
    if (on_line) hud_time[p] = 0;
    else hud_time[p] = Race_Finished(p) ? Times_Finish(p) : Times_Race();
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
    if (flags & 4) PENDING_NOTE(0x414ed0, "Hud_Standings");
    if (flags & 8) time_panel(p);
    if (flags & 0x20) PENDING_NOTE(0x4137e0, "Hud_SplitDelta");
}
