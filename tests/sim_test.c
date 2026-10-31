/* The car simulation's set-up without a GPU: the game state image and Car_Init on the 205's model
   (handling class 0, the default rally set-up). */
#include "bfl.h"
#include "c3d.h"
#include "exe.h"
#include "fixmath.h"
#include "game/gstate.h"
#include "game/mem.h"
#include "sim/car.h"
#include "sim/step.h"
#include "sim/road_follow.h"
#include "sim/progress.h"
#include "vfs.h"
#include "vfs_host.h"
#include "world/obj.h"
#include "world/stage.h"
#include "world/fin.h"
#include "world/section.h"
#include "render/d3d7.h"
#include "render/mesh.h"
#include "sim/drive.h"
#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

int main(void)
{
    char err[128];
    if (!vfs_mount_default() || !exe_init(err, sizeof err)) {
        printf("sim_test: no game data, skipped\n");
        return 0;
    }
    Math_InitTables();
    CHECK(Mem_Init());
    /* the state starts as the exe's data: a rally, no options */
    CHECK(Gs_Mode() == 0 && !Gs_Option(6) && !Gs_Option(7));
    uint8_t *d = File_Load("Game\\Cars\\205a1N.c3d", 1);
    C3d m;
    CHECK(d && C3d_Load(&m, d, File_LastSize(), 0));
    if (!d) return 1;
    memset(&g_world, 0, sizeof g_world);
    g_world.kind = 3;
    Obj *root = C3d_Objects(&m, &g_world);
    CarSlot *s = Car_Slot(0);
    s->main = root;
    s->second = Obj_FindType(root, 5);
    s->kind = 'A';
    Car *c = Car_Get(0);
    int32_t pos[3] = {185 << 16, 53 << 16, 15 << 16};
    Car_Init(c, root, 0, pos, 0, false);
    int wheels = 0;
    for (int i = 0; i < 4; i++) wheels += c->wheel[i] != NULL;
    printf("205a1N: second model %s, %d wheels; mass %.1f, half size %.3f %.3f %.3f, gear 1 %.4f, rate %08x\n",
           s->second ? "found" : "missing", wheels, CI32(c, 0x75c) / 65536.0, CI32(c, 0x204) / 65536.0,
           CI32(c, 0x208) / 65536.0, CI32(c, 0x20c) / 65536.0, CI32(c, 0x7e0) / 65536.0, (unsigned)CI32(c, 0xa98));
    CHECK(wheels == 4 && CI32(c, 0xa98) == 0x41c80000);
    CHECK(CI32(c, 0x75c) == 0x3e80000 + 0x780000 && CI32(c, 0x204) == 0x426e9 / 2);
    CHECK(CI32(c, 0x2d0) == pos[0] && CI8(c, 0xb1b) == 0 && CI32(c, 0xb48) == 2);
    /* placed and settled on fra02's start line */
    char path[64] = "Game\\Tracks\\France\\fra02hi.bfl";
    if (Stage_Mount(path, "Game\\Tracks\\France\\frahit.bfl") && Stage_LoadHpc("fra02") && Stage_LoadCsp("fra02")) {
        CHECK(Stage_LoadCat("fra02") && Cat_Points() == 492 && !Cat_Circuit());
        printf("fra02.cat: %d points, finish at %d\n", Cat_Points(), Cat_FinishPoint());
        int32_t sp[3];
        uint16_t ang[3];
        Csp_StartPos(0, sp);
        Csp_Angles(ang);
        Car_Place(root, s->second, 0, 0, ang, sp);
        Damage_Reset(c);
        printf("settled: position (%.3f, %.3f, %.3f), up (%.3f %.3f %.3f), corner 0 ground %.3f, touching %d\n",
               CI32(c, 0x2d0) / 65536.0, CI32(c, 0x2d4) / 65536.0, CI32(c, 0x2d8) / 65536.0, CI32(c, 0x36c) / 65536.0,
               CI32(c, 0x370) / 65536.0, CI32(c, 0x374) / 65536.0, CI32(c, 0x8dc) / 65536.0, CI8(c, 0xb34));
        CHECK(CI32(c, 0x370) > 0xf000 && CI32(c, 0x2d4) > 50 << 16 && CI32(c, 0x2d4) < 58 << 16);
        /* one step through the ported passes: the history moves, gravity is applied to the mass */
        static const int16_t list[1] = {0};
        Sim_StepCars(list, 1);
        printf("step: %d passes pending, gravity %.1f, wheels on the ground %d\n", g_pending_passes,
               CI32(c, 0x6ac) / 65536.0, c->s[0xb28]);
        CHECK(CI32(c, 0x2e8) != 0 && CI32(c, 0x6ac) < 0);
        /* parked: the ground holds it (Car_GroundContact), over four seconds of steps */
        int32_t y0 = CI32(c, 0x2d4);
        for (int k = 0; k < 100; k++) Sim_StepCars(list, 1), Sim_CarsPost(list, 1);
        int32_t dy = CI32(c, 0x2d4) - y0;
        printf("after 101 steps parked: y %.4f (from %.4f), speed %.4f, on the ground %d\n", CI32(c, 0x2d4) / 65536.0,
               y0 / 65536.0, CI32(c, 0x778) / 65536.0, c->s[0xb28]);
        CHECK((dy < 0 ? -dy : dy) < 0x0800 && CI32(c, 0x778) < 0x0800);
        /* first gear in the automatic box (+0xb9c on, as the race start sets it), then full throttle
           (+0x1d2 = 63) for four seconds */
        CW32(c, 0xb9c, 1);
        CW8(c, 0xb1e, 1);   /* the car drives off along the stage */
        int32_t x0 = CI32(c, 0x2d0), z0 = CI32(c, 0x2d8);
        for (int k = 0; k < 100; k++) {
            c->s[0x1d2] = 63;
            Sim_StepCars(list, 1);
            Sim_CarsPost(list, 1);
            Car_Progress(c);
            if (k % 25 == 24)
                printf("  step %d: gear %d, revs %.3f, speed %.3f, position (%.2f, %.2f, %.2f)\n", k + 1, CI8(c, 0xb1e),
                       CI32(c, 0x7a4) / 65536.0, CI32(c, 0x778) / 65536.0, CI32(c, 0x2d0) / 65536.0, CI32(c, 0x2d4) / 65536.0,
                       CI32(c, 0x2d8) / 65536.0);
        }
        int32_t dx = CI32(c, 0x2d0) - x0, dz = CI32(c, 0x2d8) - z0;
        printf("progress: %d points passed, %.1f%% of the stage\n", Car_PointsPassed(c), Car_StageShare(c) * 100.0 / 65536);
        CHECK(CI32(c, 0x778) > 0x1000 && (int64_t)dx * dx + (int64_t)dz * dz > (int64_t)0x40000 * 0x40000);
        /* the walls (fin.dat in the section grid): unsteered on full throttle the car leaves the road
           at the first bend and falls off the stage without them; with them it is stopped and held */
        D3d_Open(640, 480, 16);
        VB_CreatePool();
        CHECK(Stage_LoadC3d() && Stage_LoadFinDat("fra02") && g_nfin == 2213);
        Section_Init(&g_world);
        Car_Place(root, s->second, 0, 0, ang, sp);
        Damage_Reset(c);
        CW8(c, 0xb1e, 1);
        CW32(c, 0xb9c, 1);
        for (int k = 0; k < 400; k++) {
            c->s[0x1d2] = 63;
            Drive_Step(list, 1);
        }
        int16_t cell = (int16_t)(c->s[0xb00] | c->s[0xb01] << 8);
        printf("walls: after 400 steps (%.2f, %.2f, %.2f), speed %.3f, cell %d with %d walls\n", CI32(c, 0x2d0) / 65536.0,
               CI32(c, 0x2d4) / 65536.0, CI32(c, 0x2d8) / 65536.0, CI32(c, 0x778) / 65536.0, cell,
               cell >= 0 ? g_sec[cell].nwalls : 0);
        CHECK(CI32(c, 0x2d4) > 54 << 16 && CI32(c, 0x370) > 0xf000 && CI32(c, 0x778) < 0x2000 && cell >= 0);
    }
    printf("sim_test: %s\n", fails ? "FAILED" : "ok");
    return fails != 0;
}
