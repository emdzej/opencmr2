/* The records (0x4cf260-0x4d0590): what a race leaves in the player's profile and in the game's tables
   (the options block's 0x52b044, saved with the options).

   The championship's context (0x8173f0, set by Fe_ChampTables 0x4cf060 on the way out of the front
   end, refreshed by 0x4d0230): +0 the players, +4 the mode, +8 the rally, +0xc the stage (a circuit's
   race number), +0x10 the level, +0x14 a circuit's location; then the flags of what is new: per player
   (0x817420, 10 bytes apart) the stage, rally, championship and arcade bests, two masks (+4, +6) and a
   count (+8); 0x817410 the stage's record, 0x817412 the championship's table, 0x817413 the arcade's,
   0x817414/0x817416 two masks and 0x817418 a count. 0x817448 holds ten split times (the record's to
   race against, or the new record's), 0x817570 the session's best time.

   A profile's race block (0x52fa68 + 0x650 n, 0x408c70): +0 the championship (bits 0-3 the place, 4-5
   its low bits, 6-13 the points), +4 its time, +8 three podium counts; +0xc the rally (0-3 the place,
   4-7 the points' low bits, 8-9 the place's, 10-13 the stage wins), +0x10 its time; +0x14 the stage
   time; +0x18 the arcade (0-2 the place, 3-4 its low bits, 5-10 the points), +0x1c its time; +0x20 the
   last lap. Its records block (0x52fa8c + 0x650 n, 0x408cb0): +0 the championship's bests (16 bytes a
   level), +0x30 the rallies' (12 bytes, three levels a rally), +0x150 the stages' (8 bytes, 12 a
   rally), +0x450 the arcade's (12 bytes, three levels a location), +0x4bc the laps' (8 bytes, three
   races a location). Each best's first word: bits 0-5 the car, 6 the box, 7 set once taken, 8-12
   random.

   The game's tables in 0x52b044: +0 the championship's top five (60 bytes a level: 12-byte entries,
   the name, a word with the car, box, points at bits 7-13 and place at 14-17, the podium counts),
   +0xb4 the rallies' (a level and rally: the place at bits 7-10, stage wins 11-14, the time), +0x654
   the stages' records (8 bytes: the name, the time at bits 7-22) and +0x914 their splits (ten), +0xff4
   the arcade's top five (the place at bits 7-9, points 10-15, the time), +0x1210 the laps' records
   and +0x1258 their splits (six). */
#pragma once
#include <stdbool.h>
#include <stdint.h>

void Rec_Context(void);                           /* 0x4d0230 */
void Rec_StageStart(void);                        /* 0x4d0180: the splits to race against */
int32_t Rec_Split(int i);                         /* 0x4d0220 */
uint32_t Rec_LapRecord(void);                     /* 0x4cfe50: the lap's record time */

void Rec_SetTime(int p, int32_t t);               /* 0x4cf450 */
void Rec_RallyStart(int p);                       /* 0x4cf390 */
void Rec_StageWin(int p, int place);              /* 0x4cf3b0 */
void Rec_ChampStart(int p);                       /* 0x4cf3f0 */
void Rec_ChampAdd(int p, int32_t t, int place);   /* 0x4cf420 */
void Rec_SetRally(int p, uint32_t t, uint32_t place, uint32_t points);   /* 0x4cf470 */
void Rec_SetChamp(int p, uint32_t place, uint32_t points);               /* 0x4cf4d0 */
void Rec_SetLap(int p, uint32_t t);               /* 0x4cf530 */
void Rec_SetArcade(int p, uint32_t place, uint32_t points, uint32_t t);   /* 0x4cf550 */

/* the profile's bests, true when beaten (the new marks at 0x817420) */
bool Rec_BestStage(int p);                        /* 0x4cf5b0 */
bool Rec_BestRally(int p);                        /* 0x4cf660 */
bool Rec_BestChamp(int p);                        /* 0x4cf740 */
bool Rec_BestLap(int p);                          /* 0x4cf830 */
bool Rec_BestArcade(int p);                       /* 0x4cf8e0 */
bool Rec_SessionStage(int p);                     /* 0x4cf9d0: the session's best stage time */
bool Rec_SessionLap(int p);                       /* 0x4cfe20 */

/* the game's tables: race index i (for its splits), player p */
bool Rec_StageRecord(int i, int p, const char *name);   /* 0x4cfa10 */
bool Rec_LapTable(int i, int p);                        /* 0x4cfe80 */
/* the top fives: 0 when not in them, 1 at the top, 2 lower */
int Rec_RallyTable(int p, const char *name);            /* 0x4cfb30 */
int Rec_ChampTable(int p, const char *name);            /* 0x4cfc90 */
int Rec_ArcadeTable(int p, const char *name);           /* 0x4cfff0 */

void Rec_Tally(void);                             /* 0x4cf260: the new marks counted into the profiles */
