/* The simulation step (docs/re/architecture.md#timing): Sim_StepCars 0x433890, the car part of
   Sim_Step 0x42baf0, over a list of car indices (last to first, as the original walks it). */
#pragma once
#include "sim/car.h"
#include "sim/sim.h"

void Sim_StepCars(const int16_t *list, int n);
void Sim_SimpleStep(const CarList *l);   /* 0x444c10: the cars moved from outside (network) */
extern int g_pending_passes;   /* the port: passes of the step not ported (none now; kept for the tests) */
