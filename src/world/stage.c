/* Loading a stage (see stage.h). */
#include "world/stage.h"
#include "render/mesh.h"
#include "render/tex.h"
#include "world/section.h"
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

bool Stage_LoadC3d(void)
{
    uint32_t size = 0;
    uint8_t *d = (uint8_t *)Bfl_FindFile(&g_stage_bfl, "TEMP.C3D", NULL, &size, false);
    if (!d || !C3d_Load(&g_stage_mesh, d, size, g_tex_count)) return false;
    C3d_Textures(d + g_stage_mesh.slots, d + g_stage_mesh.paths, g_stage_mesh.ntex, g_stage_mesh.kind, track_tex(), "");
    C3d_Build(&g_stage_mesh);
    C3d_Objects(&g_stage_mesh, &g_world);
    Section_Register(&g_stage_mesh);
    return true;
}

void Stage_Unload(void)
{
    Bfl_Unmount(&g_stage_bfl);
    Bfl_Unmount(&g_track_tex);
}
