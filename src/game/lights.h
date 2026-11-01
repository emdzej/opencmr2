/* The start lights (0x463290-0x463d00, docs/re/world.md#start-lights): a row of 7 or 8 glows on the
   start gantry, placed from the stage's .bsp frame, lit step by step by the countdown and faded 0.2 a
   step. Their layout (0-3) comes from the rally and stage; the exe's tables give each light's place,
   colour (red or green), size and which glows are lit at each of the countdown's 7 steps. */
#pragma once
#include <stdint.h>

void Lights_Gantry(const uint8_t *bsp);   /* 0x463290: the gantry's frame, 12 words (NULL: none) */
void Lights_Load(void);                   /* 0x463360: the red and green textures */
void Lights_Setup(void);                  /* 0x463410: the glows, all off */
void Lights_Step(void);                   /* 0x463bd0 */
void Lights_Countdown(int step);          /* 0x463ce0: the countdown's step (above 7: 6) */
void Lights_Reset(void);                  /* 0x463d00: step 6, every glow off */
int Lights_Count(void);                   /* DAT_00547cd8 while built, else 0 */
int32_t Lights_Level(int i);              /* the light's level (0x547ae8 + 12 i) */
