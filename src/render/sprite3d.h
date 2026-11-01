/* The camera-facing sprites (0x4b1150-0x4b1530, docs/re/render.md#sprites): up to 800 a frame,
   queued by the effects (the glows, dust, sparks) and drawn by World_Render after the triangle
   queues, in runs of the same texture, z writes off. */
#pragma once
#include "render/tex.h"
#include "world/obj.h"
#include <stdint.h>

/* A sprite as the callers give it (0x24 bytes in the original): the centre (16.16), the four extents
   (+0x0c-+0x18: the corners are (-e1, e0), (-e1, e2), (-e3, e0), (-e3, e2) along the camera's right
   and up), the colour R, G, B, A, the turn about the view (4,096 a turn), a shade (+0x22) and flags
   (+0x23: 1 the texture mirrored, 2 lit: the colour times the light ramp, between its full (shade 0)
   and its dark end (shade 256)). */
typedef struct {
    int32_t pos[3];
    int32_t e[4];
    uint8_t rgba[4];
    uint16_t angle;
    uint8_t shade, flags;
} Sprite3d;

void Sprite3d_Init(void);                                  /* 0x4b1150 */
void Sprite3d_Free(void);                                  /* 0x4b1500 */
void Sprite3d_Add(const Sprite3d *s, const Tex *tex);      /* 0x4b11c0 */
/* Render_DrawIndexed 0x4b1530: every queued sprite turned to the camera, then emptied. */
void Sprite3d_Draw(const Obj *camera);
int Sprite3d_Count(void);                                  /* DAT_006dd784 */
