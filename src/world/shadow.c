/* The cars' shadows (see shadow.h). */
#include "world/shadow.h"
#include <stdlib.h>

enum { REC = 0x2a4 };

static uint8_t *recs;   /* 0x592734 */
static int count;       /* 0x592738 */

/* (the original allocates again on each load without freeing; the port frees the old block) */
void Shadow_Alloc(int n)
{
    free(recs);
    recs = calloc(n > 0 ? (size_t)n : 1, REC);
    count = n;
}

void Shadow_Free(void)
{
    free(recs);
    recs = NULL;
    count = 0;
}

uint8_t *Shadow_Get(int car) { return recs ? recs + (size_t)REC * (size_t)car : NULL; }
