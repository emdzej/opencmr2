/* The selection's mode flags (see select.h). */
#include "game/select.h"
#include "game/gstate.h"
#include "game/mem.h"
#include "game/profile.h"
#include "crt.h"
#include "exe.h"
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

int Sel_CpuCount(void) { return gm8(0x52f2a9) & 0xf; }
void Sel_SetCpuCount(int n) { gw8(0x52f2a9, (uint8_t)((gm8(0x52f2a9) & 0xf0) | ((uint32_t)n & 0xf))); }
int Sel_CpuCar(int i) { return (int)gm32(0x52ea98 + 4u * (uint32_t)i); }
static void set_cpu(int i, int car) { gw32(0x52ea98 + 4u * (uint32_t)i, (uint32_t)car); }   /* 0x40df80 */
static int car_class(int car) { return (int)exe_u32(0x516b40 + 4u * (uint32_t)car); }   /* 0x40ee90 */

/* 0x40e180: a car 0-11 of a class neither given car (-1 none) has (the flags at 0x5337d4, by class) */
static int other_class(int a, int b)
{
    for (uint32_t i = 0; i < 6; i++) gw32(0x5337d4 + 4 * i, 0);
    if (a >= 0) gw32(0x5337d4 + 4u * (uint32_t)car_class(a), 1);
    if (b >= 0) gw32(0x5337d4 + 4u * (uint32_t)car_class(b), 1);
    int c = crt_rand() % 0xc;
    while (gm32(0x5337d4 + 4u * (uint32_t)car_class(c)) == 1) c = crt_rand() % 0xc;
    gw32(0x5337c4, (uint32_t)c);
    return c;
}

/* 0x40e210: another car of the group: of 0x11-0x13 when a is one of them, else of 0x0c-0x0f. The
   given cars are marked at 0x53376c + 4 car (0x533758 for the second group), which is the group's flags
   (0x53379c) only for cars of that group; others land on other variables, kept. */
static int other_group(int a, int b)
{
    bool g2 = a == 0x13 || a == 0x11 || a == 0x12;
    uint32_t base = g2 ? 0x533758 : 0x53376c;
    for (uint32_t i = 0; i < (g2 ? 3u : 4u); i++) gw32(0x53379c + 4 * i, 0);
    if (a >= 0) gw32(base + 4u * (uint32_t)a, 1);
    if (b >= 0) gw32(base + 4u * (uint32_t)b, 1);
    int c;
    do {
        int r = crt_rand();
        c = g2 ? r % 3 : r % 4;   /* (rand() is never negative: the original's & 0x80000003 is r % 4) */
    } while (gm32(0x53379c + 4u * (uint32_t)c) == 1);
    gw32(0x5337cc, (uint32_t)c);
    return c + (g2 ? 0x11 : 0xc);
}

void Sel_CpuCars(void)
{
    if (!Sel_CpuCount()) return;
    int car = (int)(Player_Word(0) & 0x3f);   /* 0x4086b0 (not in mode 4) */
    int cls = car_class(car);
    gw32(0x5337ec, (uint32_t)cls);
    gw32(0x533798, (uint32_t)car & 0xff);
    int x;
    if ((cls < 0 || 5 < cls) && cls != 0xc) {
        if (cls == 8 || cls == 0xd) {
            int k = cls == 8 ? 0x10 : 0x15;
            for (int i = 0; i < 6; i++) set_cpu(i, k);
            return;
        }
        set_cpu(0, car), set_cpu(3, car);
        x = other_group(car, -1);
        set_cpu(1, x), set_cpu(4, x);
        x = other_group(car, x);
    } else {
        if (gm8(0x52ea52)) {   /* 0x406320 */
            set_cpu(0, 0), set_cpu(3, 0), set_cpu(1, 8), set_cpu(4, 8), set_cpu(2, 10), set_cpu(5, 10);
            return;
        }
        gw32(0x5337d0, 0);
        set_cpu(0, car), set_cpu(3, car);
        x = other_class(car, -1);
        set_cpu(1, x), set_cpu(4, x);
        x = other_class(car, x);
    }
    gw32(0x5337c8, (uint32_t)x);
    set_cpu(2, x), set_cpu(5, x);
}
