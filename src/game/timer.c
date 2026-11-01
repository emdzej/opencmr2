/* The clocks (see timer.h). */
#include "game/timer.h"
#include "game/mem.h"
#include "fixmath.h"
#include <math.h>

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

static uint32_t wall;
void Time_Set(uint32_t ms) { wall = ms; }
uint32_t Time_Get(void) { return wall; }

static float fps = 25.0f;   /* DAT_005210a0 (DAT_005210b0) */
static uint32_t f_now, f_last;
static bool f_started;

void Frame_Stats(void)
{
    if (!f_started) f_started = true, f_now = Timer_Now();
    f_last = f_now;
    f_now = Timer_Now();
    fps = 1000.0f / (float)(int32_t)(f_now - f_last);
}

float Frame_Fps(void) { return fps; }

void Frame_SetRate(int32_t rate)
{
    int32_t r = fx_mul(rate, 0xa3d);
    if (r < 0x9999) r = 0x9999;
    gw32(0x51bd40, (uint32_t)r);
    gw32(0x51bd3c, (uint32_t)(int32_t)(0x100000000LL / r));
}

int32_t Frame_Scale(void) { return gms32(0x51bd3c); }

void Frame_RateStep(void)
{
    double v = (double)(fps * 65536.0f);
    Frame_SetRate(isfinite(v) && fabs(v) < 2147483648.0 ? (int32_t)nearbyint(v) : INT32_MIN);
}
