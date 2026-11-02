/* The drivers' medals and what a stage's result opens (0x408e30-0x4097b0, 0x41b460): each profile's
   two medal words (+0x62c and +0x630, 2 bits a field: 3 less the place, only raised), its driven
   cars (+0x4c bits), and the game's progress (0x52b03c, 0x52b040) and bonus flags (0x52afc0). */
#pragma once
#include <stdbool.h>
#include <stdint.h>

bool Player_CarDriven(int player, uint32_t car);       /* 0x409010: true when newly marked */
bool Player_HasDriven(int player, int car, bool cheat);   /* 0x408e30 */
void Unlocks_AfterStage(void);                         /* 0x41b460 */
void Unlocks_ClearNew(void);                           /* 0x406580 */
