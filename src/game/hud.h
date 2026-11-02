/* The race times (0x448100-0x448de0) and the HUD (Hud_Update 0x411f70 each step, Hud_Draw 0x4125f0
   in each player's view). Ported: the race clock and the finish times, the time panel (0x413fe0). */
#pragma once
#include <stdbool.h>
#include <stdint.h>

void Times_Clear(void);          /* 0x448100: at the go */
void Times_Step(void);           /* 0x448120: the clock (0x448de0), the splits, laps, places, finishes */
void Times_Reset(void);          /* 0x447f70 (the line, the restart): the finishes, the arcade's records */
int Times_Place(int car);        /* 0x4481c0: the car's place (0 the leader) */
int Times_Standing(int i);       /* 0x4481e0: the car in place i */
int32_t Times_Race(void);        /* 0x448110: the shown race time, 1/100 s */
int32_t Times_Finish(int car);   /* 0x4483c0 */
void Times_Won(int car);         /* 0x448630 */
void Hud_LoadSounds(void);        /* 0x411120: up, down, blip, cross, timeout */
void Hud_LoadMap(void);           /* 0x411280: the map's textures (Stage_Load) */
void Hud_Init(int again);         /* 0x411450: Stage_Load (0), the line (1), a restart */
void Hud_Update(int player, bool on_line);   /* 0x411f70 */
void Hud_Draw(int player);       /* 0x4125f0 */
bool Hud_SplitScreen(void);       /* 0x411880: two players, the screen split */
uint8_t Hud_Flags(int n);         /* Profile_HudFlags 0x408a00: profile slot n's display flags (0 without) */
/* 0x417e70: a message in the big font, centred in player p's view (or at x, y unless -1), with a
   shadow when asked; one a view */
void Hud_Message(const char *text, const uint8_t *rgba, int p, bool shadow, int x, int y);
void Hud_MessageReset(void);     /* 0x417e60 (each view, 0x40f0c0): the next message is drawn */
int32_t Times_Split(int player, int k);   /* 0x448680: the race time at split k */
const int32_t *Hud_FinishTimes(void);   /* 0x53d1b8: each car's finish time (16.16 s) */
