/* The projection and the fog (docs/re/render.md#view-and-fog). Arguments are 16.16 fixed point, as the
   game keeps them. */
#pragma once
#include <stdint.h>

/* Proj_Set 0x4a8890: x and y scale (times 0.5, 0x520b80), far and near plane; 0 keeps a value. A
   left-handed perspective (m22 = far / (far - near), m32 = -near far / (far - near), m23 = 1), also
   kept in 16.16 (Proj_Fixed) for the game's own use. The game's usual call: 2.337, 4.197, 250, 1. */
void Proj_Set(int32_t xs, int32_t ys, int32_t zfar, int32_t znear);
const int32_t *Proj_Fixed(void);   /* PTR_DAT_00520b78 + 0x23e4: the 16.16 matrix, 16 values */
int32_t Proj_Far(void);            /* DAT_005207b0: the far plane as last given */

void Fog_Set(int32_t start, int32_t end, int32_t start2, int32_t end2, uint32_t rgb);   /* 0x4bcaf0 */
void Fog_SetLinear(void);          /* 0x4bcbb0 */
void Fog_Off(void);                /* 0x4bcc20 */
void Fog_SetEnabled(uint32_t on);  /* 0x4bcad0: DAT_00816704, the fog-enable state World_Render sets */
uint32_t Fog_Enabled(void);        /* 0x4bcae0 */
