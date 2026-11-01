/* The player profiles (see profile.h). */
#include "game/profile.h"
#include "game/gstate.h"
#include "game/mem.h"
#include "pending.h"
#include "game/timer.h"

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
    gw32(0x52fa00 + 4 * t, 1);
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
        gw32(0x52fa00 + 4u * (uint32_t)i, 0);
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
    gw32(0x52fa00 + 4 * t, 0);
}

static void copy_str(uint32_t to, uint32_t from)
{
    uint32_t i = 0;
    do gw8(to + i, gm8(from + i)); while (gm8(from + i++));
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
    PENDING_NOTE(0x406010, "Records_Defaults (0x52b044, 0x52c308, 0x52d5cc)");
    gw32(0x52afb4, gm32(0x52afb4) & 0x7fffffff);
}

uint32_t Player_Word(int n) { return gm32(0x531350 + 0x30u * (uint32_t)n); }
static void set_word(int n, uint32_t v) { gw32(0x531350 + 0x30u * (uint32_t)n, v); }
static uint32_t prof_word(uint32_t p, uint32_t off) { return gm32(0x52fa1c + p * 0x650 + off); }
static bool prof_used(uint32_t p) { return gm8(0x52fa18 + p * 0x650) && !(prof_word(p, 0) & 0x200000); }

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
    if ((gm32(0x52afb4) >> 3 & 0x7f) == 4) {
        for (int i = 0; i < 5; i++) gw32(0x52f490 + 0xc4u * (uint32_t)n + 4u * (uint32_t)i, v[i]);
        gw8(0x52f3ec + 0xc4u * (uint32_t)n, gm8(0x52f3ec + 0xc4u * (uint32_t)n) | 2);
        return;
    }
    uint32_t p = Player_Word(n) >> 0x12 & 0xf;
    if (p == 0xf) return;
    for (int i = 0; i < 5; i++) gw32(0x53003c + p * 0x650 + 4u * (uint32_t)i, v[i]);
    gw32(0x52fa00 + 4 * p, 1);
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
