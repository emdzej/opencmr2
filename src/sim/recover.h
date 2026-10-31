/* The recovery: a car flagged off the stage (+0xbf8: off the road too long, on its roof, a special
   wall) gets a fade out and in on its channel (0x4283e0); at the dark point (0x431c50) it is put back
   on the stage's centre line at the point it had reached, moved back past points another car is
   on, at rest, upright on the ground and in first gear. */
#pragma once
#include "sim/car.h"

void Car_RecoverCheck(Car *c);   /* 0x431c10 */
