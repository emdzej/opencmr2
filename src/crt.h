/* The parts of the original's C runtime (MSVC 6) whose results the game depends on. */
#pragma once
#include <stddef.h>
#include <stdint.h>

/* rand / srand: the runtime's linear congruential generator (state * 214013 + 2531011, bits 16-30),
   one sequence for the whole game (the original seeds it from the clock in FUN_004d15e0,
   and with fixed values in places; 64 functions draw from it, the font print among them). */
void crt_srand(uint32_t seed);
int crt_rand(void);
uint32_t crt_seed(void);   /* for the differential tests: the state */

/* qsort (msvcrt.dll's: its order of equal elements) */
void crt_qsort(void *base, size_t n, size_t w, int (*cmp)(const void *, const void *));
