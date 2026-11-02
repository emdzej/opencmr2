/* The race cameras (0x421720-0x423ff0, the chase cameras 0x447530-0x447ec0): per player two camera
   records (0x539018, 0x64 bytes) of which one is current, switched at once or blended over 12.5 steps;
   each step the current one follows its car (Camera_Step 0x4219b0) and each frame the drawn camera is
   blended between the last two steps' (Camera_Frame 0x422140). Kinds: 1, 2 and 10 the interior views,
   3 the cockpit, 4-6 the chase cameras (4 the profile's settings, 5 a far one, 6 the 45 degree view), 7 the
   trackside cameras, 8 a switch's blend. Ported: the chase cameras, the interior views and the trackside
   cameras. */
#pragma once
#include "sim/car.h"
#include "world/obj.h"
#include <stdbool.h>
#include <stdint.h>

void Camera_Replace(int p);   /* 0x421d80: the camera put back at its car */
void Camera_Reset(int player);                            /* the port: both records cleared */
void Camera_Select(int player, int kind, bool smooth);    /* 0x422fe0 / 0x421720 (the car's own, the nearest trackside) */
void Camera_SelectAt(int player, int kind, int idx, int car, bool smooth);   /* 0x421720 */
bool Camera_LoadTsc(const uint8_t *tsc, uint32_t size);   /* 0x48caa0: the stage's trackside cameras */
void Camera_Trackside(int player);                        /* 0x421930: the nearest trackside camera */
int Camera_TscIndex(int player);                          /* 0x421980 */
void Frame_Pitch(int32_t *m, uint32_t ang);                /* 0x422e70: m pitched about its first row */
/* 0x4224e0: a blend of two frames (forward by k0; mode 0 up by k1, mode 1 side by k1; position by kp) */
void Frame_Blend(int32_t *o, const int32_t *a, const int32_t *b, int32_t k0, int32_t k1, int32_t kp, int mode);
int Camera_Car(int player);                               /* 0x422fb0: the car the current camera follows */
int Camera_Kind(int player);                              /* 0x422f50 */
void Camera_Step(int player);                             /* 0x4219b0 */
/* 0x4218d0: the next kind the cycle may take (0x423970), blended to; returns it */
int Camera_Next(int player, int state);
void Camera_Frame(int player, int32_t frac, Obj *camera); /* 0x422140 */
void Camera_Projection(int player);                       /* 0x422d40 */
void Camera_Velocity(int player, int32_t v[3]);          /* 0x4220d0 */
void Camera_Position(int player, int32_t pos[3]);          /* the step's camera (0x538d28 + 0x38) */
int32_t Camera_Zoom(int player);                           /* its +0x54 (0x538d7c), read by the weather */
void Camera_Forward(int player, int32_t f[3]);             /* the step camera's forward axis (0x538d28 + 0x28) */
/* 0x4241d0: a hit's impulse (world axes) into the car's player's shake levels (0x539200: [9] from its
   third car axis when along, with its sign in [12]; [10] from its length); read by the steering
   wheels' force feedback (0x423ff0-0x424c00, DirectInput effects; gasm has none, not ported) */
void Camera_Shake(const Car *c, const int32_t imp[3], bool along);
int32_t Camera_ShakeLevel(int player, int k);              /* the port: int k of the player's record at 0x539200 */
