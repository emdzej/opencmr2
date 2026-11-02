/* The save files (see saves.h). The original writes them under the save folder (0x4aa700, the
   registry's CMR2Saves) with File_Save 0x4aa5a0; the port keeps each in the platform's user storage,
   its key the path below that folder. */
#include "game/saves.h"
#include "game/mem.h"
#include "exe.h"
#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { GAMEINFO = 0x52afa0, GAMEINFO_SIZE = 0x399c, PROFILES = 0x52fa08, PROFILE_SIZE = 0x650 };

static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

/* 0x4ea5e0: the file taken when it is 0x399c bytes with the version 0x11 at +0x10. The original then
   puts its screen settings (+0x24-+0x30: size, depth, the display flags) into the device, which the
   port's runner owns (0x405830's check of the saved device, which resets the graphics to 640 x 480
   when it differs, is moot with the port's one device); option 17 is turned off if set (0x4eac50),
   and 0x817574 cleared (0x4d0590) */
bool GameInfo_Load(void)
{
    size_t n = 0;
    uint8_t *d = plat_load_user_file("Configuration\\GameInfo.rcf", &n);
    if (!d) return false;
    if (n != GAMEINFO_SIZE || le32(d + 0x10) != 0x11) {
        free(d);
        return false;
    }
    for (uint32_t i = 0; i < GAMEINFO_SIZE; i++) gw8(GAMEINFO + i, d[i]);
    free(d);
    if (gm32(0x52e890) >> 0x11 & 1) gw32(0x52e890, gm32(0x52e890) ^ 0x20000);
    gw8(0x817574, 0);
    return true;
}

/* 0x4ea840: the windowed flag (0x52afd0 bit 0) from the device first (0x405b30; the port's stays as
   it is), then the 0x399c bytes */
void GameInfo_Save(void)
{
    static uint8_t b[GAMEINFO_SIZE];
    for (uint32_t i = 0; i < GAMEINFO_SIZE; i++) b[i] = gm8(GAMEINFO + i);
    plat_save_user_file("Configuration\\GameInfo.rcf", b, sizeof b);
}

/* 0x4eb2e0: pps\<name><a><b><c><d>.pps, the four from the profile's word (+0x14): bits 16-20, 8-11,
   0-7 and 12-15 (the date of birth's fields), two digits each */
static void profile_path(char *dst, size_t cap, uint32_t prof)
{
    uint32_t w = gm32(prof + 0x14);
    char name[17];
    for (int i = 0; i < 16; i++) name[i] = (char)gm8(prof + 0x10 + (uint32_t)i);
    name[16] = 0;
    snprintf(dst, cap, "pps\\%s%.2d%.2d%.2d%.2d.pps", name, (int)(w >> 0x10 & 0x1f), (int)(w >> 8 & 0xf), (int)(w & 0xff),
             (int)(w >> 0xc & 0xf));
}

/* 0x4eb340: a profile's 0x650 bytes to its file */
static bool profile_save(uint32_t prof)
{
    static uint8_t b[PROFILE_SIZE];
    char path[64];
    profile_path(path, sizeof path, prof);
    for (uint32_t i = 0; i < PROFILE_SIZE; i++) b[i] = gm8(prof + i);
    return plat_save_user_file(path, b, sizeof b);
}

/* 0x4eb3e0: the four profiles: each in use (bit 21 of its word clear, a name) and changed (0x52fa00)
   saved and marked unchanged; true when every save went */
bool Profiles_Save(void)
{
    bool all = true;
    for (uint32_t i = 0; i < 4; i++) {
        uint32_t prof = PROFILES + i * PROFILE_SIZE;
        if ((gm32(prof + 0x14) & 0x200000) || !gm8(prof + 0x10) || !gm8(0x52fa00 + i)) continue;
        bool ok = profile_save(prof);
        if (ok) gw8(0x52fa00 + i, 0);
        all = all && ok;
    }
    return all;
}
