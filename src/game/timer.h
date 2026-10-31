/* The clocks (docs/re/architecture.md#timing): Timer_Sample 0x4a9b70 latches the millisecond clock
   once a frame (timeGetTime() in the original; the port's app gives the frame's time), Timer_Now
   0x4a9b60 reads it, Timer_Centis 0x4a9b80 is a monotonic clock in 1/100 s built on it. */
#pragma once
#include <stdint.h>

void Timer_Sample(uint32_t ms);   /* 0x4a9b70 */
uint32_t Timer_Now(void);         /* 0x4a9b60 */
uint32_t Timer_Centis(void);      /* 0x4a9b80 */
