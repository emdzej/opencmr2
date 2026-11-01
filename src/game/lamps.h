/* The cars' lamps (0x463d60-0x464960, 0x477ac0-0x477ce0; docs/re/world.md#lamps): each car's .cin
   section 0 lists its lamps, each a glow on the car, and the lens of the second model (its type 0xe
   object) shows them through its texture's alpha. Five groups, each with a left and a right side:
   0 the headlights' side lamps, 1 the brakes, 2 the hazards (a broken engine), 3 reversing, 4 the
   headlights; a group fades in and out (Lamps_Lens), and the glows of its lamps follow. */
#pragma once
#include "render/tex.h"
#include "sim/car.h"
#include <stdbool.h>
#include <stdint.h>

void Lamps_Load(void);              /* 0x463d60: the glows cleared, the lamp textures */
void Lamps_Setup(Car *c);           /* 0x463fe0: a car's lamps as glows */
void Lens_Setup(int i, bool head_on_brakes);   /* 0x477b60 */
/* 0x466360's loop over the listed cars: the kind 9 tables cleared, then for a car in the race its
   lamps and its lens (the side lamps on the brakes but for handling classes 6, 7 and 10) */
void Lamps_Place(void);
/* 0x4664c0: every listed car's lamps once a frame (its first call, 0x496e00, is not ported) */
void Lamps_Frame(const int16_t *cars, int n);
void Lamps_Step(Car *c);            /* 0x4643f0 */
void Lens_Step(int i);              /* 0x477ce0 */
/* 0x477c20: group bit's left and right on or off for car i */
void Lens_Set(int i, bool left, bool right, uint8_t bit);
/* 0x477c80: group k's left and right levels (0-255 as 16.16 fractions, 255 = 0xff00) */
void Lens_Get(int i, int32_t *left, int32_t *right, int k);
int Lamps_Count(int car);           /* the port: the car's lamps */
Tex *Lens_Tex(int i);               /* the port: car i's lens texture (NULL: none) */
bool Lamps_On(int car, int k);      /* the port: lamp k's glow shown */
int32_t Lamps_Glow(int car, int k); /* the port: lamp k's glow level */
