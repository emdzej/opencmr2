/* The loose car (+0xc00 set: a hard knock, a steep ground, a big turn): Sim_CarsPost runs it in
   place of the road following. The body rests on its eight box corners: lifted out of the ground,
   its velocity into the ground taken off, its spin pulled and locked as an axis crosses the ground
   plane, its rows eased, a roll counted for the damage. Back on its wheels and level, still for long
   enough (+0x96c), it is handed back to the road following. */
#pragma once
#include "sim/car.h"

void Car_FreeBody(Car *c);   /* 0x42eae0 */
