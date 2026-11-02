/* The save files (the original's CMR2Saves folder), kept in the platform's user storage under the
   original's names. */
#pragma once
#include <stdbool.h>

bool GameInfo_Load(void);   /* 0x4ea5e0: Configuration\GameInfo.rcf into 0x52afa0 */
void GameInfo_Save(void);   /* 0x4ea840 */
bool Profiles_Save(void);   /* 0x4eb3e0: each changed profile in use to pps\<name>....pps */
