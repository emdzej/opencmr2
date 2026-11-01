/* The race's weather (game/weather.c) on fra02 with the stage's weather forced: medium rain, then
   heavy snow. The records' set-up, the drops kept in their box about the camera, the splats, the
   flakes, the wet value, the wind's gusts. */
#include "exe.h"
#include "fixmath.h"
#include "game/gstate.h"
#include "game/mem.h"
#include "game/race.h"
#include "game/weather.h"
#include "game/car_load.h"
#include "game/camera.h"
#include "game/common.h"
#include "game/profile.h"
#include "game/select.h"
#include "game/stage_load.h"
#include "game/timer.h"
#include "audio/snd.h"
#include "render/d3d7.h"
#include "render/mesh.h"
#include "render/sprite3d.h"
#include "sim/car.h"
#include "sim/damage.h"
#include "sim/progress.h"
#include "sim/sim.h"
#include "vfs.h"
#include "vfs_host.h"
#include "world/obj.h"
#include "world/section.h"
#include "world/stage.h"
#include "world/tod.h"
#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static Obj camera;
static Obj *host_camera(int player) { (void)player; return &camera; }
static const RaceHost host = {NULL, NULL, NULL, NULL, NULL, NULL, host_camera};

/* weather type t (0-8) at the start and end of fra02 (stage 2 of rally 2: 0x52f100 + 8) */
static void force(uint32_t t)
{
    uint32_t s = Tod_Stage();
    gw32(0x52f100 + 8 * s, t);
    gw32(0x52f104 + 8 * s, t);
    Weather_StageLoad();
    Weather_Alloc(1);
    Weather_RaceStart();
}

static void step(int frames, int *splats, int *drawn)
{
    for (int f = 0; f < frames; f++) {
        camera.local.m[14] += 0x8000;   /* the camera moving along z at 0.5 a frame */
        Weather_Step();
        int before = Sprite3d_Count();
        Weather_Draw(0);
        *drawn += Sprite3d_Count() - before;
        int n = fx_mul(Weather_Level(0), 400 << 16) >> 16;   /* the drops drawn: the level's share */
        for (int i = 0; i < n; i++) *splats += gm32(0x543fb0 + 0x24 * (uint32_t)i + 0x20) != 0;
        Sprite3d_Init();
    }
}

int main(void)
{
    char err[128];
    if (!vfs_mount_default() || !exe_init(err, sizeof err)) {
        printf("weather_test: no game data, skipped\n");
        return 0;
    }
    Math_InitTables();
    CHECK(Mem_Init());
    D3d_Open(640, 480, 16);
    VB_CreatePool();
    memset(&g_world, 0, sizeof g_world);
    g_world.kind = 3;
    Options_Defaults();
    Profiles_Init();
    Snd_Init();
    Player_SetProfile(0, 0);
    Profile_Create(0);
    gw32(0x531350, (gm32(0x531350) & ~0x3fu) | (uint32_t)Car_IdByName("205"));
    Tod_Select("fra02");
    gw32(0x52afb4, (gm32(0x52afb4) & 0xfffe1fffu) | 1u << 13);
    Sel_ModeFlags();
    gw32(0x52f2ac, (gm32(0x52f2ac) & 0xffff3fffu) | 1u << 14);   /* one player (0x407500) */
    gw32(0x52afd0, gm32(0x52afd0) & ~(3u << 25));                 /* full effects detail */
    Race_Begin(&host);
    Sprite3d_Init();
    Bfl_MountCommon();
    Weather_Load();
    CHECK(Stage_Load("fra02", "Game\\Tracks\\France\\fra02hi.bfl", "Game\\Tracks\\France\\frahit.bfl"));
    CHECK(Car_Load(0));
    Sim_InitLists(1);
    Damage_CarsLoaded();
    Frame_SetRate(0x190000);
    Car_ProgressStart(Car_Get(0));
    Car *c = Car_Get(0);
    Mat_Identity(&camera.local);
    for (int k = 0; k < 3; k++) camera.local.m[12 + k] = CI32(c, 0x2d0 + 4 * (uint32_t)k);
    camera.local.m[13] += 0x18000;

    /* medium rain (type 4): level 0.7, wet 0.85, all 400 drops at full detail */
    force(4);
    printf("rain: kind %u level 0x%x wet 0x%x\n", Weather_Kind(0), Weather_Level(0), Weather_CarWet(c));
    CHECK(Weather_Kind(0) == 1 && Weather_Wet(0) && Weather_Level(0) == 45875 && Weather_CarWet(c) == 0xd999);
    CHECK(Weather_Kind(1) == 0 && Weather_Level(1) == 0);
    int splats = 0, drawn = 0;
    int32_t y0 = camera.local.m[13];
    step(100, &splats, &drawn);
    /* every drop within its box: 12 either side of the centre in x and z, the centre 14 ahead (less
       0.3 of it at the camera's zoom 0) */
    int out = 0;
    for (int i = 0; i < 400; i++) {
        int32_t x = gms32(0x543fb0 + 0x24 * (uint32_t)i) - camera.local.m[12];
        int32_t zoom = fx_mul((int32_t)(((int64_t)Camera_Zoom(0) << 16) / 0xa000) - 0x10000, 0x4ccc);
        int32_t z = gms32(0x543fb0 + 0x24 * (uint32_t)i + 8) - (camera.local.m[14] + 0xe0000 + fx_mul(0xe0000, zoom));
        if (x < -0xc0000 || x > 0xc0000 || z < -0xc0000 || z > 0xc0000) out++;
    }
    printf("rain: 100 frames, %d drops outside the box, %d splats, %d sprites queued, wind %.3f (aim %.3f), y %.2f\n",
           out, splats, drawn, gms32(0x547940) / 65536.0, gms32(0x547944) / 65536.0, y0 / 65536.0);
    CHECK(out == 0 && splats > 0 && drawn == splats);
    CHECK(gms32(0x54794c) > 0 || gms32(0x547940) != 0);   /* a gust held or blowing */

    /* heavy snow (type 8): one flake a drop each frame */
    force(8);
    splats = drawn = 0;
    step(20, &splats, &drawn);
    printf("snow: kind %u level 0x%x, %d flakes in 20 frames\n", Weather_Kind(0), Weather_Level(0), drawn);
    CHECK(Weather_Kind(0) == 2 && !Weather_Wet(0) && Weather_Level(0) == 0x10000 && drawn == 20 * 400);

    /* clear (type 0): nothing drawn, dry */
    force(0);
    splats = drawn = 0;
    step(5, &splats, &drawn);
    CHECK(Weather_Kind(0) == 0 && drawn == 0 && Weather_CarWet(c) == 0);

    printf("weather_test: %s\n", fails ? "FAILED" : "ok");
    return fails != 0;
}
