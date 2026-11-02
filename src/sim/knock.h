/* Knocked-down scenery (docs/re/world.md#knocking-scenery-down): a movable object a car knocks over
   is hidden for that car and a copy of its mesh (made at stage load, in the object's own frame) tips
   over or is flung from the car, in a pool of 40 records (0x5894e0, 0x128 bytes each). */
#pragma once
#include <stdint.h>

void Knock_Init(void);   /* 0x46f550 (Scenery_CollInit's end): the pool's objects, the mesh copies and shapes */
void Knock_Free(void);   /* 0x46f500 */
void Knock_Hide(void);   /* 0x46f7e0: the pool empty, its objects 10,000 units down */
/* 0x46fe70: movable entry i knocked by car n in cell `cell` */
void Scenery_Knock(int i, int cell, int n);
void Knock_Step(void);            /* 0x470580 (Sim_Step) */
void Knock_Interp(int32_t frac);  /* 0x471950 (Race_PoseInterp) */
void Knock_Draw(void);            /* 0x471a60 (Race_DrawView) */
void Knock_Restore(void);         /* 0x4702a0: everything back up (the restarts) */
/* the port, for tests: the records in use, and record k's state and position */
int Knock_Count(void);
int Knock_State(int k, int32_t pos[3]);
