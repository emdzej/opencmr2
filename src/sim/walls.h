/* The cars against the stage's walls (fin.dat, world/fin.h): Sim_Step's collision pass 0x4877a0, run
   after Sim_StepCars and before Sim_CarsPost. Per wall in the car's cell within reach, by its type: a
   face pushes the car's flat box out and bounces the velocity (0x48fb80), the ends push it out of
   their corner (0x48e580); types 6, 0x18 and 0x19 flag the car (+0xbf8, off the stage) and 0x17 bumps
   the wheels. Not yet: the car-to-car contacts, the scenery objects, the damage and sparks. */
#pragma once
#include "sim/car.h"

/* The 0x98-byte flat box record per car at 0x590ed0 (FUN_0048ca40), as ints: */
enum {
    B_EXT_F = 0, B_EXT_S = 1,   /* the half lengths along the flat forward and side axes */
    B_YMAX = 2, B_YMIN = 3,     /* the corners' highest and lowest y (world) */
    B_FWD = 4, B_SIDE = 7,      /* the flat forward axis (y 0, unit), the side axis (z, 0, -x) */
    B_VALID = 10, B_PREV = 11,  /* made this step; made in the last */
    B_CORNER = 12,              /* four corners (x, the highest y, z) */
    B_OLD = 24,                 /* the last step's four */
    B_INTS = 38
};
int32_t *Walls_CarBox(Car *c);                                 /* CollBox_Make 0x486c30 on the car's record */
void Walls_Bumps(Car *c);                                      /* Car_WallBumps 0x48df50 */
void Walls_RotT(const Car *c, int32_t out[3], const int32_t v[3]);   /* 0x4b9da0 with the orientation */

void Car_Walls(Car *c, int n);                  /* 0x48e0a0 */
void Sim_Collide(const int16_t *list, int n);   /* 0x4877a0 */
