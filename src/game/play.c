/* The race started by the game (see play.h). */
#include "game/play.h"
#include "game/lamps.h"
#include "game/timer.h"
#include "game/camera.h"
#include "game/car_load.h"
#include "game/mem.h"
#include "game/race.h"
#include "game/weather.h"
#include "game/stage_load.h"
#include "platform.h"
#include "render/d3d7.h"
#include "render/light.h"
#include "render/rstate.h"
#include "render/view.h"
#include "sim/car.h"
#include "sim/damage.h"
#include "sim/drive.h"
#include "sim/pose.h"
#include "sim/progress.h"
#include "sim/sim.h"
#include "world/frame.h"
#include "world/hpc.h"
#include "world/obj.h"
#include "world/stage.h"
#include "world/section.h"
#include "world/fin.h"
#include "audio/snd.h"
#include "render/mesh.h"
#include "render/tex.h"
#include "world/tod.h"
#include "sim/scenery.h"
#include <stdio.h>
#include <string.h>

static Obj camera;
static Obj *cars[2];

static bool load_stage(void) { return true; }

/* Cars_Load 0x456d90's part for one car: Car_Load, the lists, the progress start */
static bool load_cars(void)
{
    if (!Car_Load(0)) {
        plat_log("play: the car could not be loaded");
        return false;
    }
    Sim_InitLists(1);
    Damage_CarsLoaded();
    Frame_SetRate(0x190000);   /* 25 frames a second, Cars_Load 0x466630 */
    Car_ProgressStart(Car_Get(0));
    cars[0] = Car_Slot(0)->main;
    cars[1] = Car_Slot(0)->second;
    World_SetCars(cars, cars[1] ? 2 : 1);
    return true;
}

static void input(int player, uint32_t *held, uint32_t *pressed)
{
    (void)player;
    Drive_ReadInput(held, pressed);
}

static void pose(int32_t frac) { (void)frac; }

static void draw(int player, int state)
{
    (void)state;
    int w, h;
    D3d_ScreenSize(&w, &h);
    Render_SetClearColour(0x30, 0x40, 0x58);
    Render_ClearTarget();
    int32_t r[4] = {0, 0, w, h};
    D3d_Clear(1, r, D3DCLEAR_ZBUFFER, 0, 1.0f, 0);
    camera.world = camera.local;
    camera.world.m[15] = 0x10000;
    camera.dirty = 1;
    int32_t cp[3] = {camera.world.m[12], camera.world.m[13], camera.world.m[14]};
    Sky_Follow(cp);
    Cars_ShellsToCamera(&camera);
    Lamps_Lights(player);   /* 0x464960 for the camera's car (0x422fb0) */
    Obj_Frame(&g_world, &camera);
    Weather_Draw(player);   /* 0x460330, at the end of the cars' pass 0x466570 */
    Race_DrawOverlays(player);
    int16_t view[4] = {0, 0, (int16_t)w, (int16_t)h};
    World_Render(&camera, view, 0, true);
}

static Obj *cam(int player)
{
    (void)player;
    return &camera;
}

static const RaceHost host = {load_stage, load_cars, NULL, input, pose, draw, cam};

bool Play_Begin(void)
{
    char stage[16], path[128], tex[128];
    Stage_Name(stage, sizeof stage);
    if (!Stage_Find(stage, path, tex, sizeof path) || !Stage_Load(stage, path, tex)) {
        plat_log("play: no such stage");
        return false;
    }
    Tod_StartLights();
    memset(&camera, 0, sizeof camera);
    camera.kind = 3;
    camera.mask = 0xff;
    Mat_Identity(&camera.world);
    /* the race's far plane (the race's own set-up, 0x45eca0, to come); the lights are the stage's sun
       (Light_Defaults) and the headlights' (Lamps_Setup) */
    Light_SetMode(4);
    D3d_SetTextureStageState(0, D3DTSS_MINFILTER, 2);
    Proj_Set((int32_t)(2.598f * 65536), (int32_t)(3.464f * 65536), 250 << 16, 6554);
    Race_Begin(&host);
    return true;
}

/* the race's resources freed on the way back to the front end (the original's 0x41e670, 0x40eef0 and
   0x41b300, and World_Frame's 0x49c0f0, 0x4a5be0 and 0x4a5d10) */
void Play_End(void)
{
    World_SetCars(NULL, 0);
    Scenery_CollFree();   /* 0x472720 */
    Scenery_FreeAll();
    Fin_Free();
    Stage_Unload();
    g_nsec = 0;
    g_sec_ready = false;
    g_world.child = NULL;
    Obj_FreeAll();
    Voices_StopAll();
    Snd_Truncate(0);
    Tex_FreeAll();
    VB_FreePool();
    VB_CreatePool();
}
