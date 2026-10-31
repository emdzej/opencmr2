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
    int frame;
    bool stage;
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

bool viewer_init(const char *car, const char *stage)
{
    char path[128], tex[128];
    uint32_t size = 0;
    if (stage) {
        /* Game\Tracks\<country>\<stage>Hi.bfl, the country's textures in <ccc>hit.bfl beside it */
        if (!find_stage(stage, path, tex, sizeof path)) {
            plat_log("viewer: no such stage");
            return false;
        }
        V.file = File_Load(path, 1);
        BflSlot a = {V.file, File_LastSize(), 1};
        uint8_t *m = V.file ? (uint8_t *)Bfl_Find(&a, "temp.c3d", &size) : NULL;
        if (!m || !C3d_Load(&V.m, m, size, g_tex_count)) {
            plat_log("viewer: no such stage");
            return false;
        }
        V.stage = true;
    } else {
        snprintf(path, sizeof path, "Game\\Cars\\%s.c3d", car);
        /* the texture archive is the set's: f99a1N.c3d, f99a1L.c3d use f99a1.bfl */
        snprintf(tex, sizeof tex, "Game\\Cars\\%s.bfl", car);
        size_t l = strlen(tex);
        if (!vfs_exists(tex) && l > 5 && strchr("NLS", tex[l - 5])) memmove(tex + l - 5, tex + l - 4, 5);
        V.file = File_Load(path, 1);
        if (!V.file || !C3d_Load(&V.m, V.file, File_LastSize(), g_tex_count)) {
            plat_log("viewer: no such car model");
            return false;
        }
    }
    if (!D3d_Open(640, 480, 32)) return false;
    Render_InitStates();
    VB_CreatePool();
    Bfl_Mount(&V.tex, tex);
    int before = g_tex_count;
    C3d_Textures(V.m.data + V.m.slots, V.m.data + V.m.paths, V.m.ntex, V.m.kind, &V.tex, "");
    C3d_Build(&V.m);
    build_meshes();
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
    g_ambient_a = g_ambient_b = 0xff404040u;
    /* nearly every mesh keeps the lighting mode (flag 0x20), which the world's draw sets: here lit for
       a car, the vertex colours unlit for a stage (the viewer's choice until step 5) */
    Light_SetMode(V.stage ? 4 : 2);
    D3d_SetTextureStageState(0, D3DTSS_MINFILTER, 2);
    /* a left-handed perspective, 60 degrees vertically, as Direct3D's samples build it */
    float zn = 0.1f, zf = V.stage ? 2000.0f : 100.0f, fov = 1.0471976f, ys = 1.0f / tanf(fov / 2), xs = ys * 480.0f / 640.0f;
    D3DMATRIX proj = {{{xs, 0, 0, 0}, {0, ys, 0, 0}, {0, 0, zf / (zf - zn), 1}, {0, 0, -zn * zf / (zf - zn), 0}}};
    D3d_SetTransform(D3DTS_PROJECTION, &proj);
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

void viewer_frame(void)
{
    float a = (float)V.frame++ * 0.02f + 0.6f;
    D3DMATRIX view, rot, world;
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
    D3d_BeginScene();
    Render_ClearTarget();
    int32_t r[4] = {0, 0, 640, 480};
    D3d_Clear(1, r, D3DCLEAR_ZBUFFER, 0, 1.0f, 0);
    for (int i = 0; i < V.nmesh; i++) {
        mul(&V.mesh[i].place, &rot, &world);
        D3d_SetTransform(D3DTS_WORLD, &world);
        Mesh_Draw(&V.m, (uint8_t *)C3d_Mesh(&V.m, i), 0, 0, 0);
    }
    D3d_EndScene();
    D3d_Present();
}
