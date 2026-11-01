/* The clocks (docs/re/architecture.md#timing): Timer_Sample 0x4a9b70 latches the millisecond clock
   once a frame (timeGetTime() in the original; the port's app gives the frame's time), Timer_Now
   0x4a9b60 reads it, Timer_Centis 0x4a9b80 is a monotonic clock in 1/100 s built on it. */
#pragma once
#include <stdint.h>

void Timer_Sample(uint32_t ms);   /* 0x4a9b70 */
uint32_t Timer_Now(void);         /* 0x4a9b60 */
uint32_t Timer_Centis(void);      /* 0x4a9b80 */
/* timeGetTime(): the port's millisecond clock, the app's frame time (set once a frame) */
void Time_Set(uint32_t ms);
uint32_t Time_Get(void);

/* The frame rate: Frame_Stats 0x4b21e0 (World_Render's last call; its counts over 3 s and each second
   are not kept) sets it to 1000 over the milliseconds since the last frame, infinite on the first;
   Frame_Fps 0x4b23a0 reads it (DAT_005210a0, 25 at the start). */
void Frame_Stats(void);
float Frame_Fps(void);
/* 0x466630: the rate (16.16, frames a second) into the step scale: 0x51bd40 = rate / 25, at least 0.6,
   and 0x51bd3c its inverse, the game's per-frame factor for what fades (Frame_Scale). Cars_Load sets
   25; Race_Update's 0x42b790 sets the frame rate every frame. */
void Frame_SetRate(int32_t rate);
int32_t Frame_Scale(void);
/* 0x42b790: Frame_SetRate from Frame_Fps, as fistp rounds it (0x80000000 for the first frame's
   infinity); its countdown of 0x52109c (set to 100 by 0x42b7e0, to 1 after six frames) has no reader. */
void Frame_RateStep(void);
