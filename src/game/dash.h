/* The race's gauges (0x445a60-0x447260): the first two players' speed and revs eased each step
   (0x445ea0) and between steps (0x445df0), drawn at the bottom right before the HUD (0x446210) as a
   digital rev counter (0x446270; on a split screen a plain bar) or a dial with a needle and the gears'
   column (0x446bf0), by the options or the car's class. */
#pragma once
#include <stdint.h>

void Dash_Load(void);            /* 0x445a60 (0x40f020) */
void Dash_Reset(void);           /* 0x445c80 (0x40efa0, the race's rebuild) */
void Dash_Update(int player);    /* 0x445ea0 (Race_Update) */
void Dash_Interp(int32_t frac);  /* 0x445df0 (Race_PoseInterp) */
int32_t Dash_Revs(int player);   /* 0x445dd0 */
void Dash_Draw(int player);      /* 0x446210 */
