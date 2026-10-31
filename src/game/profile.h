/* The player profiles (0x52fa08, four of 0x650 bytes from 0x52fa1c) and the player records (0x531350,
   sixteen of 0x30 bytes; bits 18-21 of the first word the profile, 0xf none). Profiles_Init 0x4eadb0
   runs at start-up (from 0x4d15e0): every profile with camera 4 (the chase), the automatic box (bit 5
   of +0x40) and the chase camera's defaults (0x4ec260), then no profile for any player (0x4ec000). */
#pragma once
#include <stdint.h>

void Profiles_Init(void);              /* 0x4eadb0 */
void Player_SetProfile(int player, int profile);   /* the front end's player set-up (port) */
