/* Loading a stage (see stage.h). */
#include "world/stage.h"
#include "render/mesh.h"
#include "render/tex.h"
#include "world/section.h"
#include <stdio.h>
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
   the sky and ground (Stage_LoadSkyGround 0x46f060, not yet). */
void Stage_LoadScenery(void)
{
    g_obj_first = g_nobjs;
    g_obj_scenery = g_nscenery;
    Stage_LoadObj();
    g_sht_first = g_nobjs;
    g_sht_scenery = g_nscenery;
    Stage_LoadSht();
}

void Stage_Unload(void)
{
    Bfl_Unmount(&g_stage_bfl);
    Bfl_Unmount(&g_track_tex);
}
