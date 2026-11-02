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
#include <stddef.h>
uint8_t *Profile_LoadFile(const uint8_t *e, size_t *size);   /* 0x4eb2e0, 0x4eb4b0: malloc'd, NULL none */

/* The saved championships (gamesave\*.rcs) */
void Champs_Scan(void);                  /* 0x4f4ef0 */
void Champs_Free(void);                  /* 0x4f4e80 */
int Champs_Count(void);                  /* 0x4f4db0 */
const uint8_t *Champs_Record(int i);     /* 0x4f4e50: its 0x7f4 bytes */
const char *Champs_Name(int i);          /* 0x4f4dc0 */
int Champs_Players(int i);               /* 0x4f4de0 */
int Champs_Rally(int i);                 /* 0x4f4e00 */
int Champs_Stage(int i);                 /* 0x4f4e20 */
const char *Champs_File(int i);          /* 0x4f4e70 */
bool Champ_Save(int i, const char *name);   /* 0x4f5190: over entry i (name NULL) or new */
