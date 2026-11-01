/* Car_Damage 0x466ef0: a hit on a car at a point (world), with the surface's normal, the size of what
   was hit and its kind (0 the car's corner, 1 a cylinder, 2 the object's corner; the walls pass 0 or 2
   for a face or an end), the impulse at +0x5c4. Without `replay` it first sounds the hit for a
   player's car (Crash_Hit on the wheels, Crash_Roll off them, a hit at most every 10 centiseconds) and
   shakes the player's view (Camera_Shake); Damage_Reset replays the damage carried over with it set.
   Ported: that first block. Not yet: the damage records (0x588b94, 0x4d0 bytes a car), the
   deformation (0x467700, 0x467e90) and the panels (0x468520). */
#pragma once
#include "sim/car.h"
#include <stdbool.h>
#include <stdint.h>

void Car_Damage(Car *c, const int32_t pt[3], const int32_t n[3], uint32_t size, uint8_t kind, bool replay);
