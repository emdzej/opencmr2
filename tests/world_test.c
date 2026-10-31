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
#include "world/stage.h"
#include "world/tre.h"
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
    /* the world frame: the whole view through World_Render */
    b = D3d_Stats();
    static const int16_t full[4] = {0, 0, 640, 480};
    CHECK(World_Render(&cam, full, 0, true));
    printf("World_Render: %u draws\n", D3d_Stats().draws - b.draws);
    CHECK(D3d_Stats().draws - b.draws > 100 && D3d_Stats().scenes - b.scenes == 1);
    /* the sky follows a position; the update moves it into that position's cell */
    int32_t at[3] = {512 << 16, 30 << 16, 1190 << 16};
    Sky_Follow(at);
    Obj_Frame(&g_world, NULL);
    CHECK(g_sky->section == 8 && g_sky->world.m[13] == (30 << 16) - 0xf0000);
    int found = 0;
    for (Obj *o = g_sec[8].objs; o; o = o->section_next) found |= o == g_sky;
    CHECK(found);
    Stage_Unload();
    printf("world_test: %s\n", fails ? "FAILED" : "ok");
    return fails != 0;
}
