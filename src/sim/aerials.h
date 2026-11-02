/* The cars' whip aerials (section 3 of a car's .cin: a stop direction for the hatch, then a count at
   +0xc and 0x20-byte aerials from +0x10: the base and the axis in the car's frame, the length, an
   R, G, B, A colour). Each swings with the air past the car (its velocity less the gusting wind)
   on a spring, is drawn as five bending line segments (the 3D line queue, render/line3d.c) fading
   from 10 to 50 units, and once the car is upside down off its wheels lies on the ground for good
   (the damage record's +0x4c0 + 4k, carried between stages). Records: 0x590c6c, 0x3c bytes, three
   per car. */
#pragma once
#include "sim/sim.h"
#include <stdint.h>

void Aerials_Step(const CarList *l);           /* 0x484e00 (after the loose parts in Parts_Step) */
void Aerials_Reset(int car, int k, int32_t down);   /* 0x486630 (Damage_Reset) */
void Aerials_Interp(int32_t frac);             /* 0x486500 (Race_PoseInterp) */
/* 0x485690 (the cars' pass 0x466570, after the shadows): player's view from cam (16.16) */
void Aerials_Draw(const int16_t *cars, int n, int player, const int32_t cam[3]);
/* the port, for tests: car's aerial count, aerial k lying, its swing (x, z) */
int Aerials_Count(int car);
int Aerials_Down(int car, int k, int32_t swing[2]);
