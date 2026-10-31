/* Model viewer (port-plan step 3's check, new code). The model goes through the ported path: C3d_Load
   with the texture count as its base, C3d_Textures (the records from the archive), C3d_Build (the shared
   vertex buffers and the batches) and Mesh_Draw per mesh, on the game's start-up states
   (Render_InitStates). The viewer only adds the camera, one light and the parts' placement:
   --param car=<model> shows a car from Game/Cars turning on a stand (lit by one light; meshes placed by
   their objects' matrices chained up the parents); --param stage=<country><nn> flies along a stage's
   mesh (Game/Tracks/<Country>/<stage>Hi.bfl's temp.c3d with the country's textures; prelit: the vertex
   colour is the emissive source). */
#include "viewer.h"
#include "bfl.h"
#include "c3d.h"
#include "image.h"
#include "platform.h"
#include "render/d3d7.h"
#include "render/light.h"
#include "render/mesh.h"
#include "render/rstate.h"
#include "render/tex.h"
#include "render/twod.h"
#include "render/view.h"
#include "font.h"
#include "world/obj.h"
#include "world/section.h"
#include "world/frame.h"
#include "world/stage.h"
#include "world/tod.h"
#include "world/hpc.h"
#include "sim/car.h"
#include "vfs.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

enum { MAX_MESH = 512 };

static struct {
    C3d m;
    uint8_t *file;
    int nmesh;
    struct {
        D3DMATRIX place;        /* object to model */
        bool used;
    } mesh[MAX_MESH];
    BflSlot tex;                /* the texture archive (the records point to it) */
    Obj stand, camera;          /* the car hangs under the stand; the camera's world gives the view */
    Obj *car;                   /* the car's first object (its root) */
    BflSlot fe;                 /* FrontEnd\\res640.bfl: the title's font */
    char name[32];
    char title[80];
    int frame;
    bool stage;
    C3d pm;                     /* stage and car: the parked car's model, file, textures, root */
    uint8_t *pfile;
    BflSlot ptex;
    Obj *parked, *cars[2];
    float centre[MAX_MESH][3];   /* stage: each mesh's vertex centre, the fly-through's path */
} V;

static int32_t rd32(const uint8_t *p) { return (int32_t)(p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24); }

static void identity(D3DMATRIX *m)
{
    memset(m, 0, sizeof *m);
    for (int i = 0; i < 4; i++) m->m[i][i] = 1;
}

static void mul(const D3DMATRIX *a, const D3DMATRIX *b, D3DMATRIX *o)
{
    D3DMATRIX r;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            r.m[i][j] = a->m[i][0] * b->m[0][j] + a->m[i][1] * b->m[1][j] + a->m[i][2] * b->m[2][j] + a->m[i][3] * b->m[3][j];
    *o = r;
}

/* An object's first matrix (16.16, row vectors). */
static void obj_matrix(const uint8_t *o, D3DMATRIX *m)
{
    for (int i = 0; i < 16; i++) m->m[i / 4][i % 4] = (float)rd32(o + 0x58 + 4 * i) / 65536.0f;
}

static void build_meshes(void)
{
    V.nmesh = V.m.nmesh1 + V.m.nmesh2;
    if (V.nmesh > MAX_MESH) V.nmesh = MAX_MESH;
    for (int i = 0; i < V.nmesh; i++) identity(&V.mesh[i].place);
    /* placement: the first object using a mesh, its matrix chained up the parents */
    for (int i = 0; i < V.m.nobj; i++) {
        const uint8_t *o = C3d_Object(&V.m, i);
        const uint8_t *me = C3d_Ref(&V.m, o + 0x0c, V.m.meshes);
        if (!me) continue;
        int mi = (int)((me - V.m.data - V.m.meshes) / C3D_MESH);
        if (mi >= V.nmesh || V.mesh[mi].used) continue;
        V.mesh[mi].used = true;
        D3DMATRIX acc;
        obj_matrix(o, &acc);
        for (const uint8_t *p = C3d_Ref(&V.m, o + 8, V.m.objects); p; p = C3d_Ref(&V.m, p + 8, V.m.objects)) {
            D3DMATRIX pm;
            obj_matrix(p, &pm);
            mul(&acc, &pm, &acc);
        }
        V.mesh[mi].place = acc;
    }
    for (int i = 0; i < V.nmesh; i++) {
        const uint8_t *me = C3d_Mesh(&V.m, i);
        const uint8_t *v = C3d_Ref(&V.m, me + 0x0c, V.m.verts);
        uint32_t nv = (uint32_t)rd32(me + 0x10);
        float c[3] = {0, 0, 0};
        for (uint32_t k = 0; v && k < nv; k++)
            for (int j = 0; j < 3; j++) {
                float f;
                memcpy(&f, v + (size_t)k * C3D_VERTEX + 4 * j, 4);
                c[j] += f / (float)nv;
            }
        memcpy(V.centre[i], c, sizeof c);
    }
}

typedef struct { const char *stage; char *path, *tex; size_t cap; char country[4]; bool found; } Find;

static void find_file(const char *name, bool is_dir, void *user)
{
    Find *f = user;
    char want[80];
    snprintf(want, sizeof want, "%shi.bfl", f->stage);
    size_t l = strlen(name);
    if (!is_dir && !strcasecmp(name, want)) f->found = true;
    if (!is_dir && l == 10 && !strcasecmp(name + 3, "hit.bfl")) snprintf(f->country, sizeof f->country, "%.3s", name);
}

static void find_dir(const char *name, bool is_dir, void *user)
{
    Find *f = user;
    if (!is_dir || f->found) return;
    char dir[128];
    snprintf(dir, sizeof dir, "Game/Tracks/%s", name);
    f->country[0] = 0;
    vfs_list(dir, find_file, f);
    if (f->found) {
        snprintf(f->path, f->cap, "Game\\Tracks\\%s\\%shi.bfl", name, f->stage);
        snprintf(f->tex, f->cap, "Game\\Tracks\\%s\\%shit.bfl", name, f->country);
    }
}

static bool find_stage(const char *stage, char *path, char *tex, size_t cap)
{
    Find f = {stage, path, tex, cap, "", false};
    vfs_list("Game/Tracks", find_dir, &f);
    return f.found;
}

/* The car's model path and its set's texture archive (f99a1N.c3d, f99a1L.c3d use f99a1.bfl). */
static void car_paths(const char *car, char *path, char *tex, size_t cap)
{
    snprintf(path, cap, "Game\\Cars\\%s.c3d", car);
    snprintf(tex, cap, "Game\\Cars\\%s.bfl", car);
    size_t l = strlen(tex);
    if (!vfs_exists(tex) && l > 5 && strchr("NLS", tex[l - 5])) memmove(tex + l - 5, tex + l - 4, 5);
}

/* The car under the world root at the stage's start line, as Car_Load 0x457000 does it: the model's
   type-5 object as its second model, the .csp position and angles, Car_Place (Car_Init, Car_Settle on
   the .hpc ground). Handling class 0 until the car table is ported. */
static bool park_car(const char *car, const char *stage)
{
    char path[128], tex[128];
    car_paths(car, path, tex, sizeof path);
    V.pfile = File_Load(path, 1);
    if (!V.pfile || !C3d_Load(&V.pm, V.pfile, File_LastSize(), g_tex_count)) return false;
    Bfl_Mount(&V.ptex, tex);
    C3d_Textures(V.pm.data + V.pm.slots, V.pm.data + V.pm.paths, V.pm.ntex, V.pm.kind, &V.ptex, "");
    C3d_Build(&V.pm);
    V.parked = C3d_Objects(&V.pm, &g_world);
    if (!V.parked || !Stage_LoadHpc(stage) || !Stage_LoadCsp(stage)) return false;
    CarSlot *s = Car_Slot(0);
    s->main = V.parked;
    s->second = Obj_FindType(V.parked, 5);
    s->kind = 'A';
    int32_t pos[3];
    uint16_t ang[3];
    Csp_StartPos(0, pos);
    Csp_Angles(ang);
    Car_Place(s->main, s->second, 0, 0, ang, pos);
    Damage_Reset(Car_Get(0));
    /* stand-in until Car_PoseInterp 0x42bf70 (it needs Sim_Step's pose buffers): the main model at
       the settled position */
    Car *c = Car_Get(0);
    int32_t at[3] = {CI32(c, 0x2d0), CI32(c, 0x2d4), CI32(c, 0x2d8)};
    Obj_SetPosition(s->main, at);
    V.cars[0] = s->main;
    V.cars[1] = s->second;
    World_SetCars(V.cars, s->second ? 2 : 1);
    return true;
}

bool viewer_init(const char *car, const char *stage)
{
    char path[128], tex[128];
    /* the device and the vertex buffers first: loading a model fills the buffers (C3d_Build) */
    if (!D3d_Open(640, 480, 32)) return false;
    Render_InitStates();
    VB_CreatePool();
    if (stage) {
        /* Game\Tracks\<country>\<stage>Hi.bfl, the country's textures in <ccc>hit.bfl beside it */
        if (!find_stage(stage, path, tex, sizeof path)) {
            plat_log("viewer: no such stage");
            return false;
        }
        if (!Stage_Mount(path, tex) || (Tod_Select(stage), Stage_LoadHorizons(), !Stage_LoadC3d()) ||
            (Stage_LoadScenery(), false)) {
            plat_log("viewer: no such stage");
            return false;
        }
        V.m = g_stage_mesh;
        V.stage = true;
    } else {
        car_paths(car, path, tex, sizeof path);
        V.file = File_Load(path, 1);
        if (!V.file || !C3d_Load(&V.m, V.file, File_LastSize(), g_tex_count)) {
            plat_log("viewer: no such car model");
            return false;
        }
    }
    int before = V.stage ? 0 : g_tex_count;
    if (!V.stage) {
        Bfl_Mount(&V.tex, tex);
        C3d_Textures(V.m.data + V.m.slots, V.m.data + V.m.paths, V.m.ntex, V.m.kind, &V.tex, "");
        C3d_Build(&V.m);
    } else {
        Section_Init(&g_world);
        Stage_LoadTre(stage, true);
        Hor_Apply();         /* 0x460da0, later in Stage_Load */
        Tod_StartLights();   /* the race start, 0x45eca0 */
        if (car && !park_car(car, stage)) plat_log("viewer: the car could not be parked");
    }
    build_meshes();
    if (!V.stage) {
        /* the scene: a stand (a node the viewer turns), the car's objects attached under it */
        V.stand.kind = V.camera.kind = 3;
        V.stand.mask = V.camera.mask = 0xff;
        Mat_Identity(&V.stand.world);
        Mat_Identity(&V.camera.world);
        V.car = C3d_Objects(&V.m, &V.stand);
    }
    int ok = 0;
    for (int i = before; i < g_tex_count; i++) ok += g_tex[i] && g_tex[i]->surface;
    char msg[200];
    snprintf(msg, sizeof msg, "viewer: %d/%d textures from %s", ok, g_tex_count - before, tex);
    plat_log(msg);
    /* one white directional light from above and in front, a grey ambient, a white material */
    D3DLIGHT7 l = {0};
    l.type = D3DLIGHT_DIRECTIONAL;
    l.diffuse = (D3DCOLORVALUE){1, 1, 1, 1};
    l.direction = (D3DVECTOR){-0.4f, -0.8f, 0.45f};
    D3d_SetLight(0, &l);
    D3d_LightEnable(0, true);
    D3DMATERIAL7 mat = {{1, 1, 1, 1}, {1, 1, 1, 1}, {0, 0, 0, 0}, {0, 0, 0, 0}, 0};
    D3d_SetMaterial(&mat);
    /* the title: hel_15pt from the front end's 640 set, through Font_Print and the 2D queues */
    Sprite_Init();
    Tri_Init();
    Font_InitSlots(4);
    if (Bfl_Mount(&V.fe, "FrontEnd\\res640.bfl")) Font_LoadSlot("FrontEnd", "hel_15pt", &V.fe, 0);
    snprintf(V.name, sizeof V.name, "%s%s%s", V.stage ? stage : car, V.parked ? " " : "", V.parked ? car : "");
    snprintf(V.title, sizeof V.title, "%s  %d polygons  %d textures", V.name, (int)V.m.npoly, V.m.ntex);
    g_ambient_a = g_ambient_b = 0xff404040u;
    /* nearly every mesh keeps the lighting mode (flag 0x20), which the world's draw sets: here lit for
       a car, the vertex colours unlit for a stage (the viewer's choice until step 5) */
    Light_SetMode(V.stage ? 4 : 2);
    D3d_SetTextureStageState(0, D3DTSS_MINFILTER, 2);
    /* Proj_Set with 60 degrees vertically at 4:3 (the scales are halved by the game): 2 cot 30 = 3.464,
       times 3/4 for x; far 2,000 for a stage, 100 for a car; near 0.1 */
    /* far 250 for a stage: the game's usual far plane, which also bounds the visible cells */
    Proj_Set((int32_t)(2.598f * 65536), (int32_t)(3.464f * 65536), (V.stage ? 250 : 100) << 16, 6554);
    return true;
}

/* A look-at view, left-handed. */
static void look_at(float ex, float ey, float ez, float tx, float ty, float tz, D3DMATRIX *m)
{
    float z[3] = {tx - ex, ty - ey, tz - ez}, up[3] = {0, 1, 0}, x[3], y[3];
    float l = sqrtf(z[0] * z[0] + z[1] * z[1] + z[2] * z[2]);
    for (int i = 0; i < 3; i++) z[i] /= l;
    x[0] = up[1] * z[2] - up[2] * z[1];
    x[1] = up[2] * z[0] - up[0] * z[2];
    x[2] = up[0] * z[1] - up[1] * z[0];
    l = sqrtf(x[0] * x[0] + x[1] * x[1] + x[2] * x[2]);
    for (int i = 0; i < 3; i++) x[i] /= l;
    y[0] = z[1] * x[2] - z[2] * x[1];
    y[1] = z[2] * x[0] - z[0] * x[2];
    y[2] = z[0] * x[1] - z[1] * x[0];
    float e[3] = {ex, ey, ez};
    identity(m);
    for (int i = 0; i < 3; i++) {
        m->m[i][0] = x[i];
        m->m[i][1] = y[i];
        m->m[i][2] = z[i];
    }
    m->m[3][0] = -(x[0] * e[0] + x[1] * e[1] + x[2] * e[2]);
    m->m[3][1] = -(y[0] * e[0] + y[1] * e[1] + y[2] * e[2]);
    m->m[3][2] = -(z[0] * e[0] + z[1] * e[1] + z[2] * e[2]);
}

/* The inverse of a rotation-and-move matrix (the camera's world from a view). */
static void invert_rigid(const D3DMATRIX *v, D3DMATRIX *o)
{
    identity(o);
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) o->m[i][j] = v->m[j][i];
    for (int j = 0; j < 3; j++)
        o->m[3][j] = -(v->m[3][0] * o->m[0][j] + v->m[3][1] * o->m[1][j] + v->m[3][2] * o->m[2][j]);
}

/* A translucent band and the title over it, queued on layer 1 of both 2D queues (the car view flushes
   them at once; World_Render flushes them last). The meshes leave blending off (alpha-tested), so it
   goes back on first, as the game does before its 2D. */
static void title(void)
{
    static const uint8_t band[4] = {0, 0, 0, 0x90}, white[4] = {255, 255, 255, 255};
    int32_t a0[2] = {0, 8 << 16}, a1[2] = {640 << 16, 8 << 16}, a2[2] = {0, 34 << 16}, a3[2] = {640 << 16, 34 << 16};
    Tri_Add(a0, a1, a2, band, 1);
    Tri_Add(a1, a3, a2, band, 1);
    Font_Print(0, V.title, 320, 12, white, 2);
    if (V.car) {
        Render_AlphaBlend(1);
        Tri_Flush(1);
        Sprite_Flush(1);
    }
}

void viewer_frame(void)
{
    float a = (float)V.frame++ * 0.02f + 0.6f;
    D3DMATRIX view, rot;
    identity(&rot);
    if (V.stage) {
        /* along the meshes in file order (the stage's sections run along the road), 12 units up */
        int n = V.nmesh > 8 ? V.nmesh - 8 : 1, i = (V.frame / 10) % n;
        float t = (float)(V.frame % 10) / 10.0f, e[3], g[3];
        for (int j = 0; j < 3; j++) {
            e[j] = V.centre[i][j] + (V.centre[i + 1][j] - V.centre[i][j]) * t;
            g[j] = V.centre[i + 6][j];
        }
        look_at(e[0], e[1] + 12.0f, e[2], g[0], g[1], g[2], &view);
    } else {
        look_at(0, 1.6f, -5.5f, 0, 0.3f, 0, &view);
        rot.m[0][0] = cosf(a);
        rot.m[0][2] = -sinf(a);
        rot.m[2][0] = sinf(a);
        rot.m[2][2] = cosf(a);
    }
    D3d_SetTransform(D3DTS_VIEW, &view);
    Render_SetClearColour(0x30, 0x40, 0x58);
    Render_ClearTarget();
    int32_t r[4] = {0, 0, 640, 480};
    D3d_Clear(1, r, D3DCLEAR_ZBUFFER, 0, 1.0f, 0);
    if (V.car) {
        D3d_BeginScene();
        /* the stand turns (16.16 rows), the camera sits 5.5 back and 1.6 up looking slightly down */
        Mat_Identity(&V.stand.world);
        V.stand.world.m[0] = (int32_t)(rot.m[0][0] * 65536);
        V.stand.world.m[2] = (int32_t)(rot.m[0][2] * 65536);
        V.stand.world.m[8] = (int32_t)(rot.m[2][0] * 65536);
        V.stand.world.m[10] = (int32_t)(rot.m[2][2] * 65536);
        V.stand.dirty = 1;
        D3DMATRIX inv;
        look_at(0, 1.6f, -5.5f, 0, 0.3f, 0, &view);
        invert_rigid(&view, &inv);
        for (int i = 0; i < 16; i++) V.camera.world.m[i] = (int32_t)lrintf(inv.m[i / 4][i % 4] * 65536);
        V.camera.dirty = 1;
        Obj_Frame(&V.stand, &V.camera);
        Obj_Draw(V.car, 0);
        title();
        D3d_EndScene();
    } else {
        /* the camera object from the fly-through's view; the sky follows it, the objects are brought up
           to date, and World_Render draws the view with the title queued on 2D layer 1 */
        if (V.parked) {
            /* a camera turning slowly around the parked car, 7 out and 2.5 up */
            const int32_t *m = V.parked->local.m;
            float cx = m[12] / 65536.0f, cy = m[13] / 65536.0f, cz = m[14] / 65536.0f, t = V.frame * 0.01f + 3.3f;
            look_at(cx + 7 * sinf(t), cy + 2.5f, cz + 7 * cosf(t), cx, cy + 0.6f, cz, &view);
            D3d_SetTransform(D3DTS_VIEW, &view);
        }
        D3DMATRIX inv;
        invert_rigid(&view, &inv);
        for (int i = 0; i < 16; i++) V.camera.world.m[i] = (int32_t)lrintf(inv.m[i / 4][i % 4] * 65536);
        V.camera.dirty = 1;
        int32_t cp[3] = {V.camera.world.m[12], V.camera.world.m[13], V.camera.world.m[14]};
        Sky_Follow(cp);
        Obj_Frame(&g_world, &V.camera);
        snprintf(V.title, sizeof V.title, "%s  %d of %d cells in view", V.name, g_nvisible, g_nsec);
        title();
        static const int16_t full[4] = {0, 0, 640, 480};
        World_Render(&V.camera, full, 0, true);
    }
    D3d_Present();
}
