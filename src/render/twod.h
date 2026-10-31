/* The 2D queues (docs/re/render.md#2d): textured sprites and flat triangles, each in four layers, queued
   during a frame and drawn per layer as pre-transformed (FVF 0x1c4) primitives. Used by the front end,
   the HUD and the fonts. */
#pragma once
#include "render/tex.h"
#include <stdint.h>

typedef struct { int16_t x, y, w, h; } Rect16;

/* Sprite_Init 0x4a3260 / Tri_Init 0x4bb610: empty queues, accepting. */
void Sprite_Init(void);
void Tri_Init(void);

/* Sprite_Add 0x4a3290: src is the rectangle in the texture (pixels), dst on the screen; layer 1-4 (others
   are dropped); rgba the colour bytes; angle in degrees (turned into 4,096 units per turn), about centre
   (x, y; NULL gives 0, 0, not the sprite's middle; -1, -1, -1 its middle); flip bit 1 (or 4) mirrors u,
   bit 2 (or 4) v. Up to 1,023 a layer. uv2 is stored and not used. */
void Sprite_Add(const Rect16 *src, const Rect16 *dst, Tex *tex, int layer, int16_t angle_deg,
                const int32_t centre[3], const Rect16 *uv2, const uint8_t rgba[4], uint32_t flip);
void Sprite_Flush(int layer);                           /* 0x4a3650 */

/* Tri_Add 0x4bb650: corners in 16.16 screen pixels (x, y), one colour (rgba bytes); layer 2, 3, 4, or
   any other value for the first queue. Up to 1,024 a layer (but see twod.c). */
void Tri_Add(const int32_t a[2], const int32_t b[2], const int32_t c[2], const uint8_t rgba[4], int layer);
void Tri_Flush(int layer);                              /* 0x4bb850 */
uint32_t Twod_Prims(void);                              /* DAT_0059ce20: 2D triangles drawn */
