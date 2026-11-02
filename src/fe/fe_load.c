/* The front end's championship pages: New / Load (0x81dc78) and the saved championships' list
   (0x820018), and a saved championship put back (0x4fb400). */
#include "exe.h"
#include "fe/fe_page.h"
#include "fe/fe_strip.h"
#include "fe/pages.h"
#include "font.h"
#include "game/gstate.h"
#include "game/input.h"
#include "game/mem.h"
#include "game/profile.h"
#include "game/saves.h"
#include "game/standings.h"
#include "game/unlocks.h"
#include "render/d3d7.h"
#include "text.h"
#include "world/tod.h"
#include <stdio.h>
#include <stdlib.h>

/* the list's state, shared with the service area's (0x82a924 the chosen, 0x82aa3c the top, 0x82ac48
   the rows shown, 0x82aa40 the count) */
enum { SEL = 0x82a924, TOP = 0x82aa3c, ROWS = 0x82ac48, COUNT = 0x82aa40 };

/* 0x4e2040: the trail (the rally, New / Load), the rows, the main menu's strip unlit */
void fe_4e2040(Menu *m)
{
    int w, h;
    D3d_ScreenSize(&w, &h);
    const char *names[2] = {Text_Get(0xe7), Text_Get(0xc)};
    Fe_Header();
    Fe_Titles(m, w * 0x18 / 0x280, h * 0x26 / 0x1e0, true, 3, names, 2);
    List_Draw(m, NULL, -1, -1, 0, true);
    Strip_DrawMain(&pg_81d6d8, false, NULL);   /* 0x4f8410 */
}

/* 0x4ef4c0: the count, the list at its top */
void fe_4ef4c0(Menu *m, bool back)
{
    (void)m, (void)back;
    gw32(COUNT, (uint32_t)Champs_Count());
    gw32(SEL, 0), gw32(TOP, 0), gw32(ROWS, 0);
}

/* 0x4ef4e0: Up and Down move in the list (the page's own keys off but choosing and Back), its top kept
   within 13 rows of the choice */
void fe_4ef4e0(Menu *m)
{
    gw32(COUNT, (uint32_t)Champs_Count());
    Menu_SetFlags(m, false, false, true, true);
    uint32_t k = Input_Record(0)->pressed;
    int n = gms32(COUNT), sel = gms32(SEL), top = gms32(TOP);
    bool down = true;
    if (n < 1) top = 0, sel = -1;
    else {
        if (sel == -1) sel = 0;
        else if ((k & MB_UP) && sel > 0) sel--;
        else if ((k & MB_DOWN) && sel < n - 1) sel++;
        down = sel < top;
    }
    if (down && --top < 0) top = 0;
    if (top + 0xd <= sel) top++;
    gw32(SEL, (uint32_t)sel), gw32(TOP, (uint32_t)top);
    gw32(ROWS, (uint32_t)(n < 0xe ? n : 0xd));
}

/* 0x4e20e0: the trail (the rally, New / Load, Load), the saved championships 13 rows from the list's
   top (the file, the players with rally and stage, the name; the chosen lit), or the message; the
   main menu's strip unlit */
void fe_4e20e0(Menu *m)
{
    (void)m;
    /* the stage names "1" to "10" and "SS" (instruction operands) */
    static const uint32_t stage_str[11] = {0x524dc8, 0x516934, 0x516930, 0x516948, 0x524dc4, 0x51693c,
                                           0x516928, 0x516938, 0x51692c, 0x524dc0, 0x524dbc};
    int w, h;
    D3d_ScreenSize(&w, &h);
    const char *names[3] = {Text_Get(0xe7), Text_Get(0xc), Text_Get(0x8a)};
    Fe_Header();
    Fe_TitleRow(w * 0x18 / 0x280, h * 0x26 / 0x1e0, names, 3);
    int rows = gms32(ROWS), top = gms32(TOP), sel = gms32(SEL);
    if (rows < 1) Font_Print(0, Text_Get(0x20d), w / 2, h / 2, gm(0x52496c), 0x12);
    for (int r = 0; r < rows; r++) {
        int i = top + r, st = Champs_Stage(i);
        char b[256];
        snprintf(b, sizeof b, Text_Get(0x210), Champs_Players(i), Text_Get(0x27 + Champs_Rally(i)),
                 st < 11 ? exe_str(stage_str[st]) : "");
        const uint8_t *c = gm(i == sel ? 0x524968 : 0x52496c);
        int y = h * 100 / 0x1e0 + h * 0x14 / 0x1e0 * r;
        Font_Print(0, Champs_File(i), w * 0x32 / 0x280, y, c, 0x11);
        Font_Print(0, b, w * 0x9b / 0x280, y, c, 0x11);
        Font_Print(0, Champs_Name(i), w * 0xff / 0x280, y, c, 0x11);
    }
    Strip_DrawMain(&pg_81d6d8, false, NULL);   /* 0x4f8410 */
}

static void put_bits(uint32_t va, uint32_t mask, uint32_t v) { gw32(va, (gm32(va) & ~mask) | (v & mask)); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static void to_gm(uint32_t dst, const uint8_t *src, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) gw8(dst + i, src[i]);
}

/* 0x4eb160: the standings from the player records' points (bits 6-12) and times (+4): the players
   into 15 down, the computers' from 0 up; then sorted */
static void records_to_standings(void)
{
    int np = (int)Gs_Players();
    for (int i = 0; i < 16; i++) {
        int k = i < np ? 0xf - i : i - np;
        uint32_t a = 0x531350 + 0x30u * (uint32_t)i;
        Stand_SetPoints(k, (int)(gm32(a) >> 6 & 0x7f));
        Stand_SetTime(k, gm32(a + 4));
    }
    Stand_Sort();
}

/* a progress field of 0x52b03c (4 bits from `shift`) raised to the rally's number */
static void reach(uint32_t shift)
{
    uint32_t r = Tod_Rally() + 1, a = 0x52b03c;
    if ((gm32(a) >> shift & 0xf) < r) put_bits(a, 0xfu << shift, r << shift);
}

/* 0x4fb400: a saved championship's record (Champ_Save) put back: the mode, level, players and the
   mode's bit 17; the profiles reset and each player given a new one with its record's bits, name and
   birth, car and set-up, then its saved profile over it (its name and birth again) unless the
   profile is a guest's (+0x14 bit 21), a guest without the file; the 16 player records; each
   player's automatic box and car driven; the counts, weather and tables, rally, stage and 0x52f2a8;
   the standings; the progress (0x52b03c) up to the rally for the level; 0x52f2b0 bit 11 clear */
bool Champ_Load(const uint8_t *rec)
{
    if (!rec) return false;
    uint32_t w = rd32(rec + 0x30);
    Fe_SetMode(w & 0x7f);
    put_bits(0x52afb4, 0x1c00, (w >> 7 & 7) << 10);    /* 0x4ea930 */
    put_bits(0x52afb4, 0x1e000, (w >> 10 & 0xf) << 13);   /* 0x4ea8c0 */
    put_bits(0x52afb4, 0x20000, (w >> 14 & 1) << 17);   /* 0x4ea950 */
    Profiles_Init();
    int np = (int)Gs_Players();
    for (int p = 0; p < np; p++) {
        Player_AssignProfile(p, -1);
        Profile_Create(p);
        Player_SetGuest(p, false);
        uint32_t t = Player_Word(p) >> 0x12 & 0xf;
        /* 0x408860 gives no profile for 15 (the original would crash on it) */
        if (t == 0xf) continue;
        uint32_t prof = 0x52fa08 + t * 0x650;
        const uint8_t *who = rec + 0xc4 + 0xc * p;
        put_bits(prof + 0x5c, 0x1fff, rd32(rec + 0xb4 + 4 * p));
        to_gm(prof + 0x10, who, 0xc);
        to_gm(0x52ebc0 + 0x148u * (uint32_t)p, rec + 0xf4 + 0x148 * p, 0x148);
        to_gm(0x52f0e0 + 7u * (uint32_t)p, rec + 0x614 + 7 * p, 7);
        if (gm32(prof + 0x14) & 0x200000) continue;
        size_t n = 0;
        uint8_t *d = Profile_LoadFile(who, &n);
        if (!d) {
            Player_SetGuest(p, false);
            continue;
        }
        /* the original copies 0x650 bytes from whatever was read */
        if (n >= 0x650) to_gm(prof, d, 0x650);
        to_gm(prof + 0x10, who, 0xc);
        free(d);
    }
    for (int i = 0; i < 16; i++) {
        uint32_t a = 0x531350 + 0x30u * (uint32_t)i, r = rd32(rec + 0x34 + 8 * i);
        put_bits(a, 0x3f, r);
        put_bits(a, 0x1fc0, r >> 1);
        gw32(a + 4, rd32(rec + 0x38 + 8 * i));
    }
    for (int p = 0; p < np; p++) {
        Fe_SetAutoBox(p, (uint8_t)(rd32(rec + 0x34 + 8 * p) >> 6 & 1));
        Player_CarDriven(p, Player_Word(p) & 0x3f);   /* 0x409010 */
    }
    for (uint32_t k = 0; k < 16; k++) gw8(0x5335a8 + k, rec[0x7dc + k]);   /* 0x40cff0 */
    /* Tod_Weather(0)'s block: with the stage table's time its two words at 0x52ea60 and what follows */
    to_gm(gm32(0x52f2ac) >> 24 & 1 ? 0x52ea60 : 0x52f100, rec + 0x630, 0xa0);
    to_gm(0x52f1a0, rec + 0x6d0, 0x50);
    to_gm(0x52f294, rec + 0x720, 0x14);
    to_gm(0x52f1f0, rec + 0x734, 0x50);
    to_gm(0x52f240, rec + 0x784, 0x50);
    uint32_t rs = rd32(rec + 0x7d8);
    put_bits(0x52f2ac, 0x1f, rs);             /* 0x4068b0 */
    put_bits(0x52f2ac, 0x3e0, rs);            /* 0x4068e0 */
    gw8(0x52f2a8, rec[0x7d4]);                /* 0x4068d0 */
    records_to_standings();
    switch (gm32(0x52afb4) >> 10 & 7) {       /* 0x405d90 */
    case 0: reach(8); break;
    case 1: reach(8), reach(0xc); break;
    case 2: reach(8), reach(0xc), reach(0x10); break;
    }
    gw32(0x52f2b0, gm32(0x52f2b0) & ~0x800u);   /* 0x40e330(0) */
    return true;
}

/* 0x4f3a00: the chosen championship put back and its stage started */
void fe_4f3a00(Menu *m, MenuItem *it)
{
    (void)m, (void)it;
    if (gms32(COUNT) < 1) return;
    if (Champ_Load(Champs_Record(gms32(SEL)))) Fe_Leave(false, true, false);
}
