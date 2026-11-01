/* Stage_Load 0x41f930 (see stage_load.h). */
#include "game/stage_load.h"
#include "game/sectors.h"
#include "audio/codriver.h"
#include "sim/progress.h"
#include "world/fin.h"
#include "world/obj.h"
#include "world/section.h"
#include "world/stage.h"
#include "world/tod.h"
#include "pending.h"
#include "exe.h"
#include "game/mem.h"
#include "vfs.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>

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
    Codriver_LoadCod(stage);   /* 0x416720 */
    /* 4: the trackside cameras' splines (0x48caa0), the sections */
    PENDING_NOTE(0x48caa0, "Stage_LoadTsc");
    Section_Init(&g_world);
    /* 5: the tree shadows and the .xhi; later the horizon colours (0x460da0) */
    Stage_LoadTre(stage, true);
    Hor_Apply();
    return true;
}

/* The port: the stage's archive and its country's textures found under Game\Tracks (the original
   builds them from the selection and its tables). */
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

bool Stage_Find(const char *stage, char *path, char *tex, size_t cap)
{
    Find f = {stage, path, tex, cap, "", false};
    vfs_list("Game/Tracks", find_dir, &f);
    return f.found;
}


/* the selection's stage id: the rally's prefix (0x51931c) and the stage number from 01 */
void Stage_Name(char *dst, size_t cap)
{
    uint32_t s = gm32(0x52f2ac);
    snprintf(dst, cap, "%s%02u", exe_str(exe_u32(0x51931c + 4 * (s & 0x1f))), (s >> 5 & 0x1f) + 1);
}
