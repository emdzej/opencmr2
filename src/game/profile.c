/* The player profiles (see profile.h). */
#include "game/profile.h"
#include "game/gstate.h"
#include "game/mem.h"
#include "pending.h"
#include "game/timer.h"
#include "world/tod.h"
#include "exe.h"
#include "text.h"
#include <stdio.h>
#include <string.h>

static uint32_t player_profile(int n) { return gm32(0x531350 + 0x30u * (uint32_t)n) >> 0x12 & 0xf; }

/* 0x4088a0: the chase camera's position, angle and distance (five ints) into the player's profile,
   and 0x408d00 marks the profile changed (0x52fa00) */
static void set_camera(int n, const uint32_t v[5])
{
    uint32_t t = player_profile(n);
    if (Gs_Mode() == 4) {
        for (uint32_t i = 0; i < 5; i++) gw32(0x52f490 + 0xc4u * (uint32_t)n + 4 * i, v[i]);
        gw8(0x52f3ec + 0xc4u * (uint32_t)n, (uint8_t)(gm8(0x52f3ec + 0xc4u * (uint32_t)n) | 2));
        return;
    }
    if (t == 0xf) return;
    for (uint32_t i = 0; i < 5; i++) gw32(0x53003c + t * 0x650 + 4 * i, v[i]);
    gw8(0x52fa00 + t, 1);
}

/* 0x4ec260: the chase camera's defaults: 1.5 up, 6.5 back, angle 0x2d, distance 14 (the same as the
   static initialiser 0x510570's) */
static void default_camera(int n)
{
    const uint32_t v[5] = {0, 0x18000, 0x68000, 0x2d, 0xe0000};
    set_camera(n, v);
}

void Profiles_Init(void)
{
    for (uint32_t a = 0x52fa08; a < 0x52fa08 + 0x650 * 4; a += 4) gw32(a, 0);
    for (uint32_t a = 0x531350; a < 0x531350 + 0xc0 * 4; a += 4) gw32(a, 0);
    gw32(0x531650, 0);
    for (int i = 0; i < 4; i++) {
        uint32_t p = 0x52fa1c + 0x650u * (uint32_t)i;
        gw32(p + 0x44, 4);   /* the camera: the chase */
        gw8(0x52fa00 + (uint32_t)i, 0);
        gw32(p, (gm32(p) & 0xffe1f17e) | 0x1017e);
        gw32(0x531654 + 4u * (uint32_t)i, 0);
        gw32(p + 0x40, gm32(p + 0x40) | 0x20);   /* the automatic box */
        gw32(p + 0x38, 0xf11);
        default_camera(i);   /* (every player has profile 0 here, so profile 0 gets it four times) */
    }
    for (int n = 0; n < 0x10; n++)   /* 0x4ebfd0: no profile, bit 13 clear */
        gw32(0x531350 + 0x30u * (uint32_t)n, (gm32(0x531350 + 0x30u * (uint32_t)n) & 0xffffdfff) | 0x3c0000);
    gw32(0x531764, 0);
}

void Player_SetProfile(int player, int profile)
{
    uint32_t a = 0x531350 + 0x30u * (uint32_t)player;
    gw32(a, (gm32(a) & ~(0xfu << 0x12)) | ((uint32_t)profile & 0xf) << 0x12);
}

void Profile_Create(int player)
{
    uint32_t t = player_profile(player), base = 0x52fa08 + t * 0x650;
    for (uint32_t a = base; a < base + 0x650; a += 4) gw32(a, 0);
    uint32_t p = 0x52fa1c + t * 0x650;
    gw32(p + 0x44, 4);
    gw32(p, (gm32(p) & 0xffe1f17e) | 0x1017e);
    gw32(p + 0x40, gm32(p + 0x40) | 0x20);
    gw32(p + 0x38, 0xf11);
    gw8(0x52ff90 + t * 0x650, (uint8_t)((gm8(0x52ff90 + t * 0x650) & 0xfc) | 0x3c));   /* the HUD panels */
    default_camera(player);
    gw8(0x52fa00 + t, 0);
}

static void copy_str(uint32_t to, uint32_t from)
{
    uint32_t i = 0;
    do gw8(to + i, gm8(from + i)); while (gm8(from + i++));
}

/* 0x406010: a records block (0x12c4 bytes) to its defaults, every holder "cmr" (0x516184): the 15
   entries at the start; for each rally 15 entries (6:00.00 and flags 0x780), the 11 stages' records
   (8:00.00; the super special's, 4:00.00, is written over the next table's first word, as the
   original does) and their ten splits (one or two minutes apart); 45 entries from +0xff8; nine from
   +0x1210 with six splits each */
static void rec_name(uint32_t a) { copy_str(a, 0x516184); }
void Records_Defaults(uint32_t p)
{
    uint32_t e = p + 4;
    for (int i = 0; i < 15; i++, e += 12) {
        rec_name(e - 4);
        gw32(e, (gm32(e) & 0xffffc000u) | 0x3c000);
        gw16(e + 4, 0);
        gw8(e + 6, 0);
    }
    e = p + 0xb8;
    uint32_t splits10 = p + 0x9dc, ss = p + 0x6a8;
    for (uint32_t R = 0xcb; R < 0x123; R += 0xb, splits10 += 0xdc, ss += 0x58) {
        for (int i = 0; i < 15; i++, e += 12) {
            rec_name(e - 4);
            gw32(e, (gm32(e) & 0xffff8780u) | 0x780);
            gw32(e + 4, 360000);
        }
        for (uint32_t s = 0; s < 0xb; s++) {
            uint32_t a = p + (s + R) * 8;
            rec_name(a - 4);
            uint32_t u = gm32(a);
            gw32(a, u & ~0x7fu);
            if (s == 10) gw32(ss, (u & 0xffaee000u) | 0x2ee000);
            else gw32(a, (u & 0xffddc000u) | 0x5dc000);
        }
        for (uint32_t s = 0; s < 0xb; s++)
            for (uint32_t i = 0; i < 10; i++) {
                if (s == 10) gw16(splits10 + 2 * i, (uint16_t)(i * 12000));
                else gw16(p - 0x6c8 + (s + R) * 0x14 + 2 * i, (uint16_t)(i * 6000));
            }
    }
    e = p + 0xff8;
    for (int i = 0; i < 45; i++, e += 12) {
        rec_name(e - 4);
        gw32(e, (gm32(e) & 0xffff0280u) | 0x280);
    }
    e = p + 0x1214;
    uint32_t sp = p + 0x1258;
    for (int i = 0; i < 9; i++, e += 8) {
        rec_name(e - 4);
        gw32(e, (gm32(e) & 0xffaee000u) | 0x2ee000);
        for (uint32_t k = 0; k < 6; k++, sp += 2) gw16(sp, (uint16_t)(k * 6000));
    }
}

void Options_Defaults(void)
{
    for (uint32_t a = 0x52afa0; a < 0x52afa0 + 0xe67 * 4; a += 4) gw32(a, 0);
    copy_str(0x52e8f0, 0x52ea5c);   /* the player's default name (empty until the registry gives one) */
    gw32(0x52afb0, 0x11);
    copy_str(0x52e905, 0x52ea5c);
    gw32(0x52e920, 8);
    gw32(0x52e930, 8);
    gw32(0x52b03c, (gm32(0x52b03c) & 0xffc11157) | 0x11154);
    gw32(0x52afb8, (gm32(0x52afb8) & 0xfcffffff) | 0xbc000000);
    gw32(0x52afbc, (gm32(0x52afbc) & 0xfff33d19) | 0x33518);
    gw32(0x52afb4, (gm32(0x52afb4) & 0x807627ff) | 0x42400);
    gw32(0x52e91c, 10);
    gw32(0x52e924, 6);
    gw32(0x52e928, 3);
    gw32(0x52e92c, 0x1e);
    /* 0x510570: the chase camera's defaults */
    gw32(0x52b028, 0x18000);
    gw32(0x52b02c, 0x68000);
    gw16(0x52b034, 0x2d);
    gw32(0x52b030, 0xe0000);
    gw32(0x52b038, 4);
    gw32(0x52afb8, (gm32(0x52afb8) & 0xfff93264) | 0x40393264);   /* the volumes: effects 100, ... */
    Records_Defaults(0x52b044);
    Records_Defaults(0x52c308);
    Records_Defaults(0x52d5cc);
    gw32(0x52afb4, gm32(0x52afb4) & 0x7fffffff);
}

uint32_t Player_Word(int n) { return gm32(0x531350 + 0x30u * (uint32_t)n); }
static void set_word(int n, uint32_t v) { gw32(0x531350 + 0x30u * (uint32_t)n, v); }
static uint32_t prof_word(uint32_t p, uint32_t off) { return gm32(0x52fa1c + p * 0x650 + off); }
static bool prof_used(uint32_t p) { return gm8(0x52fa18 + p * 0x650) && !(prof_word(p, 0) & 0x200000); }

int Player_Driver(int n)
{
    uint32_t w = Player_Word(n);
    if (Gs_Mode() == 4) {
        if (strcmp((const char *)gm(0x52f3e0 + 0xc4u * (uint8_t)n), (const char *)gm(0x52ea5c)) && (uint8_t)n < 8) return -1;
    } else if (w & 0x2000)
        return -1;
    return (int)(w >> 0xe & 0xf);
}

static bool arcade(void) { return Gs_Mode() == 5 || Gs_Mode() == 6 || Gs_Mode() == 7; }

/* 0x407f80: an arcade slot (0-5) moved by the level: the first from the first, the second from the
   sixth, else from the eleventh */
static int level_slot(int i)
{
    if (i < 0 || i > 5) i = 0;
    return Gs_Mode2() == 2 ? i : Gs_Mode2() == 1 ? i + 5 : i + 10;
}

const char *Cpu_Name(int car)
{
    if (arcade()) return exe_str(0x5166b4 + 0x14u * (uint32_t)level_slot(car - (int)Gs_SelBits14()));
    return exe_str(0x5166b4 + 0x14u * (uint32_t)(car < 0 || car > 15 ? 0 : car));
}

/* the arcade's slot is not moved past the players here (unlike 0x407ec0) */
const char *Cpu_Tag(int car)
{
    if (arcade()) return exe_str(0x5167e0 + 4u * (uint32_t)level_slot(car));
    return exe_str(0x5167e0 + 4u * (uint32_t)(car < 0 || car > 15 ? 0 : car));
}

const char *Player_Name(int n)
{
    if (Gs_Mode() == 4) return (const char *)gm(0x52f3e0 + 0xc4u * (uint8_t)n);   /* the challenge's record */
    uint32_t k = gm32(0x531350 + 0x30u * (uint32_t)n) >> 0x12 & 0xf;
    return k == 0xf ? "" : (const char *)gm(0x52fa18 + k * 0x650);
}

/* 0x408cb0: the profile's records (+0x84, 0x52fa8c), 0 without a profile */
uint32_t Player_Records(int n)
{
    uint32_t w = Player_Word(n);
    return (w & 0x3c0000) == 0x3c0000 ? 0 : 0x52fa8c + (w >> 0x12 & 0xf) * 0x650;
}

/* 0x408470: the profile's full name (0x52fa24), NULL without one */
char *Player_FullName(int n)
{
    uint32_t k = player_profile(n);
    return k == 0xf ? NULL : (char *)gm(0x52fa24 + k * 0x650);
}

/* 0x4eaf90: the full name set (no limit: the callers keep it to 0x2b) and the profile marked changed */
void Player_SetFullName(int n, const char *s)
{
    uint32_t k = player_profile(n);
    char *d = (char *)gm(0x52fa24 + k * 0x650);
    memmove(d, s, strlen(s) + 1);
    gw8(0x52fa00 + k, 1);
}

/* 0x408b10: the birth date from the profile's word (day bits 16-20, month 8-11, year 0-7, from
   1850); 0x45 for each without a profile */
void Player_BirthDate(int n, int *day, int *month, int *year)
{
    uint32_t k = player_profile(n);
    uint32_t w = k == 0xf ? 0 : gm32(0x52fa1c + k * 0x650);
    if (day) *day = k == 0xf ? 0x45 : (int)(w >> 0x10 & 0x1f);
    if (month) *month = k == 0xf ? 0x45 : (int)(w >> 8 & 0xf);
    if (year) *year = k == 0xf ? 0x45 : (int)(w & 0xff);
}

/* 0x4ebe10: the birth date into the word (not marked changed) */
void Player_SetBirthDate(int n, int day, int month, int year)
{
    uint32_t a = 0x52fa1c + player_profile(n) * 0x650;
    uint32_t w = gm32(a) & 0xffe0f000;
    gw32(a, w | ((uint32_t)day & 0x1f) << 0x10 | ((uint32_t)month & 0xf) << 8 | ((uint32_t)year & 0xff));
}

/* 0x4eb290: the full name against the twenty codes (pointers at 0x5250fc, each lowered with
   0x4aa090 first: the original does it in place); a match toggles that cheat option (0x4eac50) and
   tells whether it is now on */
bool Cheat_Match(int n, bool *on)
{
    const char *name = Player_FullName(n);
    for (uint32_t i = 0; i < 20; i++) {
        char code[64];
        snprintf(code, sizeof code, "%s", exe_str(exe_u32(0x5250fc + 4 * i)));
        Text_Lower(code);
        if (name && strcmp(code, name) == 0) {   /* 0x4eb200 */
            uint32_t bit = 1u << i;
            bool was = gm32(0x52e890) & bit;
            gw32(0x52e890, gm32(0x52e890) ^ bit);
            *on = !was;
            return true;
        }
    }
    return false;
}

bool Player_HasProfile(int n)
{
    if ((Player_Word(n) & 0x3c0000) == 0x3c0000) return false;
    if ((gm32(0x52afb4) >> 3 & 0x7f) == 4) return true;
    return Player_Word(n) >> 0x1a & 1;
}

void Player_AssignProfile(int n, int profile)
{
    uint32_t w = Player_Word(n);
    if ((w & 0x3c0000) != 0x3c0000) return;
    if (profile != -1) {
        uint32_t p = (uint32_t)profile, f = prof_word(p, 0x40);
        set_word(n, ((f & 0x40) << 1 | (p & 0xf)) << 0x12 | (w & 0xfdc3ffc0) | (f & 0x1f));
        return;
    }
    /* the first profile not in use, unless a lower player has it */
    uint32_t pick = 0;
    while (pick < 4 && prof_used(pick)) pick++;
    bool found = pick < 4;
    for (int i = 0; found && i < n; i++)
        if ((Player_Word(i) >> 0x12 & 0xf) == pick) found = false;
    if (!found) {
        /* else of those no player has, the lowest +0x40 bits 7-14; all equal, the longest unused */
        uint32_t cand[4], nc = 0;
        for (uint32_t p = 0; p < 4; p++) {
            bool taken = false;
            for (int i = 0; i < 0x10; i++)
                if ((Player_Word(i) >> 0x12 & 0xf) == p) taken = true;
            if (!taken) cand[nc++] = p;
        }
        uint32_t low = 0xff;
        bool same = true;
        pick = (uint32_t)n;
        for (uint32_t k = 0; k < nc; k++) {
            uint32_t v = prof_word(cand[k], 0x40) >> 7 & 0xff;
            if ((prof_word(cand[0], 0x40) >> 7 & 0xff) != v) same = false;
            if (v <= low) low = v, pick = cand[k];
        }
        uint32_t now = Timer_Centis(), age = 0;
        if (same)
            for (uint32_t k = 0; k < nc; k++)
                if (age <= now - gm32(0x531654 + 4 * cand[k])) pick = cand[k], age = now - gm32(0x531654 + 4 * cand[k]);
    }
    set_word(n, (pick & 0xf) << 0x12 | (Player_Word(n) & 0xffc3ffff));
    gw32(0x531654 + 4 * (Player_Word(n) >> 0x12 & 0xf), Timer_Centis());
}

void Player_DefaultCamera(int n)
{
    /* 0x4eaae0 the options' camera defaults (0x52b028-0x52b034), 0x4088a0 into the profile (or in
       mode 4 the challenge record 0x52f490) and 0x408d00 its changed flag */
    uint32_t v[5] = {0, gm32(0x52b028), gm32(0x52b02c), gm16(0x52b034), gm32(0x52b030)};
    Player_SetCamera(n, v);
}

/* 0x408930: the player's chase camera record (x, y, z, the angle, the distance), 0 without */
uint32_t Player_Camera(int n)
{
    if ((gm32(0x52afb4) >> 3 & 0x7f) == 4) return 0x52f490 + 0xc4u * (uint32_t)n;
    uint32_t p = Player_Word(n) >> 0x12 & 0xf;
    return p == 0xf ? 0 : 0x53003c + p * 0x650;
}

/* 0x4088a0 */
void Player_SetCamera(int n, const uint32_t v[5])
{
    uint32_t a = Player_Camera(n);
    if (!a) return;
    for (int i = 0; i < 5; i++) gw32(a + 4u * (uint32_t)i, v[i]);
    Player_Touch(n);   /* (0x408d00's two flags) */
}

/* 0x408760: the camera kind (+0x58 of the profile, the challenge record's +4); with the profile's
   bit 21 (the challenge always) the options' too (0x405fd0) */
void Player_SetCameraKind(int n, uint32_t kind)
{
    if ((gm32(0x52afb4) >> 3 & 0x7f) == 4) {
        gw32(0x52b038, kind);
        gw32(0x52f3e4 + 0xc4u * (uint32_t)n, kind);
        gw8(0x52f3ec + 0xc4u * (uint32_t)n, gm8(0x52f3ec + 0xc4u * (uint32_t)n) | 2);
        return;
    }
    uint32_t p = Player_Word(n) >> 0x12 & 0xf;
    if (p == 0xf) return;
    if (gm32(0x52fa1c + p * 0x650) & 0x200000) gw32(0x52b038, kind);
    gw32(0x52fa60 + p * 0x650, kind);
    gw8(0x52fa00 + p, 1);
}

/* 0x408d00: player n's record marked changed (mode 4: its challenge record's flag) */
void Player_Touch(int n)
{
    if (Gs_Mode() == 4) {
        gw8(0x52f3ec + 0xc4u * (uint32_t)n, gm8(0x52f3ec + 0xc4u * (uint32_t)n) | 2);
        return;
    }
    uint32_t p = Player_Word(n) >> 0x12 & 0xf;
    if (p != 0xf) gw8(0x52fa00 + p, 1);
}

/* 0x408a60: player n's tyre wear record (its profile's +0x57c, mode 4's challenge record +4); none on
   a circuit (0x52afb4 bit 19), in modes 5-7 or on stage 10 for a player past the race's */
uint32_t Player_Tyres(int n)
{
    uint32_t m = Gs_Mode();
    if ((Gs_ModeBit19() || m == 5 || m == 6 || m == 7 || Tod_Stage() == 10) && (int)(Gs_Players() - 1) < n) return 0;
    if (m == 4) return 0x52f3f0 + 0xc4u * (uint32_t)n;
    uint32_t p = Player_Word(n) >> 0x12 & 0xf;
    return p == 0xf ? 0 : 0x52ff94 + p * 0x650;
}

/* 0x45c750: wheel w's tally of spin (a) and slide (b) added to its counts (+0x80, +0x60, each to
   1,000) and the most kept (+0x20, +0x00); the wheel's mark (+0x90) cleared, then each set from its
   counts (3 a slide over 50, 1 a spin over 50, 2 over 300) */
void Tyres_Tally(int n, int w, int a, int b)
{
    uint32_t r = Player_Tyres(Gs_Byte537ef6() + n);
    if (!r) return;
    uint32_t o = 4u * (uint32_t)w;
    if (gms32(r + 0x80 + o) < 1000) gw32(r + 0x80 + o, (uint32_t)(gms32(r + 0x80 + o) + a));
    if (gms32(r + 0x60 + o) < 1000) gw32(r + 0x60 + o, (uint32_t)(gms32(r + 0x60 + o) + b));
    if (gms32(r + 0x20 + o) < gms32(r + 0x80 + o)) gw32(r + 0x20 + o, gm32(r + 0x80 + o));
    if (gms32(r + o) < gms32(r + 0x60 + o)) gw32(r + o, gm32(r + 0x60 + o));
    gw32(r + 0x90 + o, 0);
    for (uint32_t i = 0; i < 4; i++) {
        if (0x32 < gms32(r + 0x80 + 4 * i)) gw32(r + 0x90 + 4 * i, 3);
        if (0x32 < gms32(r + 0x60 + 4 * i)) gw32(r + 0x90 + 4 * i, 1);
        if (300 < gms32(r + 0x60 + 4 * i)) gw32(r + 0x90 + 4 * i, 2);
    }
    Player_Touch(Gs_Byte537ef6() + n);
}

/* 0x45c610: each player's wheels' wear at the start: unless again or kept, the counts taken from the
   last (+0x40 into +0x50, the larger of +0x00 and +0x10 into +0x30, +0x40 cleared), then copied up
   (+0x50 to +0x80, +0x40 to +0x60, +0x30 to +0x70) and tallied with nothing (the marks) */
void Tyres_Start(bool again, bool keep, int players)
{
    for (int n = 0; n < players; n++) {
        uint32_t r = Player_Tyres(Gs_Byte537ef6() + n);
        if (!r) continue;
        for (int w = 0; w < 4; w++) {
            uint32_t a = r + 0x50 + 4u * (uint32_t)w;
            if (!again && !keep) {
                gw32(a, gm32(a - 0x30));
                gw32(a - 0x20, gms32(a - 0x40) < gms32(a - 0x50) ? gm32(a - 0x50) : gm32(a - 0x40));
                gw32(a - 0x10, 0);
            }
            gw32(a + 0x30, gm32(a));
            gw32(a + 0x10, gm32(a - 0x10));
            gw32(a + 0x20, gm32(a - 0x20));
            Player_Touch(Gs_Byte537ef6() + n);
            Tyres_Tally(n, w, 0, 0);
        }
    }
}

void Player_SetGuest(int n, bool guest)
{
    uint32_t w = Player_Word(n), p = w >> 0x12 & 0xf;
    if (p == 0xf) return;
    uint32_t g = (uint32_t)guest << 0x1a;
    gw32(0x52fa1c + p * 0x650, (prof_word(p, 0) & 0xffdfffff) | (uint32_t)guest << 0x15);
    set_word(n, (w & 0xfbffffff) | g | 0x2000);
    if (guest) {
        set_word(n, (w & 0xfbffe000) | g | 0x2000);
        gw32(0x531354 + 0x30u * (uint32_t)n, 0);
        Player_DefaultCamera(n);
        /* the camera: the option's (0x4eaca0) into the profile of the player's number and its own,
           as the original indexes both */
        gw32(0x52fa60 + 0x650u * (uint32_t)n, gm32(0x52b038));
        gw32(0x52fa60 + 0x650u * p, gm32(0x52b038));
    }
}

void Player_DropProfile(int n)
{
    if ((Player_Word(n) & 0x3c0000) != 0x3c0000) set_word(n, (Player_Word(n) & 0xffffdfff) | 0x3c0000);
}

void Players_DropAll(void)
{
    for (int n = 0; n < 0x10; n++) set_word(n, (Player_Word(n) & 0xffffdfff) | 0x3c0000);
}

/* the profiles in use that no player (of the first four) has */
static int free_profiles(uint32_t out[4])
{
    bool had[4] = {0};
    for (uint32_t p = 0; p < 4; p++)
        for (int n = 0; n < 4; n++)
            if ((Player_Word(n) >> 0x12 & 0xf) == p) { had[p] = true; break; }
    int k = 0;
    for (uint32_t p = 0; p < 4; p++)
        if (!had[p] && prof_used(p)) out[k++] = p;
    return k;
}

int Profiles_Free(void) { uint32_t f[4]; return free_profiles(f); }
const char *Profile_FreeName(int i)
{
    uint32_t f[4];
    return i < free_profiles(f) ? (const char *)gm(0x52fa18 + f[i] * 0x650) : NULL;
}
int Profile_FreeIndex(int i)
{
    uint32_t f[4];
    return i < free_profiles(f) ? (int)f[i] : -1;
}

/* 0x4eae40: a challenge's eight player records (0x52f3e0, 0xc4 bytes each) cleared: +0xc bit 0 set (an
   automatic box), bit 1 cleared; +4 the options' word 0x52b038 (0x4eaca0); the chase defaults
   (0x4ec260, which sets bit 1 again); +0xd bits 2-5 set, bits 0-1 cleared */
void Challenge_ResetPlayers(void)
{
    for (uint32_t a = 0x52f3e0; a < 0x52f3e0 + 0x188 * 4; a += 4) gw32(a, 0);
    int n = 0;
    for (uint32_t a = 0x52f3ec; a < 0x52fa0c; a += 0xc4, n++) {
        gw8(a, (uint8_t)((gm8(a) & 0xfd) | 1));
        gw32(a - 8, gm32(0x52b038));
        default_camera(n);
        gw8(a + 1, (uint8_t)((gm8(a + 1) & 0xfc) | 0x3c));
    }
}
