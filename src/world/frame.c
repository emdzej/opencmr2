/* The world frame (see frame.h). */
#include "world/frame.h"
#include "world/carshade.h"
#include "render/readback.h"
#include "world/glow.h"
#include "world/particles.h"
#include "game/timer.h"
#include "render/sprite3d.h"
#include "render/line3d.h"
#include "exe.h"
#include "render/d3d7.h"
#include "render/light.h"
#include "render/mesh.h"
#include "render/polyq.h"
#include "render/rstate.h"
#include "render/twod.h"
#include "render/view.h"
#include "world/section.h"
#include "world/relight.h"
#include "world/stage.h"
#include "world/tre.h"
#include <stdlib.h>

static uint32_t mode;           /* DAT_0059ce14 */
static Obj **cars;              /* the port: the cars drawn where the cars' pass 0x4af120 goes */
static int ncars;

void World_SetCars(Obj **c, int n) { cars = c, ncars = n; }
static Obj *queued[OBJ_MAX];    /* 0x597d04 */
static int nqueued;             /* DAT_0059ce2c */

void World_SetMode(uint32_t m) { mode = m; }
uint32_t World_Mode(void) { return mode; }

void Obj_QueueSorted(Obj *o)
{
    if (nqueued < OBJ_MAX) queued[nqueued++] = o;   /* the original has no bound */
}

/* Equal distances (outside the type cases) also answer "before": qsort's order among them is its own. */
int Obj_CompareFar(const void *pa, const void *pb)
{
    const Obj *a = *(Obj *const *)pa, *b = *(Obj *const *)pb;
    uint32_t ta = a->type & 0xff, tb = b->type & 0xff;
    if (ta == 0x14 || tb == 0x14) return 1;
    int32_t d = a->dist - b->dist;
    if (d < 0) d = -d;
    if (d < 0x10000) {
        if (ta == 5 && tb == 0) return 1;
        if (ta == 0 && tb == 5) return -1;
    }
    return b->dist <= a->dist ? -1 : 1;
}

void Obj_DrawQueued(uint32_t pass)
{
    if (!nqueued) return;
    if (exe_u32(0x5207b4)) qsort(queued, (size_t)nqueued, sizeof *queued, Obj_CompareFar);
    for (int i = 0; i < nqueued; i++) Obj_Draw(queued[i], pass);
    nqueued = 0;
}

void Section_QueueSorted(const Obj *cam)
{
    for (int k = 0; k < g_nvisible; k++)
        for (Obj *o = g_sec[g_visible[k]].objs; o; o = o->section_next) {
            if (!o->sorted) continue;
            int32_t dx = cam->world.m[12] - o->world.m[12], dz = cam->world.m[14] - o->world.m[14];
            o->dist = (int32_t)Fx_Sqrt((uint32_t)fx_mul(dx, dx) + (uint32_t)fx_mul(dz, dz));
            Obj_QueueSorted(o);
        }
}

/* Only kind-0 objects with a pass mask and a mesh, each with its float world matrix. */
void World_DrawAll(void)
{
    Render_ZEnable(1);
    Render_ZWrite(1);
    for (int i = 0; i < g_nobjs; i++) {
        Obj *o = g_objs[i];
        if (o && o->kind == 0 && o->mask && o->mesh) {
            D3d_SetTransform(D3DTS_WORLD, &o->worldf);
            Mesh_Draw(o->model, o->mesh, 0, 0, 0);
        }
    }
    Poly_Flush(0x10);
}

static void set_viewport(uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    D3DVIEWPORT7 v = {x, y, w, h, 0.0f, 1.0f};
    D3d_SetViewport(&v);
}

static void identity_world(void)
{
    D3DMATRIX m = {{{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}}};   /* 0x5207b8 */
    D3d_SetTransform(D3DTS_WORLD, &m);
}

/* The 2D queues of a layer: lines (FUN_004bb2b0, a debugging queue nothing fills in the release: not
   ported), triangles, sprites. */
static void flush_layer(int layer)
{
    Tri_Flush(layer);
    Sprite_Flush(layer);
}

/* Not ported yet: FUN_004b6240 (pass) at the end of the 3D; of the frame statistics FUN_004b21e0 after the 2D only the frame rate. */
bool World_Render(Obj *cam, const int16_t view[4], uint32_t pass, bool world)
{
    g_polys_drawn = 0;
    Twod_ResetPrims();
    D3d_BeginScene();
    set_viewport((uint32_t)view[0], (uint32_t)view[1], (uint32_t)view[2], (uint32_t)view[3]);
    Render_AlphaBlend(1);
    Render_SetCull(3);
    Light_SetMode(5);
    flush_layer(4);
    if (world) {
        if (g_sec_ready) {
            identity_world();
            Section_BuildVisible(cam);
            for (int k = 0; k < g_nvisible; k++) Section_Relight(g_visible[k]);
        }
        Render_SetCull(Render_DefaultCull());
        Billboard_SetCamera(cam);
        Particles_Draw(cam, pass);   /* 0x4b0480 */
        if (!g_nsec) {
            World_DrawAll();
        } else {
            D3d_SetRenderState(D3DRS_FOGENABLE, Fog_Enabled());
            Fog_Off();
            Light_SetMode(5);
            int32_t far = Proj_Far();
            Proj_Set(0, 0, 0x960000, 0);   /* 150 */
            Section_DrawObjects(pass);
            Proj_Set(0, 0, far, 0);
            Fog_SetLinear();
            Light_SetMode(0);
            Stage_DrawTerrain(&g_stage_mesh);
            Light_SetMode(5);
            Stage_DrawOverlay();
            Render_AlphaBlend(1);
            Glow_Draw(cam, pass);   /* 0x4af120 */
            Poly_Flush(8);
            /* the port's cars (see frame.h) after the ground's decals (the shadows and the lamps' pools,
               no z writes, a unit toward the camera), which they cover; then the world's matrix again */
            Light_SetMode(5);
            Render_AlphaBlend(1);
            for (int i = 0; i < ncars; i++) Obj_Draw(cars[i], pass);
            identity_world();
            Light_SetMode(4);
            Poly_Flush(0x20);
            Light_SetMode(1);
            if (!mode) {
                Section_DrawBillboards(cam);
                Section_QueueSorted(cam);
                Obj_DrawQueued(pass);
                Billboard_DrawQueued();
            } else {
                Section_DrawBillboards(cam);
                Billboard_DrawQueued();
                identity_world();
                Render_AlphaBlend(1);
                Render_SetBlend(5, 6);
                D3d_SetRenderState(D3DRS_FOGENABLE, 0);
                Light_SetMode(5);
                Poly_Flush(0x10);
                Line3d_Flush();       /* 0x4bb4c0 */
                Sprite3d_Draw(cam);   /* 0x4b1530 */
                Light_SetMode(1);
                Section_QueueSorted(cam);
                Obj_DrawQueued(pass);
            }
        }
        identity_world();
        Render_AlphaBlend(1);
        Render_SetBlend(5, 6);
        D3d_SetRenderState(D3DRS_FOGENABLE, 0);
        Light_SetMode(5);
        Poly_Flush(0x10);
        Light_SetMode(4);
        Poly_Flush(0x40);
        Light_SetMode(5);
        Line3d_Flush();       /* 0x4bb4c0 */
        Sprite3d_Draw(cam);   /* 0x4b1530 */
        CarShade_Draw(pass);  /* 0x4b6240: the trees' shadows on the cars */
    }
    Screen_ColourMeasure();   /* 0x4bc490 (0, 0, 0): the sun glow's test, over the 3D */
    Render_ZWrite(0);
    Render_ZEnable(0);
    Render_SetBlend(5, 6);
    const D3dState *s = D3d_State();
    set_viewport(0, 0, (uint32_t)s->width, (uint32_t)s->height);
    Render_SetCull(3);
    flush_layer(3);
    flush_layer(2);
    flush_layer(1);
    Render_SetCull(Render_DefaultCull());
    Frame_Stats();   /* 0x4b21e0 (the frame rate) */
    D3d_EndScene();
    return true;
}
