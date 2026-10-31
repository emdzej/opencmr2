/* Loading a stage (see stage.h). */
#include "world/stage.h"
#include "render/mesh.h"
#include "render/tex.h"
#include "world/section.h"
#include "world/tre.h"
#include "world/hpc.h"
#include <stdio.h>
#include "exe.h"
#include <string.h>

BflSlot g_stage_bfl, g_track_tex;
Obj g_world;
C3d g_stage_mesh;

bool Stage_Mount(const char *stage_bfl, const char *tex_bfl)
{
    memset(&g_world, 0, sizeof g_world);
    g_world.kind = 3;
    g_world.mask = 0xff;
    g_world.section = -1;
    Mat_Identity(&g_world.world);
    Mat_Identity(&g_world.local);
    bool a = Bfl_Mount(&g_stage_bfl, stage_bfl), b = Bfl_Mount(&g_track_tex, tex_bfl);
    return a && b;
}

/* FUN_0041f500: the texture slot, NULL unless loaded */
static const BflSlot *track_tex(void) { return g_track_tex.loaded ? &g_track_tex : NULL; }

/* A stage member through C3d_Load with the world parent and the track textures (and the port's
   C3d_Build, the objects, the scenery records). */
static bool load_member(const char *name, C3d *m)
{
    uint32_t size = 0;
    uint8_t *d = (uint8_t *)Bfl_FindFile(&g_stage_bfl, name, NULL, &size, false);
    if (!d || !C3d_Load(m, d, size, g_tex_count)) return false;
    C3d_Textures(d + m->slots, d + m->paths, m->ntex, m->kind, track_tex(), "");
    C3d_Build(m);
    C3d_Objects(m, &g_world);
    Scenery_Register(m);
    return true;
}

bool Stage_LoadC3d(void)
{
    if (!load_member("TEMP.C3D", &g_stage_mesh)) return false;
    Section_Register(&g_stage_mesh);
    return true;
}

C3d g_stage_obj, g_stage_sht;
int g_obj_first, g_obj_scenery, g_sht_first, g_sht_scenery;   /* 0x58ca74, 0x58ca68, 0x58c934, 0x58c950 */

bool Stage_LoadObj(void) { return load_member("TEMP.OBJ", &g_stage_obj); }
bool Stage_LoadSht(void) { return load_member("TEMP.SHT", &g_stage_sht); }

/* 0x471af0: the object and scenery counts before each (the ranges they own), TEMP.OBJ, TEMP.SHT, then
   the sky and ground. */
void Stage_LoadScenery(void)
{
    g_obj_first = g_nobjs;
    g_obj_scenery = g_nscenery;
    Stage_LoadObj();
    g_sht_first = g_nobjs;
    g_sht_scenery = g_nscenery;
    Stage_LoadSht();
    Stage_LoadSkyGround(0, 0, false);
}

BflSlot g_clouds;                               /* 0x589448 */
static C3d sky_m, cloud_m, top_m, ground_m;
Obj *g_sky, *g_cloud, *g_cloud_top, *g_ground; /* 0x589438, 0x589440, 0x589444, 0x58943c */

static void wr32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8), p[2] = (uint8_t)(v >> 16), p[3] = (uint8_t)(v >> 24); }

/* +0x180 off (always active) and FUN_004b2e40: the mesh's +0x2c to 0 */
static void sky_part(Obj *o)
{
    if (!o) return;
    o->sorted = 0;
    if (o->mesh) wr32(o->mesh + 0x2c, 0);
}

static Obj *load_into(const BflSlot *arch, const BflSlot *tex, const char *name, C3d *m)
{
    uint32_t size = 0;
    uint8_t *d = (uint8_t *)Bfl_FindFile(arch, name, NULL, &size, false);
    if (!d || !C3d_Load(m, d, size, g_tex_count)) return NULL;
    C3d_Textures(d + m->slots, d + m->paths, m->ntex, m->kind, tex, "");
    C3d_Build(m);
    return C3d_Objects(m, &g_world);
}

/* The cloud level is the larger of the stage's two conditions through the byte table at 0x51c688 (0
   light, 1 medium, 2 heavy; anything else loads no clouds). low picks the PcLow set (FUN_00405d00). */
void Stage_LoadSkyGround(int cond0, int cond1, bool low)
{
    g_sky = load_into(&g_stage_bfl, track_tex(), exe_str(0x51c6f4), &sky_m);   /* TEMP.SKY */
    if (g_sky) {
        sky_part(g_sky);
        Obj *c = g_sky->child;
        sky_part(c);
        if (c && c->next) {
            sky_part(c->next);
            sky_part(c->next->next);
        }
    }
    const uint8_t *tbl = exe_data(0x51c688, 16);
    uint8_t level = 0;
    if (tbl) {
        if (tbl[cond0 & 15]) level = tbl[cond0 & 15];
        if (level < tbl[cond1 & 15]) level = tbl[cond1 & 15];
    }
    static const uint32_t names[3] = {0x51c694, 0x51c6a0, 0x51c6ac};   /* Cloud_Light, _Med, _Heavy */
    if (level <= 2) {
        char path[260], name[64], member[80];
        snprintf(name, sizeof name, "%s", exe_str(names[level]));
        snprintf(path, sizeof path, exe_str(0x51c6c4), "Game\\Tracks");   /* "%s\\Clouds\\" (FUN_0040ed20) */
        size_t l = strlen(path);
        snprintf(path + l, sizeof path - l, "%s%s%s", exe_str(low ? 0x51c6b8 : 0x51c6c0), name, exe_str(0x51c6ec));
        Bfl_Mount(&g_clouds, path);
        snprintf(member, sizeof member, "%s%s", name, exe_str(0x51c6dc));   /* <name>top.c3d */
        g_cloud_top = load_into(&g_clouds, &g_clouds, member, &top_m);
        if (g_cloud_top) {
            sky_part(g_cloud_top);
            g_cloud_top->mask = 0;
        }
        snprintf(member, sizeof member, "%s%s", name, exe_str(0x51c6e4));   /* <name>.c3d */
        g_cloud = load_into(&g_clouds, &g_clouds, member, &cloud_m);
        sky_part(g_cloud);
    }
    g_ground = load_into(&g_stage_bfl, track_tex(), exe_str(0x51c6d0), &ground_m);   /* TEMP.GRO */
    sky_part(g_ground);
}

/* Sky_Follow 0x46f330: the sky, ground and clouds over the view's position, 15 lower, plus the stage's
   height offsets (FUN_004928c0, not ported yet: 0); the cloud top 20 below the clouds. */
void Sky_Follow(const int32_t pos[3])
{
    int32_t y = pos[1] - 0xf0000;
    int32_t sky[3] = {pos[0], y, pos[2]}, ground[3] = {pos[0], y, pos[2]}, cloud[3] = {pos[0], y, pos[2]};
    if (g_sky) Obj_SetPosition(g_sky, sky);
    if (g_ground) Obj_SetPosition(g_ground, ground);
    if (g_cloud) Obj_SetPosition(g_cloud, cloud);
    if (g_cloud_top) {
        cloud[1] -= 0x140000;
        Obj_SetPosition(g_cloud_top, cloud);
    }
}

/* The part of Stage_Load 0x41f930 after Section_Init: "<stage>.tre" and "<stage>.xhi" from the stage
   slot, the shadows only at high scenery detail (0x405ba0: bit 4 of 0x52afd0). */
void Stage_LoadTre(const char *stage, bool detail)
{
    char name[32];
    uint32_t size = 0, xsize = 0;
    snprintf(name, sizeof name, "%s.tre", stage);
    uint8_t *tre = (uint8_t *)Bfl_FindFile(&g_stage_bfl, name, NULL, &size, false);
    snprintf(name, sizeof name, "%s.xhi", stage);
    const uint8_t *xhi = Bfl_FindFile(&g_stage_bfl, name, NULL, &xsize, false);
    if (tre && !detail) tre = NULL;
    Tre_Load(tre, size, xhi);
}

static const uint8_t *member(const char *stage, const char *ext, uint32_t *size)
{
    char name[32];
    snprintf(name, sizeof name, "%s.%s", stage, ext);
    *size = 0;
    return Bfl_FindFile(&g_stage_bfl, name, NULL, size, false);
}

bool Stage_LoadHpc(const char *stage)
{
    uint32_t size;
    const uint8_t *d = member(stage, "hpc", &size);
    return d && Hpc_Parse(d, size);
}

static const uint8_t *csp;   /* 0x542870 */

bool Stage_LoadCsp(const char *stage)
{
    uint32_t size;
    csp = member(stage, "csp", &size);
    if (size < 0x1e) csp = NULL;
    return csp != NULL;
}

static int32_t rd32s(const uint8_t *p) { return (int32_t)((uint32_t)p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24); }

/* 0x456c70 -> 0x4583d0 in a rally (the head-to-head and arcade cases take other entries): the first
   record's x and z; y is left as the buffer at 0x542878 held. */
void Csp_StartPos(int car, int32_t pos[3])
{
    (void)car;
    static int32_t held[3];   /* 0x542878 */
    if (csp) {
        held[0] = rd32s(csp);
        held[2] = rd32s(csp + 8);
    }
    for (int i = 0; i < 3; i++) pos[i] = held[i];
}

/* 0x456c90: the start angles, three u16 at +0x18 */
void Csp_Angles(uint16_t a[3])
{
    for (int i = 0; i < 3; i++) a[i] = csp ? (uint16_t)(csp[0x18 + 2 * i] | csp[0x19 + 2 * i] << 8) : 0;
}

void Stage_Unload(void)
{
    Hpc_Free();
    csp = NULL;
    Tre_Free();
    Bfl_Unmount(&g_stage_bfl);
    Bfl_Unmount(&g_track_tex);
}
