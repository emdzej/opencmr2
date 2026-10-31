/* The cars against the stage's walls (fin.dat, world/fin.h): Sim_Step's collision pass 0x4877a0, run
   after Sim_StepCars and before Sim_CarsPost. Per wall in the car's cell within reach, by its type: a
   face pushes the car's flat box out and bounces the velocity (0x48fb80), the ends push it out of
   their corner (0x48e580); types 6, 0x18 and 0x19 flag the car (+0xbf8, off the stage) and 0x17 bumps
   the wheels. Not yet: the car-to-car contacts, the scenery objects, the damage and sparks. */
#pragma once
#include "sim/car.h"

void Car_Walls(Car *c, int n);                  /* 0x48e0a0 */
void Sim_Collide(const int16_t *list, int n);   /* 0x4877a0 */
