/* The rally's weather (0x40d6c0-0x40dc30): before a rally (or stage) the front end picks the weather
   of each of its stages (0x52f100, two words a stage: at the start and at the end), the light level
   of each (0x52f294), the main type around the start's share (0x52f1a0) and the stages with rain
   (0x52f1f0, 0x52f240), from the rally's three tables of ten weather types (0x51627c, 0x78 bytes a
   rally: a stage block of four stages each). A weather chosen on the menus (0x52f290 set, 0x52f0fc
   the type) makes it the same everywhere. */
#pragma once

void Weather_Choose(void);   /* 0x40dc30 */
#include <stdbool.h>
#include <stdint.h>
#include "sim/car.h"

/* The race's weather (0x45e5b0-0x462d80): per player a record of its rain or
   snow (0x547ac8, 0x178 bytes: the type, the level along the stage, 400 / players drops in a box that
   follows the camera), per car the sky's blend and the wet value along the stage (0x543eb8, 0x543ecc),
   the wind's gusts and the lightning. */
void Weather_Alloc(int n);        /* 0x45e5b0 (Cars_Load): the records for n cars, zeroed */
void Weather_Load(void);          /* 0x45eb50 (in 0x40f020): rain, splat, splat2, snow1-3, sunglow */
void Weather_StageLoad(void);     /* 0x45ea70 (Stage_Load, before Stage_LoadTm) */
/* 0x45e6b0: the wet values at the start and end, and the points (16.16) between which it changes */
void Weather_Change(int32_t *wet_a, int32_t *wet_b, int32_t *at_a, int32_t *at_b);
void Weather_RaceStart(void);     /* Race_StartLights 0x45eca0 (in 0x466360, the cars placed) */
void Weather_Restart(void);       /* 0x45e610 */
void Weather_CarsStep(void);      /* Race_Step 0x45e7f0: the cars' wet values along the stage */
void Weather_Step(void);          /* 0x45f4a0 (the end of 0x4664c0, once a frame) */
void Weather_Interp(int32_t frac);   /* 0x461bb0 (Race_PoseInterp) */
/* 0x461c30 (World_Frame, before Sky_Follow): view p's sky by its blend along the stage */
void Weather_Sky(int p);
/* 0x460330 (the end of the cars' pass 0x466570): player p's rain or snow queued, and the sun glow */
void Weather_Draw(int p);
/* Weather_CarValue 0x460c80: the car's wet value (0 dry to 1.0, +8 of its record at 0x543ecc), for the
   grip and the lamps' shine on the ground (named apart while effects.c keeps a static stub of it) */
int32_t Weather_CarWet(const Car *c);
/* 0x460bf0 / 0x460c10: record n's type (callers test 1 rain and 2 snow) and level (+0x54); wet: rain.
   The records past the players' are zero (the original's block is allocated zeroed). */
bool Weather_Wet(int car);
uint8_t Weather_Kind(int car);
int32_t Weather_Level(int car);
/* 0x460a30: the wind's vector, its direction by the strength by the players' mean level (at least
   0.3), doubled when one has snow */
void Wind_Vector(int32_t out[3]);
