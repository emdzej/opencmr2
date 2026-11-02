/* The race's intro (Race_DrawHud 0x40f8d0's last part): for 2.25 s after the cars are loaded the
   loading screen's colour over the view fades out with the stage's name and records on it
   (Race_IntroInfo 0x410100) and the full loading bar (0x40fec0). */
#pragma once

void Intro_Start(void);            /* Race_S4_Cars 0x4010a0's end: 0x536ac8 */
void Intro_Draw(int player, int state);
/* 0x40fec0 without the world: the loading bar, eleven squares at the bottom right, those past pct
   lit, alpha a */
void LoadBar_Draw(int pct, int alpha);
