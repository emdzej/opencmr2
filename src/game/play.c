/* The race started by the game (see play.h). */
#include "pending.h"
#include "game/effects.h"
#include "game/gstate.h"
#include "game/shadow.h"
#include "game/play.h"
#include "world/carshade.h"
#include "sim/knock.h"
#include "sim/aerials.h"
#include "game/fireworks.h"
#include "game/lamps.h"
#include "game/timer.h"
#include "game/camera.h"
#include "game/mud.h"
#include "game/pause.h"
#include "game/car_load.h"
#include "game/mem.h"
#include "game/race.h"
#include "game/ghost.h"
#include "game/weather.h"
#include "game/skids.h"
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
#include "sim/parts.h"
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
#include "game/profile.h"
#include "game/carpass.h"
#include "game/cockpit.h"
#include <stdio.h>
#include <string.h>

static Obj cameras[2];   /* 0x538e2c: a camera object per view */
static Obj *cars[2 * CAR_MAX + 2];
static int ncars;   /* the cars' objects (the cockpits after them, each frame) */

static char st_name[16], st_path[128], st_tex[128];

/* state 3 (0x401000): Stage_Load 0x41f930, after the set-up has mounted the common archives */
static bool load_stage(void)
{
    if (!Stage_Load(st_name, st_path, st_tex)) {
        plat_log("play: no such stage");
        return false;
    }
    Tod_StartLights();
    return true;
}

/* Cars_Load 0x456d90's part: the grid, every car of the race (0x456ca0), Car_Load, the progress start, the
   lists */
static bool load_cars(void)
{
    int n = Cars_Count();
    if (Gs_SelBit22() || Gs_SelBit31()) Cars_Grid(false);   /* 0x4584d0 */
    for (int i = 0; i < n; i++) {
        if (!Car_Load(i)) {
            plat_log("play: a car could not be loaded");
            return false;
        }
        Car_ProgressStart(Car_Get(i));
    }
    if (Gs_SelBit22()) Cars_GridPoints();   /* 0x458480 */
    Sim_InitLists(n);
    Damage_CarsLoaded();
    Tyres_Start(false, false, (int)Gs_SelBits14());   /* 0x45c610 */
    Skids_Reset();                                     /* 0x465530 */
    for (int i = 0; i < n; i++) Pose_Snap(i);          /* 0x42c840 */
    for (int i = 0; i < n; i++) Car_PoseToObjects(Car_Get(i));   /* 0x43ecd0 */
    if (Ghost_On()) Ghost_SetCar(Car_Get(n - 1));   /* 0x465fc0: the last car */
    Frame_SetRate(0x190000);   /* 25 frames a second, Cars_Load 0x466630 */
    int k = 0;
    for (int i = 0; i < n; i++) {
        cars[k++] = Car_Slot(i)->main;
        if (Car_Slot(i)->second) cars[k++] = Car_Slot(i)->second;
    }
    ncars = k;
    World_SetCars(cars, k);
    return true;
}

static void pose(int32_t frac) { (void)frac; }

static void draw(int player, int state)
{
    (void)state;
    Obj *camera = &cameras[player & 1];
    if (player == 0) {
        Render_SetClearColour(0x30, 0x40, 0x58);
        Render_ClearTarget();
    }
    int16_t view[4];
    View_Rect(player, view);
    int32_t r[4] = {view[0], view[1], view[0] + view[2], view[1] + view[3]};   /* a D3DRECT */
    D3d_Clear(1, r, D3DCLEAR_ZBUFFER, 0, 1.0f, 0);
    camera->world = camera->local;
    camera->world.m[15] = 0x10000;
    camera->dirty = 1;
    int32_t cp[3] = {camera->world.m[12], camera->world.m[13], camera->world.m[14]};
    Knock_Draw();   /* 0x471a60 */
    /* (0x4292c0, each car's models shifted 1.5 units on its line to the camera (inferred), runs only on a
       device without a depth buffer: 0x4b74b0, the triangle caps' ZBUFFERLESSHSR read by
       Render_ReadCaps 0x4b7210. The port's device has one.) */
    Cars_ShellsToCamera(camera);
    Weather_Sky(player);   /* 0x461c30 */
    Sky_Follow(cp);
    /* the mud on the first car's body, in the first view of a one-player race, not paused */
    if (player == 0 && Gs_SelBits14() == 1 && !Pause_Active() && (Car_Slot(0)->kind == 'A' || Car_Slot(0)->kind == 'C')) {
        Mud_Throw(0);   /* 0x46ea10 */
        Mud_Paint();    /* 0x46e780 */
    }
    for (int i = 0; i < g_all.n; i++) Car_GroundLight(i, player);   /* 0x462aa0 */
    Cars_Lod(player, view);   /* 0x428bf0 */
    Lamps_Lights(player);   /* 0x464960 for the camera's car (0x422fb0) */
    /* the cars' kinds per view (0x46bb40), per car its masks (0x46b8f0) and the fallen-off parts
       hidden (0x46b670) */
    Cars_ViewKinds();
    for (int i = g_all.n - 1; i >= 0; i--) {
        Car_Masks(Car_Get(g_all.car[i]));
        Parts_HideFallen(Car_Get(g_all.car[i]));
    }
    /* the cockpits drawn with the cars (the original puts them in their stage section, 0x476410) */
    int k = ncars;
    for (int i = 0; i < 2 && k < (int)(sizeof cars / sizeof *cars); i++)
        if (Cockpit_Model(i)) cars[k++] = Cockpit_Model(i);
    World_SetCars(cars, k);
    Obj_Frame(&g_world, camera);
    /* the trees' shadows on the cars, once a frame in the first view (0x536ad0): each car's items
       from its far model when shown, else its main one, then its second, main and far models shaded */
    if (player == 0) {
        CarShade_Begin();   /* 0x4b5e60 */
        for (int i = g_all.n - 1; i >= 0; i--) {
            const Car *c = Car_Get(g_all.car[i]);
            const Obj *at = c->far[0] && c->far[0]->mask ? c->far[0] : c->main;
            CarShade_Gather(at, CI32(c, 0x758), CI16(c, 0xb00));   /* 0x4b5f90 */
            uint8_t passes = Car_Shown(CI8(c, 0xb1a));   /* 0x46bd40 */
            CarShade_Shade(c->second, CI32(c, 0xa70), passes);   /* 0x4b5ee0 */
            CarShade_Shade(c->main, CI32(c, 0xa70), passes);
            if (c->far[0]) CarShade_Shade(c->far[0], CI32(c, 0xa70), passes);
        }
        CarShade_End();   /* 0x4b5eb0 */
    }
    /* the cars' pass 0x466570: each car's shadow but those the view hides (0x46bd20's 7) */
    bool multi = Gs_SelBits14() > 1 && Race_State(0) == 10;   /* 0x41f3a0, as weather.c */
    for (int i = 0; i < g_all.n; i++) {
        const Car *c = Car_Get(g_all.car[i]);
        int n = CI8(c, 0xb1a);
        if (CI32(c, 0xc0c) || CI32(c, 0xb68 + 4 * (uint32_t)player)) continue;
        if (!multi) {
            if (gm32(0x588cd4 + 4u * (uint32_t)(player + n * 2)) != 7) Shadow_Draw(c, player);
        } else if (Gs_SelBit27() || Gs_SelBit26() || i == (Camera_Car(1) & 0xff)) Shadow_Draw(c, 1);
        /* (in a shared replay otherwise only the second view's car, its number compared with the
           list's place as in the original) */
    }
    int32_t eye[3] = {camera->world.m[12], camera->world.m[13], camera->world.m[14]};
    Aerials_Draw(g_all.car, g_all.n, player, eye);   /* 0x485690 */
    Weather_Draw(player);   /* 0x460330, at the end of the cars' pass 0x466570 */
    if (Fireworks_On()) Fireworks_Draw();   /* 0x47f740 */
    Race_DrawOverlays(player);
    Camera_Projection(player);   /* 0x422d40 */
    World_Render(camera, view, (uint32_t)player, true);
    /* after the world, as Race_DrawView 0x40f0c0: the ghost's see-through level (0x466100); the marks'
       triangles (drawn with the next frame's) */
    if (Ghost_On()) Ghost_Fade(player, camera);
    if (!gm8(0x52af40)) {
        Skids_Centres(player);   /* 0x4657d0 */
        Skids_Add(player);       /* 0x464cb0 */
    }
    Skids_Draw(player);          /* 0x465780 */
}

static Obj *cam(int player) { return &cameras[player & 1]; }

static const RaceHost host = {load_stage, load_cars, NULL, NULL, pose, draw, cam};

bool Play_Begin(void)
{
    Race_SetPlayers();
    bool found = Stage_Arcade(st_name, sizeof st_name, st_path, st_tex, sizeof st_path);
    if (!found) {
        Stage_Name(st_name, sizeof st_name);
        found = Stage_Find(st_name, st_path, st_tex, sizeof st_path);
    }
    if (!found) {
        plat_log("play: no such stage");
        return false;
    }
    /* the stage's directory (Race_Setup 0x41f560's 0x53823c, "Game\\Tracks\\<country>"; 0x41f8e0 gives it) */
    const char *slash = strrchr(st_path, '\\');
    snprintf((char *)gm(0x53823c), 0x104, "%.*s", slash ? (int)(slash - st_path) : 0, st_path);
    Race_Names();   /* 0x41f560 */
    memset(cameras, 0, sizeof cameras);
    for (int i = 0; i < 2; i++) {
        cameras[i].kind = 3;
        cameras[i].mask = 0xff;
        Mat_Identity(&cameras[i].world);
    }
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
    Sim_InitLists(0);   /* the cars' lists emptied with the cars (the next race's first draws run before its load) */
    Cockpit_Free();
    CarShade_Free();   /* 0x4b5380 (in the cars' free 0x457ed0) */
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
