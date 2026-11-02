/* The cars' shadows (0x494b10-0x498590, docs/re/world.md#car-shadows): a record of 0x2a4 bytes per car
   at *0x592734 (allocated by Cars_Load through 0x494b50, freed by 0x494b10), set up when the cars are
   placed (0x494bb0), built each frame (0x496e00) and drawn with each car (0x494db0). The skid marks
   (0x4657d0) take the wheels' ground points from it (+0x198-+0x1f8). */
#pragma once
#include <stdint.h>
#include "sim/car.h"

void Shadow_Alloc(int ncars);   /* 0x494b50: zeroed records */
void Shadow_Free(void);         /* 0x494b10 */
/* 0x498570: car n's record (NULL while none are allocated; the original returns the base plus
   0x2a4 n whatever the base) */
uint8_t *Shadow_Get(int car);
/* 0x494bb0 (in 0x466360, the cars placed): which parts each car's shadow has, and the car's shadow
   profile (its .cin section 1) */
void Shadow_Place(void);
/* 0x496e00 (in 0x4664c0, before each car's lamps): the wheels' patches, the body's quad and the sun's
   outline, dropped onto the ground */
void Shadow_Build(const Car *c);
/* 0x494db0 (in the cars' pass 0x466570): the record's triangles, nudged toward view's camera */
void Shadow_Draw(const Car *c, int view);
/* 0x498370: the sun direction the outline is cast along (from Sun_Direction 0x463070) */
void Shadow_SunDirection(const int32_t d[3]);
/* 0x4984b0 (from 0x462aa0): car's outline scale (+0x250), and the texture factor's alpha, 100 plus 75 v */
void Shadow_Light(int car, int32_t v);
