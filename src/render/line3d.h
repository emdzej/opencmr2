/* The 3D line queue (0x72d680, 200 lines of 0x24 bytes, count 0x730fc8): world-space lines with a
   colour at each end, drawn untextured near the end of the 3D (World_Render). The cars' aerials are
   its only users. */
#pragma once
#include <stdint.h>

/* 0x4bb430: a line from a to b (16.16), the colours as R, G, B, A bytes; past 200 dropped */
void Line3d_Add(const int32_t a[3], const int32_t b[3], const uint8_t ca[4], const uint8_t cb[4]);
void Line3d_Flush(void);   /* 0x4bb4c0 */
