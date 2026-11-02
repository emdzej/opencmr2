/* The super special's fireworks (0x47e4d0-0x480600): up to 20 rockets launched from four pads beside
   the arena while the finish is shown, each rising with a sparkling trail, then bursting into 18 sparks
   mirrored four ways or into smaller rockets, with bangs, whistles and a flash of the stage light. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

bool Fireworks_On(void);             /* DAT_0058896c: the super special */
void Fireworks_Setup(int n);         /* 0x47e4d0 (0x466360) */
void Fireworks_Free(void);           /* 0x47ea20 */
void Fireworks_Step(void);           /* 0x47eab0 (Stage_Step 0x466520) */
void Fireworks_Light(void);          /* 0x480220 (Stage_Step): the flash in the low light colour */
void Fireworks_Launch(void);         /* 0x480380 (Race_S10_Replay): one rocket from a random pad */
void Fireworks_Interp(int32_t frac); /* 0x480600 (Race_PoseInterp) */
void Fireworks_Draw(void);           /* 0x47f740 (the cars' pass 0x466570) */
