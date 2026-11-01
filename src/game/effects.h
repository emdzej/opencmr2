/* The race's particle effects (0x459c80-0x45a170, docs/re/world.md#particles): their textures and
   their types. The emitters (dust and gravel from the wheels 0x45af90 / 0x45b580, exhaust smoke
   0x45c820, splashes 0x45dc00, 0x47dd70, sparks 0x499750, paint and glass chips 0x499de0 / 0x49b3e0)
   and the types' callbacks are not ported yet. */
#pragma once
#include "render/tex.h"
#include <stdint.h>

void Effects_LoadTextures(void);    /* 0x459c80 (in 0x40f020) */
/* 0x45a170 (the end of 0x466360): the particles in use by the effects detail (bits 25-26 of 0x52afd0:
   0 400, 1 100, 2 none) and the 32 types */
void Effects_Types(void);
/* 0x49af20: the thrown stones' record at 0x593860 for a height and a time; returns their gravity */
int32_t Stones_Setup(int32_t height, int32_t time);
Tex *Effects_Tex(uint32_t va);      /* the port: the texture the original keeps at va (NULL: none) */
/* 0x45def0, each frame for each listed car: the wheels' ground points, the marks' runs, the touches
   and the shades (tables at 0x543120-0x543b58 in the game state) */
void Wheels_Track(int car);
/* 0x45af00, each frame after it, not in some modes: the car's light and its wheels' effects */
void Effects_Car(int car);
/* Wheel_Slip 0x465d70: wheel w's slip as 0..1 (0 for cars from 8) */
int32_t Wheel_Slip(int car, int w);
