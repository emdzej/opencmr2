/* The race started by the game (the port): the race (game/race.c) with the loads, input and drawing
   of a rally stage for one player, as the front end leaves it set up (the selection's rally and
   stage, player 0's car and profile). Stands in for the stage set-up screens' loads (mode 2, 0x4ff450)
   and the race's own load states until they are ported. */
#pragma once
#include <stdbool.h>

bool Play_Begin(void);
