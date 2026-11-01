/* The collision sounds (0x418780-0x418d20): 21 WAVs from the common archive's Sounds\collision\ (the
   name tables at 0x5188e4: SWIPE1-4 and COLLIS1-6, ROLL1-4 and LAND1-3, HLIGHT1-2, SCREEN1 and
   MIRROR), played on four voice slots that follow the camera's distance each step. A hit on the wheels
   plays a swipe or a crash by its strength, one off them a roll or a landing (from Car_Damage
   0x466ef0); the glass is for the lights, the windscreen and the mirror breaking (0x4692f0, not ported). */
#pragma once
#include <stdbool.h>
#include <stdint.h>

void Crash_LoadSounds(void);                    /* 0x4188c0 */
void Crash_StopSounds(void);                    /* 0x418780 */
void Crash_UpdateSounds(void);                  /* 0x418b00: the slots' volumes, the finished freed */
void Crash_Volume(int32_t vol);                 /* 0x418d20: the base volume, 16.16 */
void Crash_Hit(int car, int32_t v, int player);              /* 0x418ba0 */
void Crash_Roll(int car, int32_t v, bool land, int player);  /* 0x418c30 */
void Crash_Glass(int car, int kind, int player);             /* 0x418cd0 */
int32_t Crash_Voice(int slot);                  /* the port: slot's voice handle (0x5373b0), -1 none */
