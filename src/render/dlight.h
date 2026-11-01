/* The Direct3D lights (0x4b2e50-0x4b35b0): up to 60 light records (D3DLIGHT7, the slot's index at
   +0x68) at 0x6dffa4, each set and switched on as it is made, and when given a parent an object of
   kind 1 that places it (Obj_Commit moves a point or spot light to the object's world position). The
   stage's sun (Light_Defaults 0x4918d0) is the first, the headlights' two (Lamps_Setup) the next:
   Light_SetMode's 0, 1 and 2 switch lights 0-2. */
#pragma once
#include "render/d3d7.h"
#include "world/obj.h"
#include <stdint.h>

enum { DLIGHT_MAX = 60 };

/* 0x4b2f80: type 0 a point light (the colour also its ambient, a range of 60, attenuation 0, 0,
   0.1), 1 a spot (range 100, theta 0.2, phi 2), 2 directional along -pos made unit, 3 the Direct3D
   type 4; the colour 16.16, pos 16.16. With a parent, a new object under it placed at pos turned by
   angles; returns that object (NULL without a parent or a free slot). */
Obj *Dlight_Add(int type, int32_t r, int32_t g, int32_t b, const int32_t pos[3], const uint16_t angles[3],
                Obj *parent);
void Dlight_Restore(void);                     /* 0x4b2e50: every light set again, the material, ambient B */
void Dlight_SetAtten(Obj *o, int32_t a);       /* 0x4b3570: a light object's constant attenuation (16.16) */
void Dlight_Free(D3DLIGHT7 *l);                /* 0x4b3480 */
void Dlight_FreeAll(void);                     /* 0x4b3450 (at exit) */
int Dlight_Count(void);                        /* DAT_006e0b48 */
D3DLIGHT7 *Dlight_Get(int i);                  /* the port: slot i (NULL: free) */
