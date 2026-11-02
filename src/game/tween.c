/* The tweens (see tween.h). */
#include "game/tween.h"
#include "game/mem.h"
#include "game/timer.h"

/* the original's 32-bit products and quotients (wrapping, not undefined) */
static int32_t mul(int32_t a, int32_t b) { return (int32_t)((uint32_t)a * (uint32_t)b); }

enum { POOL = 0x521138, SLOT = 0x30, SLOTS = 0x20 };
#define S(i, off) (POOL + SLOT * (uint32_t)(i) + (off))

bool Tween_Active(uint32_t handle)
{
    uint8_t i = gm8(handle);
    if (gm32(S(i, 0x20)) != handle) return false;
    uint32_t steps = gm32(S(i, 4)) + (gm8(S(i, 0x14)) ? 3 : 0);
    return gm32(S(i, 0x28)) < steps;
}

int32_t Tween_Value(uint8_t i)
{
    uint32_t step = gm32(S(i, 0x28)), steps = gm32(S(i, 4));
    uint8_t b = (uint8_t)(steps <= step ? steps : step);
    int32_t delay = gms32(S(i, 0x24));
    if (delay) {
        gw32(S(i, 0x24), (uint32_t)(delay - 1));
        return gms32(S(i, 0x10));
    }
    int32_t u;
    switch (gm8(S(i, 0))) {
    case 0: u = b; break;
    case 1:
    case 4:
        if (steps - 0x11 < b) {
            /* the bounce: 17 offsets before 0x521130, by the direction, at the unit rate */
            u = gms8(0x521130 + (uint32_t)b - steps) * gms8(S(i, 1));
            gw32(S(i, 0x1c), 0x1000);
        } else if (gm8(S(i, 0)) == 1)
            u = (int32_t)(steps - b - 0x11);
        else
            u = (int32_t)((steps - b - 0x10) * (steps - b - 0x11)) / 2;
        break;
    case 2: u = (int32_t)((steps - b + 1) * (steps - b)) / 2; break;
    case 3: u = (int32_t)((uint32_t)(b + 1) * b / 2); break;
    case 5: u = gms32(S(i, 0xc)); break;
    default: return gms32(S(i, 0xc));
    }
    if (step < steps + 3) {
        uint32_t now = Timer_Now();
        int32_t d = (int32_t)(now - gm32(S(i, 0x2c)));
        if ((d < 0 ? -d : d) > 0x10) {
            gw32(S(i, 0x28), step + 1);
            gw32(S(i, 0x2c), Timer_Now());
        }
    }
    if (gm8(S(i, 0)) == 1 || gm32(S(i, 0x28)) < steps)
        return mul(gms32(S(i, 0x1c)), u) / 0x1000 + gms32(S(i, 8));
    gw8(S(i, 0), 5);
    return gms32(S(i, 0xc));
}

/* 0x4bc410: the first free slot (0 when none is) */
static uint8_t free_slot(void)
{
    for (int i = 0; i < SLOTS; i++)
        if (gm8(S(i, 0)) == 5) return (uint8_t)i;
    return 0;
}

/* 0x4bc3e0: the slot's number ORed with its first 24 bytes */
static uint8_t sum(uint8_t i)
{
    uint8_t v = i;
    for (uint32_t k = 0; k < 0x18; k++) v |= gm8(S(i, k));
    return v;
}

void Tween_Start(uint32_t handle, int curve, int steps, int delay, int32_t from, int32_t to, bool hold)
{
    if (gm32(0x816290)) delay = 0, hold = false, from = to;   /* (nothing sets it) */
    int32_t tri = (steps + 1) * steps / 2, rate;
    uint8_t old = gm8(handle);
    if (old < SLOTS && gm32(S(old, 0x20)) == handle) gw8(S(old, 0), 5);
    uint8_t i = free_slot();
    gw8(handle, i);
    int32_t base;
    switch (curve) {
    case 0: base = from, rate = mul(to - from, 0x1000) / steps; break;
    case 1: rate = mul(from - to, 0x1000) / steps, base = to, steps += 0x11; break;
    case 2: base = to, rate = mul(from - to, 0x1000) / tri; break;
    case 3: base = from, rate = mul(to - from, 0x1000) / tri; break;
    case 4: rate = mul(from - to, 0x1000) / tri, base = to, steps += 0x11; break;
    default: return;
    }
    gw32(S(i, 8), (uint32_t)base);
    gw32(S(i, 0x1c), (uint32_t)rate);
    gw8(S(i, 0), (uint8_t)curve);
    gw32(S(i, 4), (uint32_t)steps);
    gw32(S(i, 0x24), (uint32_t)delay);
    gw8(S(i, 0x14), hold);
    gw8(S(i, 1), (uint8_t)(rate < 0 ? -1 : 1));
    gw32(S(i, 0x10), (uint32_t)from);
    gw32(S(i, 0xc), (uint32_t)to);
    gw32(S(i, 0x20), handle);
    gw32(S(i, 0x28), 0);
    gw8(S(i, 0x18), sum(gm8(handle)));
}

void Tween_ClearAll(void)
{
    for (uint32_t k = 0; k < SLOT * SLOTS; k++) gw8(POOL + k, 0);
    for (int i = 0; i < SLOTS; i++) gw8(S(i, 0), 5);
}

void Tween_Stop(uint32_t handle) { gw8(S(gm8(handle), 0), 5); }
