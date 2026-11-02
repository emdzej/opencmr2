/* The championship standings (game/standings.c) on made-up results: a stage's times added in its
   order, the rally's points by place (equal times sharing one, the first tied at sixth only), the
   ties broken for a player, the points taken back. Skips
   (exit 0) without data (the game state needs the exe). */
#include "exe.h"
#include "game/mem.h"
#include "game/standings.h"
#include "vfs.h"
#include "vfs_host.h"
#include <stdio.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static int32_t pts(int i) { return gms32(0x533558 + 4u * (uint32_t)i) >> 16; }
static int8_t t_order(int k) { return (int8_t)gm8(0x533638 + (uint32_t)k); }
static int8_t p_order(int k) { return (int8_t)gm8(0x533538 + (uint32_t)k); }

int main(void)
{
    char err[256];
    if (!vfs_mount_default() || !exe_init(err, sizeof err)) {
        printf("standings_test: no game data, skipped\n");
        return 0;
    }
    CHECK(Mem_Init());
    gw32(0x52afb4, (gm32(0x52afb4) & 0xfffe1fffu) | 1u << 13);   /* one player (15) */
    /* the orders as the championship's start leaves them (race.c, 0x40d100) */
    for (uint32_t i = 0; i < 16; i++) gw8(0x533638 + i, (uint8_t)i), gw8(0x533538 + i, (uint8_t)i);
    /* drivers 0..15, the stage's order: 15 (the player) first, then 3, then 4 and 5 equal, then 0.. */
    int8_t order[16] = {15, 3, 4, 5, 0, 1, 2, 6, 7, 8, 9, 10, 11, 12, 13, 14};
    int8_t by[16];
    int32_t t[16];
    for (int k = 0; k < 16; k++) by[k] = (int8_t)k, t[k] = (100 + k) << 16;
    t[3] = t[2];   /* 4 and 5 equal */
    Stand_AddTimes(order, by, t);
    CHECK(t_order(0) == 15 && t_order(1) == 3);
    CHECK((t_order(2) == 4 && t_order(3) == 5) || (t_order(2) == 5 && t_order(3) == 4));
    Stand_AwardPoints();
    CHECK(pts(15) == 12 && pts(3) == 8 && pts(4) == 6 && pts(5) == 6);
    CHECK(pts(0) == 2 && pts(1) == 1 && pts(2) == 0);   /* places 4 and 5 (0-based) */
    CHECK(gm8(0x5335a8 + 15) == 1);
    CHECK(p_order(0) == 15 && p_order(1) == 3);
    Stand_UndoPoints();
    CHECK(pts(15) == 0 && pts(3) == 0 && pts(4) == 0);
    /* equal points: the player before a computer at the same count */
    for (uint32_t i = 0; i < 16; i++) gw32(0x533558 + 4 * i, 0), gw8(0x5335a8 + i, 0);
    Stand_SetPoints(2, 10), Stand_SetPoints(15, 10);
    Stand_Sort();
    CHECK(p_order(0) == 15 && p_order(1) == 2);
    gw8(0x5335a8 + 2, 1);   /* a win more: first */
    Stand_Sort();
    CHECK(p_order(0) == 2 && p_order(1) == 15);
    Stand_SetTime(0, 12345);
    CHECK(gms32(0x533658) == (int32_t)(((int64_t)12345 * 0x28f5c28) >> 16));
    printf("standings_test: %s\n", fails ? "FAILED" : "ok");
    return fails ? 1 : 0;
}
