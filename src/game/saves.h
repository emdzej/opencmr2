/* The save files (the original's CMR2Saves folder), kept in the platform's user storage under the
   original's names. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

bool GameInfo_Load(void);   /* 0x4ea5e0: Configuration\GameInfo.rcf into 0x52afa0 */
void GameInfo_Save(void);   /* 0x4ea840 */
bool Profiles_Save(void);   /* 0x4eb3e0: each changed profile in use to pps\<name>....pps */
bool Profile_SavePlayer(int player);           /* 0x4eb370 */
void Profiles_Scan(void);                      /* 0x4eb700: the saved profiles' list */
int Profiles_Count(void);                      /* 0x4eb440 */
const uint8_t *Profiles_Entry(int i);          /* 0x4eb450: name (4 bytes), word, word */
bool Profiles_InUse(int i);                    /* 0x4ebd60 */
bool Profile_LoadInto(int player, int i);      /* 0x4eb4c0 */
