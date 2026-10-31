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
#include "vfs.h"
#include "vfs_host.h"
#include "world/obj.h"
#include "world/stage.h"
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
        int32_t sp[3];
        uint16_t ang[3];
        Csp_StartPos(0, sp);
        Csp_Angles(ang);
        Car_Place(root, s->second, 0, 0, ang, sp);
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
        /* without the wheel and contact forces (pending) the integration lets the car fall: gravity
           0.3 x 0.2 per step on the velocity */
        int32_t y0 = CI32(c, 0x2d4);
        for (int k = 0; k < 4; k++) Sim_StepCars(list, 1);
        printf("after 5 steps: y %.4f (from %.4f), vy %.4f, speed %.4f\n", CI32(c, 0x2d4) / 65536.0, y0 / 65536.0,
               CI32(c, 0x40c) / 65536.0, CI32(c, 0x778) / 65536.0);
        CHECK(CI32(c, 0x40c) < 0 && CI32(c, 0x2d4) < y0);
    }
    printf("sim_test: %s\n", fails ? "FAILED" : "ok");
    return fails != 0;
}
