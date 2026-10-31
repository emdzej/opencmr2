/* The clocks (see timer.h). */
#include "game/timer.h"

static uint32_t now;                       /* DAT_00663ee0 */
static int started;                        /* DAT_00663ecc */
static uint32_t last, total, high;         /* DAT_00663ed4, DAT_00663ed8, DAT_00663edc */

void Timer_Sample(uint32_t ms) { now = ms; }
uint32_t Timer_Now(void) { return now; }

/* The tenth of the millisecond clock, summed by its changes; never below the highest returned */
uint32_t Timer_Centis(void)
{
    uint32_t c = Timer_Now() / 10;
    if (!started) {
        total = high = 0;
        started = 1;
        last = c;
    }
    if (c - last != 0) {
        total += c - last;
        last = c;
        if (total < high) return total = high;
        return high = total;
    }
    last = c;
    return total;
}
