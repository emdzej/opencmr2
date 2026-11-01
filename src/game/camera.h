/* The race cameras (0x421720-0x423ff0, the chase cameras 0x447530-0x447ec0): per player two camera
   records (0x539018, 0x64 bytes) of which one is current, switched at once or blended over 12.5 steps;
   each step the current one follows its car (Camera_Step 0x4219b0) and each frame the drawn camera is
   blended between the last two steps' (Camera_Frame 0x422140). Kinds: 1, 2 and 10 the interior views,
   3, 4-6 the chase cameras (4 the profile's settings, 5 a far one, 6 the 45 degree view), 7 the
   trackside cameras, 8 a switch's blend. Ported: the chase cameras and the interior views. */
#pragma once
#include "world/obj.h"
#include <stdbool.h>
#include <stdint.h>

void Camera_Reset(int player);                            /* the port: both records cleared */
void Camera_Select(int player, int kind, bool smooth);    /* 0x422fe0 / 0x421720 */
int Camera_Kind(int player);                              /* 0x422f50 */
void Camera_Step(int player);                             /* 0x4219b0 */
/* 0x4218d0: the next kind the cycle may take (0x423970), blended to; returns it */
int Camera_Next(int player, int state);
void Camera_Frame(int player, int32_t frac, Obj *camera); /* 0x422140 */
void Camera_Position(int player, int32_t pos[3]);          /* the step's camera (0x538d28 + 0x38) */
