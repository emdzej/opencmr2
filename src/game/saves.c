/* The save files (see saves.h). The original writes them under the save folder (0x4aa700, the
   registry's CMR2Saves) with File_Save 0x4aa5a0; the port keeps each in the platform's user storage,
   its key the path below that folder. */
#include "game/saves.h"
#include "game/mem.h"
#include "game/profile.h"
#include "game/gstate.h"
#include "sim/car.h"
#include "world/tod.h"
#include "exe.h"
#include "platform.h"
#include <stdio.h>
#include "render/tex.h"
#include <stdlib.h>
#include <string.h>

enum { GAMEINFO = 0x52afa0, GAMEINFO_SIZE = 0x399c, PROFILES = 0x52fa08, PROFILE_SIZE = 0x650 };

static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

/* 0x4ea5e0: the file taken when it is 0x399c bytes with the version 0x11 at +0x10. The original then
   puts its screen settings (+0x24-+0x30: size, depth) into the device, which the port's runner owns
   (0x405830's check of the saved device, which resets the graphics to 640 x 480 when it differs, is
   moot with the port's one device), and the display flags from 0x52afd0: the environment map (bits
   1-2: 8 standard, 8 | 0x10 bump, 8 | 0x80 cubic), 0x20 and 0x40 from bits 3 and 4, the mip filter
   (bits 18-19: 1 or 2), 4 from bit 29 (and +0x3c0, never read, from 0x405ca0); option 17 is turned off
   if set (0x4eac50), and 0x817574 cleared (0x4d0590) */
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
    uint32_t f = gm32(0x52afd0), o = g_render_opts & ~(0x08u | 0x10u | 0x80u | 0x20u | 0x40u | 1u | 2u | 4u);
    switch (f & 6) {
    case 2: o |= 8; break;
    case 4: o |= 8 | 0x10; break;
    case 6: o |= 8 | 0x80; break;
    default: break;
    }
    o |= (f & 8) << 2 | (f & 0x10) << 2;
    if ((f & 0xc0000) == 0x40000) o |= 1;
    else if ((f & 0xc0000) == 0x80000) o |= 2;
    o |= f >> 0x1b & 4;
    g_render_opts = o;
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

/* 0x4eb2e0 and 0x4eb4b0: the saved profile of a name and word (12 bytes as at a profile's +0x10) */
uint8_t *Profile_LoadFile(const uint8_t *e, size_t *size)
{
    char path[64];
    entry_path(path, sizeof path, e);
    return plat_load_user_file(path, size);
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

/* ---- the saved championships (gamesave\*.rcs, 0x7f4 bytes each) ---- */

enum { CHAMP = 0x81a858, CHAMP_SIZE = 0x7f4, CHAMP_MAX = 256 };

/* The list (0x81b14c the records, 0x81b150 their file names, 0x81b154 the count); the port keeps the
   records here rather than behind pointers in the game state */
static uint8_t *champ[CHAMP_MAX];
static char champ_name[CHAMP_MAX][64];
static int nchamp;

/* 0x4f4e80 */
void Champs_Free(void)
{
    for (int i = 0; i < nchamp; i++) free(champ[i]), champ[i] = NULL;
    nchamp = 0;
}

/* 0x4f4ef0: every gamesave\*.rcs in the platform's file order, 0x7f4 bytes taken from each. The
   original copies 0x7f4 bytes from whatever File_Load gave; the port skips a shorter file. */
void Champs_Scan(void)
{
    Champs_Free();
    char name[160];
    for (int i = 0; plat_user_file_name(i, name, sizeof name) && nchamp < CHAMP_MAX; i++) {
        size_t n = strlen(name);
        if (strncmp(name, "gamesave\\", 9) || n < 13 || strcmp(name + n - 4, ".rcs")) continue;
        size_t sz = 0;
        uint8_t *d = plat_load_user_file(name, &sz);
        if (!d) continue;
        if (sz < CHAMP_SIZE) {
            free(d);
            continue;
        }
        champ[nchamp] = d;
        snprintf(champ_name[nchamp++], sizeof champ_name[0], "%s", name + 9);
    }
}

int Champs_Count(void) { return nchamp; }                                               /* 0x4f4db0 */
const uint8_t *Champs_Record(int i) { return i >= 0 && i < nchamp ? champ[i] : NULL; } /* 0x4f4e50 */
const char *Champs_Name(int i) { return i >= 0 && i < nchamp ? (const char *)champ[i] + 0x10 : ""; }   /* 0x4f4dc0 */
int Champs_Players(int i) { return i >= 0 && i < nchamp ? (int)(le32(champ[i] + 0x30) >> 10 & 0xf) : 0; }   /* 0x4f4de0 */
int Champs_Rally(int i) { return i >= 0 && i < nchamp ? (int)(le32(champ[i] + 0x7d8) & 0x1f) : 0; }        /* 0x4f4e00 */
int Champs_Stage(int i) { return i >= 0 && i < nchamp ? (int)(le32(champ[i] + 0x7d8) >> 5 & 0x1f) : 0; }   /* 0x4f4e20 */
const char *Champs_File(int i) { return i >= 0 && i < nchamp ? champ_name[i] : ""; }   /* 0x4f4e70 */

/* 0x4f5150: the first gamesave\game<nnnnn>.rcs not there (_access) */
static void champ_new_name(char *dst, size_t cap)
{
    for (int i = 0;; i++) {
        snprintf(dst, cap, "gamesave\\game%.5d.rcs", i);
        size_t n = 0;
        uint8_t *d = plat_load_user_file(dst, &n);
        if (!d) return;
        free(d);
    }
}

static void put_bits(uint32_t va, uint32_t mask, uint32_t v) { gw32(va, (gm32(va) & ~mask) | (v & mask)); }

static void copy_gm(uint32_t dst, uint32_t src, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) gw8(dst + i, gm8(src + i));
}

/* 0x40d480: a championship time (0x533658) to 1/100 s */
static uint32_t champ_time(int32_t t) { return (uint32_t)(int32_t)(((int64_t)(t + 0x147) << 16) / 0x28f5c28); }

/* 0x408d80: the player records' points (bits 6-12, 0x40d070: the high half of 0x533558) and times (+4,
   0x40d3d0) from the standings: the players from the standings' end (15 down), the computers' from
   their start */
static void standings_to_records(void)
{
    int np = (int)Gs_Players();
    for (int i = 0; i < 16; i++) {
        int k = i < np ? 0xf - i : i - np, r = i;
        uint32_t a = 0x531350 + 0x30u * (uint32_t)r;
        put_bits(a, 0x1fc0, (uint32_t)((gms32(0x533558 + 4u * (uint32_t)k) >> 0x10) & 0x7f) << 6);
        gw32(a + 4, champ_time(gms32(0x533658 + 4u * (uint32_t)k)));
    }
}

/* 0x4f5190: the championship into its record (0x81a858) and to a file: the old file of list entry i
   (name kept) when name is NULL (nothing for i < 0, the record still made), else a new file with the
   name. The record: +0x10 the name; +0x30 the mode (bits 0-6), level (7-9), players (10-13), the
   mode's bit 17 (14); +0x34 the 16 player records (8 bytes: the car, the car flag of a player, the
   points, the time); +0xb4 the players' profile bits 0-12 (+0x5c), +0xc4 their name and birth (+0x10,
   12 bytes), +0xf4 the cars as they will race (0x148 each), +0x614 the set-ups (7 each); +0x630 the
   weather (0xa0 bytes from Tod_Weather(0): with the stage table's time, its two words and what
   follows them), +0x6d0-+0x7d4 the weather tables (0x52f1a0, 0x52f294, 0x52f1f0, 0x52f240), +0x7d4
   0x52f2a8, +0x7d8 rally and stage, +0x7dc 0x5335a8's 16 bytes. The original passes the name to
   sprintf as the format; the name's letters have no '%'. */
bool Champ_Save(int i, const char *name)
{
    standings_to_records();
    uint32_t w = 0;
    w |= gm32(0x52afb4) >> 3 & 0x7f;
    w |= (gm32(0x52afb4) >> 10 & 7) << 7;
    w |= (Gs_Players() & 0xf) << 10;
    w |= Gs_ModeBit17() << 0xe;
    put_bits(0x81a888, 0x7fff, w);
    int np = (int)Gs_Players();
    for (int p = 0; p < np; p++) put_bits(0x81a88c + 8u * (uint32_t)p, 0x40, Champ_CarFlag((uint8_t)p) ? 0x40 : 0);
    for (uint32_t p = 0; p < 16; p++) {
        uint32_t r = 0x81a88c + 8 * p, s = 0x531350 + 0x30 * p;
        put_bits(r, 0x3f, gm32(s));
        put_bits(r, 0x3f80, (gm32(s) & 0x1fc0) << 1);
        gw32(r + 4, gm32(s + 4));
    }
    for (int k = 0; k < 16; k++) gw8(0x81b034 + (uint32_t)k, gm8(0x5335a8 + (uint32_t)k));
    for (uint32_t p = 0; p < (uint32_t)np; p++) {
        uint32_t t = gm32(0x531350 + 0x30 * p) >> 0x12 & 0xf;
        /* 0x408860 gives no profile for 15, which the original then reads at 0x5c: a crash there */
        if (t != 0xf) {
            uint32_t prof = PROFILES + t * PROFILE_SIZE;
            put_bits(0x81a90c + 4 * p, 0x1fff, gm32(prof + 0x5c));
            copy_gm(0x81a91c + 0xc * p, prof + 0x10, 0xc);
        }
        copy_gm(0x81a94c + 0x148 * p, 0x52ebc0 + 0x148 * p, 0x148);
        copy_gm(0x81ae6c + 7 * p, 0x52f0e0 + 7 * p, 7);
    }
    uint32_t wsrc = 0x52f100;
    if (gm32(0x52f2ac) >> 24 & 1) {
        /* 0x407520 with the stage table's time: the value into 0x52ea60 and 0x52ea64, copied from there */
        const int32_t *tw = Tod_Weather(0);
        gw32(0x52ea60, (uint32_t)tw[0]), gw32(0x52ea64, (uint32_t)tw[1]);
        wsrc = 0x52ea60;
    }
    copy_gm(0x81ae88, wsrc, 0xa0);
    copy_gm(0x81af28, 0x52f1a0, 0x50);
    copy_gm(0x81af78, 0x52f294, 0x14);
    copy_gm(0x81af8c, 0x52f1f0, 0x50);
    copy_gm(0x81afdc, 0x52f240, 0x50);
    put_bits(0x81b030, 0x3ff, (Tod_Rally() & 0x1f) | (Tod_Stage() & 0x1f) << 5);
    gw8(0x81b02c, gm8(0x52f2a8));
    char path[96];
    if (!name) {
        if (i < 0 || i >= nchamp) return false;
        snprintf((char *)gm(0x81a868), 0x20, "%s", Champs_Name(i));
        snprintf(path, sizeof path, "gamesave\\%s", champ_name[i]);
    } else {
        snprintf((char *)gm(0x81a868), 0x20, "%s", name);
        champ_new_name(path, sizeof path);
    }
    return plat_save_user_file(path, gm(CHAMP), CHAMP_SIZE);
}
