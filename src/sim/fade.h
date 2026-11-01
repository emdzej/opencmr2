/* The fades (0x428410-0x4285b0): eight channels, one per car (channel 0 also the screen's), each a
   timer over a duration in steps (dt 0x519c8c a step) with a callback at its end. Modes: 1 runs up
   once; 3 runs up, calls back, then runs down from -duration to 0 (a fade out and in, the recovery);
   2 starts at -duration and calls back at once; 4 is the step after a run down ends. Level (0x53a06c)
   is the position as a fraction of the duration, 0 to 1, for the screen's darkening. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef void (*FadeFn)(int ch);

/* 0x428410: start (force: replace a running one, calling its callback first); with modes 0 and 2 the
   callback runs at once. a, b, c are kept for the callers (0x53a20c, 0x53a0cc: hold while paused, 0x53a00c). */
void Fade_Start(int ch, int32_t dur, FadeFn fn, int mode, int32_t a, int32_t b, int32_t c, bool force);
/* 0x4283e0: Fade_Start with 12.5 steps, mode 3 */
void Fade_OutIn(int ch, FadeFn fn, int32_t a, int32_t b, int32_t c, bool force);
bool Fade_Active(int ch);       /* 0x428740 */
bool Fade_Rising(int ch);       /* 0x428760: active and past 0 */
void Fade_Tick(int ch, bool paused);                  /* 0x4284d0 */
void Fade_Level(int ch, int32_t frac, bool paused);   /* 0x4285b0: between the last two steps */
int32_t Fade_LevelOf(int ch);   /* 0x53a06c */
int32_t Fade_Arg(int ch);       /* 0x53a20c */
int32_t Fade_Hold(int ch);      /* 0x53a0cc: held while paused */
int32_t Fade_Colour(int ch);    /* 0x53a00c: the overlay's colour (rgb) */
