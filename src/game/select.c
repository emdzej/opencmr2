/* The selection's mode flags (see select.h). */
#include "game/select.h"
#include "game/gstate.h"
#include "game/mem.h"
#include <string.h>

void Sel_ModeFlags(void)
{
    uint32_t s = gm32(0x52f2ac), m = Gs_Mode();
    s &= ~0x2000000u;
    if ((s >> 5 & 0x1f) == 10) s |= 0x2000000;   /* stage 11 */
    s &= ~0x10000000u;
    if (m == 7 || m == 3) s |= 0x10000000;
    s &= ~0x8000000u;
    if ((s & 0x2000000) && !(s & 0x10000000)) s |= 0x8000000;
    s &= ~0x1000000u;
    if (m == 5 || m == 6 || m == 7 || m == 11 || m == 12) s |= 0x1000000;
    s &= ~0x4000000u;
    if (m == 5 || m == 6) s |= 0x4000000;
    s &= 0x7fffffffu;
    if (m == 11 || m == 12) s |= 0x80000000u;
    s &= ~0x400000u;
    if (s & 0xc000000) s |= 0x400000;
    s &= ~0x20000000u;
    if (m <= 2 || m == 8 || m == 9 || m == 10) s |= 0x20000000;
    if (!(s & 0x1000000)) s |= 0x40000000;
    s &= ~0x200000u;
    if ((s & 0x20000000) || m == 4) s |= 0x200000;
    uint32_t any = s & 0x31000000;
    s &= ~0x800000u;
    if (any) s |= 0x800000;
    gw32(0x52f2ac, s);
}

/* 0x409a30: the players' records (0x531778), the stage results (0x531bb8, 0x531e00) and their slots
   (-1), the results' places (0x5317f0, 0x80 apart), then 0x40e890 and the flags */
void Sel_Reset(void)
{
    memset(gm(0x531778), 0, 0x100 * 4);
    memset(gm(0x531bb8), 0, 0x38 * 4);
    memset(gm(0x531e00), 0, 0x30 * 4);
    for (uint32_t a = 0x531e08; a < 0x531ec8; a += 0x18) {
        gw32(a - 8, 0xffffffffu);
        gw32(a, 0);
        gw32(a - 4, 0xffffffffu);
    }
    for (uint32_t a = 0x5317f0; a < 0x531b70; a += 0x80) gw32(a, 0xffffffffu);
    gw32(0x531ce8, 0);
    gw32(0x531df8, 0xffffffffu);
    gw32(0x535b8c, 0);   /* 0x40e890 */
    Sel_ModeFlags();
}
