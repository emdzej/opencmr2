/* The world step (docs/re/architecture.md#timing): the race's car lists (0x42b660 at the race's set-up,
   0x428810 each step), the steps due per car from the latched times (Sim_StepCount 0x42c890), the step
   of the whole world (Sim_Step 0x42baf0) and the counters' decrement (Sim_StepDone 0x42bc80). */
#pragma once
#include "sim/car.h"

typedef struct { int16_t car[CAR_MAX]; int16_t n; } CarList;
extern CarList g_all;       /* 0x53b500, count 0x53a3a0: every car in the race */
extern CarList g_stepped;   /* 0x53b4f0, 0x53a310: full simulation this step (+0xc18 clear, a step due) */
extern CarList g_listA;     /* 0x53a314, 0x53c9a0: the same cars (the later passes' list) */
extern CarList g_simple;    /* 0x53bd6c, 0x53b532: +0xc18 set, a step due */
extern CarList g_second;    /* 0x53a364, 0x53b534: 0x43f630's cars (none in a rally) */
extern CarList g_collide;   /* 0x53a270, 0x53b558: the collision pass's cars */

void Sim_InitLists(int n);                   /* 0x42b660 */
uint32_t Sim_StepCount(int32_t *frac);       /* 0x42c890: steps due, car 0's fraction */
void Sim_StepDone(const CarList *l);         /* 0x42bc80 */
void Sim_Step(void);                         /* 0x42baf0 */
void Sim_ResetClock(void);                   /* 0x41f350's flag 0x538114: no steps owed */
