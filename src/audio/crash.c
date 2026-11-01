/* The collision sounds (see crash.h). */
#include "audio/crash.h"
#include "audio/engine.h"
#include "audio/snd.h"
#include "game/common.h"
#include "game/gstate.h"
#include "game/mem.h"
#include "game/paths.h"
#include "game/timer.h"
#include "sim/car.h"
#include "crt.h"
#include "exe.h"
#include "fixmath.h"
#include <stdio.h>

static int hit_base, roll_base, glass_base;   /* 0x537360, 0x5373ac, 0x5373a8: the tables' first ids */
static int32_t volume;                         /* 0x537394 */
/* the four slots: the voice, its player (the camera), when it started, its volume, its car */
static int32_t slot_h[4] = {-1, -1, -1, -1};   /* 0x5373b0 */
static int32_t slot_player[4];                 /* 0x537364 */
static uint32_t slot_t[4];                     /* 0x537374 */
static int32_t slot_vol[4];                    /* 0x537384 */
static int32_t slot_car[4];                    /* 0x537398 */

/* the voice slots are looked at for the first (players) of them only, in the stop and the update */
static int players(void) { return (int)Gs_SelBits14(); }   /* 0x4074f0 */

/* each name "<sounds>\collision\<name>" (0x518a34), loaded whether or not the last one was (so a
   missing sound moves the ids after it, as in the original) */
static void load_table(uint32_t from, uint32_t to)
{
    for (uint32_t a = from; a < to; a += 4) {
        char p[260];
        snprintf(p, sizeof p, "%s%s%s", Path_Sounds(), exe_str(0x518a34), exe_str(gm32(a)));
        Snd_LoadNamed(p, Bfl_CommonArc());
    }
}

void Crash_LoadSounds(void)
{
    hit_base = Snd_Count();
    load_table(0x5188e4, 0x51890c);
    roll_base = Snd_Count();
    load_table(0x51890c, 0x518928);
    glass_base = Snd_Count();
    load_table(0x518928, 0x518938);
    for (int i = 0; i < 4; i++) slot_h[i] = -1;
}

void Crash_StopSounds(void)
{
    for (int i = 0; i < players(); i++)
        if (Voice_Playing(slot_h[i])) Voice_Stop(slot_h[i]), slot_h[i] = -1;
}

static int32_t level(int i) { return fx_mul(Engine_DistanceFade(slot_car[i], slot_player[i]), fx_mul(volume, slot_vol[i])); }

void Crash_UpdateSounds(void)
{
    for (int i = 0; i < players(); i++) {
        if (slot_h[i] == -1) continue;
        if (!Voice_Playing(slot_h[i])) slot_h[i] = -1;
        else Voice_SetVolume(slot_h[i], level(i));
    }
}

void Crash_Volume(int32_t vol) { volume = vol; }

/* 0x4187d0: the first free slot, else the one started longest ago (the first of equals), its voice
   stopped; the sound at 44,100 Hz */
static void play(int car, int id, int32_t vol, int player)
{
    uint32_t now = Timer_Centis();
    int s = -1;
    for (int i = 0; i < 4 && s < 0; i++)
        if (slot_h[i] == -1) s = i;
    if (s < 0) {
        uint32_t age = 0;
        s = 0;
        for (int i = 0; i < 4; i++)
            if (age < now - slot_t[i]) s = i, age = now - slot_t[i];
    }
    if (slot_h[s] != -1 && Voice_Playing(slot_h[s])) Voice_Stop(slot_h[s]);
    slot_car[s] = car, slot_player[s] = player, slot_vol[s] = vol;
    slot_h[s] = Voice_Play(id, level(s), 0xac44, 0, false, false);
    slot_t[s] = now;
}

/* the car's +0xb46 raised to 1 + 7v, at most 8 */
static void shake(int car, int32_t v)
{
    int32_t n = (fx_mul(v, 0x70000) >> 16) + 1;
    if (8 < n) n = 8;
    Car *c = Car_Get(car);
    if (c->s[0xb46] < n) c->s[0xb46] = (uint8_t)n;
}

/* above 0.05: SWIPE1-4 and COLLIS1-6 by tenths of v */
void Crash_Hit(int car, int32_t v, int player)
{
    if (v <= 0xccc) return;
    int32_t k = fx_mul(v, 0xa0000) >> 16;
    if (9 < k) k = 9;
    play(car, hit_base + k, 0x10000, player);
    shake(car, v);
}

/* ROLL1-4 or LAND1-3 at random, at volume v */
void Crash_Roll(int car, int32_t v, bool land, int player)
{
    int id = land ? crt_rand() % 3 + 4 + roll_base : crt_rand() % 4 + roll_base;
    play(car, id, v, player);
    shake(car, v);
}

/* kind 0: HLIGHT1 or 2 at random; 2: SCREEN1; else HLIGHT1 */
void Crash_Glass(int car, int kind, int player)
{
    int k = kind == 0 ? crt_rand() % 2 : kind == 2 ? 2 : 0;
    play(car, glass_base + k, 0x10000, player);
}

int32_t Crash_Voice(int slot) { return slot_h[slot]; }
