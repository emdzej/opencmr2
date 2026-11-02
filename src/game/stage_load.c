/* Stage_Load 0x41f930 (see stage_load.h). */
#include "game/stage_load.h"
#include "game/camera.h"
#include "game/weather.h"
#include "game/sectors.h"
#include "game/hud.h"
#include "game/opponents.h"
#include "game/ai.h"
#include "audio/codriver.h"
#include "audio/surface.h"
#include "game/race.h"
#include "sim/progress.h"
#include "sim/scenery.h"
#include "world/fin.h"
#include "world/obj.h"
#include "world/section.h"
#include "world/stage.h"
#include "world/tod.h"
#include "game/lights.h"
#include "game/replay.h"
#include "game/records.h"
#include "render/mesh.h"
#include "pending.h"
#include "exe.h"
#include "game/mem.h"
#include "vfs.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include "game/gstate.h"

bool Stage_Load(const char *stage, const char *path, const char *tex)
{
    /* 1: the country's textures and the stage archive; the loading bar (0x40fec0) */
    if (!Stage_Mount(path, tex)) return false;
    Hud_LoadMap();   /* 0x411280 */
    /* the reflection texture (Tex_LoadEnvMap 0x4b23c0) from the track's archive: the replays' name
       (0x538340, "...\\fra02hi") with the stage's two digits replaced by the detail, "...\\frahi.tga"; in
       a time trial (0x407e50) the archive's name (0x538548) with its "t.bfl" for ".tga", the same */
    {
        char n[0x110];
        if (Gs_SelBit24()) {
            snprintf(n, sizeof n, "%s", (const char *)gm(0x538548));
            size_t l = strlen(n);
            if (l >= 5) snprintf(n + l - 5, sizeof n - (l - 5), "%s", exe_str(0x519484));
        } else {
            snprintf(n, sizeof n, "%s", (const char *)gm(0x538340));
            size_t l = strlen(n);
            if (l >= 4) n[l - 4] = n[l - 2], n[l - 3] = n[l - 1], n[l - 2] = 0;
            strncat(n, exe_str(0x519484), sizeof n - strlen(n) - 1);
        }
        Env_Load(g_track_tex.loaded ? &g_track_tex : NULL, n);
    }
    PENDING_NOTE(0x40fec0, "LoadBar_Step");
    /* 2: the horizons for the selection's time of day, the BSP (0x420020), TEMP.C3D */
    Stage_LoadHorizons();
    {
        /* Stage_LoadBsp 0x420020: the start gantry's frame, <stage>.bsp */
        char n[80];
        snprintf(n, sizeof n, "%s.bsp", stage);
        uint32_t size;
        const uint8_t *bsp = Bfl_FindFile(&g_stage_bfl, n, NULL, &size, false);
        Lights_Gantry(bsp && size >= 48 ? bsp : NULL);   /* 0x463290 */
    }
    if (!Stage_LoadC3d()) return false;
    /* 3: the collision mesh, the surfaces (0x41fcd0), the walls, the scenery, the start, the centre
       line, the co-driver's notes (0x416720) */
    if (!Stage_LoadHpc(stage)) return false;
    {
        /* Stage_LoadSrf 0x41fcd0: <stage>.srf, the surfaces whose sound sets are loaded */
        char n[80];
        snprintf(n, sizeof n, "%s.srf", stage);
        uint32_t size = 0;
        const uint8_t *srf = Bfl_FindFile(&g_stage_bfl, n, NULL, &size, false);
        Surface_Mark(srf, srf ? size : 0);   /* 0x41afe0 */
    }
    Stage_LoadFinDat(stage);
    Stage_LoadScenery();
    Stage_LoadCsp(stage);
    Stage_LoadCat(stage);
    Sectors_Init();   /* 0x458090 */
    Hud_Init(0);      /* 0x411450 */
    if (Gs_Mode() != 4) Rec_StageStart();   /* 0x4d0180 */
    Codriver_LoadCod(stage);   /* 0x416720 */
    if (!gm8(0x52ea52) && Gs_Mode() == 3) Replay_Setup();   /* 0x41c260: mode 3's replays here */
    Weather_StageLoad();       /* 0x45ea70 */
    Stage_LoadTm(stage);       /* 0x455300 */
    if (Race_Reentry() != 2) Opp_Times(true);   /* 0x455470 (state 3, not on the way to state 4) */
    Ai_LoadLines(stage);       /* 0x47c2f0 */
    /* 4: the trackside cameras' splines (0x48caa0), the sections */
    {
        char n[80];
        snprintf(n, sizeof n, "%s.tsc", stage);
        uint32_t size = 0;
        const uint8_t *tsc = Bfl_FindFile(&g_stage_bfl, n, NULL, &size, false);
        Camera_LoadTsc(tsc, tsc ? size : 0);   /* 0x48caa0 */
    }
    Section_Init(&g_world);
    /* 5: the tree shadows and the .xhi; later the horizon colours (0x460da0) */
    Stage_LoadTre(stage, true);
    Hor_Apply();
    Scenery_CollInit();   /* 0x471dd0 */
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

/* Race_Setup 0x41f560's arcade branch (modes 5-7, 11, 12): the location (selection bits 10-11) and
   its stage (12-13) index 0-7 into the rally table {6, 3, 1, 4, 0, 2, 5, 7} (on the stack); the stage
   archive Tracks\chall\cha<n><hi|lo>.bfl, its files <prefix>-ch (the eighth "UKi-ch"), the rally's
   textures. hi or lo by the clouds' detail (0x405d00). */
bool Stage_Arcade(char *stage, size_t scap, char *path, char *tex, size_t cap)
{
    uint32_t m = Gs_Mode();
    if (m != 5 && m != 6 && m != 7 && m != 11 && m != 12) return false;
    static const int rally_of[8] = {6, 3, 1, 4, 0, 2, 5, 7};
    uint32_t sel = gm32(0x52f2ac);
    int idx = (int)(sel >> 10 & 3) * 3 + (int)(sel >> 12 & 3);
    if (idx > 7) idx = 7;
    int r = rally_of[idx];
    const char *detail = exe_str((gm32(0x52afd0) >> 0x1b & 3) == 0 ? 0x519468 : 0x519464);
    const char *prefix = exe_str(exe_u32(0x51931c + 4u * (uint32_t)r));
    snprintf(stage, scap, "%s-ch", idx == 7 ? exe_str(0x5193f0) : prefix);
    for (char *c = stage; *c; c++) *c = (char)tolower((unsigned char)*c);   /* 0x4aa700 */
    snprintf(path, cap, "Game\\Tracks\\chall\\cha%02d%s.bfl", idx + 1, detail);
    snprintf(tex, cap, "Game\\Tracks\\%s\\%s%st.bfl", exe_str(exe_u32(0x5192f8 + 4u * (uint32_t)r)), prefix, detail);
    return true;
}
