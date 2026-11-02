/* The player profiles (0x52fa08, four of 0x650 bytes from 0x52fa1c) and the player records (0x531350,
   sixteen of 0x30 bytes; bits 18-21 of the first word the profile, 0xf none). Profiles_Init 0x4eadb0
   runs at start-up (from 0x4d15e0): every profile with camera 4 (the chase), the automatic box (bit 5
   of +0x40) and the chase camera's defaults (0x4ec260), then no profile for any player (0x4ec000). */
#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Options_Defaults 0x510410 (start-up, before Profiles_Init): the options block (0x52afa0, 0x399c
   bytes) cleared and set: the mode and option words' defaults (the volumes in 0x52afb8: effects at
   bits 7-13, speech at 14-20), the chase camera's (0x510570), the best times' tables (0x406010) */
void Options_Defaults(void);
void Records_Defaults(uint32_t block);   /* 0x406010: 0x52b044, 0x52c308, 0x52d5cc */
void Profiles_Init(void);              /* 0x4eadb0 */
void Player_SetProfile(int player, int profile);   /* the front end's player set-up (port) */
/* 0x4ebf20: the player's profile cleared to the defaults: camera 4, the automatic box, the HUD panels
   (display flags 0x3c: the standings, the time, the split difference), the chase camera's settings */
void Profile_Create(int player);

/* The front end's player and profile set-up (0x4085a0, 0x4eb000-0x4ec210). A profile is in use when
   its byte at +0x14 (0x52fa18) is set and bit 21 of its first word (0x52fa1c) clear. */
uint32_t Player_Word(int player);          /* the player record's first word (0x531350 + 0x30 n) */
const char *Player_Name(int player);        /* 0x408400: the profile's name ("" without one) */
bool Player_HasProfile(int player);
char *Player_FullName(int player);                          /* 0x408470 */
void Player_SetFullName(int player, const char *s);         /* 0x4eaf90 */
void Player_BirthDate(int player, int *day, int *month, int *year);   /* 0x408b10 */
void Player_SetBirthDate(int player, int day, int month, int year);   /* 0x4ebe10 */
bool Cheat_Match(int player, bool *on);                     /* 0x4eb290 */
void Player_Touch(int player);                /* 0x408d00 */
uint32_t Player_Tyres(int player);            /* 0x408a60: the tyre wear record's address (0 none) */
void Tyres_Tally(int n, int w, int spin, int slide);   /* 0x45c750 */
void Tyres_Start(bool again, bool keep, int players);  /* 0x45c610 */        /* 0x4085a0: one set (in mode 4 always), and bit 26 */
void Player_AssignProfile(int player, int profile);   /* 0x4eb860: -1 picks a free or the oldest one */
void Player_SetGuest(int player, bool guest);         /* 0x4eb000 */
void Player_DropProfile(int player);       /* 0x4ebe80 */
void Players_DropAll(void);                /* 0x4ec000 */
int Profiles_Free(void);                   /* 0x4ec020: those in use and not a player's */
const char *Profile_FreeName(int i);       /* 0x4ec110: the i-th of them's name (0x52fa18) */
int Profile_FreeIndex(int i);              /* 0x4ec090 */
void Player_DefaultCamera(int player);     /* 0x4ec210 */
