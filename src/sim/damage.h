/* The car damage (0x4667c0-0x46b400, docs/re/architecture.md#damage): per car a 0x4d0-byte record and
   a 0x290-byte hit log. The record keeps up to 15 mesh slots of the second model (object types 5-19)
   with their rest vertices; a hit dents them (a flat hit or a round one, each vertex within its limits,
   which the models keep in their vertex colours), adds to a 3 x 3 grid of cells over the car, and the
   cells set 34 panels whose values are the handling factors (engine, steering, brakes, drag) and the
   glass. Each hit is logged (20 entries, the weakest replaced) and the log is carried between stages,
   replayed by Damage_Reset. Not ported: the loose parts (0x480000), the glass pieces (0x49a910) and the
   relighting marks (0x4b4100). */
#pragma once
#include "sim/car.h"
#include <stdbool.h>
#include <stdint.h>

/* Car_Damage 0x466ef0: a hit on a car at a point (world), with the surface's normal, the size of what
   was hit and its kind (0 the car's corner or a wall face, 1 a cylinder, 2 an object's corner or a
   wall's end), the impulse at +0x5c4. Without `replay` it sounds the hit for a player's car (Crash_Hit
   on the wheels, Crash_Roll off them, a hit at most every 10 centiseconds) and shakes the player's view
   (Camera_Shake); then, with dents on and stronger than 0.1, the dent, and the cells, panels and glass.
   A replay (Damage_Reset) takes the hit from the decoded log entry. */
void Car_Damage(Car *c, const int32_t pt[3], const int32_t n[3], uint32_t size, uint8_t kind, bool replay);

int32_t Damage_Flag(const Car *c, int n);   /* 0x469bc0: the record's +0x4b0 + 4n (the fallen-off parts) */
uint8_t *Damage_Log(int n);                /* the port: car n's 0x290-byte hit log (0x588b98) */
void Damage_SetDents(int on, int n);        /* 0x46b400: car n's dents on (0x588970, set by Car_Load) */
void Damage_Alloc(int n);                   /* 0x4667c0 */
void Damage_Clear(int first, int n);        /* 0x4669b0: the records zeroed */
void Damage_ResetLogs(void);                /* 0x4668d0 */
/* Damage_Setup 0x4669f0: for each listed car (from the last) the slots from the second model, the
   records' defaults, a player's carried log (unless keep, also as this stage's), the rest shape, then
   Damage_Reset; again brackets it differently (the restart's call). */
void Damage_Setup(bool again, bool keep, const int16_t *cars, int n);
/* The port: Cars_Load 0x456d90's damage calls (Damage_Alloc, before the cars in the original, then
   after the lists Damage_Clear, Damage_ResetLogs and Damage_Setup for every car). */
void Damage_CarsLoaded(void);
void Damage_Finish(int car);   /* 0x469a80: at the finish, this stage's panels and parts kept, logging off */
void Damage_Store(int car);    /* 0x469b50: this stage's log into the player's carried damage (0x52ebc0) */
void Damage_Misfire(Car *c);   /* 0x46b4e0: a broken engine's cutting out (the countdown +0x40c) */
/* For the lamps (game/lamps.c): */
bool Damage_Dents(int n);                    /* 0x46b4c0: car n's dents on */
Obj *Damage_PartObj(int n, int k);           /* 0x484de0: car n's loose part k's object (NULL: none) */
int Damage_Slots(int n);                     /* the record's +0x45c: the mesh slots */
int Damage_SlotVerts(int n, int s);          /* +0x420 + 4s: slot s's vertices */
/* slot s's vertex v at rest (the +0x78 arrays, 16.16); false when the slot has none */
bool Damage_RestVert(int n, int s, int v, int32_t p[3]);
/* 0x46b440: slot s's vertex v as the mesh has it now (the floats rounded to 16.16) */
bool Damage_Vert(int n, int s, int v, int32_t p[3]);
