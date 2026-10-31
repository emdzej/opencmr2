/* The race's process flow (game/race.c) on fra02 with the 205 at full throttle, with a minimal host:
   the loads, the line, the five-second countdown on the stage clock, the stage, the timer from the go. */
#include "c3d.h"
#include "exe.h"
#include "bfl.h"
#include "fixmath.h"
#include "game/gstate.h"
#include "game/mem.h"
#include "game/race.h"
#include "game/profile.h"
#include "game/timer.h"
#include "render/d3d7.h"
#include "render/mesh.h"
#include "sim/car.h"
#include "sim/drive.h"
#include "sim/progress.h"
#include "sim/sim.h"
#include "vfs.h"
#include "vfs_host.h"
#include "world/fin.h"
#include "world/obj.h"
#include "world/section.h"
#include "world/stage.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static int stage_loads, car_loads;

static bool load_stage(void)
{
    stage_loads++;
    return Stage_Mount("Game\\Tracks\\France\\fra02hi.bfl", "Game\\Tracks\\France\\frahit.bfl") && Stage_LoadC3d() &&
           Stage_LoadFinDat("fra02") && (Section_Init(&g_world), Stage_LoadHpc("fra02")) && Stage_LoadCsp("fra02") &&
           Stage_LoadCat("fra02");
}

static bool load_cars(void)
{
    car_loads++;
    uint8_t *d = File_Load("Game\\Cars\\205a1N.c3d", 1);
    static C3d m;
    if (!d || !C3d_Load(&m, d, File_LastSize(), 0)) return false;
    Obj *root = C3d_Objects(&m, &g_world);
    CarSlot *s = Car_Slot(0);
    s->main = root;
    s->second = Obj_FindType(root, 5);
    s->kind = 'A';
    int32_t sp[3];
    uint16_t ang[3];
    Csp_StartPos(0, sp);
    Csp_Angles(ang);
    Car_Place(root, s->second, 0, 0, ang, sp);
    Car *c = Car_Get(0);
    Damage_Reset(c);
    c->main = root;
    c->second = s->second;
    Sim_InitLists(1);
    return true;
}

static void input(int player, uint32_t *held, uint32_t *pressed)
{
    (void)player;
    *held = ACT_THROTTLE;
    *pressed = 0;
}

static const RaceHost host = {load_stage, load_cars, NULL, input, NULL, NULL, NULL};

int main(void)
{
    char err[128];
    if (!vfs_mount_default() || !exe_init(err, sizeof err)) {
        printf("race_test: no game data, skipped\n");
        return 0;
    }
    Math_InitTables();
    CHECK(Mem_Init());
    D3d_Open(640, 480, 16);
    VB_CreatePool();
    memset(&g_world, 0, sizeof g_world);
    g_world.kind = 3;
    Profiles_Init();
    Player_SetProfile(0, 0);   /* the front end's player set-up */
    Race_Begin(&host);
    CHECK(Game_Mode() == 3);
    int seen[14] = {0}, frame = 0, go = -1, fin = -1;
    for (; frame < 60 * 20 && Game_Mode() == 3; frame++) {
        Timer_Sample((uint32_t)((uint64_t)frame * 1000 / 60));
        Race_Frame();
        int st = Race_State(0);
        if (st >= 0 && st < 14 && !seen[st]) {
            seen[st] = frame + 1;
            printf("frame %4d: state %d, stage time %d, clock %d\n", frame, st, Race_StageTime(0), Race_Clock());
        }
        if (st == 8 && go < 0) go = frame;
        if (getenv("RACE_DBG") && st >= 7 && frame % 30 == 0) {
            Car *c = Car_Get(0);
            printf("  f%d st%d gear %d b84 %d b20 %d b24 %d b1f %d b48 %d revs %.3f thr %d hb %d b9c %d km/h %.1f\n", frame,
                   st, CI8(c, 0xb1e), CI32(c, 0xb84), CI8(c, 0xb20), CI8(c, 0xb24), CI8(c, 0xb1f), CI32(c, 0xb48),
                   CI32(c, 0x7a4) / 65536.0, c->s[0x1d2], CI32(c, 0x1d8), CI32(c, 0xb9c), CI32(c, 0x778) / 65536.0 * 90);
        }
        if (st == 8 && fin < 0 && frame - go == 180) {
            Car *c = Car_Get(0);
            printf("3 s after the go: stage time %d, %.1f km/h, %.1f%% of the stage\n", Race_StageTime(0),
                   CI32(c, 0x778) / 65536.0 * 90, Car_StageShare(c) * 100.0 / 65536);
            CHECK(Race_StageTime(0) >= 290 && Race_StageTime(0) <= 310 && CI32(c, 0x778) > 0x8000);
            fin = frame;
        }
    }
    CHECK(stage_loads == 1 && car_loads == 1);
    CHECK(seen[5] && seen[7] && seen[8] && seen[5] < seen[7] && seen[7] < seen[8]);
    /* the countdown: five seconds of 40 ms steps from its first frame, 300 frames at 60 Hz */
    CHECK(seen[8] - seen[7] >= 299 && seen[8] - seen[7] <= 302 && fin > 0);
    printf("race_test: %s\n", fails ? "FAILED" : "ok");
    return fails != 0;
}
