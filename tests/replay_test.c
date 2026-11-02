/* The replays (game/replay.c) on fra01: the race's own replay records a scripted drive from the go and
   plays it back from its snapshot on the same path, step for step; the disc's demo run
   (Game/DemoRuns/Fra01H.rpl) plays its 725 frames, 4,211 steps, with the recorded state changes. */
#include "exe.h"
#include "bfl.h"
#include "game/gstate.h"
#include "game/mem.h"
#include "game/race.h"
#include "game/replay.h"
#include "game/weather.h"
#include "game/select.h"
#include "game/car_load.h"
#include "game/stage_load.h"
#include "game/profile.h"
#include "game/timer.h"
#include "render/d3d7.h"
#include "render/mesh.h"
#include "audio/snd.h"
#include "sim/car.h"
#include "sim/damage.h"
#include "sim/progress.h"
#include "sim/sim.h"
#include "sim/drive.h"
#include "world/obj.h"
#include "world/tod.h"
#include "vfs.h"
#include "vfs_host.h"
#include "world/stage.h"
#include "world/section.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

/* RPL_DEMO=ken03 or swe01 for the other demo runs (the drive's scripted check is fra01's) */
static const char *demo_stage(void) { return getenv("RPL_DEMO") ? getenv("RPL_DEMO") : "fra01"; }

static bool load_stage(void)
{
    const char *s = demo_stage();
    char a[64], b[64];
    const char *country = s[0] == 'k' ? "Kenya" : s[0] == 's' ? "Sweden" : "France";
    snprintf(a, sizeof a, "Game\\Tracks\\%s\\%shi.bfl", country, s);
    snprintf(b, sizeof b, "Game\\Tracks\\%s\\%.3shit.bfl", country, s);
    return Stage_Load(s, a, b);
}

static bool load_cars(void)
{
    if (!Car_Load(0)) return false;
    Sim_InitLists(1);
    Damage_CarsLoaded();
    Frame_SetRate(0x190000);
    Car_ProgressStart(Car_Get(0));
    return true;
}

/* the scripted drive: full throttle, a turn right, one left, a stop on the brake (by the race's steps
   from the go); none while `script` is off */
static bool script;
static int32_t go_clock;
static void input(int player, InputRecord *r)
{
    (void)player;
    r->held = 0, r->pressed = 0;
    if (getenv("RPL_REST_THR")) r->held = ACT_THROTTLE;
    if (!script) return;
    int k = (Race_Clock() - go_clock) / 4;
    r->held = ACT_THROTTLE;
    if (k >= 40 && k < 52) r->held |= ACT_RIGHT;
    if (k >= 100 && k < 108) r->held |= ACT_LEFT;
    if (k >= 150 && k < 170) r->held = ACT_BRAKE;
}
static Obj camera;
static Obj *host_camera(int player) { (void)player; return &camera; }
static const RaceHost host = {load_stage, load_cars, NULL, input, NULL, NULL, host_camera};

static int frame;
static void step(void)
{
    Timer_Sample((uint32_t)((uint64_t)frame * 1000 / 60));
    Race_Frame();
    frame++;
}

int main(void)
{
    char err[128];
    if (!vfs_mount_default() || !exe_init(err, sizeof err)) {
        printf("replay_test: no game data, skipped\n");
        return 0;
    }
    Math_InitTables();
    CHECK(Mem_Init());
    Input_Init();   /* 0x49eaf0 */
    D3d_Open(640, 480, 16);
    VB_CreatePool();
    memset(&g_world, 0, sizeof g_world);
    g_world.kind = 3;
    Options_Defaults();
    Profiles_Init();
    Snd_Init();
    Player_SetProfile(0, 0);
    Profile_Create(0);
    int car_id = getenv("RPL_CAR") ? atoi(getenv("RPL_CAR")) : Car_IdByName("205");
    gw32(0x531350, (gm32(0x531350) & ~0x3fu) | (uint32_t)car_id);
    Tod_Select(demo_stage());
    gw32(0x52afb4, (gm32(0x52afb4) & 0xfffe1fffu) | 1u << 13);
    Sel_ModeFlags();
    Race_Begin(&host);
    if (getenv("RPL_REST")) {
        /* the demo's snapshot (the original's car at rest on the line) put into the car in the
           countdown: the port's steps should keep it there */
        while (Race_State(0) != 7) step();
        for (int q = 0; q < 30; q++) step();
        char dn0[64];
        snprintf(dn0, sizeof dn0, "Game\\DemoRuns\\%c%c%c%c%cH.rpl", demo_stage()[0] - 32, demo_stage()[1], demo_stage()[2], demo_stage()[3], demo_stage()[4]);
        Replay *d0 = Replay_Load(dn0);
        const uint8_t *im = Replay_Image(d0, NULL) + 0x110 + 0x4d0;
        Car *c = Car_Get(0);
        memcpy(c->s, im, 0x71c);
        memcpy(c->s + 0x754, im + 0x754, 0xb1a - 0x754);
        memcpy(c->s + 0xb1b, im + 0xb1b, CAR_SIZE - 0xb1b);
        int32_t c0 = Race_Clock(), last = -1;
        while ((Race_Clock() - c0) / 4 <= 100 && Race_State(0) == 7) {
            step();
            int k = (Race_Clock() - c0) / 4;
            if (k != last && (k < 10 || k % 10 == 0)) {
                last = k;
                int nd = 0;
                for (uint32_t o = 0; o < 0x400; o += 4) nd += CI32(c, o) != (int32_t)(im[o] | im[o + 1] << 8 | im[o + 2] << 16 | (uint32_t)im[o + 3] << 24);
                if (getenv("RPL_REST_ALL") && (k == 1 || k == 2 || k == 10)) {
                    for (uint32_t o = 0; o < CAR_SIZE; o += 4) {
                        int32_t was = (int32_t)(im[o] | im[o + 1] << 8 | im[o + 2] << 16 | (uint32_t)im[o + 3] << 24);
                        if (CI32(c, o) != was) printf("    step %d +0x%03x: snapshot %08x port %08x (%+d)\n", k, o, (unsigned)was, (unsigned)CI32(c, o), CI32(c, o) - was);
                    }
                }
                printf("  rest step %3d: y %+.5f (snapshot %.5f) vel %d %d %d  spins %d %d  %d of 256 ints of 0-0x3ff changed\n", k,
                       (CI32(c, 0x2d4) - (int32_t)(im[0x2d4] | im[0x2d5] << 8 | im[0x2d6] << 16 | (uint32_t)im[0x2d7] << 24)) / 65536.0,
                       CI32(c, 0x2d4) / 65536.0, CI32(c, 0x408), CI32(c, 0x40c), CI32(c, 0x410), CI32(c, 0x860), CI32(c, 0x868), nd);
            }
        }
        return 0;
    }
    while (frame < 60 * 20 && Race_State(0) != 8) step();
    CHECK(Race_State(0) == 8);
    /* the race's own replay records from the go: a scripted drive of 250 steps, then played back from
       its snapshot with no input, the car on the same path step for step */
    /* RPL_ONLY: the demo alone, from the go (without the own run's 250 steps first) */
    if (!getenv("RPL_ONLY")) {
        Replay *own = Race_Replay(0);
        CHECK(own && Replay_Recording(own));
        enum { N = 250 };
        static int32_t path[N + 1][3];
        script = true, go_clock = Race_Clock();
        int k = 0;
        while (k < N) {
            step();
            for (int s = (Race_Clock() - go_clock) / 4; k < s && k < N; k++) memcpy(path[k], Car_Get(0)->s + 0x2d0, 12);
        }
        script = false;
        CHECK(Replay_Stop(own));
        printf("own run: %d frames for %d steps, the car at %.1f km/h\n", Replay_Frames(own, 0), (Race_Clock() - go_clock) / 4,
               CI32(Car_Get(0), 0x778) / 65536.0 * 90);
        if (getenv("RPL_OWN")) {
            const uint8_t *im = Replay_Image(own, NULL) + 0x110 + 0x114c;
            for (int f = 0; f < Replay_Frames(own, 0); f++)
                printf("  frame %d: %02x %02x %02x %02x (x%d)\n", f, im[4 * f], im[4 * f + 1], im[4 * f + 2], im[4 * f + 3], im[4 * f + 2] & 0x3f);
        }
        CHECK(Replay_Play(own, 0, 0, 0));
        go_clock = Race_Clock();
        int same = 0;
        k = 0;
        while (k < N && Replay_Playing(own)) {
            step();
            for (int s = (Race_Clock() - go_clock) / 4; k < s && k < N; k++) {
                bool eq = !memcmp(path[k], Car_Get(0)->s + 0x2d0, 12);
                if (!eq && getenv("RPL_OWN") && same == k) {
                    const int32_t *q = (const int32_t *)(Car_Get(0)->s + 0x2d0);
                    printf("  step %d (frame %d, %d steps at once): recorded %d %d %d, played %d %d %d\n", k, frame, Race_Clock() / 4 - (go_clock / 4 + k), path[k][0], path[k][1], path[k][2], q[0], q[1], q[2]);
                }
                same += eq;
            }
        }
        printf("own run played: %d of %d steps on the recorded path\n", same, N);
        CHECK(same == N);
        Replay_End(own);
    } else
        Replay_End(Race_Replay(0));
    char dn[64];
    snprintf(dn, sizeof dn, "Game\\DemoRuns\\%c%c%c%c%cH.rpl", demo_stage()[0] - 32, demo_stage()[1], demo_stage()[2], demo_stage()[3], demo_stage()[4]);
    Replay *demo = Replay_Load(dn);
    CHECK(demo != NULL);
    if (!demo || fails) return 1;
    uint32_t size;
    Replay_Image(demo, &size);
    printf("demo: %u bytes, %d frames\n", size, Replay_Frames(demo, 0));
    if (getenv("RPL_DBG")) {
        const int32_t *w = Tod_Weather(Tod_Stage());
        printf("demo: rally %u stage %u, weather %d %d, times %d %d, wet %x\n", Tod_Rally(), Tod_Stage(), w[0], w[1], Tod_StartTime(), Tod_EndTime(),
               Weather_CarWet(Car_Get(0)));
    }
    CHECK(size == 34702 && Replay_Frames(demo, 0) == 725);
    if (getenv("RPL_SEC")) {
        int32_t p[3] = {0x7617569, 0x2a109c, 0x4a43ad8};
        printf("sections: %d, row %d, size %.2f; cell of the start %d; the car's +0xb00 %d\n", g_nsec, g_sec_row, g_sec_size / 65536.0,
               Section_Find(p), CI16(Car_Get(0), 0xb00));
        for (int i = 0; i < 3; i++) printf("  section %d at %.1f %.1f\n", i, g_sec[i].x / 65536.0, g_sec[i].z / 65536.0);
    }
    if (getenv("RPL_DIFF")) {
        const uint8_t *im = Replay_Image(demo, NULL) + 0x110 + 0x4d0;
        Car *c = Car_Get(0);
        for (uint32_t o = 0; o < CAR_SIZE; o += 4) {
            int32_t a = CI32(c, o), b = (int32_t)(im[o] | im[o + 1] << 8 | im[o + 2] << 16 | (uint32_t)im[o + 3] << 24);
            if (a != b) printf("  +0x%03x: port %08x demo %08x\n", o, (unsigned)a, (unsigned)b);
        }
    }
    CHECK(Replay_Play(demo, 0, 0, 0));
    if (getenv("RPL_NUDGE")) {   /* one unit (1/65536) added to the snapshot's x after the restore */
        Car *c = Car_Get(0);
        int32_t d = atoi(getenv("RPL_NUDGE"));
        CW32(c, 0x2d0, CI32(c, 0x2d0) + d);
    }
    int steps = 0, fin_step = -1, st9 = -1;
    int32_t c0 = Race_Clock();
    while (frame < 60 * 60 * 5 && Replay_Playing(demo)) {
        step();
        steps = (Race_Clock() - c0) / 4;
        if (st9 < 0 && Replay_State(demo) == 9) st9 = steps;
        if (fin_step < 0 && Race_Finished(0)) fin_step = steps;
        if (getenv("RPL_LAT") && steps % 5 == 0 && steps <= 400) {
            static int last = -1;
            if (steps != last) {
                last = steps;
                Car *c = Car_Get(0);
                int ci = Car_CatIndex(c);
                int32_t p[3], q[3];
                Cat_Point(ci, p);
                Cat_Point(ci + 1 < Cat_Points() ? ci + 1 : ci, q);
                double dx = (q[0] - p[0]) / 65536.0, dz = (q[2] - p[2]) / 65536.0, l = sqrt(dx * dx + dz * dz);
                double ex = CI32(c, 0x2d0) / 65536.0 - p[0] / 65536.0, ez = CI32(c, 0x2d8) / 65536.0 - p[2] / 65536.0;
                double lat = l > 0 ? (ex * dz - ez * dx) / l : 0;
                printf("  step %3d: cat %4d lateral %6.2f km/h %5.1f steer %d %d thr %d brk %d gear %d\n", steps, ci, lat,
                       CI32(c, 0x778) / 65536.0 * 90, c->s[0x1d0], c->s[0x1d1], c->s[0x1d2], c->s[0x1d3], CI8(c, 0xb1e));
            }
        }
        if (getenv("RPL_DBG") && frame % 120 == 0) {
            Car *c = Car_Get(0);
            printf("  step %d: wet %x, pos %.1f %.1f %.1f, %.1f km/h, %.1f%% of the stage, state %d\n", steps, Weather_CarWet(Car_Get(0)), CI32(c, 0x2d0) / 65536.0,
                   CI32(c, 0x2d4) / 65536.0, CI32(c, 0x2d8) / 65536.0, CI32(c, 0x778) / 65536.0 * 90,
                   Car_StageShare(c) * 100.0 / 65536, Replay_State(demo));
        }
    }
    Car *c = Car_Get(0);
    printf("demo: played %d steps, recorded finish at step %d, the port's at %d; %.1f%% of the stage\n", steps, st9, fin_step,
           Car_StageShare(c) * 100.0 / 65536);
    /* the frames' steps (4,211) and the recorded finish (the change to state 9 at frame 723, step 4,112)
       come from the file; the port's car does not yet reach the finish: it leaves the recorded path
       after about 270 steps (open: the simulation's remaining differences) */
    CHECK(!Replay_Playing(demo) && steps == 4211 && st9 == 4112);
    if (fin_step < 0) printf("demo: not at the finish (the simulation differs from the original's)\n");
    return fails ? 1 : 0;
}
