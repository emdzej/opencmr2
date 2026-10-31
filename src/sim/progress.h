/* The stage's centre line (<stage>.cat, docs/formats/stage.md#cat) and each car's progress along it:
   the points passed (+0x08 of the car's record at 0x538aa8), so the finish and the share of the stage
   done. */
#pragma once
#include "sim/car.h"
#include <stdbool.h>

bool Stage_LoadCat(const char *stage);   /* 0x420630 */
int Cat_Points(void);                     /* 0x421430 */
int Cat_FinishPoint(void);                /* DAT_00538a84: the last flagged point + 1 */
bool Cat_Circuit(void);                   /* 0x421500 */
void Cat_Tangent(int32_t out[3], int i);  /* 0x4201d0 */
/* 0x420850: a car's progress started at the nearest point (on circuits) */
void Car_ProgressStart(Car *c);
void Car_Progress(Car *c);                /* 0x420a30 */
int32_t Car_StageShare(const Car *c);     /* 0x421470: 0 to 1.0, 1.0 at the finish */
int Car_PointsPassed(const Car *c);       /* the record's +0x08 */
