/* The drivers' medals and the unlocks after a stage (see unlocks.h). */
#include "game/unlocks.h"
#include "game/gstate.h"
#include "game/mem.h"
#include "game/opponents.h"
#include "game/profile.h"
#include "world/tod.h"

enum { PROG = 0x52b03c, PROG2 = 0x52b040, FLAGS = 0x537f68, CAR = 0x537f78, BONUS = 0x537f0c };

static bool no_profile(int p) { return (Player_Word(p) & 0x3c0000) == 0x3c0000; }
static uint32_t prof_of(int p) { return (Player_Word(p) >> 0x12 & 0xf) * 0x650u; }
static void put_bits(uint32_t va, uint32_t mask, uint32_t v) { gw32(va, (gm32(va) & ~mask) | (v & mask)); }
static void dirty(void) { gw8(0x52ea51, 1); }   /* 0x4057c0: the game's settings to be saved */
static bool option(int n) { return gm32(0x52e890) >> n & 1; }   /* 0x406410 */
static void flag(int p, uint32_t f) { gw32(FLAGS + 4u * (uint32_t)p, gm32(FLAGS + 4u * (uint32_t)p) | f); }

/* 0x409010: car c marked driven in the player's profile (+0x4c), the profile changed and its count at
   +0x18 up (0x408f20); false without a profile or when marked already */
bool Player_CarDriven(int p, uint32_t c)
{
    if (no_profile(p)) return false;
    uint32_t k = Player_Word(p) >> 0x12 & 0xf, a = 0x52fa54 + k * 0x650, bit = 1u << (c & 0x1f);
    if (gm32(a) & bit) return false;
    gw32(a, gm32(a) | bit);
    gw8(0x52fa00 + k, 1);
    gw8(0x52fa20 + k * 0x650, (uint8_t)(gm8(0x52fa20 + k * 0x650) + 1));
    return true;
}

/* 0x408e30: car c driven by the player; with `cheat` also when the all-cars option (12) or the car's
   own (7: 0 or 18, 16: 1, 15: 2, 20: 3) is on */
bool Player_HasDriven(int p, int c, bool cheat)
{
    if (no_profile(p)) return false;
    if (cheat) {
        if (option(0xc)) return true;
        if (c == 7 && (option(0) || option(0x12))) return true;
        if (c == 0x10 && option(1)) return true;
        if (c == 0xf && option(2)) return true;
        if (c == 0x14 && option(3)) return true;
    }
    return gm32(0x52fa54 + prof_of(p)) >> (c & 0x1f) & 1;
}

/* a medal: the 2-bit field of word va at `shift` raised to 3 less the place; the profile's +0x19
   count up (0x408f70) */
static void medal(int p, uint32_t va, uint32_t shift, int place)
{
    uint32_t m = (uint32_t)(3 - place);
    if ((gm32(va) >> shift & 3) >= m) return;
    put_bits(va, 3u << shift, m << shift);
    if (!no_profile(p)) gw8(0x52fa21 + prof_of(p), (uint8_t)(gm8(0x52fa21 + prof_of(p)) + 1));
}

static uint32_t medals_a(int p) { return 0x530034 + prof_of(p); }   /* the profile's +0x62c */
static uint32_t medals_b(int p) { return 0x530038 + prof_of(p); }   /* +0x630 */

/* 0x409090: the championship's medal by the level (bits 0-1, 2-3, 4-5 of the first word) */
static void champ_medal(int p, int place)
{
    uint32_t l = Gs_Mode2();
    if (l <= 2) medal(p, medals_a(p), 2 * l, place);
}

/* 0x409150: a rally's medal (eight each): level 0 the first word's bits 16 up, 1 the second's 0 up, 2
   the second's 16 up */
static void rally_medal(int p, uint32_t rally, int place)
{
    uint32_t l = Gs_Mode2();
    if (rally > 7) return;
    if (l == 0) medal(p, medals_a(p), 0x10 + 2 * rally, place);
    else if (l == 1) medal(p, medals_b(p), 2 * rally, place);
    else if (l == 2) medal(p, medals_b(p), 0x10 + 2 * rally, place);
}

/* 0x409680: the arcade's medal: level 0 bits 6-7; 1 bits 8-9 (kind 0) or 12-13; 2 bits 10-11 or 14-15 */
static void arcade_medal(int p, uint32_t kind, int place)
{
    uint32_t l = Gs_Mode2();
    if (l == 0) medal(p, medals_a(p), 6, place);
    else if (l == 1) medal(p, medals_a(p), kind == 0 ? 8 : 0xc, place);
    else if (l == 2) medal(p, medals_a(p), kind == 0 ? 10 : 0xe, place);
}

/* 0x4097b0: every field of both medal words exactly 2 (the original compares each for equality, so
   a field at 3 does not count) */
static bool all_medals(int p)
{
    if (no_profile(p)) return false;
    uint32_t a = gm32(medals_a(p)), b = gm32(medals_b(p));
    for (uint32_t s = 0; s < 32; s += 2)
        if ((a >> s & 3) != 2 || (b >> s & 3) != 2) return false;
    return true;
}

static void open_car(int p, bool guest, uint32_t c)
{
    if (!guest && Player_CarDriven(p, c)) gw32(CAR + 4u * (uint32_t)p, c), flag(p, 0x80);
}

/* the progress's 4-bit count at `shift` one up when it is the rally's number (not after the last) */
static void next_rally(int p, uint32_t shift, uint32_t f)
{
    uint32_t r = Tod_Rally(), v = gm32(PROG);
    if (r + 1 != (v >> shift & 0xf) || r == 7) return;
    put_bits(PROG, 0xfu << shift, v + (1u << shift));
    if (shift == 0xc && (gm32(PROG) >> 8 & 0xf) < (gm32(PROG) >> 0xc & 0xf))
        put_bits(PROG, 0xf00, gm32(PROG) >> 4);   /* level 1 also carries level 0 */
    flag(p, f);
    dirty();
}

/* the odd rallies' bits of 0x52b040 (bit (rally + 1) / 2 of five): level 0's at 0, level 1's at 5
   (set with level 0's), level 2's at 10 (with both) */
static void odd_rally(int p, uint32_t shift, uint32_t f)
{
    uint32_t r = Tod_Rally() + 1;
    if (!(r & 1)) return;
    uint32_t b = 1u << (r / 2 & 0x1f) & 0x1f;
    if (gm32(PROG2) >> shift & b) return;
    uint32_t set = b | (shift >= 5 ? b << 5 : 0) | (shift >= 10 ? b << 10 : 0);
    gw32(PROG2, gm32(PROG2) | set);
    flag(p, f);
    dirty();
}

/* the bonus stage k (0x52afc0's bit, 0x406360 / 0x406380) opened for the player as bonus n */
static void bonus(int p, int k, int n)
{
    if (gm8(0x52afc0) >> k & 1) return;
    gw8(0x52afc0, (uint8_t)(gm8(0x52afc0) | 1u << k));
    gw32(BONUS + 4u * (uint32_t)p, (uint32_t)n);
    flag(p, 0x2000);
}

/* the even rallies' bonus stages, won at level 1 (0, 2, 4, 6) and 2 (1, 3, 5, 7) */
static void even_rally_bonus(int p)
{
    uint32_t r = Tod_Rally();
    if (r & 1 || r > 6) return;
    if (Gs_Mode2() == 1) bonus(p, (int)r, (int)r + 1);
    if (Gs_Mode2() == 2) bonus(p, (int)r + 1, (int)r + 2);
}

static int player_car(int p)
{
    if (Gs_Mode() == 4) return gm8(0x52f3e8 + 0xc4u * (uint32_t)p);
    return (int)(Player_Word(p) & 0x3f);
}

/* 0x41b460: after a stage, for each player (its flags 0x537f68 and car 0x537f78 for the results):
   in the championship's last rally (7) at its super special (10) the championship medal (the
   points' place under 3), at level 1 the progress's bit 0 for a win, at level 0 car 21 for a win
   (not a guest); at a rally's last stage the rally's medal (the times' place under 3) and with a
   place under 6, in the championship, the next rally opened at the level (with the odd rallies'
   bits), and for a win at levels 1 and 2 an odd rally's car (the table 0x519198) or an even rally's
   bonus stage. In the arcade (mode 5) at its last kind the medal, and for a win cars 5, 2, 6, 3 and
   the progress bits by the kind and level; in mode 6 cars 13 or 14 for a win in car 12 driven. Then
   car 1 for all the medals at 2. The settings to be saved when a bonus stage opened. */
void Unlocks_AfterStage(void)
{
    int np = (int)Gs_Players();
    for (int p = 0; p < np; p++) {
        gw32(FLAGS + 4u * (uint32_t)p, 0), gw32(CAR + 4u * (uint32_t)p, 0);
        bool guest = !no_profile(p) && (Gs_Mode() == 4 || (Player_Word(p) >> 0x1a & 1));   /* 0x4085a0 */
        int slot = Opp_PlayerSlot(p);
        int pplace = (int8_t)gm8(0x533548 + (uint32_t)slot), tplace = (int8_t)gm8(0x533648 + (uint32_t)slot);
        uint32_t m = Gs_Mode(), lvl = Gs_Mode2(), rally = Tod_Rally(), stage = Tod_Stage();
        bool even_bonus = false;
        switch (m) {
        case 0:
        case 1: {
            if (m == 0 && rally == 7 && stage == 10) {
                if (pplace < 3) champ_medal(p, pplace);
                if (lvl == 1 && pplace == 0 && !(gm32(PROG) & 1)) gw32(PROG, gm32(PROG) | 1), flag(p, 1), dirty();
                if (lvl == 0 && pplace == 0) open_car(p, guest, 0x15);
            }
            /* 0x407150: the rally's stages */
            uint32_t n = (rally & 1) + (lvl == 0 ? 4u : lvl == 1 ? 8u : 10u);
            bool last = stage == n - 1 || stage == 10;
            if (last && tplace < 3) rally_medal(p, rally, tplace);
            if (!last || tplace > 5) break;
            if (lvl == 0) {
                if (m == 0) next_rally(p, 8, 2), odd_rally(p, 0, 0x10);
                break;
            }
            if (lvl == 2) {
                if (m == 0) next_rally(p, 0x10, 8), odd_rally(p, 10, 0x40);
            } else if (lvl == 1) {
                if (m == 0) next_rally(p, 0xc, 4), odd_rally(p, 5, 0x20);
            } else break;
            if (tplace != 0) break;
            if (rally & 1) {
                if (!guest) open_car(p, false, gm32(0x519198 + 4u * ((rally >> 1) + lvl * 4)));
                break;
            }
            even_bonus = true;
            break;
        }
        case 5: {
            uint32_t kind = gm32(0x52f2ac) >> 10 & 3, last = gm32(0x52f2ac) >> 0xc & 3;   /* 0x406940, 0x406950 */
            int aplace = (int8_t)gm8(0x533618 + (uint32_t)p);   /* 0x40ceb0 */
            if (last == 2 && aplace < 3) arcade_medal(p, kind, aplace);
            if (player_car(p) >= 0xc || last != 2 || aplace != 0) break;
            uint32_t v = gm32(PROG);
            if (kind == 0) {
                if (lvl == 1) {
                    if ((v & 0x30) == 0x10) put_bits(PROG, 0x30, v + 0x10), flag(p, 0x100), dirty();
                    open_car(p, guest, 5);
                } else if (lvl == 2) {
                    if ((v & 0xc0) == 0x40) put_bits(PROG, 0xc0, v + 0x40), flag(p, 0x200), dirty();
                    open_car(p, guest, 2);
                }
            } else if (lvl == 1) {
                if (!(gm32(PROG) & 2)) gw32(PROG, gm32(PROG) | 2), flag(p, 0x400), dirty();
                if (!(gm32(PROG) & 0x100000)) gw32(PROG, gm32(PROG) | 0x100000), flag(p, 0x800), dirty();
                open_car(p, guest, 6);
            } else if (lvl == 2) {
                open_car(p, guest, 3);
                /* the original marks this one without the settings' save */
                if (!(gm32(PROG) & 0x200000)) gw32(PROG, gm32(PROG) | 0x200000), flag(p, 0x1000);
            }
            break;
        }
        case 6:
            if (player_car(p) < 0xc && !guest && (gm32(0x52f2ac) >> 10 & 3) == 2 && !gm8(0x53e184 + (uint32_t)p)) {
                uint32_t c = (gm32(0x52f2ac) >> 0xc & 3) == 0 ? 0xd : 0xe;
                if (Player_HasDriven(p, 0xc, false)) open_car(p, false, c);
            }
            break;
        }
        if (even_bonus) even_rally_bonus(p);
        if (!guest && all_medals(p)) open_car(p, false, 1);
    }
    for (int p = 0; p < np; p++)
        if (gm32(FLAGS + 4u * (uint32_t)p) & 0x2000) {
            dirty();
            break;
        }
}
