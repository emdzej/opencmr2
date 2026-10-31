/* Car_SurfaceGrip 0x4781d0 (see grip.c). */
#pragma once
#include "sim/car.h"

/* wet: the weather value 0x460c80 gives (0 dry, 1.0 fully wet) */
void Car_SurfaceGrip(Car *c, int32_t wet);
