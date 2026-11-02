/* The time trial's ghost (see ghost.h). */
#include "game/ghost.h"
#include "game/camera.h"
#include "game/paths.h"
#include "game/mem.h"
#include "world/tod.h"
#include "render/mesh.h"
#include "exe.h"
#include "render/d3d7.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static Replay *ghost;     /* DAT_00588758 */
static Car *gcar;         /* DAT_0058875c */
static int8_t owner = -1; /* DAT_00588760: the player whose car it shows */
static int8_t from = -1;  /* DAT_00588761: the player whose run is to become it */
static int alpha = -1;    /* DAT_00588864: the see-through level set last */

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static void wr32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8), p[2] = (uint8_t)(v >> 16), p[3] = (uint8_t)(v >> 24); }

/* 0x465ec0: a mesh object blended (its part flags 0x40008) at alpha a in its polygons' colours
   (0x4ab960), with `grey` their colour 0x808080 (0x4ab8f0), the vertices' colours again (0x4b1ea0) */
static void see_through(Obj *o, uint8_t a, bool grey)
{
    if (!o || o->kind != 0 || !o->mesh || !o->model) return;
    uint8_t *part = o->mesh;
    wr32(part + 0x30, rd32(part + 0x30) | 0x40008);
    uint8_t *p = (uint8_t *)C3d_Ref(o->model, part + 0x24, o->model->polys);
    uint32_t np = rd32(part + 0x28);
    for (uint32_t i = 0; p && i < np; i++)
        for (int k = 0; k < 3; k++) p[(size_t)i * C3D_POLY + 0x37 + 4 * k] = a;
    Mesh_RestoreColours(o->model, part);
    if (!grey) return;
    for (uint32_t i = 0; p && i < np; i++)
        for (int k = 0; k < 3; k++) memset(p + (size_t)i * C3D_POLY + 0x34 + 4 * k, 0x80, 3);
    Mesh_RestoreColours(o->model, part);
}

/* 0x465f20: a sibling list and everything below it */
static void see_through_list(Obj *o, uint8_t a, bool grey)
{
    for (; o; o = o->next) {
        see_through(o, a, grey);
        if (o->child) see_through_list(o->child, a, grey);
    }
}

static void car_through(Car *c, uint8_t a, bool grey)
{
    if (!c) return;
    see_through(c->main, a, grey);
    if (c->main) see_through_list(c->main->child, a, grey);
    see_through(c->second, a, grey);
    if (c->second) see_through_list(c->second->child, a, grey);
}

/* 0x465f80 */
static void reset(void)
{
    owner = -1;
    alpha = -1;
}

void Ghost_New(int segs, int frames)
{
    ghost = Replay_New(segs, frames, 0);
    reset();
}

void Ghost_Load(const char *path)
{
    ghost = Replay_Load(path);
    if (!ghost) Ghost_New(1, 0x1d4c);
    reset();
}

/* 0x465fc0: the last car is the ghost: +0xc0c, its models grey at 100 */
void Ghost_SetCar(Car *c)
{
    gcar = c;
    CW32(c, 0xc0c, 1);
    from = -1;
    alpha = -1;
    car_through(c, 100, true);
}

void Ghost_Start(int seg, int pre)
{
    if (!gcar) return;
    uint8_t idx = gcar->s[0xb1a];
    Replay_Play(ghost, seg, pre, idx);   /* 0x46cce0 */
    gcar->s[0xb1a] = idx;
    CW32(gcar, 0xc0c, 1);
}

void Ghost_End(void) { Replay_End(ghost); }   /* 0x46d2a0 */
bool Ghost_Playing(void) { return ghost && Replay_Playing(ghost); }

/* 0x4660a0 on the race's replay slot p: swapped with the ghost's when p is the one named */
void Ghost_Take(int p, int car)
{
    if (from != p) return;
    Replay *r = Race_Replay(p);
    Race_SetReplay(p, ghost);
    ghost = r;
    from = -1;
    owner = (int8_t)car;
}

void Ghost_TakeFrom(int p) { from = (int8_t)p; }
int Ghost_Owner(void) { return owner; }

/* 0x466100: car 1's height (1.0 up from its position, along the view's up) on player p's screen, in
   640 x 480 pixels: from 5 up the ghost thins (alpha 1 - (d - 5) / 45, by 255), the trackside cameras
   0x80; off the screen 0 */
void Ghost_Fade(int p, const Obj *camera)
{
    Car *c = Car_Get(1);
    if (!c || !c->main || !camera) return;
    int32_t pos[3] = {c->main->local.m[12], c->main->local.m[13] + 0x10000, c->main->local.m[14]}, s0[2], s1[2];
    Obj_ToScreen(s0, pos, camera);
    if (s0[0] != -0x640000 || s0[1] != -0x640000) {
        const float k = (float)(1.0 / 65536.0);
        float x0 = (float)s0[0], y0 = (float)s0[1];
        int32_t up[3] = {camera->local.m[4] + pos[0], camera->local.m[5] + pos[1], camera->local.m[6] + pos[2]};
        Obj_ToScreen(s1, up, camera);
        if (s1[0] != -0x640000 || s1[1] != -0x640000) {
            int w, h;
            D3d_ScreenSize(&w, &h);
            float dx = ((float)s1[0] * k - x0 * k) * 640.0f / (float)w;
            float dy = ((float)s1[1] * k - y0 * k) * 480.0f / (float)h;
            float d = sqrtf(dx * dx + dy * dy);
            if (d <= 5.0f) return;
            float f = 1.0f - (d - 5.0f) * (1.0f / 45.0f);   /* (0x5113d0) */
            int a = f < 1.0f ? (f > 0.0f ? (int)lrintf(f * 255.0f) : 0) : 0xff;
            if (Camera_Kind(p) == 7) a = 0x80;
            if (a == alpha) return;
            car_through(c, (uint8_t)a, false);
            alpha = a;
            return;
        }
    }
    if (alpha == 0) return;
    car_through(c, 0, false);
    alpha = 0;
}

const char *Ghost_StageName(void)
{
    static char b[0x104];
    bool low = (gm32(0x52afd0) >> 0x1b & 3) != 0;   /* 0x405d00 */
    uint32_t m = Gs_Mode();
    if (m == 5 || m == 6 || m == 7 || m == 11 || m == 12) {
        static const int rally_of[8] = {6, 3, 1, 4, 0, 2, 5, 7};
        uint32_t sel = gm32(0x52f2ac);
        int idx = (int)(sel >> 10 & 3) * 3 + (int)(sel >> 12 & 3);
        snprintf(b, sizeof b, exe_str(low ? 0x519388 : 0x51939c), Path_Root(), exe_str(exe_u32(0x51931c + 4u * (uint32_t)rally_of[idx & 7])));
    } else
        snprintf(b, sizeof b, exe_str(low ? 0x519404 : 0x519418), Path_Root(), exe_str(exe_u32(0x51931c + 4 * Tod_Rally())), (int)Tod_Stage() + 1);
    return b;
}
