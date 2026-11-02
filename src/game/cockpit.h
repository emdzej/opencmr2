/* The cockpit view (camera kind 3, 0x475f80-0x4779e0): for the first two cars the interior model
   <car>a5.c3d (c5 by an option) with its textures, under the car's second model. Each step of the
   view the head turns and moves with the car's acceleration (0x476e00), the wheel turns with the
   steering and the hand moves between it and the gear lever on a change (0x476640, 0x476850), the
   wipers sweep in the rain (0x476a40, 0x476c70), and the camera sits at the head (0x4778b0). The
   model is drawn only in this view (the cars' masks, 0x46b8f0). Not yet: the rev counter's needle drawn
   into the REVCT texture (0x477460, 0x4775f0). */
#pragma once
#include "world/obj.h"
#include <stdbool.h>
#include <stdint.h>

void Cockpit_Reset(void);                      /* 0x475f80 */
void Cockpit_Load(int slot, int car);          /* 0x4760a0 */
bool Cockpit_Ready(int car);                   /* 0x476520 */
Obj *Cockpit_Model(int car);                   /* 0x58d49c */
void Cockpit_Masks(int car, uint8_t model, uint8_t objs);   /* 0x46b8f0's part */
void Cockpit_Init(int slot, int car, int cls, int32_t m[16]);   /* 0x476410 */
void Cockpit_Step(int car, const int32_t *r, int32_t m[16]);    /* 0x4765e0 */
void Cockpit_Snap(int car);                    /* 0x4764e0 */
void Cockpit_On(int car, bool on);             /* 0x4764c0 / 0x476500 */
