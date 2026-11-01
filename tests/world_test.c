/* The stage path without a GPU: France stage 2's TEMP.C3D through Stage_LoadC3d, its section grid
   (Section_Init), the cell lookup (Section_Find) and a camera's visible cells (Section_BuildVisible).
   Skips without data. */
#include "exe.h"
#include "fixmath.h"
#include "render/d3d7.h"
#include "render/mesh.h"
#include "render/tex.h"
#include "render/view.h"
#include "vfs.h"
#include "vfs_host.h"
#include "world/section.h"
#include "world/frame.h"
#include "game/mem.h"
#include "world/stage.h"
#include "world/tre.h"
#include "world/tod.h"
#include "world/sky.h"
#include "world/relight.h"
#include "world/hpc.h"
#include <stdio.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

int main(void)
{
    char err[200];
    if (!vfs_mount_default() || !exe_init(err, sizeof err)) {
        printf("world_test: no game data, skipped\n");
        return 0;
    }
    Mem_Init();
    Math_InitTables();
    D3d_Open(640, 480, 16);
    VB_CreatePool();
    CHECK(Stage_Mount("Game\\Tracks\\France\\Fra02Hi.bfl", "Game\\Tracks\\France\\frahit.bfl"));
    CHECK(Stage_LoadC3d());
    Stage_LoadScenery();
    CHECK(g_nscenery == 1595 + 236 && g_sht_scenery == 1595);
    CHECK(g_sky && g_ground && g_cloud && g_cloud_top && g_cloud_top->mask == 0 && g_sky->sorted == 0);
    Section_Init(&g_world);
    Stage_LoadTre("fra02", true);
    /* fra02 runs from 12:00 to 13:00 in clear weather: the 12:00 record as it is */
    CHECK(Tod_Select("fra02") && Tod_Rally() == 2 && Tod_Stage() == 1);
    CHECK(Tod_StartTime() == 1200 && Tod_EndTime() == 1300);
    Stage_LoadHorizons();
    Hor_Apply();
    CHECK(g_hor_start && g_hor_end);
    printf("fra02 lights: low %d %d %d, high %d %d %d, shadow %06x alpha %d\n", g_hor_low[0] >> 16, g_hor_low[1] >> 16,
           g_hor_low[2] >> 16, g_hor_high[0] >> 16, g_hor_high[1] >> 16, g_hor_high[2] >> 16, g_shadow_rgb & 0xffffff, g_shadow_alpha >> 16);
    CHECK(g_hor_low[0] == 106 << 16 && g_hor_high[2] == 245 << 16);
    Tod_StartLights();
    printf("ramps: %08x .. %08x, shadow %08x .. %08x\n", Light_TerrainRamp(0), Light_TerrainRamp(0x10000),
           Light_ShadowRamp(0), Light_ShadowRamp(0x10000));
    /* the ramp's last step is 49 x 0x539, just short of 1.0 */
    CHECK(Light_TerrainRamp(0) == 0xff6a83a4u && Light_TerrainRamp(0x10000) == 0xfffefef4u);
    printf("fra02.tre: %d nodes, %d items, %d matched to scenery\n", g_tre_nodes, g_tre_items, g_tre_matched);
    CHECK(g_tre_nodes == 121 && g_tre_items == 1572 && g_tre_matched == 1572);
    int placed = 0;
    for (int i = 0; i < g_nsec; i++) placed += g_sec[i].nscenery;
    CHECK(placed == g_nscenery);
    CHECK(g_nsec == 360 && g_sec_row == 18 && g_sec_rows == 20 && g_sec_size == 64 << 16 && g_sec_half == 32 << 16);
    int32_t p[3] = {512 << 16, 0, 1190 << 16};   /* in cell 8 (x 480-544, z 1184-1248) */
    CHECK(Section_Find(p) == 8);
    int32_t far_out[3] = {-(5000 << 16), 0, 0};
    CHECK(Section_Find(far_out) == 0);           /* out of the grid: 0 */
    Proj_Set(0, 0, 250 << 16, 0);
    Obj cam = {0};
    Mat_Identity(&cam.world);                    /* looking along +z ... */
    cam.world.m[8] = 0x10000, cam.world.m[10] = 0, cam.world.m[0] = 0, cam.world.m[2] = -0x10000;   /* ... turned to +x */
    cam.world.m[12] = 345 << 16;
    cam.world.m[14] = 1083 << 16;
    Section_BuildVisible(&cam);
    int with_mesh = 0, has41 = 0;
    for (int i = 0; i < g_nvisible; i++) {
        with_mesh += g_sec[g_visible[i]].mesh != NULL;
        has41 |= g_visible[i] == 41;
    }
    printf("fra02: %d cells, %d visible from cell 41 looking +x (%d with a mesh)\n", g_nsec, g_nvisible, with_mesh);
    CHECK(has41 && g_nvisible > 9 && g_nvisible < 60);
    D3dStats b = D3d_Stats();
    Stage_DrawTerrain(&g_stage_mesh);
    CHECK(D3d_Stats().draws > b.draws);
    Billboard_SetCamera(&cam);
    b = D3d_Stats();
    Section_DrawBillboards(&cam);
    Billboard_DrawQueued();
    printf("scenery in view: %u draws\n", D3d_Stats().draws - b.draws);
    CHECK(D3d_Stats().draws > b.draws);
    /* the collision mesh and the start line */
    CHECK(Stage_LoadHpc("fra02") && Stage_LoadCsp("fra02"));
    int32_t sp[3], nrm[3];
    uint16_t ang[3], surf = 0;
    int16_t tri = -1;
    Csp_StartPos(0, sp);
    Csp_Angles(ang);
    int32_t gy = Hpc_Ground(sp, nrm, &tri, &surf, sp[1]);
    printf("fra02.hpc: %d vertices, %d triangles; start (%.2f, %.2f, %.2f) angles %u %u %u, triangle %d surface %u normal (%.3f %.3f %.3f)\n",
           g_hpc_verts, g_hpc_tris, sp[0] / 65536.0, gy / 65536.0, sp[2] / 65536.0, ang[0], ang[1], ang[2], tri, surf,
           nrm[0] / 65536.0, nrm[1] / 65536.0, nrm[2] / 65536.0);
    CHECK(g_hpc_verts == 6954 && g_hpc_tris == 12768 && tri >= 0 && nrm[1] > 0xf000);
    /* the start angles turn a rest matrix about its own axes; the rows stay unit */
    Obj t = {0};
    Mat_Identity(&t.rest);
    Mat_Identity(&t.local);
    Obj_SetAngles(&t, ang);
    printf("start heading: x row (%.3f %.3f %.3f)\n", t.local.m[0] / 65536.0, t.local.m[1] / 65536.0, t.local.m[2] / 65536.0);
    CHECK(t.local.m[5] > 0xf000 && t.dirty == 1);
    /* the world frame: the whole view through World_Render */
    b = D3d_Stats();
    static const int16_t full[4] = {0, 0, 640, 480};
    CHECK(World_Render(&cam, full, 0, true));
    printf("World_Render: %u draws\n", D3d_Stats().draws - b.draws);
    CHECK(D3d_Stats().draws - b.draws > 100 && D3d_Stats().scenes - b.scenes == 1);
    /* the sky follows a position, 15 lower and raised by the horizon's offset (the start record's, cut
       at Hor_Apply); the update moves it into that position's cell */
    int32_t at[3] = {512 << 16, 30 << 16, 1190 << 16}, so, go, co;
    Sky_Offsets(&so, &go, &co);
    printf("sky offsets: %.2f %.2f %.2f, scale %.2f\n", so / 65536.0, go / 65536.0, co / 65536.0, Sky_Scale() / 65536.0);
    Sky_Follow(at);
    Obj_Frame(&g_world, NULL);
    CHECK(g_sky->section == 8 && g_sky->world.m[13] == (30 << 16) - 0xf0000 + so);
    CHECK(Sky_Scale() == 0x10000 && g_sky->local.m[5] == 0x10000);
    int found = 0;
    for (Obj *o = g_sec[8].objs; o; o = o->section_next) found |= o == g_sky;
    CHECK(found);
    Stage_Unload();
    printf("world_test: %s\n", fails ? "FAILED" : "ok");
    return fails != 0;
}
