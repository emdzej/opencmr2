/* Car_Load 0x457000 (see car_load.h). */
#include "game/car_load.h"
#include "game/race.h"
#include "game/gstate.h"
#include "game/mem.h"
#include "game/select.h"
#include "game/sectors.h"
#include "game/timer.h"
#include "crt.h"
#include "sim/progress.h"
#include "fixmath.h"
#include "bfl.h"
#include "c3d.h"
#include "exe.h"
#include "pending.h"
#include "sim/car.h"
#include "sim/damage.h"
#include "sim/pose.h"
#include "sim/sim.h"
#include "game/profile.h"
#include "game/skids.h"
#include "world/obj.h"
#include "world/stage.h"
#include "render/tex.h"
#include "render/mesh.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static C3d models[CAR_MAX];
static uint8_t *cins[CAR_MAX];     /* 0x5428c4 region: the cars' .cin files */
static uint32_t cin_size[CAR_MAX];
static BflSlot textures[CAR_MAX];   /* the slots' texture archives (0x542634 + 0x24 n) */
static C3d far_models[CAR_MAX];     /* 0x5428c4 + 4 n: the far models */
static uint8_t *far_files[CAR_MAX];   /* 0x542944 + 4 n */
static BflSlot far_tex[CAR_MAX];    /* 0x542af4 + 0x18 n */

static const char *car_name(int id) { return exe_str(exe_u32(0x516bb0 + 4u * (uint32_t)id)); }   /* 0x40ee60 */

int Car_IdByName(const char *name)
{
    for (int id = 0; id < 22; id++)
        if (!strncasecmp(car_name(id), name, 3)) return id;
    return -1;
}

static void put(uint32_t at, const char *s, size_t n) { for (size_t i = 0; i < n; i++) gw8(at + (uint32_t)i, (uint8_t)s[i]); }

/* 0x457c50: the detail letters (A the most) by the detail setting (0x52afd4, 0 most); with option
   bit 16 of 0x52e890 and the texture memory for it, the most for all */
void Car_LodTable(void)
{
    if (gm32(0x52e890) & 0x10000) {
        PENDING_NOTE(0x4b7560, "Tex_MemoryFor (taken as enough)");
        put(0x51a3d0, "AAAACCCAA", 9);
        put(0x51a3d0 + 0xb, "AAAAAAAAA", 9);
        gw8(0x5429c4, 'A'), gw8(0x5429c5, 'A');
        return;
    }
    uint32_t d = gm32(0x52afd4) & 3;
    if (d == 0) {
        put(0x51a3d0, "AAACACDAC", 9);
        put(0x51a3d0 + 0xb, "ADDD", 4);
        put(0x51a3d0 + 0x11, "DEF", 3);
        gw8(0x5429c4, 'A'), gw8(0x5429c5, 'C');
    } else if (d == 2) {
        put(0x51a3d0, "CCDDDEFDD", 9);
        put(0x51a3d0 + 0xb, "CDDDDDDEF", 9);
        gw8(0x5429c4, 'D'), gw8(0x5429c5, 'D');
    }
}

/* the tail of 0x457000: a CPU car of modes 5, 6, 8, 9, 11 and 12 (not with option 16) has a far model,
   <car><letter><set>.C3D with its .bfl under the world root; its body (type 5) and wheels (1-4) into
   +0x724-+0x734 and hung under the main model. (0x4b2d90's car number in the meshes and 0x4b45d0's
   shadows, not ported for the main model either.) */
static bool far_load(int n, const char *name, char letter, int set, Car *c)
{
    for (int i = 0; i < 5; i++) c->far[i] = NULL;
    if (!letter || (gm32(0x52e890) & 0x10000)) return true;
    gw8(0x542651 + 0x24u * (uint32_t)n, (uint8_t)letter);
    char path[96];
    snprintf(path, sizeof path, "Game\\Cars\\%s%c%d.C3D", name, letter, set);
    free(far_files[n]);
    far_files[n] = File_Load(path, 0);
    if (!far_files[n]) return false;
    uint32_t size = File_LastSize();
    snprintf(path, sizeof path, "Game\\Cars\\%s%c%d.bfl", name, letter, set);
    Bfl_Mount(&far_tex[n], path);
    C3d *m = &far_models[n];
    if (!C3d_Load(m, far_files[n], size, g_tex_count)) return false;
    C3d_Textures(m->data + m->slots, m->data + m->paths, m->ntex, m->kind, &far_tex[n], "");
    C3d_Build(m);
    Obj *root = C3d_Objects(m, &g_world);
    if (!root) return false;
    static const uint32_t types[5] = {5, 1, 2, 3, 4};
    for (int i = 0; i < 5; i++) c->far[i] = Obj_FindType(root, types[i]);
    for (int i = 0; i < 5; i++)
        if (c->far[i]) Obj_Reparent(c->far[i], c->main);
    if (letter != 'A') PENDING_NOTE(0x42cb90, "Car_Textures (the far wheels)");
    return true;
}

bool Car_Load(int n)
{
    uint32_t mode = Gs_Mode();
    bool split = Gs_SelBits14() >= 2 && !Gs_ModeBit17();
    Car_LodTable();
    bool cpu = (uint32_t)n >= Gs_SelBits14();   /* a car after the players */
    int id;
    /* the player: in alternation (the race's kind 4) the one after the base, 0x41b370 */
    uint32_t pl = (uint32_t)n + (Race_Kind() == 4 ? (uint32_t)Race_Base() : 0);
    if (!cpu) id = mode == 4 ? gm8(0x52f3e8 + 0xc4u * (uint32_t)n) : (int)(gm32(0x531350 + 0x30u * pl) & 0x3f);
    else if (mode == 5 || mode == 6) id = Sel_CpuCar(n);   /* 0x408010: by the car's own slot */
    else {
        PENDING_NOTE(0x457000, "Car_Load (the CPU cars of the other modes)");
        id = 0;
    }
    int cls = (int)exe_u32(0x516b40 + 4u * (uint32_t)id);
    /* the detail letter (0x51a3d0) and the set by the mode */
    char letter, far = 0;   /* the far model's letter (local_123) when there is one */
    int set = 1;
    const uint32_t L = 0x51a3d0;
    switch (mode) {
    case 3: case 7:
        PENDING_NOTE(0x457000, "Car_Load (the time trial's ghost car letters)");
        letter = (char)gm8(L + (split ? 0xe : 1));
        set = letter < 'E' ? 1 : 3;
        break;
    case 5: case 6: case 8: case 9: case 11: case 12:
        if (cpu) {
            letter = (char)gm8(L + (split ? 0x12 : 5));
            far = (char)gm8(L + (split ? 0x13 : 6));
        } else letter = (char)gm8(L + (split ? 0x11 : 4));
        set = letter != 'A' ? 3 : 1;
        break;
    case 10:
        letter = (char)gm8(L + 1);
        break;
    default:
        /* a rally: the first letter, the split screen's 0xd (the network cases 0xb, 0xc open) */
        letter = (char)gm8(L + (split ? 0xd : 0));
        break;
    }
    if (gm32(0x52e890) & 0x10000) set = 1;   /* option 16 */
    Car_Slot(n)->model = (uint8_t)cls;   /* (0x542630 + 0x24 n: +0x00 the class, +0x20 the letter) */
    Car_Slot(n)->kind = letter;
    gw32(0x54260c + 4u * (uint32_t)n, (uint32_t)id);
    char path[96], tex[96];
    const char *name = car_name(id);
    if (letter == 'A') snprintf(path, sizeof path, "Game\\Cars\\%sA1N.c3d", name);
    else snprintf(path, sizeof path, "Game\\Cars\\%s%c%d.C3D", name, letter, set);
    uint8_t *d = File_Load(path, 1);
    if (!d) return false;
    uint32_t size = File_LastSize();
    snprintf(tex, sizeof tex, "Game\\Cars\\%s.cin", name);
    cins[n] = File_Load(tex, 1);
    cin_size[n] = cins[n] ? File_LastSize() : 0;
    PENDING_NOTE(0x4a3dc0, "Car_Load 0x4a3dc0");
    snprintf(tex, sizeof tex, "Game\\Cars\\%s%c%d.bfl", name, letter, set);
    if (!Bfl_Mount(&textures[n], tex)) return false;
    C3d *m = &models[n];
    if (!C3d_Load(m, d, size, g_tex_count)) return false;
    C3d_Textures(m->data + m->slots, m->data + m->paths, m->ntex, m->kind, &textures[n], "");
    C3d_Build(m);
    Obj *root = C3d_Objects(m, &g_world);
    if (!root) return false;
    CarSlot *s = Car_Slot(n);
    s->main = root;
    s->second = Obj_FindType(root, 5);
    int32_t sp[3];
    uint16_t ang[3];
    Csp_StartPos(n, sp);
    Csp_Angles(ang);
    Car_Place(s->main, s->second, n, cls, ang, sp);
    Car *c = Car_Get(n);
    c->main = s->main;
    c->second = s->second;
    /* dents on for the rally modes (0x46b400), never turned off here */
    if (!(mode == 5 || mode == 6 || mode == 7 || mode == 11 || mode == 12) && !(Gs_SelBit28() && Gs_Bit30())) Damage_SetDents(1, n);
    PENDING_NOTE(0x42cb50, "Car_Load (the wheels' other models)");
    return far_load(n, name, far, set, c);
}

const uint8_t *Car_Cin(int n) { return n >= 0 && n < CAR_MAX ? cins[n] : NULL; }

const uint8_t *Car_CinSection(int n, int k)
{
    const uint8_t *c = Car_Cin(n);
    if (!c) return NULL;
    uint32_t off = (uint32_t)(c[2 * (k > 0 ? k : 0)] | c[2 * (k > 0 ? k : 0) + 1] << 8);
    return off < cin_size[n] ? c + off : NULL;
}

/* 0x42b800: cars first to first + n - 1 put back on the grid. Their models, wheels, parts, the
   handling class (+0xb1b, 0 in mode 4) and the words +0x724, +0x728-+0x734, +0xc0c and +0xb58 are kept while the
   blocks (0x42b740) and their counters (0x42b5b0, 0x53a230) are cleared; each is placed at its start
   (Car_Place, its models kept), then the damage (0x4669b0, Damage_Setup again), the players' tyres
   (0x45c610 again), the clouds' texture (0x46ef20, not ported), the skids, the poses snapped and
   taken by the models, the progress started again outside selection bits 26 and 27, the poses
   captured (0x42af50). */
void Cars_Reset(int first, int n, bool keep)
{
    CarList l;
    l.n = (int16_t)n;
    for (int i = 0; i < n; i++) l.car[i] = (int16_t)(first + i);   /* 0x53a300 */
    struct {
        uint32_t w724, wc0c, wb58, hub[4];
        uint8_t cls;
    } k[CAR_MAX];
    for (int i = first; i < first + n; i++) {
        Car *c = Car_Get(i);
        k[i].w724 = (uint32_t)CI32(c, 0x724), k[i].wc0c = (uint32_t)CI32(c, 0xc0c), k[i].wb58 = (uint32_t)CI32(c, 0xb58);
        for (uint32_t q = 0; q < 4; q++) k[i].hub[q] = (uint32_t)CI32(c, 0x728 + 4 * q);
        k[i].cls = Gs_Mode() == 4 ? 0 : (uint8_t)c->s[0xb1b];
    }
    for (int i = first; i < first + n; i++) {
        Car *c = Car_Get(i);
        memset(c->s, 0, CAR_SIZE);   /* 0x42b740 (the port's pointers beside the block stay) */
        for (uint32_t q = 0; q < 4; q++) gw16(0x53a230 + 8u * (uint32_t)i + 2 * q, 0);   /* 0x42b5b0 */
        CW32(c, 0x724, (int32_t)k[i].w724), CW32(c, 0xc0c, (int32_t)k[i].wc0c), CW32(c, 0xb58, (int32_t)k[i].wb58);
        for (uint32_t q = 0; q < 4; q++) CW32(c, 0x728 + 4 * q, (int32_t)k[i].hub[q]);
    }
    for (int i = first; i < first + n; i++) {
        int32_t sp[3];
        uint16_t ang[3];
        Csp_StartPos(i, sp);
        Csp_Angles(ang);
        Car_PlaceKeep(Car_Get(i)->main, Car_Get(i)->second, i, k[i].cls, ang, sp, true);
    }
    Damage_Clear(first, n);   /* 0x4669b0 */
    Damage_Setup(true, keep, l.car, n);
    Tyres_Start(true, keep, (int)Gs_SelBits14());   /* 0x45c610 */
    Skids_Reset();
    for (int i = first; i < first + n; i++) Pose_Snap(i);   /* 0x42c840 */
    for (int i = first; i < first + n; i++) Car_PoseToObjects(Car_Get(i));   /* 0x43ecd0 */
    if (!Gs_SelBit26() && !Gs_SelBit27())
        for (int i = first; i < first + n; i++) Progress_Reset(i);   /* 0x4207a0 */
    Pose_Capture(&l);
}

/* 0x420190: the cars racing: with selection bit 26 (not networked) the players and the CPU cars,
   with bit 27 two, else the players */
int Cars_Racing(void)
{
    if (Gs_SelBit26() && !Gs_ModeBit19()) return (int)Gs_SelBits14() + Sel_CpuCount();
    if (Gs_SelBit27() && !Gs_ModeBit19()) return 2;
    return (int)Gs_SelBits14();
}

/* 0x456ca0: the cars loaded: Cars_Racing, one more for the ghost (bit 28 with 0x406310); the network's
   others open */
int Cars_Count(void)
{
    int n = Cars_Racing();
    if (Gs_SelBit28() && Gs_Bit30()) n++;
    if (Gs_ModeBit19()) PENDING_NOTE(0x456ca0, "Cars_Count (the network's cars)");
    return n < 1 ? 1 : n > CAR_MAX ? CAR_MAX : n;
}

/* 0x4584d0 (in Cars_Load with selection bit 22 or 31, again on a restart and a car 0 replay's
   start): the start grid at 0x542cd8 for the Cars_Racing cars. Case 0, the arcade: along the centre
   line's first step (from its last point before the finish to point 0), slot k of car i (the
   championship's order 0x533620 in mode 5 with more than two; two players swapped) 5 k - 5 n units
   along it (-10 for two cars), 4 to the right for even k and the left for odd; the car's progress
   start (0x542d38 + 4 k) the last point (a whole number, which 0x458480 takes as 16.16: point 0).
   Case 1, the head to head (bit 27, two cars): the side (0x542cb0, kept in 0x542cd4 when `keep`)
   drawn, rand() <= 0x3fff after srand(the time); the car on it starts at point 0, the other at the
   second split (Csp_StartPos gives its place). The other cases (bit 31, the network) are pending. */
void Cars_Grid(bool keep)
{
    int n = Cars_Racing();
    int kind = 0;
    if (Gs_SelBit31()) kind = 2;
    else if (Gs_ModeBit19() && (gm32(0x52f2ac) >> 5 & 0x1f) == 10 && Gs_Mode() != 10) kind = 3;
    else {
        if (n == 1 || Gs_Mode() == 10) return;
        if (!Gs_SelBit24() && n == 2) {
            if (!Gs_SelBit27()) return;
            kind = 1;
        }
    }
    if (kind == 1) {
        uint32_t side;
        if (keep) side = gm32(0x542cd4);
        else {
            crt_srand(Timer_Now());
            side = crt_rand() <= 0x3fff;
        }
        gw32(0x542cd4, side);
        gw32(0x542cb0, side);
        for (uint32_t k = 0; k < 2; k++) gw32(0x542d38 + 4 * k, (side + k) & 1 ? (uint32_t)Sectors_Point(1) : 0);
        return;
    }
    if (kind != 0) {
        PENDING_NOTE(0x4584d0, "Cars_Grid (bit 31 and the network grids)");
        return;
    }
    int32_t a[3], b[3];
    Cat_Point(Cat_FinishPoint() - 1, a);
    Cat_Point(0, b);
    int idx = Fx_DegIndex(Fx_Atan2Deg(b[2] - a[2], b[0] - a[0]));
    int32_t s = fx_mul(0x40000, g_sin[idx]), c = fx_mul(0x40000, g_sin[(idx + 0x400) & 0xfff]);
    for (int i = 0; i < n; i++) {
        int k = i;
        if (Gs_Mode() == 5 && n > 2) k = (int8_t)gm8(0x533620 + (uint32_t)i);   /* 0x40cec0 */
        if (Gs_SelBits14() == 2) k = 1 - i;
        gw32(0x542d38 + 4u * (uint32_t)k, (uint32_t)(Cat_FinishPoint() - 1));
        int32_t back = n > 2 ? fx_mul(0x50000, k << 16) - ((n * 5) << 16) : (int32_t)0xfff60000;
        int32_t dx = fx_mul(back, g_sin[(idx + 0x400) & 0xfff]), dz = fx_mul(back, g_sin[idx]);
        if (k % 2 == 0) dx += s, dz -= c;
        else dx -= s, dz += c;
        Csp_SetGrid(i, b[0] + dx, b[2] + dz);
    }
}

/* 0x458480: each racing car's progress (0x4213d0) at its grid point (0x542d38, 16.16) */
void Cars_GridPoints(void)
{
    for (int i = 0; i < Sectors_Cars(); i++) Progress_SetPoint(Car_Get(i), (int)gm32(0x542d38 + 4u * (uint32_t)i) >> 16);
}
