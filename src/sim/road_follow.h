/* Sim_CarsPost 0x42cd00 and Car_RoadFollow 0x42ce60 (see road_follow.c). */
#pragma once
#include "sim/car.h"

void Car_RoadFollow(Car *c);                   /* 0x42ce60 */
void Sim_CarsPost(const int16_t *list, int n); /* 0x42cd00 */
