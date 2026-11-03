/* The stage's results (0x448e00-0x454400): pages on the menu engine shown over the replay (race
   state 10), from the replay's title through the times, the splits and the standings to the choices
   (restart, restart at the service area, quit), which fade out and post the race's event 1 like the
   pause menu's. Started by state 9 once every player has finished (0x44a1b0). The rally's, the time
   trial's, the challenge's and the arcade's pages are ported; the network modes' (8-12) are not. */
#pragma once
#include <stdbool.h>

void Results_ContinueDone(int ch);   /* 0x449fe0: the continue fade's end */
void Results_Start(bool again);   /* 0x44a1b0 */
bool Results_Active(void);
void Results_Stop(void);
void Results_Step(void);          /* 0x448e70, each step of state 10 */
void Results_SuperSpecialTick(void);   /* 0x44a150 */
void Results_Draw(void);          /* 0x44b330, in state 10's draw */
bool Results_AtChoices(void);     /* the port: the choices page is up */
