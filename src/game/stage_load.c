/* Stage_Load 0x41f930 (see stage_load.h). */
#include "game/stage_load.h"
#include "game/sectors.h"
#include "sim/progress.h"
#include "world/fin.h"
#include "world/obj.h"
#include "world/section.h"
#include "world/stage.h"
#include "world/tod.h"
#include "pending.h"

bool Stage_Load(const char *stage, const char *path, const char *tex)
{
    /* 1: the country's textures and the stage archive; the loading bar (0x40fec0) */
    if (!Stage_Mount(path, tex)) return false;
    PENDING_NOTE(0x40fec0, "LoadBar_Step");
    /* 2: the horizons for the selection's time of day, the BSP (0x420020), TEMP.C3D */
    Stage_LoadHorizons();
    PENDING_NOTE(0x420020, "Stage_LoadBsp");
    if (!Stage_LoadC3d()) return false;
    /* 3: the collision mesh, the surfaces (0x41fcd0), the walls, the scenery, the start, the centre
       line, the co-driver's notes (0x416720) */
    if (!Stage_LoadHpc(stage)) return false;
    PENDING_NOTE(0x41fcd0, "Stage_LoadSrf");
    Stage_LoadFinDat(stage);
    Stage_LoadScenery();
    Stage_LoadCsp(stage);
    Stage_LoadCat(stage);
    Sectors_Init();   /* 0x458090 */
    PENDING_NOTE(0x416720, "Codriver_LoadCod");
    /* 4: the trackside cameras' splines (0x48caa0), the sections */
    PENDING_NOTE(0x48caa0, "Stage_LoadTsc");
    Section_Init(&g_world);
    /* 5: the tree shadows and the .xhi; later the horizon colours (0x460da0) */
    Stage_LoadTre(stage, true);
    Hor_Apply();
    return true;
}
