/* The sectors (0x458090-0x459390): the centre line's flagged points are the splits; each car's
   0x1c-byte record (0x542e78) steps its point (+0x00) towards the car's and counts the splits passed
   (+0x04 the last, +0x0a the next; +0x0c the last split's index, +0x0e the finish's), flagging each
   step's events (+0x16 moved, +0x17 a split, +0x19 the first) and the finish (+0x1a). Ported for a
   rally (one way, no laps); the arcade and network cases are logged. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

void Sectors_Init(void);     /* 0x458090 (Stage_Load): the split points */
int Sectors_Cars(void);     /* DAT_00542c68: the cars racing at Sectors_Init */
void Sectors_Start(void);    /* 0x458100 (the line, 0x424c50): every car's record */
void Sectors_Step(void);     /* 0x4581d0 (Race_Update) */
bool Sectors_Finished(int car);   /* 0x4582b0 */
bool Sectors_Split(int car);      /* 0x458250: a split passed this step */
int Sectors_Count(void);          /* 0x542c74: the split points */
bool Sectors_Moved(int car);          /* 0x458230 */
int Sectors_SplitIndex(int car);      /* 0x458370 */
int Sectors_Shown(void);              /* 0x4583c0 */
int32_t Sectors_Point(int i);         /* 0x4583b0: split i's cat point (16.16) */
int Sectors_Last(int car);            /* 0x458350 */
bool Sectors_LapFirst(int car);       /* 0x458290 */
int Sectors_Most(int car);            /* 0x4589e0: the furthest point reached (+0x10) */
bool Sectors_Lapped(int car);         /* 0x458270: the line crossed round a circuit this step (+0x18) */
int Sectors_LastLap(int car);         /* 0x458330: the lap of the last split (+0x04) */
int Sectors_Lap(int car);             /* 0x4582d0: the record's +0x02, the lap shown */
int Sectors_CarPoint(int car);        /* 0x4582f0: the record's point (+0x00) */
int Sectors_After(int point);         /* 0x459350: the next point (0 after the last with 0x542cae) */
int Sectors_Steps(int car);           /* 0x458310: the points moved, back ones less (+0x12) */
