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
