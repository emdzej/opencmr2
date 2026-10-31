/* The car set-up's helpers other passes call (see car.c). */
#pragma once
#include "sim/car.h"

void Car_CornerGround(Car *c, int n);   /* 0x4930e0 */
void Car_GroundNormal(Car *c);          /* 0x42de20 */
void Car_Contacts(Car *c);              /* 0x42f820 */
/* 0x4adb10: the rotation matrix for an angle about a unit axis (16 ints) */
void Mat_AxisAngleRows(int32_t m[16], const int32_t a[3], int32_t ang);
/* 0x42e450 (road_follow.c): the wheels' spring travel and ground penetration, the bump noise */
void Car_Travel(Car *c);
void Car_LiftOut(Car *c);     /* 0x42f8c0 (road_follow.c) */
void Car_WheelLoad(Car *c);   /* 0x432b30 (step.c) */
