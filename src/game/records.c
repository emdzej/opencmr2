/* The records (see records.h). */
#include "game/records.h"
#include "crt.h"
#include "game/gstate.h"
#include "game/hud.h"
#include "game/mem.h"
#include "game/profile.h"
#include "game/sectors.h"
#include "sim/car.h"
#include "world/tod.h"
#include <stdio.h>

enum { TABLES = 0x52b044, FLAGS = 0x817420 };

static uint32_t c_mode(void) { return gm32(0x8173f4); }
static uint32_t c_rally(void) { return gm32(0x8173f8); }
static uint32_t c_stage(void) { return gm32(0x8173fc); }
static uint32_t c_level(void) { return gm32(0x817400); }
static uint32_t c_loc(void) { return gm32(0x817404); }

/* 0x408c70: the profile's race block, 0 without a profile */
static uint32_t block(int p)
{
    uint32_t t = Player_Word(p) >> 0x12 & 0xf;
    return t == 0xf ? 0 : 0x52fa68 + t * 0x650u;
}

/* 0x4d0280: the stage time's slot (+0x14) in the modes that keep one (0-3, 8-10). Without a profile
   the original gives 0x14 and reads through it; the port gives none. */
static uint32_t time_slot(int p)
{
    uint32_t a = block(p);
    switch (c_mode()) {
    case 0: case 1: case 2: case 3: case 8: case 9: case 10: return a ? a + 0x14 : 0;
    default: return 0;
    }
}

/* 0x4d02d0: the rally's word (+0xc) in a championship, a single rally or mode 8 */
static uint32_t rally_slot(int p)
{
    uint32_t a = block(p), m = c_mode();
    return a && (m < 2 || m == 8) ? a + 0xc : 0;
}

static void put_bits(uint32_t va, uint32_t mask, uint32_t v) { gw32(va, (gm32(va) & ~mask) | (v & mask)); }
static void name_to(uint32_t va, const char *name) { snprintf((char *)gm(va), 4, "%s", name ? name : ""); }

/* the car and box into a table's word */
static void car_to(uint32_t e, int p)
{
    put_bits(e, 0x3f, Player_Word(p));
    put_bits(e, 0x40, Champ_CarFlag((uint8_t)p) ? 0x40 : 0);
}

void Rec_Context(void)
{
    gw32(0x8173f8, Tod_Rally());
    if (Gs_SelBit24()) {
        gw32(0x8173fc, gm32(0x52f2ac) >> 0xc & 3);   /* 0x406950 */
        gw32(0x817404, gm32(0x52f2ac) >> 10 & 3);    /* 0x406940 */
    } else
        gw32(0x8173fc, Tod_Stage());
}

/* (bit 23 of the selection) the record's splits, a circuit's lap's or the stage's; ten either way,
   so a lap's run on into the next record's */
void Rec_StageStart(void)
{
    if (gm32(0x52f2ac) >> 0x17 & 1) {   /* 0x407e40 */
        Rec_Context();
        for (uint32_t i = 0; i < 10; i++) {
            uint32_t at = Gs_SelBit24() ? TABLES + 0x1258 + (i + (c_stage() + c_loc() * 3) * 6) * 2
                                        : TABLES + 0x914 + (i + (c_stage() + c_rally() * 0xb) * 10) * 2;
            gw32(0x817448 + 4 * i, gm16(at));
        }
    }
    gw32(0x817570, 360000);
}

int32_t Rec_Split(int i) { return (int32_t)gm32(0x817448 + 4u * (uint32_t)i); }

uint32_t Rec_LapRecord(void) { return gm32(TABLES + 0x1214 + (c_stage() + c_loc() * 3) * 8) >> 7 & 0xffff; }

void Rec_SetTime(int p, int32_t t)
{
    Rec_Context();
    uint32_t s = time_slot(p);
    if (s) gw32(s, (uint32_t)t);
}

void Rec_RallyStart(int p)
{
    uint32_t r = rally_slot(p);
    if (r) put_bits(r, 0x3c00, 0);
}

void Rec_StageWin(int p, int place)
{
    uint32_t r = rally_slot(p);
    if (r && place == 0) put_bits(r, 0x3c00, gm32(r) + 0x400);
}

void Rec_ChampStart(int p)
{
    uint32_t a = block(p);
    if (a) gw32(a + 4, 0), gw16(a + 8, 0), gw8(a + 10, 0);
}

void Rec_ChampAdd(int p, int32_t t, int place)
{
    uint32_t a = block(p);
    if (!a) return;
    gw32(a + 4, gm32(a + 4) + (uint32_t)t);
    if (place < 3) gw8(a + 8 + (uint32_t)place, (uint8_t)(gm8(a + 8 + (uint32_t)place) + 1));
}

/* the place in bits 0-3 and its low two again in 8-9, the points' low four in 4-7 (as the original
   packs them) */
void Rec_SetRally(int p, uint32_t t, uint32_t place, uint32_t points)
{
    uint32_t r = rally_slot(p);
    if (!r) return;
    gw32(r + 4, t);
    put_bits(r, 0x3ff, (place & 0xf) | (points & 0xf) << 4 | (place & 3) << 8);
}

/* the place (0-3, its low bits again at 4-5) and points (6-13); the time cleared, so the bests and
   the table take 0 */
void Rec_SetChamp(int p, uint32_t place, uint32_t points)
{
    uint32_t a = block(p);
    if (!a) return;
    put_bits(a, 0x3fff, (place & 0xf) | (place & 3) << 4 | (points & 0xff) << 6);
    gw32(a + 4, 0);
}

/* the original's check of the slot (+0x20) can't fail; without a profile it writes at 0x20 */
void Rec_SetLap(int p, uint32_t t)
{
    Rec_Context();
    uint32_t a = block(p);
    if (a) gw32(a + 0x20, t);
}

void Rec_SetArcade(int p, uint32_t place, uint32_t points, uint32_t t)
{
    uint32_t a = block(p);
    if (!a) return;
    gw32(a + 0x1c, t);
    put_bits(a + 0x18, 0x7ff, (place & 7) | (place & 3) << 3 | (points & 0x3f) << 5);
}

bool Rec_BestStage(int p)
{
    uint32_t s = time_slot(p), b = Player_Records(p);
    if (!s || !b) return false;
    uint32_t e = b + 0x150 + (c_stage() + c_rally() * 0xc) * 8;
    if (!(gm32(s) < gm32(e + 4) || !(gm32(e) & 0x80))) return false;
    put_bits(e, 0x80, 0x80);
    put_bits(e, 0x1f00, (uint32_t)crt_rand() << 8);
    gw32(e + 4, gm32(s));   /* 0x4d0300 */
    car_to(e, p);
    Player_Touch(p);
    gw8(FLAGS + 10u * (uint32_t)p, 1);
    return true;
}

/* better: the place, then the time */
bool Rec_BestRally(int p)
{
    uint32_t r = rally_slot(p), b = Player_Records(p);
    if (!r || !b) return false;
    uint32_t e = b + (c_rally() * 3 + 4 + c_level()) * 0xc, rw = gm32(r), ew = gm32(e + 4);
    if (!((((rw ^ ew) & 0xf) == 0 && gm32(r + 4) < gm32(e + 8)) || (rw & 0xf) < (ew & 0xf) || !(gm32(e) & 0x80))) return false;
    put_bits(e, 0x80, 0x80);
    put_bits(e, 0x1f00, (uint32_t)crt_rand() << 8);
    gw32(e + 8, gm32(r + 4));   /* 0x4d0310 */
    put_bits(e + 4, 0x3fff, rw);
    car_to(e, p);
    Player_Touch(p);
    gw8(FLAGS + 1 + 10u * (uint32_t)p, 1);
    return true;
}

/* better: the place, then more points */
bool Rec_BestChamp(int p)
{
    uint32_t a = block(p), b = Player_Records(p);
    if (!a || !b) return false;
    uint32_t e = b + c_level() * 0x10, aw = gm32(a), ew = gm32(e + 4);
    if (!((((aw ^ ew) & 0xf) == 0 && (ew & 0x3fc0) < (aw & 0x3fc0)) || (aw & 0xf) < (ew & 0xf) || !(gm32(e) & 0x80))) return false;
    put_bits(e, 0x80, 0x80);
    put_bits(e, 0x1f00, (uint32_t)crt_rand() << 8);
    gw32(e + 8, gm32(a + 4));   /* 0x4d0370 */
    put_bits(e + 4, 0x3fff, aw);
    car_to(e, p);
    Player_Touch(p);
    gw8(FLAGS + 2 + 10u * (uint32_t)p, 1);
    return true;
}

/* (no new mark) */
bool Rec_BestLap(int p)
{
    uint32_t a = block(p), b = Player_Records(p);
    if (!a || !b) return false;
    uint32_t e = b + 0x4bc + (c_stage() + c_loc() * 3) * 8;
    if (!(gm32(a + 0x20) < gm32(e + 4) || !(gm32(e) & 0x80))) return false;
    put_bits(e, 0x80, 0x80);
    put_bits(e, 0x1f00, (uint32_t)crt_rand() << 8);
    gw32(e + 4, gm32(a + 0x20));
    car_to(e, p);
    Player_Touch(p);
    return true;
}

/* better: the place, then more points */
bool Rec_BestArcade(int p)
{
    uint32_t a = block(p), b = Player_Records(p);
    if (!a || !b) return false;
    uint32_t s = a + 0x18, e = b + (c_loc() * 3 + 0x5c + c_level()) * 0xc, sw = gm32(s), ew = gm32(e + 4);
    if (!((((sw ^ ew) & 7) == 0 && (ew & 0x7e0) < (sw & 0x7e0)) || (sw & 7) < (ew & 7) || !(gm32(e) & 0x80))) return false;
    put_bits(e, 0x80, 0x80);
    put_bits(e, 0x1f00, (uint32_t)crt_rand() << 8);
    gw32(e + 8, gm32(s + 4));   /* 0x4d03b0 */
    put_bits(e + 4, 0x7ff, sw);
    car_to(e, p);
    Player_Touch(p);
    gw8(FLAGS + 3 + 10u * (uint32_t)p, 1);
    return true;
}

bool Rec_SessionStage(int p)
{
    uint32_t s = time_slot(p);
    if (!s || gm32(s) >= gm32(0x817570)) return false;
    gw32(0x817570, gm32(s));
    return true;
}

bool Rec_SessionLap(int p)
{
    uint32_t a = block(p);
    if (!a || gm32(a + 0x20) >= gm32(0x817570)) return false;
    gw32(0x817570, gm32(a + 0x20));
    return true;
}

/* the stage's record when faster: the holder, the car, the box, the time in bits 7-22, the ten
   split times (at +0x914 and 0x817448; the original reads the tenth from the next player's row) */
bool Rec_StageRecord(int i, int p, const char *name)
{
    uint32_t s = time_slot(p), idx = c_stage() + c_rally() * 0xb, e = TABLES + 0x654 + idx * 8;
    if (!s || (gm32(e + 4) >> 7 & 0xffff) <= gm32(s)) return false;
    name_to(e, name);
    car_to(e + 4, p);
    put_bits(e + 4, 0x7fff80, (gm32(s) & 0xffff) << 7);
    for (int k = 0; k < 10; k++) {
        uint16_t sp = (uint16_t)Times_Split(i + k / 9, k % 9);   /* 0x448680 */
        gw16(TABLES + 0x914 + idx * 0x14 + 2u * (uint32_t)k, sp);
        gw32(0x817448 + 4u * (uint32_t)k, sp);
    }
    gw8(0x817410, 1);
    return true;
}

/* the lap's record when faster: the time, the lap's six splits (0x448240; at 0x817448 too, the time
   after the last split, 0x4583a0), the holder, car and box */
bool Rec_LapTable(int i, int p)
{
    uint32_t a = block(p), idx = c_stage() + c_loc() * 3, e = TABLES + 0x1210 + idx * 8;
    (void)Sectors_Lap(i);   /* 0x4582d0, its value unused */
    if (!a || gm32(a + 0x20) >= (gm32(e + 4) >> 7 & 0xffff)) return false;
    put_bits(e + 4, 0x7fff80, (gm32(a + 0x20) & 0xffff) << 7);
    for (uint32_t k = 0; k < 6; k++) {
        uint16_t sp = (uint16_t)gm32(0x53d990 + (k + (uint32_t)i * 0x22) * 4);
        gw16(TABLES + 0x1258 + idx * 0xc + 2 * k, sp);
        gw32(0x817448 + 4 * k, sp);
    }
    gw32(0x817448 + 4u * (uint32_t)Sectors_Count(), gm32(e + 4) >> 7 & 0xffff);
    name_to(e, Player_Name(p));
    car_to(e + 4, p);
    return true;
}

/* the top fives: the entries from k down one (the name, the word's fields, the rest) and the new one
   at k */
static void shift(uint32_t base, int k, uint32_t fields, int rest)
{
    for (int j = 4; j > k; j--) {
        uint32_t d = base + 12u * (uint32_t)j, s = d - 12;
        name_to(d, (const char *)gm(s));
        put_bits(d + 4, fields, gm32(s + 4));
        for (int b = 0; b < rest; b++) gw8(d + 8 + (uint32_t)b, gm8(s + 8 + (uint32_t)b));
    }
}

/* better: the place, then the time */
int Rec_RallyTable(int p, const char *name)
{
    uint32_t r = rally_slot(p);
    if (!r) return 0;
    uint32_t base = TABLES + 0xb4 + (c_level() + c_rally() * 3) * 0x3c, rw = gm32(r);
    for (int k = 0; k < 5; k++) {
        uint32_t e = base + 12u * (uint32_t)k, ep = gm32(e + 4) >> 7 & 0xf;
        if (!((rw & 0xf) < ep || (ep == (rw & 0xf) && gm32(r + 4) < gm32(e + 8)))) continue;
        shift(base, k, 0x7fff, 4);   /* 0x4d03f0 */
        name_to(e, name);
        car_to(e + 4, p);
        put_bits(e + 4, 0x7f80, (rw & 0xf) << 7 | (rw & 0x3c00) << 1);
        gw32(e + 8, gm32(r + 4));
        return k ? 2 : 1;
    }
    return 0;
}

/* better: the place, then (as the original compares them) fewer points */
int Rec_ChampTable(int p, const char *name)
{
    uint32_t a = block(p);
    if (!a) return 0;
    uint32_t base = TABLES + c_level() * 0x3c, aw = gm32(a);
    for (int k = 0; k < 5; k++) {
        uint32_t e = base + 12u * (uint32_t)k, ew = gm32(e + 4), ep = ew >> 0xe & 0xf;
        if (!((aw & 0xf) < ep || (ep == (aw & 0xf) && (aw & 0x3fc0) < (ew >> 1 & 0x1fc0)))) continue;
        shift(base, k, 0x3ffff, 3);   /* 0x4d0470 */
        name_to(e, name);
        car_to(e + 4, p);
        put_bits(e + 4, 0x3ff80, (aw & 0xf) << 0xe | (aw & 0x1fc0) << 1);
        for (uint32_t b = 0; b < 3; b++) gw8(e + 8 + b, gm8(a + 8 + b));
        gw8(0x817412, 1);
        return k ? 2 : 1;
    }
    return 0;
}

/* better: the place, then more points */
int Rec_ArcadeTable(int p, const char *name)
{
    uint32_t a = block(p);
    if (!a) return 0;
    uint32_t base = TABLES + 0xff4 + (c_level() + c_loc() * 3) * 0x3c, sw = gm32(a + 0x18);
    for (int k = 0; k < 5; k++) {
        uint32_t e = base + 12u * (uint32_t)k, ew = gm32(e + 4), ep = ew >> 7 & 7;
        if (!((sw & 7) < ep || (ep == (sw & 7) && (sw & 0x7e0) < (ew >> 5 & 0x7e0)))) continue;
        shift(base, k, 0xffff, 4);   /* 0x4d0500 */
        name_to(e, name);
        car_to(e + 4, p);
        put_bits(e + 4, 0xff80, (sw & 7) << 7 | (sw & 0x7e0) << 5);
        gw32(e + 8, gm32(a + 0x1c));
        gw8(0x817413, 1);
        return k ? 2 : 1;
    }
    return 0;
}

/* 0x408fc0: the profile's count of records (+0x22 of the profile, 0x52fa22) up */
static void counted(int p)
{
    uint32_t t = Player_Word(p) >> 0x12 & 0xf;
    if (t != 0xf) gw8(0x52fa22 + t * 0x650, (uint8_t)(gm8(0x52fa22 + t * 0x650) + 1));
}

static void game_counted(void)
{
    gw32(0x817418, gm32(0x817418) + 1);
    gw8(0x52ea51, 1);   /* 0x4057c0 */
}

/* each new mark one more record for its player's profile (and the player's own count), the tables'
   for the game's: the two masks' eleven and eight bits, then the championship and arcade bests */
void Rec_Tally(void)
{
    int n = (int)gm32(0x8173f0);
    for (int pass = 0; pass < 2; pass++) {
        int bits = pass ? 8 : 0xb;
        for (int i = 0; i < bits; i++) {
            for (int j = 0; j < n; j++) {
                uint32_t f = FLAGS + 10u * (uint32_t)j;
                if ((int16_t)gm16(f + 4 + 2u * (uint32_t)pass) >> i & 1) gw16(f + 8, (uint16_t)(gm16(f + 8) + 1)), counted(j);
            }
            if ((int16_t)gm16(0x817414 + 2u * (uint32_t)pass) >> i & 1) game_counted();
        }
    }
    for (int j = 0; j < n; j++) {
        uint32_t f = FLAGS + 10u * (uint32_t)j;
        if (gm8(f + 2)) gw16(f + 8, (uint16_t)(gm16(f + 8) + 1)), counted(j);
        if (gm8(f + 3)) gw16(f + 8, (uint16_t)(gm16(f + 8) + 1)), counted(j);
    }
    if (gm8(0x817412)) game_counted();
    if (gm8(0x817413)) game_counted();
}
