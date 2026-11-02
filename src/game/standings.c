/* The championship standings (see standings.h). */
#include "game/standings.h"
#include "game/gstate.h"
#include "game/mem.h"

enum { POINTS = 0x533558, P_ORDER = 0x533538, P_PLACE = 0x533548, TIMES = 0x533658, T_ORDER = 0x533638,
       T_PLACE = 0x533648, COUNT = 0x5335a8 };

/* 0x40d090: the points times 65536.0 (0x5112e0), rounded: exact for whole points */
void Stand_SetPoints(int i, int points) { gw32(POINTS + 4u * (uint32_t)i, (uint32_t)points << 16); }

/* 0x40d3f0: bits 16-47 of the time times 0x28f5c28 (655.36 x 65536) */
void Stand_SetTime(int i, uint32_t cs)
{
    gw32(TIMES + 4u * (uint32_t)i, (uint32_t)(((int64_t)(int32_t)cs * 0x28f5c28) >> 16));
}

static int8_t idx(uint32_t order, int i) { return (int8_t)gm8(order + (uint32_t)i); }
static int32_t val(uint32_t v, int i) { return gms32(v + 4u * (uint32_t)i); }

/* 0x40d520 on the game state: the order set to 0-15 and sorted by the values, down (0) or up (1), by
   exchanges */
static void sort_by(uint32_t v, uint32_t order, int up)
{
    for (int i = 0; i < 16; i++) gw8(order + (uint32_t)i, (uint8_t)i);
    for (int i = 0; i < 16; i++)
        for (int j = i + 1; j < 16; j++) {
            int8_t a = idx(order, i), b = idx(order, j);
            if ((up == 0 && val(v, a) < val(v, b)) || (up == 1 && val(v, b) < val(v, a)))
                gw8(order + (uint32_t)i, (uint8_t)b), gw8(order + (uint32_t)j, (uint8_t)a);
        }
}

/* a player (15 down to 16 - players) */
static bool player(int k) { return 0x10 - k <= (int)Gs_Players(); }

/* 0x40d270 (points, with the count) and 0x40d180 (times): the places from the order, then within each
   run of equal values from each place on, a player after a computer at the same count comes first,
   and for the points a higher count first (the places swapped with them) */
static void ties(uint32_t v, uint32_t order, uint32_t place, bool count)
{
    for (int i = 0; i < 16; i++) gw8(place + (uint32_t)idx(order, i), (uint8_t)i);
    for (int i = 0; i < 16; i++)
        for (int j = i + 1; j < 16; j++) {
            int8_t a = idx(order, i), b = idx(order, j);
            if (val(v, a) != val(v, b)) break;
            int8_t ca = count ? (int8_t)gm8(COUNT + (uint32_t)a) : 0, cb = count ? (int8_t)gm8(COUNT + (uint32_t)b) : 0;
            if ((ca == cb && player(b) && !player(a)) || ca < cb) {
                gw8(place + (uint32_t)a, (uint8_t)j), gw8(place + (uint32_t)b, (uint8_t)i);
                gw8(order + (uint32_t)i, (uint8_t)b), gw8(order + (uint32_t)j, (uint8_t)a);
            }
        }
}

/* 0x40d0c0: the points down, the times up, each with its ties */
void Stand_Sort(void)
{
    sort_by(POINTS, P_ORDER, 0);
    ties(POINTS, P_ORDER, P_PLACE, true);
    sort_by(TIMES, T_ORDER, 1);
    ties(TIMES, T_ORDER, T_PLACE, false);
}
