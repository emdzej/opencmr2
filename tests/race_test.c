/* The race's process flow (game/race.c) on fra02 with the 205 at full throttle, with a minimal host:
   the loads, the line, the five-second countdown on the stage clock, the stage, the timer from the go. */
#include "c3d.h"
#include "exe.h"
#include "bfl.h"
#include "fixmath.h"
#include "game/gstate.h"
#include "game/mem.h"
#include "game/race.h"
#include "game/results.h"
#include "game/select.h"
#include "game/opponents.h"
#include "platform_host.h"
#include "audio/dsound.h"
#include "audio/snd.h"
#include "audio/codriver.h"
#include <math.h>
#include "game/camera.h"
#include "game/hud.h"
#include "game/sectors.h"
#include "game/car_load.h"
#include "world/tod.h"
#include "game/stage_load.h"
#include "game/profile.h"
#include "game/timer.h"
#include "render/d3d7.h"
#include "render/mesh.h"
#include "sim/car.h"
#include "sim/drive.h"
#include "sim/progress.h"
#include "sim/sim.h"
#include "sim/pose.h"
#include "sim/fade.h"
#include "render/twod.h"
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
    return Stage_Load("fra02", "Game\\Tracks\\France\\fra02hi.bfl", "Game\\Tracks\\France\\frahit.bfl");
}

static bool load_cars(void)
{
    car_loads++;
    if (!Car_Load(0)) return false;
    Sim_InitLists(1);   /* after the cars, as Cars_Load 0x456d90 does */
    Car_ProgressStart(Car_Get(0));
    return true;
}

static int press_camera;
static void input(int player, uint32_t *held, uint32_t *pressed)
{
    (void)player;
    *held = ACT_THROTTLE;
    *pressed = press_camera ? ACT_CAMERA : 0;
    press_camera = 0;
}

static Obj camera;
static Obj *host_camera(int player) { (void)player; return &camera; }
static void draw(int player, int state)
{
    (void)state;
    Codriver_Notes(player);   /* the overlays' speech each frame (the 2D drawing is the GPU tests') */
}
static const RaceHost host = {load_stage, load_cars, NULL, input, NULL, draw, host_camera};

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
    Options_Defaults();
    Profiles_Init();
    Snd_Init();
    Player_SetProfile(0, 0);
    Profile_Create(0);   /* the front end's player set-up: profile 0, the 205 */
    gw32(0x531350, (gm32(0x531350) & ~0x3fu) | (uint32_t)Car_IdByName("205"));
    Tod_Select("fra02");   /* the front end's selection: one player, the mode's flags (0x408020) */
    gw32(0x52afb4, (gm32(0x52afb4) & 0xfffe1fffu) | 1u << 13);
    Sel_ModeFlags();
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
        if (st == 8 && frame - go == 60) {
            CHECK(Camera_Kind(0) == 4);
            press_camera = 1;   /* the camera key: from the chase (4) to the far chase (5), blended */
        }
        if (st == 8 && frame - go == 120) {
            printf("camera kind after the key: %d\n", Camera_Kind(0));
            CHECK(Camera_Kind(0) == 5);
        }
        if (getenv("RACE_DBG") && st >= 7 && frame % 30 == 0) {
            Car *c = Car_Get(0);
            printf("  pos %.1f %.1f %.1f b43 %d a98 %08x f%d st%d gear %d b84 %d b20 %d b24 %d b1f %d b48 %d revs %.3f thr %d hb %d b9c %d km/h %.1f\n",
                   CI32(c, 0x2d0) / 65536.0, CI32(c, 0x2d4) / 65536.0, CI32(c, 0x2d8) / 65536.0, c->s[0xb43], (unsigned)CI32(c, 0xa98), frame, st, CI8(c, 0xb1e), CI32(c, 0xb84), CI8(c, 0xb20), CI8(c, 0xb24), CI8(c, 0xb1f), CI32(c, 0xb48),
                   CI32(c, 0x7a4) / 65536.0, c->s[0x1d2], CI32(c, 0x1d8), CI32(c, 0xb9c), CI32(c, 0x778) / 65536.0 * 90);
        }
        if (st == 8 && fin < 0 && frame - go == 180) {
            Car *c = Car_Get(0);
            printf("camera at (%.1f, %.1f, %.1f), car at (%.1f, %.1f, %.1f)\n", camera.local.m[12] / 65536.0,
                   camera.local.m[13] / 65536.0, camera.local.m[14] / 65536.0, CI32(c, 0x2d0) / 65536.0, CI32(c, 0x2d4) / 65536.0,
                   CI32(c, 0x2d8) / 65536.0);
            printf("3 s after the go: stage time %d, %.1f km/h, %.1f%% of the stage\n", Race_StageTime(0),
                   CI32(c, 0x778) / 65536.0 * 90, Car_StageShare(c) * 100.0 / 65536);
            /* the poses (Car_PoseInterp): the front left wheel's matrix turns from frame to frame */
            static int32_t last[12];
            Obj *w = c->wheel[0];
            CHECK(w != NULL);
            if (w) {
                int diff = 0;
                for (int q = 0; q < 12; q++) diff += w->local.m[q] != last[q], last[q] = w->local.m[q];
                Timer_Sample((uint32_t)((uint64_t)(frame + 2) * 1000 / 60));
                Race_Frame();
                diff = 0;
                for (int q = 0; q < 12; q++) diff += w->local.m[q] != last[q];
                printf("wheel 0: %d of 12 terms changed over a step\n", diff);
                CHECK(diff > 0);
            }
            printf("HUD race time %d\n", Times_Race());
            /* the engine (0x47a710): its 13 sounds loaded (after the co-driver's 59 and the HUD's 5), the mixer
               not silent */
            static float pcm[1470 * 2];
            Dsb_Mix(pcm, 1470);
            double e = 0;
            for (int q = 0; q < 1470 * 2; q++) e += pcm[q] * pcm[q];
            printf("engine: %d sounds, rms %.4f\n", Snd_Count(), sqrt(e / (1470 * 2)));
            CHECK(Snd_Count() == 59 + 5 + 13 && e > 0);
            CHECK(Times_Race() >= 290 && Times_Race() <= 310);
            CHECK(Race_StageTime(0) >= 290 && Race_StageTime(0) <= 310 && CI32(c, 0x778) > 0x8000);
            fin = frame;
        }
    }
    CHECK(stage_loads == 1 && car_loads == 1);
    /* the co-driver: three, two, one, go, then the calls the car passed (spoken in the overlays) */
    for (int q = 0; q < 4; q++) Race_DrawOverlays(0);
    printf("co-driver: %d words queued, the car at point %d\n", Codriver_Said(), Car_CatIndex(Car_Get(0)));
    CHECK(Codriver_Said() > 4);
    /* the overlays: the time panel's box and the recovery's fade (0x428680), two triangles each, and
       the split bar (0x4143f0): a box per split (the passed part apart) and marks between them */
    if (Game_Mode() == 3) {
        CW32(Car_Get(0), 0xbf8, 1);
        for (int k = 0; k < 6; k++, frame++) {
            Timer_Sample((uint32_t)((uint64_t)frame * 1000 / 60));
            Race_Frame();
        }
        Tri_Init();
        int before = Tri_Queued();
        Race_DrawOverlays(0);
        printf("fade: level %.3f, %d triangles queued\n", Fade_LevelOf(0) / 65536.0, Tri_Queued() - before);
        CHECK(Fade_LevelOf(0) > 0 && Tri_Queued() - before >= 4 + 2 * Sectors_Count());
    }
    CHECK(seen[5] && seen[7] && seen[8] && seen[5] < seen[7] && seen[7] < seen[8]);
    /* the countdown: five seconds of 40 ms steps from its first frame, 300 frames at 60 Hz */
    CHECK(seen[8] - seen[7] >= 299 && seen[8] - seen[7] <= 302 && fin > 0);
    /* the sectors (0x4581d0): the car walked along the centre line passes each split and finishes */
    {
        Car *c = Car_Get(0);
        int splits = 0, at = -1;
        for (int i = Car_CatIndex(c); i < Cat_Points() && at < 0; i++) {
            int32_t p[3];
            Cat_Point(i, p);
            CW32(c, 0x2d0, p[0]), CW32(c, 0x2d8, p[2]);
            Car_Progress(c);
            Sectors_Step();
            splits += Sectors_Split(0);
            if (Sectors_Finished(0)) at = i;
        }
        printf("sectors: %d split points, %d passed, finished at point %d of %d\n", Sectors_Count(), splits, at,
               Cat_FinishPoint());
        CHECK(at > 0 && splits >= 1);
        /* past the finish: state 9 for four seconds, then the replay with the results' pages; Enter
           through the title, times, splits and board, up (round) to "quit", Enter: the fade, the
           results state, the front end */
        int f2 = 0;
        for (; f2 < 900 && Race_State(0) != 10; f2++, frame++) {
            Timer_Sample((uint32_t)((uint64_t)frame * 1000 / 60));
            Race_Frame();
        }
        printf("results after %d frames: state %d, active %d\n", f2, Race_State(0), Results_Active());
        /* the computer drivers' times from fra02.tm<difficulty>: in order, a few minutes */
        printf("computer drivers: %d, fastest %d, 2nd %d, slowest %d (1/100 s)\n", Opp_Computers(), Opp_Time(0), Opp_Time(1),
               Opp_Time(Opp_Computers() - 1));
        CHECK(Opp_Computers() == 15 && Opp_Time(0) > 6000 && Opp_Time(0) <= Opp_Time(1) &&
              Opp_Time(1) <= Opp_Time(Opp_Computers() - 1));
        CHECK(Race_State(0) == 10 && Results_Active());
        int pages = 0;
        for (; pages < 8 && !Results_AtChoices(); pages++)
            for (int i = 0; i < 40; i++, frame++) {
                plat_host_key(0x1c, i < 3);
                Timer_Sample((uint32_t)((uint64_t)frame * 1000 / 60));
                Race_Frame();
            }
        printf("the choices after %d pages (a new record %d)\n", pages, gm8(0x817410));
        CHECK(Results_AtChoices());
        static const int keys[] = {0xc8, 0x1c};
        for (int k = 0; k < 2; k++)
            for (int i = 0; i < 40; i++, frame++) {
                plat_host_key(keys[k], i < 3);
                Timer_Sample((uint32_t)((uint64_t)frame * 1000 / 60));
                Race_Frame();
            }
        for (int i = 0; i < 200 && Game_Mode() == 3; i++, frame++) {
            Timer_Sample((uint32_t)((uint64_t)frame * 1000 / 60));
            Race_Frame();
        }
        printf("after the results' quit: mode %u\n", Game_Mode());
        CHECK(Game_Mode() == 0);
    }
    printf("race_test: %s\n", fails ? "FAILED" : "ok");
    return fails != 0;
}
