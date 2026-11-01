/* The computer drivers' stage times (0x455300-0x456a30): the stage's .tm file for the difficulty
   (16 drivers: a base, a random range, dry and wet adjustments; and per split their dry and wet times)
   gives, with the stage's splits and the weather, each driver's time at each split and the order; the
   players' times go in at their splits. The results' board shows them. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

void Stage_LoadTm(const char *stage);   /* 0x455300 */
void Opp_Times(bool new_seed);          /* 0x455470: with new_seed the seed from the clock (a load) */
void Opp_Step(int race);                /* 0x455590 */
void Opp_Insert(int who, int32_t cs, int split);   /* 0x455af0 */
int Opp_Computers(void);                /* DAT_00541f98 */
int Opp_Place(int player);              /* 0x455ca0 */
int Opp_Order(int i);                   /* 0x455cf0 */
int Opp_PlayerSlot(int player);         /* 0x455de0 */
int32_t Opp_Time(int i);                /* 0x455d70: the i-th place's time, 1/100 s */
bool Opp_Loaded(void);                  /* 0x455ae0 */
int Opp_PlaceAt(int player, int split); /* 0x455c80 */
int Opp_Entries(int split);             /* 0x455c70 */
int Opp_DriverAt(int place, int split); /* 0x455cc0: -1 none */
int32_t Opp_TimeAt(int place, int split);   /* 0x455d40: 1/100 s */
