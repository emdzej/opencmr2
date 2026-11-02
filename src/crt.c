/* The original's C runtime parts (see crt.h). */
#include "crt.h"

static uint32_t holdrand = 1;   /* the runtime's initial state */

void crt_srand(uint32_t seed) { holdrand = seed; }
uint32_t crt_seed(void) { return holdrand; }

int crt_rand(void)
{
    holdrand = holdrand * 214013u + 2531011u;
    return (int)(holdrand >> 16 & 0x7fff);
}
