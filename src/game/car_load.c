/* Car_Load 0x457000 (see car_load.h). */
#include "game/car_load.h"
#include "game/gstate.h"
#include "game/mem.h"
#include "bfl.h"
#include "c3d.h"
#include "exe.h"
#include "pending.h"
#include "sim/car.h"
#include "world/obj.h"
#include "world/stage.h"
#include "render/tex.h"
#include "render/mesh.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>

static C3d models[CAR_MAX];
static BflSlot textures[CAR_MAX];   /* the slots' texture archives (0x542634 + 0x24 n) */

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

bool Car_Load(int n)
{
    uint32_t mode = Gs_Mode();
    bool split = Gs_SelBits14() >= 2 && !Gs_ModeBit17();
    Car_LodTable();
    int id;
    if ((uint32_t)n < Gs_SelBits14()) id = mode == 4 ? gm8(0x52f3e8 + 0xc4u * (uint32_t)n) : (int)(gm32(0x531350 + 0x30u * (uint32_t)n) & 0x3f);
    else {
        PENDING_NOTE(0x457000, "Car_Load (CPU cars and the other modes' choices)");
        id = 0;
    }
    int cls = (int)exe_u32(0x516b40 + 4u * (uint32_t)id);
    char letter;
    int set = 1;
    if (mode == 10) letter = (char)gm8(0x51a3d0 + 1);
    else if (mode == 0 || mode == 1 || mode == 2) {
        /* a rally: the first letter, the split screen's 0xd (the network cases 0xb, 0xc open) */
        letter = (char)gm8(0x51a3d0 + (split ? 0xd : 0));
    } else {
        PENDING_NOTE(0x457000, "Car_Load (the detail letter in modes 3-9, 11, 12)");
        letter = (char)gm8(0x51a3d0);
    }
    if (gm32(0x52e890) & 0x10000) set = 1;
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
    PENDING_NOTE(0x4a3dc0, "Car_Cin");   /* the .cin's use */
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
    Damage_Reset(c);
    PENDING_NOTE(0x42cb50, "Car_Load (the wheels' other models)");
    return true;
}
