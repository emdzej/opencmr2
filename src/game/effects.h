/* The race's particle effects (0x459c80-0x45a170, docs/re/world.md#particles): their textures, their
   types and the emitters (the wheels' dust, grass, splashes and stones, the exhausts, the sparks).
   The option 0 balls are game/balls.c. */
#pragma once
#include "render/tex.h"
#include "sim/car.h"
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
void Car_GroundLight(int car, int view);   /* 0x462aa0 */
void Effects_Car(int car);
void Chips_Setup(void);
void Glass_Setup(void);   /* 0x49ab90 (0x466360, before 0x494bb0): the glass shapes */
/* 0x49a910 (the damage's glass 0x4692f0, a panel broken): the panel id against the damage record's
   first four (+0x460-+0x463): the side windows queue kind 0 or 1, the front and rear screens two each
   (2 and 3, 4 and 5), with the car's impact (+0x5c4); the others none */
void Glass_Queue(const uint8_t *dmg, const Car *c, uint8_t id);
/* 0x49ab20 (the end of each car in Parts_Step 0x480bb0): the queue thrown, last first (0x49a870) */
void Glass_Pieces(Car *c);
/* Sparks_Add 0x499750 (scrapes: the walls 0x48fb80, the loose parts 0x483100, the cars' contacts
   0x48ae90, the scenery 0x469e40, 0x46a500): 1 to most (16.16) at random of sparks (type 26) at pos
   (relative to the car's first model's place), each with a velocity from the three rows (along,
   across, up) scaled by strength times 0.2, 0.1 and 0.05 and a random share, the second and third
   rows each one way or the other at random; of a random share under chips, paint chips (type 27,
   the car as their argument) while the damage record's chip count (+0x469) is under 8, sparks after.
   A chip, or the spark in its place, marks the record (+0x46c) and adds 2 to the count (at most 255).
   Not in mode bit 26 or for a car out of the race (+0xc0c). */
void Sparks_Add(int32_t strength, const int32_t pos[3], Car *c, const int32_t rows[9], int32_t most, int32_t chips);   /* 0x498dd0 (0x466360, after Race_StartLights) */
/* 0x45b550 (from the engine's backfire in Engine_Mix 0x479360, not ported): the car's exhausts flame
   for the given frames */
void Effects_Backfire(int car, int frames);
/* Wheel_Slip 0x465d70: wheel w's slip as 0..1 (0 for cars from 8) */
int32_t Wheel_Slip(int car, int w);
