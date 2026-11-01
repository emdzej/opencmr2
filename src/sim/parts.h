/* The cars' loose parts (0x480870-0x484d30): up to four panels of the second model per car (object
   types 9, 10, 12, 13 from the table at 0x51f888; the damage slots of those types) that work loose
   once their damage panel passes a threshold, swing about a hinge on the car's body, scrape sparks on
   the ground, break off their hinge and, once too far from the car, fall off (the damage record's
   part flag, then hidden when drawn). Each part has a 0x1a0-byte record per car (four arrays at
   0x590d7c); its update is per type: 0 a wobble about the hinge (0x4814d0), 1 the hinged panel
   (0x4816f0), 2 and 3 their own (0x484310, 0x483570, not ported). */
#pragma once
#include "sim/car.h"
#include "sim/sim.h"
#include <stdbool.h>
#include <stdint.h>

/* For the damage (sim/damage.c): */
void Parts_Init(void);                            /* 0x480a60: srand(400), the thresholds */
void Parts_ClearFlag(void);                       /* 0x480a50: 0x590d70 cleared */
void Parts_Register(const Car *c, Obj *o, int slot);   /* 0x480af0 */
int Parts_Slot(int k, int car);                   /* 0x484d10: part k's damage slot (0xff none) */
Obj *Parts_Obj(int car, int k);                   /* 0x484de0: car's part k's object (NULL none) */
void Parts_Reset(const Car *c);                   /* 0x480b40: srand(400); the parts back to rest */
void Parts_Hide(const Car *c, int k, int32_t off);   /* 0x480ac0: a fallen-off part not drawn */

/* Cars_Load 0x456d90: */
void Parts_Alloc(int n);                          /* 0x480900 */
void Parts_CinLinks(const int16_t *cars, int n);  /* 0x4809e0: section 3 of each car's .cin */
void Parts_Empty(void);                           /* 0x480980: the records and part objects cleared */

/* Sim_Step 0x42baf0: per listed car the loose parts' check and step, then 0x484e00 (not ported). */
void Parts_Step(const CarList *l);                /* 0x480bb0 */
/* Race_PoseInterp 0x41d060: every moving part's drawn matrix between its last two steps. */
void Parts_Interp(int32_t frac);                  /* 0x484d30 */
/* Race_DrawView 0x40f0c0 per car (after 0x46b8f0): the fallen-off parts hidden. */
void Parts_HideFallen(const Car *c);              /* 0x46b670 */

/* For tests: car n's part k is loose (its record taken, bit 0 of +0x150), its flags. */
bool Parts_Active(int car, int k);
uint8_t Parts_Flags(int car, int k);
