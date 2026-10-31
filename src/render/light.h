/* Lighting modes (0x4b6f00): which of the first three lights are on, which ambient colour, and whether
   the vertex colour feeds the material. The lights and the two ambient colours are set up by the stage
   and car lighting code (0x4b2e50, 0x4b2f80, 0x4b34d0-0x4b35b0, 0x4ad8d0; step 5). */
#pragma once
#include <stdint.h>

extern uint32_t g_ambient_a;     /* DAT_006e0bac (modes 0, 1, 4, 5) */
extern uint32_t g_ambient_b;     /* DAT_006e020c (modes 2, 3) */
extern uint32_t g_lights_keep;   /* DAT_005210c0: when 0, lights 1 and 2 are switched off after any mode */

void Light_SetMode(uint32_t mode);   /* 0x4b6f00 */
uint32_t Light_Mode(void);           /* 0x4b7200 */
