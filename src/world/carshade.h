/* The trees' shadows on the cars (0x4b4100-0x4b6340), with the shadow map option (0x52afd0 bit 4,
   0x405ba0) and the stage's .tre. Car_Load lists each car's model trees (0x4b45d0): every mesh object
   under them (not types 6, 0xe, 0xf), the wheels' and the main model's meshes (but a kind A car's) as
   a plain cylinder of their size. Once a frame, in the first view, each car takes the tree items of the
   nine cells round its own whose shadow boxes it reaches (0x4b5f90); its triangles that face the sun get
   a copy, coloured by the shadow colour and faded towards the edge-on (0x4b4180), and each such tree's
   billboard texture is projected onto them along the sun (0x4b5770), the triangles inside the texture
   going into a vertex buffer of 28,800 vertices. After the world's passes the buffer is drawn in runs
   of one texture each, for the passes the car is shown in (0x4b6240). */
#pragma once
#include "world/obj.h"
#include <stdbool.h>
#include <stdint.h>

void CarShade_Add(Obj *o, bool keep);            /* 0x4b45d0 (keep: the meshes as they are) */
void CarShade_Free(void);                        /* 0x4b5380 */
void CarShade_Mark(const Obj *o, const uint8_t *mesh);   /* 0x4b4100: a mesh changed (the dents) */
void CarShade_Begin(void);                       /* 0x4b5e60 */
void CarShade_Gather(const Obj *o, int32_t reach, int16_t cell);   /* 0x4b5f90 */
void CarShade_Shade(const Obj *o, int32_t strength, uint8_t passes);   /* 0x4b5ee0 */
void CarShade_End(void);                         /* 0x4b5eb0 */
void CarShade_Draw(uint32_t pass);               /* 0x4b6240 */
