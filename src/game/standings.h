/* The championship standings (0x533538-0x533698): sixteen points (0x533558, 16.16) with their order
   (0x533538) and places (0x533548), sixteen times (0x533658, 16.16 s) with theirs (0x533638,
   0x533648), and a count each (0x5335a8, the tie-break of equal points). Index 15 down are the
   players, 0 up the computers. */
#pragma once
#include <stdint.h>

void Stand_SetPoints(int i, int points);   /* 0x40d090 */
void Stand_SetTime(int i, uint32_t cs);    /* 0x40d3f0: from 1/100 s */
void Stand_Sort(void);                     /* 0x40d0c0 */
void Stand_AddTimes(const int8_t *order, const int8_t *by, const int32_t *t);   /* 0x40d120 */
void Stand_AwardPoints(void);              /* 0x40cf30: at a rally's end */
void Stand_UndoPoints(void);               /* 0x40d010 */
int Stand_ArcadePoints(int place);         /* 0x40ce40: 6, 4, 3, 2, 1, then 0 */
void Stand_ArcadeAward(const int32_t *finish);   /* 0x40ccd0 (0x448620): the arcade race's points */
