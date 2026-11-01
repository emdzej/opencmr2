/* The stage's light colours and the cells' vertex relighting (docs/re/world.md#stage-lights): a low
   and a high colour make two 50-entry ramps, one for the terrain and scenery (opaque colours) and one
   for the tree shadows (the shadow colour, alpha rising with the contrast); each vertex's 16.16 intensity
   (the meshes' +0x34 arrays) picks its colour. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

/* 0x4b3740: the low colour (R, G, B, A); the device ambient (0x8b) set to it. */
void Light_SetLow(const uint8_t c[4], bool shadow_from_low);
void Light_GetLow(uint8_t c[4]);   /* 0x4b3840 */
void Light_GetHigh(uint8_t c[4]);  /* 0x4b3850: its bytes (R, G, B; the fourth unset) */
/* 0x4b3f90: the high colour, 16.16 per channel. */
void Light_SetHigh(const int32_t c[3], bool shadow_from_low);
/* 0x4918d0: low (255, 255, 255, 255) and high 255 (the shadow choice by the rally, 3 otherwise); the
   sun first: a directional light under the world (Dlight_Add). */
void Light_Defaults(void);
extern struct Obj *g_sun;   /* 0x5920b0: the sun's light object */
uint32_t Light_TerrainRamp(int32_t intensity);   /* 0x4b3b40 */
/* 0x4b3ae0: the same ramp from its byte copy (0x6e0b34: R, G, B, 0xff in memory order) */
uint32_t Light_ByteRamp(int32_t intensity);
uint32_t Light_ShadowRamp(int32_t intensity);    /* 0x4b3ba0 */
extern uint32_t g_shadow_rgb;   /* 0x6e0098: bytes R, G, B */
extern int32_t g_shadow_alpha;  /* 0x6e0b38: 16.16, at most 255 */

/* The per-cell caches (0x6e019c, 0x6dfd90), made by Tre_Load. */
void Relight_Alloc(void);
void Relight_Free(void);
/* Section_Relight 0x4b3c00: a cell whose cached colours differ from the lights gets its terrain,
   scenery and tree shadows recoloured from the ramps and uploaded again. */
void Section_Relight(int cell);
void Light_RelightAll(void);   /* 0x4b3f20 */
