/* The cars' shadows (0x494b10-0x498590): a record of 0x2a4 bytes per car at *0x592734 (allocated by
   Cars_Load through 0x494b50, freed by 0x494b10), set up when the cars are placed (0x494bb0), built
   each frame (0x496e00) and drawn with each car (0x494db0). The skid marks (0x4657d0) take the wheels'
   ground points from it (+0x198-+0x1f8). */
#pragma once
#include <stdint.h>

void Shadow_Alloc(int ncars);   /* 0x494b50: zeroed records */
void Shadow_Free(void);         /* 0x494b10 */
/* 0x498570: car n's record (NULL while none are allocated; the original returns the base plus
   0x2a4 n whatever the base) */
uint8_t *Shadow_Get(int car);
