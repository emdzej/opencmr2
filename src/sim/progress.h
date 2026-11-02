/* The stage's centre line (<stage>.cat, docs/formats/stage.md#cat) and each car's progress along it:
   the points passed (+0x08 of the car's record at 0x538aa8), so the finish and the share of the stage
   done. */
#pragma once
#include "sim/car.h"
#include <stdbool.h>

bool Stage_LoadCat(const char *stage);   /* 0x420630 */
int Cat_Points(void);                     /* 0x421430 */
void Cat_Point(int i, int32_t p[3]);    /* 0x421530: x, 0, z */
int Car_CatIndex(const Car *c);          /* 0x421370: the point the car is at (rec +0x00) */
int Car_CatIndex16(const Car *c);        /* 0x4213a0: the record's +0x14 */
const uint8_t *Cat_Record(int i);        /* 0x421440: the 0x2c-byte record, NULL without points */
int Cat_FinishPoint(void);                /* DAT_00538a84: the last flagged point + 1 */
void Progress_SetPoint(Car *c, int p);    /* 0x4213d0 */
bool Cat_Circuit(void);                   /* 0x421500 */
void Cat_Tangent(int32_t out[3], int i);  /* 0x4201d0 */
/* 0x420850: a car's progress started at the nearest point (on circuits) */
void Car_ProgressStart(Car *c);
void Car_Progress(Car *c);                /* 0x420a30 */
int32_t Car_StageShare(const Car *c);     /* 0x421470: 0 to 1.0, 1.0 at the finish */
int Car_PointsPassed(const Car *c);       /* the record's +0x08 */
void Progress_Save(int n, uint8_t out[0x18]);       /* 0x421510's record, in the original's layout */
void Progress_Load(int n, const uint8_t in[0x18]);
void Progress_Reset(int n);                          /* 0x4207a0 */
