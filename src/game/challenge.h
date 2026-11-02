/* The challenge (mode 4): a knockout of head-to-head matches on the rally's super special, its draw in
   the record at 0x52f2b4 (select.c draws it, 0x4069c0) and the race's state 6 between the matches
   (0x472e00, drawn by 0x4759d0). docs/re/challenge.md.

   The record: word 0x52f2b4 bits 0-2 the depth, 3-5 the round (1 the last sixteen, 2 the quarter
   finals, 3 the semi finals, 4 the final), 6-8 the opponents' level, 12-15 the match in the round,
   16-19 the rally, 21 the round over (on to the next stage), 22 the round's matches all run, 23 the
   challenge given up. The rounds' matches follow, 12 bytes each: the final at 0x52f2b8, the semi
   finals at 0x52f2c4, the quarter finals at 0x52f2dc, the last sixteen at 0x52f30c. A match: word
   bits 0-4 the first driver, 5-9 the second (0x1f: nobody, a bye), 10 run, 11 the first won, 12 the
   second won; then the two times (1/100 s). A driver below the players is a player (0x408500 -1). */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "game/proc.h"

uint32_t Challenge_Match(void);         /* 0x473620: the match being run (its address), 0 outside */
bool Challenge_TwoPlayers(void);        /* 0x472cb0's test: both drivers of the match players */
void Challenge_Players(void);           /* 0x472cb0: the match's players into the race, their pads */
void Challenge_MatchEnd(void);          /* 0x472a30: the match's times and winner, on to the next */
bool Challenge_Over(void);              /* 0x4729f0: the challenge left (given up or no player left) */
void Challenge_Reset(void);             /* 0x472ca0: state 6 from its start */
void Challenge_KeepRestart(bool on);    /* 0x4728b0 / 0x4728c0: a restart is the match's, not the challenge's */
bool Challenge_KeptRestart(void);       /* 0x4728d0 */
bool Challenge_PlayerWon(uint32_t match);   /* 0x472990 */
bool Challenge_PlayersLeft(void);       /* 0x4728e0: a player still to drive in the round, or through */
const char *Challenge_Name(uint32_t match, int side);   /* 0x4736b0 */
const char *Challenge_Tag(uint32_t match, int side);    /* 0x473810 */
bool Challenge_IsPlayer(uint32_t match, int side);      /* 0x473790: the side a player's */
bool Challenge_Side(uint32_t match, int side);          /* 0x4737d0 */

void Challenge_Step(ProcGroup *g, uint32_t slot);   /* state 6: 0x472e00 */
void Challenge_Draw(ProcGroup *g, uint32_t slot);   /* state 6's draw: 0x4759d0 */
