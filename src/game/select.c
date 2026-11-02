/* The selection's mode flags (see select.h). */
#include "game/select.h"
#include "game/gstate.h"
#include "game/mem.h"
#include "game/profile.h"
#include "game/opponents.h"
#include "pending.h"
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

/* 0x4072d0: the championship or single rally on to its next stage (the stage word 0x52f2ac): the
   stage one on; past the rally's last (0x407150) or the super special (10) the rally ends: the single
   rally's over (false), the championship's next rally from stage 0, after the eighth only when a
   player's points place is under 3 (the final), never after a ninth; before an odd rally's last stage
   the super special (10) instead. True with a stage to drive (the mode flags again, 0x408020). */
/* 0x4074a0: in mode 5 (the arcade) the location's next stage (selection bits 12-13, one up, two
   bits round): true while it is below the fourth and the location (bits 10-11) is not the third */
bool Sel_ArcadeNext(void)
{
    if (Gs_Mode() != 5) return false;
    uint32_t w = gm32(0x52f2ac);
    w = (w & ~0x3000u) | ((w & 0x3000u) + 0x1000u & 0x3000u);
    gw32(0x52f2ac, w);
    return (w & 0x3000u) < 0x3000u && (w & 0xc00u) != 0x800u;
}

bool Sel_NextStage(void)
{
    uint32_t m = Gs_Mode(), l = Gs_Mode2();
    if (Gs_ModeBit19()) {
        PENDING_NOTE(0x4072d0, "the network rally's next stage (the stage table 0x52ea68)");
        return false;
    }
    uint32_t s = gm32(0x52f2ac), n = (s & 1) + (l == 0 ? 4u : l == 1 ? 8u : 10u);   /* 0x407150 */
    if (m != 0 && m != 1) return false;
    s = (s & ~0x3e0u) | ((s & ~0x1fu) + 0x20 & 0x3e0);
    gw32(0x52f2ac, s);
    uint32_t st = s >> 5 & 0x1f;
    if (st == n || (s & 0x3e0) > 0x140) {
        if (m == 1) return false;
        s = (s & ~0x1fu) | ((s + 1) & 0x1f);
        gw32(0x52f2ac, s);
        if ((s & 0x1f) == 8) {
            for (int i = 0; i < (int)Gs_Players(); i++)
                if ((int8_t)gm8(0x533548 + (uint32_t)Opp_PlayerSlot(i)) < 3) {
                    Sel_ModeFlags();
                    return true;
                }
            return false;
        }
        if ((s & 0x1f) == 9) return false;
        gw32(0x52f2ac, s & 0xfffffc1fu);
    } else if (st + 1 == n && (s & 1)) {
        gw32(0x52f2ac, (s & 0xfffffd5fu) | 0x140);
        Sel_ModeFlags();
        return true;
    }
    Sel_ModeFlags();
    return true;
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

/* 0x4084c0: a player record made a CPU driver: bits 0-17 cleared, the CPU's number in bits 14-17, the
   second word cleared */
static void cpu_driver(int p, int n)
{
    uint32_t a = 0x531350 + 0x30u * (uint32_t)(p & 0xff);
    gw32(a, ((uint32_t)n & 0xf) << 0xe | (gm32(a) & 0xfffc0000u));
    gw32(a + 4, 0);
}

/* the challenge's rounds: 1, 2, 4 and 8 matches of three words (the drivers in bits 0-4 and 5-9, 0x1f
   none), the final first */
static const uint32_t round_at[5] = {0, 0x52f2b8, 0x52f2c4, 0x52f2dc, 0x52f30c};

/* 0x4069c0: the challenge's draw. The rounds emptied, the first round from the depth (0x52f2b4 bits
   0-2: 1 the final, 4 sixteen drivers) filled with the players and (with an opponents' level) CPU
   drivers at random places; then two players never meet in the first round while some match has two
   CPU drivers, and in each match the lower player comes first */
void Sel_ChallengeDraw(void)
{
    uint32_t s = gm32(0x52f2b4), depth = s & 7;
    int matches = 0;   /* the original leaves it unset for other depths */
    s = (s & 0xffff03ffu) | 0x200;
    switch (depth) {   /* bits 3-5: the rounds to go */
    case 1: matches = 1, s = (s & 0xffffffe7u) | 0x20; break;
    case 2: matches = 2, s = (s & 0xffffffdfu) | 0x18; break;
    case 3: matches = 4, s = (s & 0xffffffd7u) | 0x10; break;
    case 4: matches = 8, s = (s & 0xffffffcfu) | 0x08; break;
    }
    gw32(0x52f2b4, s);
    uint32_t players = Gs_Players();
    for (int r = 4; r >= 2; r--)
        for (int i = 0; i < 1 << (r - 1); i++) {
            uint32_t a = round_at[r] + 12u * (uint32_t)i;
            gw32(a, gm32(a) & ~0x400u);
            gw32(a + 4, 0), gw32(a + 8, 0);
            gw32(a, (gm32(a) & ~0x1800u) | 0x3ff);
        }
    gw32(0x52f2bc, 0), gw32(0x52f2c0, 0);
    gw32(0x52f2b8, (gm32(0x52f2b8) & 0xffffe3ffu) | 0x3ff);
    if (depth >= 1 && depth <= 4) {
        uint32_t n = (gm32(0x52afb4) >> 0x14 & 7) ? 2u << (depth - 1) : players;   /* 0x405dd0 */
        int cpu = 0;
        for (uint32_t d = 0; d < n; d++) {
            if (players <= d) cpu_driver((int)d, cpu++);
            for (;;) {
                int k = depth == 1 ? crt_rand() & 1 : crt_rand() % (2 << (depth - 1));
                uint32_t a = round_at[depth] + 12u * (uint32_t)(k / 2);
                bool first = depth == 1 ? (k & 1) : !(k & 1);   /* the final's sides the other way */
                if (first) {
                    if ((gm32(a) & 0x1f) == 0x1f) { gw32(a, (gm32(a) & ~0x1fu) | (d & 0x1f)); break; }
                } else if ((gm32(a) & 0x3e0) == 0x3e0) {
                    gw32(a, (gm32(a) & 0xfffffc1fu) | (d & 0x1f) << 5);
                    break;
                }
            }
        }
    }
    /* the original walks the sixteen drivers' round whatever the depth */
    for (int i = 0; i < matches; i++) {
        uint32_t a = 0x52f30c + 12u * (uint32_t)i, e = gm32(a);
        if ((e & 0x1f) >= players || (e >> 5 & 0x1f) >= players || i + 1 >= matches) continue;
        for (int j = i + 1; j < matches; j++) {
            uint32_t b = 0x52f30c + 12u * (uint32_t)j, q = gm32(b);
            if ((q & 0x1f) >= players && (q >> 5 & 0x1f) >= players) {
                e = gm32(a);
                gw32(b, (e >> 5 & 0x1f) | (q & 0xffffffe0u));
                gw32(a, (q & 0x1f) << 5 | (e & 0xfffffc1fu));
            }
        }
    }
    for (int r = 4; r >= 1; r--)
        for (int i = 0; i < 1 << (r - 1); i++) {
            uint32_t a = round_at[r] + 12u * (uint32_t)i, e = gm32(a), x = e & 0x1f, y = e >> 5 & 0x1f;
            if (x < players && y < players && y < x) gw32(a, (e & 0xfffffc00u) | y | x << 5);
        }
}

void Sel_ChallengeStage(void)
{
    uint32_t s = gm32(0x52f2ac), r = gm32(0x52f2b4) >> 0x10 & 0xf;
    s = (s & ~0x1fu) | (r & 0x1f);          /* 0x4068b0 */
    s = (s & 0xfffffc1fu) | 10u << 5;        /* 0x4068e0 */
    gw32(0x52f2ac, s);
}
