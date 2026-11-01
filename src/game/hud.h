/* The race times (0x448100-0x448de0) and the HUD (Hud_Update 0x411f70 each step, Hud_Draw 0x4125f0
   in each player's view). Ported: the race clock and the finish times, the time panel (0x413fe0). */
#pragma once
#include <stdbool.h>
#include <stdint.h>

void Times_Clear(void);          /* 0x448100: at the go */
void Times_Step(void);           /* 0x448120's rally part: the clock (0x448de0), the finish times */
int32_t Times_Race(void);        /* 0x448110: the shown race time, 1/100 s */
int32_t Times_Finish(int car);   /* 0x4483c0 */
void Hud_LoadSounds(void);        /* 0x411120: up, down, blip, cross, timeout */
void Hud_Init(int again);         /* 0x411450: Stage_Load (0), the line (1), a restart */
void Hud_Update(int player, bool on_line);   /* 0x411f70 */
void Hud_Draw(int player);       /* 0x4125f0 */
int32_t Times_Split(int player, int k);   /* 0x448680: the race time at split k */
