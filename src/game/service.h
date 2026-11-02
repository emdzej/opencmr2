/* The stage set-up and service screens: game mode 2, World_Frame 0x4ff450 (0x4ff420-0x50fdc0). Before
   each stage of a rally (and a single stage or time trial) the players see the stage's information,
   set their cars up, repair them in the time the service allows, save the championship, and go on to
   the race (mode 3) or quit to the front end (mode 0). A process group of one slot, its state table
   0x526ee0 and transitions 0x526f18. */
#pragma once

int World_Frame(void);   /* 0x4ff450 */
