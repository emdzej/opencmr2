/* The player profiles (see profile.h). */
#include "game/profile.h"
#include "game/gstate.h"
#include "game/mem.h"
#include "pending.h"

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
