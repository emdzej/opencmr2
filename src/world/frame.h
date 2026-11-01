/* The world frame (docs/re/world.md#the-world-frame): World_Render 0x49d3f0 draws one view of the
   stage and the 2D layers around it, and the sorted object pass it uses. */
#pragma once
#include "world/obj.h"
#include <stdbool.h>
#include <stdint.h>

void World_SetMode(uint32_t m);   /* 0x49c420: DAT_0059ce14, the effects drawn before the sorted objects */
uint32_t World_Mode(void);        /* 0x49c430 */
void Obj_QueueSorted(Obj *o);     /* 0x49cb70 */
/* 0x49cbc0: farther first; type 0x14 always "after"; within 1 unit, type 5 after type 0. */
int Obj_CompareFar(const void *a, const void *b);
void Obj_DrawQueued(uint32_t pass);          /* 0x49cd20: sorted (when 0x5207b4 is set), drawn, emptied */
/* 0x49d290: the listed cells' sorted objects, their distance to the camera (x and z) into +0x16c,
   queued. */
void Section_QueueSorted(const Obj *camera);
void World_DrawAll(void);                    /* 0x49cd90: every object, z on (no stage sections) */
/* World_Render 0x49d3f0: the viewport (x, y, w, h), the scene, layer 4 of the 2D queues, with world
   set the stage passes, then layers 3 to 1 over the whole screen. False when the scene can't begin. */
/* The port, until the cars' own drawing is found: these cars' object trees are drawn with Obj_Draw
   just before the glows (0x4af120). */
void World_SetCars(Obj **cars, int n);
bool World_Render(Obj *camera, const int16_t view[4], uint32_t pass, bool world);
