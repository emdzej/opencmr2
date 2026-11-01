/* Option 0's balls (0x47d510-0x47e4d0): on the super special, or with selection bits 24 or 27, the
   handbrake fires a glowing ball ahead of the car (at most one every 16 seconds a car). A ball rolls on
   the ground at its launch velocity along the car's forward axis for up to 50 seconds, trailing sparks'
   glow particles, and knocks the first other car whose flat box it enters. */
#pragma once
#include "sim/car.h"
#include <stdbool.h>

bool Balls_On(void);                          /* the mode and option test of 0x466360/0x466520 */
void Balls_Setup(void);                       /* 0x47d510 (in 0x466360) */
void Balls_Fire(int car);                     /* 0x47d5a0 (Car_Boost 0x4932f0) */
void Balls_Step(void);                        /* 0x47dd70 (Stage_Step 0x466520) */
void Balls_Hit(Car *c, const int32_t *box);   /* 0x47d850 (Cars_Collide), box the car's flat box */
void Balls_Frame(int32_t frac);               /* 0x47e1e0 (Race_PoseInterp 0x41d060) */
/* The port, for tests: ball i in use, its position */
bool Balls_At(int i, int32_t pos[3]);
