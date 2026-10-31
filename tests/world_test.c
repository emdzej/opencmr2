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
#include "world/stage.h"
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
    Section_Init(&g_world);
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
    Stage_Unload();
    printf("world_test: %s\n", fails ? "FAILED" : "ok");
    return fails != 0;
}
