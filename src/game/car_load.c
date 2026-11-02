/* Car_Load 0x457000 (see car_load.h). */
#include "game/car_load.h"
#include "world/carshade.h"
#include "game/race.h"
#include "game/gstate.h"
#include "game/mem.h"
#include "game/select.h"
#include "game/opponents.h"
#include "game/ghost.h"
#include "game/mud.h"
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
#include "world/tod.h"
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
static C3d alt_models[CAR_MAX];     /* 0x54263c + 0x24 n's file: the wheels' other model */
static uint8_t *alt_files[CAR_MAX];
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

static uint32_t rd_flags(const uint8_t *m) { return (uint32_t)m[0x30] | m[0x31] << 8 | m[0x32] << 16 | (uint32_t)m[0x33] << 24; }
static void gw_flags(uint8_t *m, uint32_t v) { m[0x30] = (uint8_t)v, m[0x31] = (uint8_t)(v >> 8), m[0x32] = (uint8_t)(v >> 16), m[0x33] = (uint8_t)(v >> 24); }
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
   +0x724-+0x734 and hung under the main model. */
/* 0x4b2d90: the car's number in bits 15-17 of its meshes' flags (+0x30; the object and each child's
   first-child chain, kind 0), which picks its environment map */
static void mesh_number(Obj *o, int n)
{
    if (!o) return;
    if (o->kind == 0 && o->mesh) gw_flags(o->mesh, ((uint32_t)n & 7) << 0xf | (rd_flags(o->mesh) & 0xfffc7fffu));
    for (Obj *c = o->child; c; c = c->next)
        for (Obj *x = c; x; x = x->child)
            if (x->kind == 0 && x->mesh) gw_flags(x->mesh, ((uint32_t)n & 7) << 0xf | (rd_flags(x->mesh) & 0xfffc7fffu));
}

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
    if (gm32(0x52afd0) >> 4 & 1) CarShade_Add(c->far[0], false);   /* 0x4b45d0 */
    mesh_number(c->far[0], n);
    for (int i = 0; i < 5; i++)
        if (c->far[i]) Obj_Reparent(c->far[i], c->main);
    Car_Textures(letter, &c->far[1]);   /* 0x42cb90: the far wheels */
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
    if (mode == 4) {
        /* the challenge: both cars of the match the same, one of the six at 0x516b98 (0x40eea0), the
           first car's drawn unlike the last match's (0x51a8b8), into the record's slot of the car's
           number (0x408600: not the match's driver's), flagged */
        int32_t k = (int32_t)gm32(0x51a8b8);
        if (n == 0) do k = crt_rand() % 6; while (k == (int32_t)gm32(0x51a8b8));
        gw32(0x51a8b8, (uint32_t)k);
        id = (int)exe_u32(0x516b98 + 4u * (uint32_t)k);
        gw32(0x52f3e8 + 0xc4u * (uint32_t)n, (uint32_t)id & 0xff);
        gw8(0x52f3ec + 0xc4u * (uint32_t)n, gm8(0x52f3ec + 0xc4u * (uint32_t)n) | 2);
    } else if ((mode == 3 || mode == 7) && Gs_Bit30() && n == Cars_Count() - 1) {
        /* the ghost: its owner's car (0x4660f0, none: 0) */
        int o = Ghost_Owner();
        id = (int)(gm32(0x531350 + 0x30u * (uint32_t)(o == -1 ? 0 : o)) & 0x3f);
    } else if (Gs_SelBit27() && !Gs_ModeBit19()) {
        /* the head to head: the players' cars, then the paired driver's (0x407fc0 of 0x455ab0) */
        if (!cpu) id = (int)(gm32(0x531350 + 0x30u * pl) & 0x3f);
        else {
            int d = Opp_Pair(Race_Base());
            if (mode == 5 || mode == 6 || mode == 7) id = d >= 0 && d < 6 ? (int)(uint8_t)gm32(0x54260c + 4u * (uint32_t)d) : 0;
            else id = d >= 0 && d <= 0xe ? gm8(0x51681c + (uint32_t)d) : 0;
        }
    } else if (Gs_SuperSpecial()) {
        /* the super special: every car the championship's leading player's (the lowest place,
           0x533548 by 0x455de0; the first on a tie) */
        int best = 99;
        uint32_t who = 0;
        for (uint32_t i = 0; i < Gs_Players(); i++) {
            int place = (int8_t)gm8(0x533548 + (uint32_t)Opp_PlayerSlot((int)i));
            if (place < best) best = place, who = i;
        }
        id = (int)(gm32(0x531350 + 0x30u * who) & 0x3f);
    } else if (Gs_ModeBit19() && n > 0) {
        /* round a circuit the cars after the first: the n-th network slot in use (0x409cb0; none
           found: the mode's number, as the original leaves it), marked (0x40b200), its car (0x409d00)
           and the car's number written back (0x40aff0) */
        uint32_t slot = mode;
        for (uint32_t i = 0, k = 0; i < 7; i++)
            if (gm32(0x53177c + 0x80u * i) >> 7 & 1) {
                if ((int)k == n - 1) { slot = i; break; }
                k++;
            }
        gw32(0x53177c + 0x80u * slot, gm32(0x53177c + 0x80u * slot) | 0x800000);
        id = (int)(gm32(0x53177c + 0x80u * slot) & 0x1f);
        gw32(0x531780 + 0x80u * slot, (uint32_t)n);
    } else id = (int)(gm32(0x531350 + 0x30u * pl) & 0x3f);
    if (cpu && (mode == 5 || mode == 6)) id = Sel_CpuCar(n);   /* 0x408010: by the car's own slot */
    /* the class (0x4a3630); in the challenge the drawn number in its place */
    int cls = mode == 4 ? (int)gm32(0x51a8b8) : (int)exe_u32(0x516b40 + 4u * (uint32_t)id);
    /* the detail letter (0x51a3d0) and the set by the mode */
    char letter, far = 0;   /* the far model's letter (local_123) when there is one */
    int set = 1;
    const uint32_t L = 0x51a3d0;
    switch (mode) {
    case 3: case 7:
        /* the time trials: with the ghost its letter (3, the split screen's 0xf) and the players' (2,
           0x10), else 1 (0xe) */
        if (!Gs_Bit30()) letter = (char)gm8(L + (split ? 0xe : 1));
        else if (n == Cars_Count() - 1) letter = (char)gm8(L + (split ? 0xf : 3));
        else letter = (char)gm8(L + (split ? 0x10 : 2));
        set = letter < 'E' ? 1 : 3;
        break;
    case 5: case 6: case 8: case 9: case 11: case 12:
        if (cpu) {
            letter = (char)gm8(L + (split ? 0x12 : 5));
            far = (char)gm8(L + (split ? 0x13 : 6));
        } else letter = (char)gm8(L + (split ? 0x11 : 4));
        set = letter != 'A' ? 3 : 1;
        break;
    case 4:
        /* the challenge: the split screen's letters at 0x5429c4 (a player) and 0x5429c5 */
        if (cpu) letter = (char)(split ? gm8(0x5429c5) : gm8(L + 8));
        else letter = (char)(split ? gm8(0x5429c4) : gm8(L + 7));
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
    Tex_SetKeepBodf(Gs_SelBit24());   /* 0x4a3dc0 */
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
    /* the class 0 for the head to head's CPU run (one player), the super special and the challenge */
    bool none = (Gs_SelBit25() && !Gs_ModeBit19() && !split && n == 1) || Gs_SuperSpecial() || mode == 4;
    Car_Place(s->main, s->second, n, none ? 0 : cls, ang, sp);
    Car *c = Car_Get(n);
    c->main = s->main;
    c->second = s->second;
    /* the trees' shadows on the car, with the shadow map option (0x405ba0): a kind A car's meshes as
       they are, else its main model's as cylinders */
    if (gm32(0x52afd0) >> 4 & 1) {
        CarShade_Add(s->second, s->kind == 'A');   /* 0x4b45d0 */
        CarShade_Add(s->main, s->kind == 'A');
    }
    mesh_number(s->second, n), mesh_number(s->main, n);
    /* dents on for the rally modes (0x46b400), never turned off here */
    if (!(mode == 5 || mode == 6 || mode == 7 || mode == 11 || mode == 12) && !(Gs_SelBit28() && Gs_Bit30())) Damage_SetDents(1, n);
    /* a player's tyres new outside the rally modes 0 and 1, and in those on the rally's first stage
       and the stages after a service (4, 8, 10) */
    if ((uint32_t)n < Gs_SelBits14() && ((mode != 0 && mode != 1) || Tod_Stage() == 0 || Tod_Stage() == 4 ||
                                         Tod_Stage() == 8 || Tod_Stage() == 10))
        for (int w = 0; w < 4; w++) Tyres_Reset(n, w);   /* 0x45c6b0 */
    if (!Gs_SelBit24() && (letter == 'C' || letter == 'A')) Mud_Setup(n, cls);   /* 0x458050 */
    /* a kind A car's wheels from its S model on snow (0x42cae0 with 1), or its L one where the wheels
       show as they are (0x42cb50; the S file is then left loaded): the model under the world root with
       the car's textures (0x54263c), the wheels' meshes kept (+0x10-+0x1c, never read back) and the
       main model's wheels given the other model's */
    if (letter == 'A') {
        uint8_t *alt = NULL;
        uint32_t asize = 0;
        if (Pose_Narrow(c, 1)) {
            snprintf(path, sizeof path, "Game\\Cars\\%sA1S.c3d", name);
            alt = File_Load(path, 0), asize = File_LastSize();
        }
        if (Car_PlainWheels(c)) {
            snprintf(path, sizeof path, "Game\\Cars\\%sA1L.c3d", name);
            alt = File_Load(path, 0), asize = File_LastSize();
        }
        free(alt_files[n]);
        alt_files[n] = alt;
        C3d *am = &alt_models[n];
        if (alt && C3d_Load(am, alt, asize, g_tex_count)) {
            C3d_Textures(am->data + am->slots, am->data + am->paths, am->ntex, am->kind, &textures[n], "");
            C3d_Build(am);
            Obj *ar = C3d_Objects(am, &g_world);
            for (uint32_t t = 1; ar && t <= 4; t++) {
                Obj *from = Obj_FindType(ar, t), *to = Obj_FindType(s->main, t);
                if (from && to) to->mesh = from->mesh, to->model = from->model;
            }
        }
    }
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
   (0x45c610 again), the mud washed off (0x46ef20), the skids, the poses snapped and
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
    Mud_Reset();   /* 0x46ef20 */
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
   second split (Csp_StartPos gives its place). The other cases are the network game's (bit 31 is set
   only in modes 11 and 12, 0x408020) and pending. */
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
        PENDING_NOTE(0x4584d0, "Cars_Grid (the network grids)");
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
