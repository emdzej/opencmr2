/* The front end's work on the way out (0x406820, 0x4cf060, 0x4d0230, 0x4eabc0, 0x4ec1a0, and the
   teardown 0x49c0f0 / 0x4a5be0 / 0x4a5d10 runs). */
#include "game/saves.h"
#include "fe/fe.h"
#include "fe/fe_page.h"
#include "audio/snd.h"
#include "font.h"
#include "game/gstate.h"
#include "game/mem.h"
#include "game/profile.h"
#include "game/select.h"
#include "world/tod.h"
#include "exe.h"
#include "render/tex.h"
#include "text.h"
#include <string.h>

/* 0x4cf060 with 0x4d0230: the players, mode, difficulty, rally and stage (a super special's two
   indices with bit 24) for the championship's tables (0x8173f0), each player's entry cleared */
void Fe_ChampTables(void)
{
    uint32_t mw = gm32(0x52afb4), sel = gm32(0x52f2ac), n = mw >> 13 & 0xf;
    gw32(0x8173f0, n);
    gw32(0x8173f4, mw >> 3 & 0x7f);
    gw32(0x817400, mw >> 10 & 7);
    gw32(0x8173f8, sel & 0x1f);
    if (sel >> 0x18 & 1) {
        gw32(0x8173fc, sel >> 0xc & 3);
        gw32(0x817404, sel >> 10 & 3);
    } else
        gw32(0x8173fc, sel >> 5 & 0x1f);
    for (uint32_t i = 0, a = 0x817424; i < n; i++, a += 10) {
        gw16(a + 4, 0), gw16(a, 0), gw16(a + 2, 0);
        gw8(a - 4, 0), gw8(a - 3, 0), gw8(a - 2, 0), gw8(a - 1, 0);
    }
    gw32(0x817418, 0);
    gw16(0x817414, 0), gw16(0x817416, 0);
    gw8(0x817410, 0), gw8(0x817411, 0), gw8(0x817412, 0), gw8(0x817413, 0);
}

/* 0x4eabc0: the options word's third byte from the second by the mode: modes 1-3 and 7 all its bits,
   4-6 bits 8, 10 and 11 only, 0 and the demo bit 10 */
void Fe_OptionsMode(void)
{
    uint32_t o = gm32(0x52afc0), m = Gs_Mode(), keep;
    if (gm8(0x52ea52) || m == 0 || m == 4) keep = 0x400;
    else if (m == 5 || m == 6) keep = 0xd00;
    else keep = 0xff00;
    gw32(0x52afc0, (o & keep) << 8 | (o & 0xff00ffffu));
}

/* 0x406820: each player's set-up from the rally's (0x51682c, 7 bytes); with a chosen wet weather the
   tyres (even ones but 6) one up */
void Fe_CarSetups(void)
{
    uint32_t r = gm32(0x52f2ac) & 0x1f;
    for (uint32_t i = 0; i < 4; i++) {
        uint32_t p = 0x52f0e0 + 7 * i;
        for (uint32_t k = 0; k < 7; k++) gw8(p + k, gm8(0x51682c + r * 7 + k));
        if (gm32(0x52f290) && gm32(0x52f0fc) > 2) {
            int8_t c = (int8_t)gm8(p);
            if (c % 2 == 0 && c != 6) gw8(p, (uint8_t)(c + 1));
        }
    }
}

/* 0x4ec1a0: each player's profile one race older (+0x40 bits 7-14, to 0xff) */
void Fe_AgeProfiles(void)
{
    uint32_t n = gm32(0x52afb4) >> 13 & 0xf;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t p = gm32(0x531350 + 0x30 * i) >> 0x12 & 0xf, a = 0x52fa5c + p * 0x650, v = gm32(a);
        if ((v & 0x7f80) < 0x7f80) gw32(a, ((((v & 0xffffff80u) + 0x80) ^ v) & 0x7f80) ^ v);
    }
}

/* the at-exit functions the front end registered since its start (0x49c0f0 back to its mark): its
   archives (0x4d2100), the saves list (0x4f4e80), the sound layer (0x4b7ae0), the text (0x4f4cc0)
   and the credits (0x4f48b0); then every texture's surface and record (0x4a5be0, 0x4a5d10) */
void Fe_Teardown(void)
{
    Bfl_Unmount(Fe_Res());
    Champs_Free();
    Bfl_Unmount(Fe_ResLang());
    Bfl_Unmount(Fe_Common());
    for (int i = 0; i < 5; i++) Bfl_Unmount(Fe_Text(i));
    Voices_StopAll();
    Snd_Truncate(0);
    Text_Free();
    Font_FreeSlots();
    Tex_FreeAll();
}

/* the selection's fields the demo changes (0x52afb4: the mode bits 3-9 with 0x4ea8e0's network copy,
   the players 13-16, the level 10-12; 0x52f2a9's CPU cars; 0x52f2ac: the class 10-11, the race
   12-13, the laps 16-19, the rally 0-4, the stage 5-9, the last two with the mode's flags again) */
static void put(uint32_t va, uint32_t mask, uint32_t v) { gw32(va, (gm32(va) & ~mask) | (v & mask)); }
static void set_mode_bits(uint32_t m)   /* 0x4ea8e0 */
{
    put(0x52afb4, 0x3f8, m << 3);
    if (m > 7) gw32(0x52e920, gm32(0x52afb4) >> 3 & 0x7f);
}
static void set_race(uint32_t r) { put(0x52f2ac, 0x3000, r << 0xc); Sel_ModeFlags(); }     /* 0x406960 */
static void set_stage(uint32_t s) { put(0x52f2ac, 0x3e0, s << 5); Sel_ModeFlags(); }      /* 0x4068e0 */

/* 0x406470: the selection kept (the mode, players, level, CPU cars, class, race, laps, stage, rally,
   and 0x52afb4's bits 23-26 and 27-30) */
static void demo_keep(void)
{
    uint32_t mw = gm32(0x52afb4), sel = gm32(0x52f2ac);
    gw32(0x52af94, mw >> 3 & 0x7f);
    gw32(0x52ea44, mw >> 13 & 0xf);
    gw32(0x52af9c, mw >> 10 & 7);
    gw32(0x52af80, gm8(0x52f2a9) & 0xf);
    gw32(0x52af84, sel >> 10 & 3);
    gw32(0x52af8c, sel >> 12 & 3);
    gw32(0x52e93c, sel >> 16 & 0xf);
    gw32(0x52ea48, Tod_Stage());
    gw32(0x52e940, Tod_Rally());
    gw8(0x52af98, (uint8_t)(mw >> 0x17 & 0xf));
    gw8(0x52af88, (uint8_t)(mw >> 0x1b & 0xf));
}

void Fe_DemoSetup(void)
{
    static const uint8_t rally[3] = {5, 3, 2}, stage[3] = {2, 0, 0};   /* (instruction operands) */
    uint32_t k = gm32(0x52f3d8);
    gw32(0x52f3d8, (k + 1) & 3);
    Player_AssignProfile(0, -1);   /* 0x4eb860 */
    Profile_Create(0);
    Player_SetGuest(0, true);      /* 0x4eb000 */
    Fe_SetPlayerName(0, exe_str(0x519ec4));   /* "cmr" */
    Fe_SetCar(0, 0);
    Fe_SetAutoBox(0, 1);
    demo_keep();
    if (k == 3) {
        /* the arcade: one player, the third level, five CPU cars of the first class at the first
           location's first race, one lap, the CPU cars by the car (the demo's fixed set) */
        set_mode_bits(6);
        put(0x52afb4, 0x1e000, 1u << 13);
        put(0x52afb4, 0x1c00, 2u << 10);
        gw32(0x52f2b0, gm32(0x52f2b0) & ~0x800u);   /* 0x40e330 */
        Sel_SetCpuCount(5);
        put(0x52f2ac, 0xc00, 0);
        set_race(0);
        put(0x52f2ac, 0xf0000, 1u << 16);
        Sel_CpuCars();   /* 0x40dfa0 */
        put(0x52f2ac, 0x1f, 5);
        set_stage(2);
        return;
    }
    /* a single stage for one player at the second level, without the chosen weather, one lap */
    set_mode_bits(2);
    gw32(0x52f290, 1), gw32(0x52f0fc, 0);   /* 0x40df60 */
    put(0x52afb4, 0x1e000, 1u << 13);
    put(0x52afb4, 0x1c00, 1u << 10);
    gw32(0x52f2b0, gm32(0x52f2b0) & ~0x800u);
    put(0x52f2ac, 0xf0000, 1u << 16);
    put(0x52f2ac, 0x1f, rally[k]);
    set_stage(stage[k]);
}

void Fe_DemoRestore(void)
{
    set_mode_bits(gm32(0x52af94));
    put(0x52afb4, 0x1e000, gm32(0x52ea44) << 13);
    put(0x52afb4, 0x1c00, gm32(0x52af9c) << 10);
    Sel_SetCpuCount((int)gm32(0x52af80));
    put(0x52f2ac, 0xc00, gm32(0x52af84) << 10);
    set_race(gm32(0x52af8c));
    put(0x52f2ac, 0xf0000, gm32(0x52e93c) << 16);
    set_stage(gm32(0x52ea48));
    put(0x52f2ac, 0x1f, gm32(0x52e940));
    gw32(0x52afb4, ((uint32_t)(gm8(0x52af88) & 0xf) << 4 | (gm8(0x52af98) & 0xf)) << 0x17 | (gm32(0x52afb4) & 0x807fffffu));
}
