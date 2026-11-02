/* The save files (see saves.h). The original writes them under the save folder (0x4aa700, the
   registry's CMR2Saves) with File_Save 0x4aa5a0; the port keeps each in the platform's user storage,
   its key the path below that folder. */
#include "game/saves.h"
#include "game/mem.h"
#include "game/profile.h"
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

/* 0x4eb2e0: pps\<name><a><b><c><d>.pps for a name (4 bytes) and word: the word's bits 16-20, 8-11,
   0-7 and 12-15 (the date of birth's fields), two digits each */
static void entry_path(char *dst, size_t cap, const uint8_t *e)
{
    uint32_t w = le32(e + 4);
    char name[5];
    memcpy(name, e, 4);
    name[4] = 0;
    snprintf(dst, cap, "pps\\%s%.2d%.2d%.2d%.2d.pps", name, (int)(w >> 0x10 & 0x1f), (int)(w >> 8 & 0xf), (int)(w & 0xff),
             (int)(w >> 0xc & 0xf));
}

static void profile_path(char *dst, size_t cap, uint32_t prof)
{
    uint8_t e[8];
    for (uint32_t i = 0; i < 8; i++) e[i] = gm8(prof + 0x10 + i);
    entry_path(dst, cap, e);
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

/* 0x4eb370: the player's profile saved now if in use and changed; true when saved or not needed */
bool Profile_SavePlayer(int player)
{
    uint32_t k = gm32(0x531350 + 0x30u * (uint32_t)player) >> 0x12 & 0xf;
    uint32_t prof = PROFILES + k * PROFILE_SIZE;
    if ((gm32(prof + 0x14) & 0x200000) || !gm8(0x52fa00 + k)) return true;
    if (!profile_save(prof)) return false;
    gw8(0x52fa00 + k, 0);
    return true;
}

/* The saved profiles found (0x531764, 12 bytes each: the name, the word and the next word of the
   file's +0x10; their count 0x531650) */
enum { LIST_MAX = 64 };
static uint8_t list[LIST_MAX][12];
static int nlist;

/* 0x4eb700: every pps\*.pps file (0x4ebee0 each: its +0x10-+0x1b); the platform's file order */
void Profiles_Scan(void)
{
    nlist = 0;
    char name[160];
    for (int i = 0; plat_user_file_name(i, name, sizeof name) && nlist < LIST_MAX; i++) {
        size_t n = strlen(name);
        if (strncmp(name, "pps\\", 4) || n < 8 || strcmp(name + n - 4, ".pps")) continue;
        size_t sz = 0;
        uint8_t *d = plat_load_user_file(name, &sz);
        if (!d) continue;
        if (sz >= 0x1c) memcpy(list[nlist++], d + 0x10, 12);
        free(d);
    }
}

int Profiles_Count(void) { return nlist; }                                       /* 0x4eb440 */
const uint8_t *Profiles_Entry(int i) { return i >= 0 && i < nlist ? list[i] : NULL; }   /* 0x4eb450 */

/* the date of birth's fields of two words the same (bits 0-7, 8-11, 12-15, 16-20, 22-27) */
static bool same_birth(uint32_t a, uint32_t b)
{
    uint32_t x = a ^ b;
    return !(x & 0x1f0f00) && !(x & 0xff) && !(x & 0xfc0f000);
}

static bool same_name(uint32_t prof, const uint8_t *name)
{
    for (uint32_t i = 0; i < 4; i++) {
        uint8_t c = gm8(prof + 0x10 + i);
        if (c != name[i]) return false;
        if (!c) return true;
    }
    return true;
}

/* 0x4ebd60: a found profile already a player's (by its name and birth fields) */
bool Profiles_InUse(int i)
{
    const uint8_t *e = Profiles_Entry(i);
    if (!e) return false;
    for (uint32_t p = 0; p < 16; p++) {
        uint32_t t = gm32(0x531350 + 0x30 * p) >> 0x12 & 0xf;
        if (t == 0xf) continue;
        uint32_t prof = PROFILES + t * PROFILE_SIZE;
        if (same_birth(gm32(prof + 0x14), le32(e + 4)) && same_name(prof, e)) return true;
    }
    return false;
}

/* 0x4eb4c0: found profile i into the player: its file (bits 7-14 of +0x54 cleared) over the profile
   of the same name and birth fields, or a new one (0x4eb860, 0x4ebf20); its +0x18-+0x1a cleared; the
   player's word: the car (+0x54 bits 0-4), bit 6 into bit 25, bit 13 set, bits 22-24 clear, the
   second word 0 */
bool Profile_LoadInto(int player, int i)
{
    const uint8_t *e = Profiles_Entry(i);
    if (!e) return false;
    char path[64];
    entry_path(path, sizeof path, e);
    size_t n = 0;
    uint8_t *d = plat_load_user_file(path, &n);
    if (!d) return false;
    if (n < PROFILE_SIZE) {
        free(d);
        return false;
    }
    uint32_t w54 = le32(d + 0x54) & 0xffff807f;
    d[0x54] = (uint8_t)w54, d[0x55] = (uint8_t)(w54 >> 8), d[0x56] = (uint8_t)(w54 >> 16), d[0x57] = (uint8_t)(w54 >> 24);
    uint32_t pr = 0x531350 + 0x30u * (uint32_t)player, slot = 4;
    for (uint32_t k = 0; k < 4; k++) {
        uint32_t prof = PROFILES + k * PROFILE_SIZE;
        if (same_name(prof, d + 0x10) && same_birth(le32(d + 0x14), gm32(prof + 0x14))) {
            slot = k;
            break;
        }
    }
    if (slot < 4) gw32(pr, (gm32(pr) & 0xffc3ffff) | slot << 0x12);
    else {
        gw32(pr, gm32(pr) | 0x3c0000);
        Player_AssignProfile(player, -1);
        Profile_Create(player);
    }
    uint32_t t = gm32(pr) >> 0x12 & 0xf, prof = PROFILES + t * PROFILE_SIZE;
    for (uint32_t k = 0; k < PROFILE_SIZE; k++) gw8(prof + k, d[k]);
    gw8(prof + 0x1a, 0), gw8(prof + 0x19, 0), gw8(prof + 0x18, 0);
    uint32_t w = gm32(pr), c54 = gm32(prof + 0x54);
    gw32(pr, (c54 & 0x40) << 0x13 | (c54 & 0x1f) | (w & 0xfdffe000) | 0x2000);
    gw32(pr + 4, 0);
    gw32(pr, gm32(pr) & 0xfe3fffff);
    free(d);
    return true;
}
