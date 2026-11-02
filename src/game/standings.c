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
static void sort_by(uint32_t v, uint32_t order, int up, bool init)
{
    if (init)
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
    sort_by(POINTS, P_ORDER, 0, true);
    ties(POINTS, P_ORDER, P_PLACE, true);
    sort_by(TIMES, T_ORDER, 1, true);
    ties(TIMES, T_ORDER, T_PLACE, false);
}

/* 0x40d120: the k-th of `order` given the time of the `by`'s k-th (16.16 s), for the 16; the times
   sorted up from their last order, with their ties */
void Stand_AddTimes(const int8_t *order, const int8_t *by, const int32_t *t)
{
    for (int k = 0; k < 16; k++) {
        uint32_t a = TIMES + 4u * (uint32_t)order[k];
        gw32(a, gm32(a) + (uint32_t)t[by[k]]);
    }
    sort_by(TIMES, T_ORDER, 1, false);
    ties(TIMES, T_ORDER, T_PLACE, false);
}

/* 0x40d420: the points for a place */
static int place_points(int place)
{
    static const int8_t pts[6] = {12, 8, 6, 4, 2, 1};
    return place >= 0 && place < 6 ? pts[place] : 0;
}

/* 0x40cf30: the rally's points by the times' order, equal times sharing a place: 12, 8, 6, 4, 2, 1,
   the winners' counts up (0x5335a8), each driver's points kept (0x533598, the rest cleared); it stops
   after the first driver at sixth place (another tied there gets nothing). Then the points sorted down
   from their last order, with their ties. The original would read past the 16 if all were within five
   places; the port stops there. */
void Stand_AwardPoints(void)
{
    for (uint32_t i = 0; i < 16; i++) gw8(0x533598 + i, 0);
    int place = 0;
    for (int k = 0; k < 16 && place < 5; k++) {
        int8_t d = idx(T_ORDER, k);
        if (k > 0 && val(TIMES, idx(T_ORDER, k - 1)) < val(TIMES, d)) place = k;
        int pts = place_points(place);
        if (place == 0) gw8(COUNT + (uint32_t)d, (uint8_t)(gm8(COUNT + (uint32_t)d) + 1));
        gw8(0x533598 + (uint32_t)d, (uint8_t)pts);
        gw32(POINTS + 4u * (uint32_t)d, gm32(POINTS + 4u * (uint32_t)d) + ((uint32_t)pts << 16));
    }
    sort_by(POINTS, P_ORDER, 0, false);
    ties(POINTS, P_ORDER, P_PLACE, true);
}

/* 0x40d010: the rally's points taken back (times -65536.0, 0x5112e8); the counts stay */
void Stand_UndoPoints(void)
{
    for (uint32_t i = 0; i < 16; i++) {
        uint32_t a = POINTS + 4 * i;
        gw32(a, gm32(a) - ((uint32_t)(int32_t)(int8_t)gm8(0x533598 + i) << 16));
    }
}
