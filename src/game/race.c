/* The race (see race.h). The original keeps the race's variables at 0x537dd0-0x538124; they are the
   fields of R, named by address where the meaning is open. */
#include "game/balls.h"
#include "game/shadow.h"
#include "game/race.h"
#include "sim/knock.h"
#include "sim/aerials.h"
#include "game/ai.h"
#include "game/fireworks.h"
#include "game/weather.h"
#include "game/skids.h"
#include "game/proc.h"
#include "game/camera.h"
#include "game/input.h"
#include "game/common.h"
#include "game/hud.h"
#include "game/dash.h"
#include "game/cockpit.h"
#include "game/sectors.h"
#include "sim/contact.h"
#include "audio/crash.h"
#include "audio/engine.h"
#include "audio/codriver.h"
#include "audio/surface.h"
#include "game/intro.h"
#include "render/rstate.h"
#include "game/gstate.h"
#include "game/mem.h"
#include "game/timer.h"
#include "sim/sim.h"
#include "sim/damage.h"
#include "game/lamps.h"
#include "game/lights.h"
#include "render/sprite3d.h"
#include "world/glow.h"
#include "world/particles.h"
#include "game/effects.h"
#include "sim/pose.h"
#include "sim/parts.h"
#include "sim/drive.h"
#include "sim/fade.h"
#include "sim/progress.h"
#include "exe.h"
#include "audio/snd.h"
#include "fe/fe_page.h"
#include "game/select.h"
#include "game/unlocks.h"
#include "world/tod.h"
#include "game/standings.h"
#include "fixmath.h"
#include "render/d3d7.h"
#include "render/twod.h"
#include "game/profile.h"
#include "text.h"
#include <stdio.h>
#include "pending.h"
#include "game/pause.h"
#include "game/results.h"
#include "game/opponents.h"
#include "game/car_load.h"
#include "game/replay.h"
#include "audio/codriver.h"
#include <stdlib.h>
#include <string.h>

static const RaceHost *H;

static struct {
    ProcGroup group;        /* 0x537dd0 */
    ProcSlot slots[4];      /* 0x537de0 */
    bool built;             /* DAT_00537ef4 */
    bool rebuild;           /* DAT_00537ef5 */
    bool restart;           /* DAT_00537ef8: rebuilt from state 5 */
    uint32_t kind;          /* DAT_00537df0: 0 one player, 1 split screen, 2 and 3 championship, 4 alternate */
    bool first;             /* DAT_00537f08: the countdown's first frame (set by the loads, 9 and 10) */
    int32_t clock;          /* DAT_00537f24: steps since the start */
    int32_t t0;             /* DAT_00537f00 */
    int32_t countdown;      /* DAT_00537fc0 */
    int32_t frac;           /* DAT_00537f60: car 0's step fraction */
    uint32_t steps;         /* DAT_00538124 */
    uint8_t done;           /* DAT_00537fcc: players whose finish has been shown */
    uint8_t nfinished;      /* DAT_00537f88 */
    uint8_t finished[4];    /* DAT_00537f98 */
    uint32_t shown[4];      /* DAT_00537fd8 */
    uint32_t finish_at[4];  /* DAT_00537f34: Timer_Centis at the finish */
    uint32_t load_at;       /* DAT_00537ffc */
    int place;              /* DAT_005297f0: the cars to be put on the line */
    int place_count;        /* DAT_005298f4 */
    bool loading;           /* DAT_00538108 */
    uint8_t start_count;    /* DAT_0053811c */
    int8_t winner;          /* DAT_005191a0: the head to head's winner, -1 none */
    bool skip;              /* DAT_00817574 */
    bool exited;            /* the race torn down (0x41e670) for the next stage without the front end */
    int32_t t_start[4], t_elapsed[4];   /* 0x58da10, 0x58da30 */
} R;

uint32_t Game_Mode(void) { return gm32(0x52ea4c); }
static void set_mode(uint32_t m) { gw32(0x52ea4c, m); }   /* 0x4057e0 */
static uint32_t players(void) { return Gs_SelBits14(); }
void Race_Skip(void) { R.skip = true; }
int Race_State(int p) { return p < R.group.n ? Proc_State(&R.group, (uint32_t)p) : -1; }
uint32_t Race_ProcWord(int p) { return p >= 0 && p < R.group.n ? R.group.slots[p].word : 0; }
int32_t Race_Clock(void) { return R.clock << 2; }
int32_t Race_StageTime(int p) { return R.t_elapsed[p]; }
int32_t Race_Countdown(void) { return R.countdown; }
bool Race_Finished(int p) { return Sectors_Finished(p); }

/* the stage timers 0x478130-0x4781c0: started, run, cleared, read */
static void timer_start(int p) { R.t_start[p] = Race_Clock(); }
static void timer_run(int p) { R.t_elapsed[p] = Race_Clock() - R.t_start[p]; }
static void timer_clear(int p) { R.t_elapsed[p] = 0; }

/* 0x422fe0: the camera of the current kind (0) on another car */
static void camera_set(int p, int kind, int car) { Camera_SelectAt(p, kind ? kind : Camera_Kind(p), 0xffff, car, false); }

/* 0x41d0c0 (Race_Update, once every player has finished): while a player's replay plays, F1 (0x1000)
   between the trackside cameras and the chase camera, F2 (0x2000) the camera on to the next car (one
   player only); with two players in the replay the second player's keys work the second view (not
   in modes 0-2), unless the first's replay plays */
static void player_step(int p)
{
    if (!Replay_Playing(Race_Replay(p))) return;   /* 0x41f3d0 */
    uint32_t keys = Input_Record(p)->pressed;
    int n = Cars_Racing();
    if (!(Gs_SelBits14() > 1 && Proc_State(&R.group, 0) == 10)) {   /* 0x41f3a0 */
        if (keys & 0x1000) {
            Camera_SelectAt(p, Camera_Kind(p) != 7 ? 7 : 4, 0xffff, Camera_Car(p), false);
            return;   /* (0x4ae410 for each car, empty) */
        }
        if ((keys & 0x2000) && 1 < n && Gs_SelBits14() == 1) {
            int c = (uint8_t)(Camera_Car(0) + 1);
            camera_set(0, 0, n <= c ? 0 : c);
        }
        return;
    }
    if (p != 0 && Replay_Playing(Race_Replay(0))) return;
    if (keys & 0x1000) {
        uint32_t m = Gs_Mode();
        if (m != 2 && m != 1 && m != 0) Camera_SelectAt(1, Camera_Kind(1) != 7 ? 7 : 4, 0xffff, Camera_Car(1), false);
    } else if (keys & 0x2000) {
        int c = (uint8_t)(Camera_Car(1) + 1);
        camera_set(1, 0, n <= c ? 0 : c);
    }
}

static void Race_Update(uint8_t state, bool flag)
{
    uint32_t np = players();
    PENDING_NOTE(0x445a20, "Replay_Playing");   /* a replay: no controls, Sim_Step only on its frames */
    Frame_RateStep();   /* 0x42b790 */
    R.steps = Sim_StepCount(&R.frac);
    for (uint32_t k = 0; k < R.steps; k++) {
        bool car0 = Car_Get(0)->s[0xb43] != 0;
        Input_PollAll();   /* 0x49eab0 */
        if (car0) {
            R.clock++;
            if (flag && state == 10) Results_Step();   /* 0x448e70 */
        }
        for (int i = 0; i < g_all.n; i++) {
            Car *c = Car_Get(i);
            if (!c->s[0xb43]) continue;
            uint8_t kind;
            /* the player's input record (0x42b710: 0x53acc8, the players' devices, Sim_InitLists) */
            int rec = i < 8 ? (int8_t)gm8(0x53acc8 + (uint32_t)i) : -1;
            if ((uint32_t)i < np) {
                /* a player's replay playing: the state it recorded (0x41f3d0, +0x10c) */
                kind = Replay_Playing(Race_Replay(i)) ? Replay_State(Race_Replay(i)) : Proc_State(&R.group, (uint32_t)i);
                if (H->read_input && rec >= 0) H->read_input(i, Input_Record(rec));
            } else {
                /* a CPU car: on the line until player 0 races, then racing; head to head (bit 27, not
                   networked) stopped once it has finished. (Bit 28's ghost, 0x466090, is open.) */
                kind = Proc_State(&R.group, 0) < 8 ? 7 : 8;
                if (kind == 8 && Gs_SelBit28() && Gs_Bit30()) PENDING_NOTE(0x466090, "the ghost's controls");
                else if (kind == 8 && Gs_SelBit27() && !Gs_ModeBit19() && Race_Finished(i)) kind = 9;
            }
            bool cpu = (uint32_t)i >= np;   /* no pad (0x42b710 -1) */
            Replay *rp = Race_Replay(i);   /* (0x537f3c by the car: the CPU runs' for the CPU cars) */
            switch (kind) {
            case 5: case 7:
                R.start_count = 0;
                if (cpu) Car_StartLineControlsCpu(c);
                else Car_StartLineControls(c, rec);
                break;
            case 8: case 13:
                if (cpu) Car_RaceControlsCpu(c, i);
                else Car_RaceControls(c, rec);
                break;
            case 9:
                /* after the finish: with the computer drivers (bit 26) a player's car driven on
                   (0x47d330), a CPU car still racing; else braking to a stop, the player's input read
                   first once finished while the replay records or plays (0x41f3f0, 0x41f3d0) */
                if (Gs_SelBit26()) {
                    if (!cpu) Ai_AutoDrive(i, false);
                    else Car_RaceControlsCpu(c, i);
                } else if (i < 4 && R.finished[i] && (Replay_Recording(rp) || Replay_Playing(rp)))
                    Car_FinishedPlayerControls(c, rec);
                else Car_FinishedControls(c);
                break;
            case 10:
                /* the replay's state: as racing while a replay plays (the frames then give the
                   controls), else stopping, a player driven on with the computer drivers */
                if (Gs_SelBit26() && !cpu && !Replay_Playing(rp)) Ai_AutoDrive(i, false);
                else if (Replay_Playing(rp)) {
                    if (cpu) Car_RaceControlsCpu(c, i);
                    else Car_RaceControls(c, rec);
                } else Car_FinishedControls(c);
                break;
            }
            if ((uint32_t)i < np) {
                Dash_Update((int)i);   /* 0x445ea0 */
                if (kind == 5 || kind == 7) Hud_Update(i, true);   /* 0x411f70 */
                else if (kind == 8 || kind == 9) Hud_Update(i, false);
                if (state == 8 || state == 9) Codriver_Step(i);   /* 0x417090 */
                else if (state == 7) Codriver_Start(i);           /* 0x416f70 */
                /* 0x404e10: the pause, on the line, in the countdown (not in a network game) and on the
                   stage (by the step's state), not while the replay plays, a fade runs or in the demo */
                if ((kind == 8 || kind == 5 || (kind == 7 && !Gs_ModeBit19())) && Pause_Pressed((int)i) &&
                    !Replay_Playing(rp) && !Fade_Active((int)i) && !gm8(0x52ea52)) {
                    Pause_Begin((int)i);   /* 0x404ea0 */
                    Engine_Pause();       /* 0x478c40 */
                    Surface_Silence();    /* 0x418ee0 */
                    Crash_StopSounds();   /* 0x418780 */
                }
            }
        }
        for (uint32_t i = 0; i < np; i++) {
            if (!Car_Get((int)i)->s[0xb43]) continue;
            if (flag) player_step((int)i);   /* 0x41d0c0 */
            if (Camera_Kind((int)i) == 7) Camera_Trackside((int)i);   /* 0x421930 */
        }
        Sim_Step();
        if (car0) {
            /* Stage_Step 0x466520 */
            Lights_Step();   /* 0x463bd0 */
            if (Balls_On()) Balls_Step();   /* 0x47dd70 */
            if (Fireworks_On()) Fireworks_Step(), Fireworks_Light();   /* 0x47eab0, 0x480220 */
        }
        for (int i = 0; i < g_all.n; i++)
            if (Car_Get(i)->s[0xb43]) Fade_Tick(i, false);
        if (car0) {
            for (int i = 0; i < g_all.n; i++) Car_Progress(Car_Get(g_all.car[i]));   /* 0x4209f0 */
            Sectors_Step();   /* 0x4581d0 */
            Times_Step();   /* 0x448120 */
            if (gm32(0x52f2ac) >> 0x15 & 1) Opp_Step(R.kind == 4 ? Race_Base() : 0);   /* 0x455590 */
            Weather_CarsStep();   /* Race_Step 0x45e7f0 */
        }
        for (uint32_t i = 0; i < np; i++)
            if (Car_Get((int)i)->s[0xb43]) Camera_Step((int)i);   /* 0x4219b0 */
        /* the particles (not while a replay plays); the original passes the camera after the last
           player's (0x538e2c[players]), read only by the types' callbacks */
        if (car0) Particles_Step(NULL);   /* 0x4b0110 */
        /* the players' sounds once the countdown runs, not while paused (0x47a710, 0x41af60, 0x418b00) */
        if (car0 && state > 6 && !gm8(0x52af40))
            for (uint32_t i = 0; i < np; i++) Engine_Update((int)i, (int)i), Surface_Step((int)i, (int)i), Crash_UpdateSounds();
        Sim_StepDone(&g_all);
    }
    Knock_Interp(R.frac);   /* 0x471950, first in Race_PoseInterp */
    Car_PoseInterp();   /* Race_PoseInterp 0x41d060 (its other effects not ported) */
    Dash_Interp(R.frac);   /* 0x445df0 */
    if (Gs_Option(0)) Balls_Frame(R.frac);   /* 0x47e1e0, in 0x41d060 */
    Parts_Interp(R.frac);   /* 0x484d30 */
    Aerials_Interp(R.frac);   /* 0x486500 */
    Particles_Interp(R.frac);   /* 0x4b06a0 */
    Weather_Interp(R.frac);     /* 0x461bb0 */
    if (Gs_SuperSpecial()) Fireworks_Interp(R.frac);   /* 0x480600 */
    if (H->pose) H->pose(R.frac);
    for (uint32_t i = 0; i < np; i++)
        if (H->camera) Camera_Frame((int)i, CI32(Car_Get((int)i), 0xa90), H->camera((int)i));   /* 0x422140 */
    Lamps_Frame(g_all.car, g_all.n);   /* 0x4664c0 */
    for (int i = 0; i < g_all.n; i++) Fade_Level(i, R.frac, false);
    /* not while paused (or a replay plays): per listed car (its index used as the car) the sounds,
       the wheels' tracking and, but in the network mode and modes 11 and 12, the effects */
    if (!Pause_Active())
        for (int i = 0; i < g_all.n; i++) {
            Skids_Step(i);     /* 0x464c60 */
            Wheels_Track(i);   /* 0x45def0 */
            if (!Gs_SelBit26() && Gs_Mode() != 12 && Gs_Mode() != 11) Effects_Car(i);   /* 0x45af00 */
        }
}

/* of 0x41bf80, for the first player: 0x4728c0 (a challenge's 0x58cf68); the championship's
   standings by mode: a rally championship's 0x40cf00 at its first stage of the first rally and
   0x40d100 at the first stage of any (a single rally too), an arcade championship's 0x40cc60 at its
   first location, then 0x40ccb0 (the start order from the standings); 0x4cf0f0 (not in a challenge)
   the stage's records' flags */
static void race_resets(void)
{
    uint32_t sel = gm32(0x52f2ac), rally = sel & 0x1f, stage = sel >> 5 & 0x1f;   /* 0x406910, 0x406930 */
    if (Gs_Mode() == 4) gw32(0x58cf68, 0);
    switch (Gs_Mode()) {
    case 0:
        if (stage != 0) break;
        if (rally == 0)
            for (uint32_t i = 0; i < 0x10; i++) {   /* 0x40cf00 */
                gw8(0x533538 + i, (uint8_t)i), gw8(0x533548 + i, (uint8_t)i);
                gw32(0x533558 + 4 * i, 0), gw8(0x533598 + i, 0), gw8(0x5335a8 + i, 0);
            }
        /* fall through */
    case 1:
        if (Gs_Mode() == 1 && stage != 0) break;
        for (uint32_t i = 0; i < 0x10; i++) gw32(0x533658 + 4 * i, 0), gw8(0x533638 + i, (uint8_t)i);   /* 0x40d100 */
        break;
    case 5:
        if (!(sel >> 0xc & 3))   /* 0x406950 */
            for (uint32_t i = 0; i < 8; i++) {   /* 0x40cc60 */
                gw32(0x5335b8 + 4 * i, 0), gw32(0x5335f0 + 4 * i, 0), gw8(0x533628 + i, 0);
                gw8(0x533610 + i, (uint8_t)i), gw8(0x533618 + i, (uint8_t)i), gw8(0x533620 + i, (uint8_t)i);
                gw16(0x5335d8 + 3 * i, 0), gw8(0x5335da + 3 * i, 0);
            }
        for (uint32_t i = 0; i < 8; i++) gw8(0x533620 + (uint32_t)(int8_t)gm8(0x533610 + i), (uint8_t)i);   /* 0x40ccb0 */
        break;
    }
    if (Gs_Mode() != 4) {   /* 0x4cf0f0 */
        int n = (int)gm32(0x8173f0);
        for (int i = 0; i < n; i++) gw8(0x817420 + 10u * (uint32_t)i, 0);
        gw32(0x817410, 0);
    }
}

/* state 0, 0x41bf80: the race's variables cleared; for the first player the stage's selection, the
   common archives, the car tables */
static void s0_setup(ProcGroup *g, uint32_t slot)
{
    R.loading = true;
    R.start_count = 0;
    /* 0x409dd0: the seven ghost and network records at 0x531788 (0x80 bytes apart), their first
       halfwords */
    for (uint32_t a = 0x531788; a < 0x531b08; a += 0x80) gw16(a, 0);
    if (slot == 0) {
        race_resets();
        Replay_FreeAll();   /* the last race's (the original's exit handlers, 0x41e670: 0x46c6d0) */
        /* 0x427c10: the engines and the surfaces at a third of the effects volume, the collisions at
           two thirds */
        Engine_Volume((int32_t)(gm32(0x52afb8) >> 7 & 0x7f));
        Surface_Volume(fx_mul((int32_t)(gm32(0x52afb8) >> 7 & 0x7f) * 0x10000 / 100, 0x5555));   /* 0x41b040 */
        Codriver_Volume((int32_t)(gm32(0x52afb8) >> 0xe & 0x7f));   /* 0x418560 */
        Crash_Volume(fx_mul((int32_t)(gm32(0x52afb8) >> 7 & 0x7f) * 0x10000 / 100, 0xaaaa));   /* 0x418d20 */
        PENDING_NOTE(0x41f560, "Race_Setup 0x41f560, 0x41f260, 0x40efa0, 0x475f00");
        Bfl_MountCommon();
        Race_LoadText();   /* 0x478a20 */
        Race_LoadFonts();
        /* 0x40efa0, in part: the sprite queue (0x4b1150), the particles (0x4b0060: 32 types, 400),
           the glows (0x4ae170: 4 with option 0, else 160); of 0x40f020 the lamps' and the start
           lights' textures (0x463360), the weather's (0x45eb50) and the effects' (0x459c80) */
        Sprite3d_Init();
        Particles_Alloc(0x20, 400);
        Glow_Alloc(Gs_Option(0) ? 4 : 0xa0);
        Dash_Reset();    /* 0x445c80 */
        Lamps_Load();    /* 0x463d60 */
        Lights_Load();
        Weather_Load();
        Effects_LoadTextures();
        Codriver_LoadArrows();   /* 0x4165e0 */
        Dash_Load();     /* 0x445a60 */
        Pause_Build();   /* 0x403500 */
    }
    Proc_Event(g, slot, 0, 2);
}

/* state 1, 0x41f420: the loading screen until it is up (0x41f410) */
static void s1_loading(ProcGroup *g, uint32_t slot)
{
    PENDING_NOTE(0x41f410, "LoadScreen_Ready");
    Proc_Event(g, slot, 0, 2);
}

/* state 2, 0x41c0e0: the stage archive (Bfl_MountStage), the countdown's first frame flagged */
static void s2_mount(ProcGroup *g, uint32_t slot)
{
    if (slot == 0) R.load_at = Timer_Centis();
    Proc_Event(g, slot, 0, 2);
    R.first = true;
}

/* states 3 and 4, 0x401000 and 0x4010a0: the stage, then the cars; on a failure every process to the
   results' exit (event 1) and the front end */
static bool load_or_quit(ProcGroup *g, uint32_t slot, bool (*load)(void))
{
    if (slot != 0) return true;
    if (load && load()) return true;
    for (uint32_t i = 0; i < g->n; i++) Proc_Event(g, i, 1, 3);
    set_mode(0);
    return false;
}

static void s3_stage(ProcGroup *g, uint32_t slot)
{
    if (!load_or_quit(g, slot, H->load_stage)) return;
    if (slot == 0) {
        Surface_LoadSounds();    /* 0x418ff0 */
        Crash_LoadSounds();      /* 0x4188c0 */
        Codriver_LoadSpeech();   /* 0x416710 */
        Hud_LoadSounds();        /* 0x411120 */
    }
    Proc_Event(g, slot, 0, 2);
}

static void s4_cars(ProcGroup *g, uint32_t slot)
{
    if (!load_or_quit(g, slot, H->load_cars)) return;
    if (slot == 0) {
        Weather_Alloc(g_all.n);        /* 0x45e5b0 in Cars_Load */
        Shadow_Alloc(g_all.n);         /* 0x494b50 */
        Engine_Load((int)players());   /* 0x478f50 in 0x420150 */
        Engine_Reset();                /* 0x478dc0 */
    }
    R.place = 1;
    /* (the bar at 100% and the clear colour, sage or with 0x52afb4 bit 17 black, are the loading
       screen's: 0x40fec0, 0x49d940) */
    Render_SetClearColour(Gs_ModeBit17() ? 0 : 0x9c, Gs_ModeBit17() ? 0 : 0xb4, Gs_ModeBit17() ? 0 : 0xac);
    Intro_Start();   /* 0x536ac8 */
    Proc_Event(g, slot, 0, 2);
}

/* state 5, 0x401150: the cars put on the line once (0x424ed0, 0x424c50), the cameras set (0x422fe0),
   one round of steps on the line, then the countdown */
static void s5_line(ProcGroup *g, uint32_t slot)
{
    if (slot == 0) {
        if (R.place) {
            if (H->place_cars) H->place_cars();
            Glow_Clear();      /* 0x4ae260, the first of 0x424c50 */
            Skids_Load();      /* 0x465600 */
            if (Balls_On()) Balls_Setup();   /* 0x47d510, in 0x466360 before the cars */
            Lamps_Place();     /* 0x466360's cars */
            Glass_Setup();     /* 0x49ab90, in 0x466360 */
            Shadow_Place();        /* 0x494bb0, in 0x466360 */
            Weather_RaceStart();   /* Race_StartLights 0x45eca0, in 0x466360 */
            Chips_Setup();     /* 0x498dd0, in 0x466360 */
            Lights_Setup();    /* 0x463410, in 0x466360 */
            Effects_Types();   /* 0x45a170, the end of 0x466360 */
            if (Fireworks_On()) Fireworks_Setup(20);   /* 0x47e4d0, the end of 0x466360 */
            /* the replays (Stage_Load's in mode 3); the demo's run (0x41c510) */
            if (gm8(0x52ea52)) PENDING_NOTE(0x41c510, "Replay_LoadDemo");
            else if (Gs_Mode() != 3) Replay_Setup();   /* 0x41c260 */
            if (Gs_SelBit26()) Ai_Init();   /* 0x48ca70 -> 0x47bda0 */
            Times_Reset();     /* 0x447f70 */
            Sectors_Start();   /* 0x458100, in 0x424c50 */
            Hud_Init(1);       /* 0x411450 */
            R.place_count = 1;
            R.place = 0;
        }
        /* 0x421610 (in 0x424c50): the cockpits for the four camera records (two a player) */
        Cockpit_Reset();   /* 0x475f80 */
        for (int r = 0; r < 4; r++) Cockpit_Load(r, r >> 1);   /* 0x4760a0 */
        /* 0x422fe0 per player: the profile's camera (0x408800), the chase (4) when it cannot be had */
        for (uint32_t i = 0; i < players(); i++) {
            uint32_t t = gm32(0x531350 + 0x30 * i) >> 0x12 & 0xf;
            int kind = Gs_Mode() == 4 ? gm8(0x52f3e4 + 0xc4 * i) : t != 0xf ? (int)gm32(0x52fa60 + t * 0x650) : 0;
            if (!(kind == 1 || kind == 2 || (kind == 3 && Cockpit_Ready((int)i)) || (kind >= 4 && kind <= 6) || kind == 10)) kind = 4;   /* 0x4232a0 */
            Camera_Reset((int)i);
            Camera_Select((int)i, kind, false);
        }
        R.place_count--;
        Race_Update(Proc_State(g, 0), false);
    }
    if (R.place_count < 1) Proc_Event(g, slot, 0, 2);
}

/* state 7, 0x41d2b0: the countdown: the stage clock restarted on its first frame, five seconds of steps
   (500 1/100 s) on the line */
static void s7_countdown(ProcGroup *g, uint32_t slot)
{
    R.winner = -1;
    R.finished[slot] = 0;
    R.shown[slot] = 0;
    if (Gs_SelBit27() && !Gs_ModeBit19()) gw8(0x537fbc, 0), gw8(0x537fbd, 0);   /* the head to head's winners */
    timer_run((int)slot);
    int32_t t = Race_StageTime((int)slot);
    if (R.first) {
        R.clock = 0;
        timer_start((int)slot);
        timer_clear((int)slot);
        R.t0 = Race_StageTime((int)slot);
        R.countdown = 1;
        if (slot == (uint32_t)g->n - 1) R.first = false;
        if (slot == 0) Race_Update(Proc_State(g, 0), false);
    } else if (slot == 0) {
        R.countdown = t - R.t0;
        if (!R.countdown) R.countdown = 1;
        Race_Update(Proc_State(g, 0), false);
    }
    gw32(0x5418d4, 0);   /* 0x44a120 (nothing reads it) */
    if (R.countdown >= 500 || R.skip) {
        Proc_Event(g, slot, 0, 2);
        Times_Clear();            /* 0x448100 */
        if (Gs_SelBit22() && !Gs_SelBit25() && !Gs_ModeBit19()) {
            Cars_Reset((int)Gs_SelBits14(), g_all.n - (int)Gs_SelBits14(), false);   /* 0x42b800: the CPU cars */
            if (Gs_SelBit26()) Ai_Go(false);   /* 0x47bdc0 */
        }
        /* the replays from the go: the player's records; with the CPU runs for one player the run of
           each car that is a computer driver's (0x408500; in mode 4 each without a pad, 0x42b710; with
           bit 17 car 1's) plays from its start (0x46d2a0, 0x46cce0), the others' record (0x46c750, as
           the original, into the slot's car). The network game's are not ported. */
        if (Gs_ModeBit19()) PENDING_NOTE(0x41d2b0, "the replays' start (network)");
        else if (players() == 1 && Gs_SelBit25()) {
            int n = Gs_Mode() == 4 ? 2 : 2 - (int)Gs_SelBit28();
            for (int i = 0; i < n; i++) {
                bool run = Gs_Mode() == 4 ? (int8_t)gm8(0x53acc8 + (uint32_t)i) == -1
                         : Gs_ModeBit17() ? i != 0 : Player_Driver(i) != -1;
                if (run) {
                    Replay_End(Race_Replay(i));
                    Replay_Play(Race_Replay(i), 0, 0, i);
                } else Replay_Start(Race_Replay(i), (int)slot);
            }
        } else Replay_Start(Race_Replay((int)slot), (int)slot);   /* 0x46c750 */
        if (Gs_SelBit28() && Gs_Bit30()) PENDING_NOTE(0x466030, "the ghost's run");
        timer_start((int)slot);   /* the stage time runs from the go */
    }
}

/* state 8, 0x41d7a0: the stage: steps until the player is past the finish (0x4582b0), the finish's
   time noted */
static void s8_stage(ProcGroup *g, uint32_t slot)
{
    timer_run((int)slot);
    bool fin = Race_Finished((int)slot);
    if ((gm32(0x52f2b0) >> 0xb & 1) && gm8(0x53d1a7)) fin = true;   /* 0x4082e0, 0x448ca0 */
    /* the head to head (bit 27, not networked): once the cars touch (0x487130) within three points
       of each other (0x4582f0), the winner (0x5191a0) is car 1 when car 0's place (0x4481c0) is ahead
       of car 1's, else car 0, and is through (0x448630, 0x537fbc) */
    if (Gs_SelBit27() && !Gs_ModeBit19() && Cars_Touching() && abs(Sectors_CarPoint(0) - Sectors_CarPoint(1)) < 3) {
        R.winner = (int8_t)(Times_Place(0) < Times_Place(1));
        if ((int)slot == R.winner) {
            Times_Won((int)slot);
            gw8(0x537fbc + slot, 1);
            fin = true;
        }
    }
    if (fin) {
        R.nfinished++;
        R.finished[slot] = 1;
        Damage_Finish((int)slot);   /* 0x469a80 */
        R.finish_at[slot] = Timer_Centis();
        Proc_Event(g, slot, 0, 2);
    }
    if (slot == 0) Race_Update(Proc_State(g, 0), R.nfinished == players());
    if (fin) Camera_SelectAt((int)slot, 7, 0xffff, Camera_Car((int)slot), false);   /* the trackside cameras */
}

/* 0x4071c0 (not in the network game): a rally's last stage, the super special (10) of an odd rally,
   else by the level 3, 7 or 9 */
static int Rally_LastStage(uint32_t rally, uint32_t level)
{
    if (Gs_ModeBit19()) PENDING_NOTE(0x4071c0, "the network rally's last stage");
    if (rally & 1) return 10;
    return level == 0 ? 3 : level == 1 ? 7 : level == 2 ? 9 : 0;
}

/* state 9, 0x41db10: after the finish, four seconds (by the clock) before the finish is counted shown;
   once every player's is and no fade runs, on to the replay */
static void s9_finished(ProcGroup *g, uint32_t slot)
{
    if (R.start_count < 100) R.start_count++;
    if (400 <= Timer_Centis() - R.finish_at[slot] && !R.shown[slot]) {
        R.shown[slot] = 1;
        R.done++;
        Replay_Stop(Race_Replay((int)slot));   /* 0x46cc60 (the network game's every replay) */
        if (Gs_Mode() == 3) PENDING_NOTE(0x46d3f0, "the time trial's best run saved as the .grp");
    }
    int fading = 0;
    for (uint32_t i = 0; i < players(); i++) fading += Fade_Active((int)i);
    bool all = R.done == players() && !fading;
    Race_Update(Proc_State(g, 0), true);
    if (!all && !R.skip) {
        R.first = true;
        return;
    }
    for (uint32_t i = 0; i < g->n; i++) Proc_Event(g, i, 0, 2);
    uint32_t m = Gs_Mode();
    if (Gs_SelBit22() || Gs_SelBit24()) Times_Estimate();   /* 0x4483e0 */
    /* the head to head's winners' times into the computer drivers' tables (0x455af0); with one player
       in the arcade or round a circuit, fewer than two through, car 1's time at split 2 as its
       paired driver's (0x455ab0: 0x541f79, made with the championship's pairs, not ported) */
    if (Gs_SelBit27() && !Gs_ModeBit19()) {
        for (int p = 0; p < 2; p++)
            if (gm8(0x537fbc + (uint32_t)p)) Opp_Insert(Opp_PlayerSlot(p), Times_Finish(p), Sectors_Shown());
        if ((Gs_SelBit22() || Gs_SelBit24()) && players() == 1 && gm8(0x53e18c) < 2)
            PENDING_NOTE(0x455ab0, "the head to head's paired driver (0x541f79)");
    }
    /* in alternation only once the last player has driven: the stage's times into the championship's
       (rally, single rally, mode 8), and at a championship rally's last stage its points */
    if (!Gs_ModeBit17() || Race_Base() == (int)Gs_Players() - 1) {
        if (m == 0 || m == 1 || m == 8) {
            Opp_AddStageTimes();   /* 0x455d10 */
            if (m == 0 && (int)Tod_Stage() == Rally_LastStage(Tod_Rally(), Gs_Mode2())) Stand_AwardPoints();   /* 0x40cf30 */
        }
    }
    if (m == 5) Stand_ArcadeAward(Hud_FinishTimes());   /* 0x448620 */
    if (m != 4 && !(m >= 8 && m <= 12) && (!Gs_ModeBit17() || Race_Base() == (int)Gs_Players() - 1)) {
        if (gm8(0x52afc2)) {
            /* with a cheat found: nothing opened */
            for (uint32_t i = 0; i < Gs_Players(); i++) gw32(0x537f68 + 4 * i, 0), gw32(0x537f78 + 4 * i, 0);
        } else Unlocks_AfterStage();   /* 0x41b460 */
    }
    Results_Start(false);   /* 0x44a1b0 */
    Skids_Reset();          /* 0x465530 */
    R.first = true;
}


/* 0x41e220, a player's replay fade's call: the replay from its start (the network game's others, the
   championship's and the CPU runs' replays of the other cars not ported) */
static void replay_again(int p)
{
    Replay_Play(Race_Replay(p), 0, 0, p);   /* 0x46cce0 */
    if (Gs_ModeBit19() || Gs_Mode() == 4 || Gs_SelBit25()) PENDING_NOTE(0x41e220, "the other cars' replays");
}

/* 0x41f2b0: every process on to the results, the players' damage stored (0x469b50), their replays
   ended (the ghost's and the championship's rest not ported) */
static void end_replay(ProcGroup *g)
{
    for (int i = 0; i < (int)players(); i++) Damage_Store(i);
    for (uint32_t i = 0; i < g->n; i++) {
        Proc_Event(g, i, 0, 2);
        Replay_End(Race_Replay((int)i));
    }
    if ((Gs_SelBit28() && Gs_Bit30()) || Gs_Mode() == 4) PENDING_NOTE(0x41f2b0, "Race_EndReplay (the ghost, the championship)");
}

/* state 10, 0x41e350: the replay with the results' pages over it (Results_Step in the steps): while no
   player's replay plays each player's fade (from the finish's shown flag a fade in at once, then out
   and in) starts it again from the start; the skip key (0x4d0580) ends it (0x41f2b0), the results'
   choices go on through their fades. The super special plays its CPU run (champend.rpl, not ported)
   and launches a rocket each frame once it has ended. */
static void s10_replay(ProcGroup *g, uint32_t slot)
{
    if (Gs_Mode() == 10) PENDING_NOTE(0x41e350, "the arcade replay's time-out");
    if (R.skip || gm8(0x817574)) {
        R.skip = false;
        end_replay(g);
    }
    if (slot != 0) return;
    if (!Gs_SuperSpecial() || !Race_Replay(0)) {
        bool any = false;
        for (int i = 0; i < (int)players(); i++) any |= Replay_Playing(Race_Replay(i));
        if (!any)
            for (uint32_t i = 0; i < players(); i++) {
                int mode = 3;
                if (R.shown[i]) mode = 2, R.shown[i] = 0;
                Fade_Start((int)i, 0xc8000, replay_again, mode, 2, 0, 0, false);   /* 0x428410 */
            }
    }
    /* (the super special's run taken as ended) */
    if (Gs_SuperSpecial()) Fireworks_Launch();   /* 0x480380 */
    Race_Update(Proc_State(g, 0), true);
    R.first = true;
}

/* 0x41e670 and 0x41b300: the race torn down (its voices stopped, its archives and textures freed by
   the port's Play_End) and to be built again */
static void race_exit(ProcGroup *g)
{
    (void)g;
    Voices_StopAll();
    R.exited = true;
    R.built = false;
}

/* state 11, 0x41e8d0: the results until the key; in a rally on to the next stage through the world
   mode (2: the service area and the loading screens) */
static void s11_results(ProcGroup *g, uint32_t slot)
{
    (void)slot;
    Results_Stop();
    R.winner = -1;   /* 0x41f250 */
    /* the players' replays ended (0x41e8d0 on each way out; nothing steps them here) */
    for (int i = 0; i < (Gs_Mode() == 4 || Gs_SelBit25() ? 2 : (int)players()); i++)
        Replay_End(Race_Replay(i)), Replay_Stop(Race_Replay(i));
    if (Pause_QuitAsked()) {
        /* the pause's exit: every process to state 0, back to the front end (0x41e670, 0x41b300) */
        Pause_ClearAsks();
        for (uint32_t i = 0; i < g->n; i++) Proc_Event(g, i, 0, 2);
        set_mode(0);
        R.built = false;
        return;
    }
    if (Pause_RestartAsked()) {
        uint32_t m = Gs_Mode();
        bool service = Pause_ServiceAsked();
        Pause_ClearAsks();
        /* a championship restarted at its rally's last stage: that rally's points back (0x40d010) */
        if (m == 0 && (int)Tod_Stage() == Rally_LastStage(Tod_Rally(), Gs_Mode2())) Stand_UndoPoints();
        /* the players' carried damage dropped (0x408290) in the modes the original's switch lists */
        if (m <= 5 || (m >= 8 && m <= 10)) memset(gm(0x52ebc0), 0, 0x148 * 4);
        if ((m == 2 || m == 3 || m == 9 || m == 10) && !service && R.kind != 4) {
            /* 0x41e6b0: the co-driver and the start lights again, the grid in its modes, the cars
               back on it (0x42b800), the sectors, records, HUD, skids, knocked scenery and the CPU
               drivers again, then every process to the line (event 4). (The wheel's force tables
               0x424ed0, the network and ghost records 0x409dd0/0x409ab0, the challenge records
               0x4cf140 are not kept by the port.) */
            Codriver_Reset();   /* 0x416670 */
            Lights_Reset();     /* 0x463d00 */
            if (Gs_SelBit26() || Gs_SelBit25() || Gs_SelBit31()) Cars_Grid(Gs_Mode() != 4);   /* 0x4584d0 */
            Cars_Reset(0, g_all.n, false);   /* 0x42b800 */
            if (Gs_Mode() == 4)
                for (int i = 0; i < g_all.n; i++) Car_ProgressStart(Car_Get(g_all.car[i]));
            Cars_GridPoints();   /* 0x458480 */
            Opp_Times(false);   /* 0x455470 */
            Sectors_Start();    /* 0x458100 */
            Hud_Init(gm8(0x53810c));   /* 0x411450 */
            Skids_Reset();      /* 0x465530 */
            Knock_Restore();    /* 0x4702a0 */
            if (Gs_SelBit26()) Ai_Go(true);   /* 0x47bdc0 */
            Times_Reset();      /* 0x447f70 */
            Engine_Reset();     /* (the port: the engines' voices, Race_Frame's rebuild in the original) */
            Times_Clear();
            Input_SetMenuMode(false);   /* 0x49ea90 */
            if (Gs_Mode() != 4) gw8(0x537ef6, 0);   /* 0x41b360 */
            gw32(0x53810c, gm32(0x53810c) & 0xffffff00u);
            /* the replays ended, for the championship and the CPU runs the first two */
            for (int i = 0; i < (Gs_Mode() == 4 || Gs_SelBit25() ? 2 : (int)players()); i++)
                Replay_End(Race_Replay(i)), Replay_Stop(Race_Replay(i));
            R.nfinished = 0, R.done = 0;
            memset(R.finished, 0, sizeof R.finished);
            memset(R.shown, 0, sizeof R.shown);
            R.place = 1;
            R.first = true;
            for (uint32_t i = 0; i < g->n; i++) Proc_Event(g, i, 4, 2);
            return;
        }
        /* a rally's restart and the service area's go through mode 2; a championship's or single
           rally's from its first stage with the rally's set-ups (0x4068e0(0), 0x406820) */
        if (m == 0 || m == 1) {
            gw32(0x52f2ac, gm32(0x52f2ac) & 0xfffffc1fu);
            Fe_CarSetups();
        }
        set_mode(2);
        for (uint32_t i = 0; i < g->n; i++) Proc_Event(g, i, 0, 3);
        R.built = false;
        return;
    }
    /* the way on by the race's kind and the mode */
    uint32_t m = Gs_Mode();
    bool fe = true;
    switch (R.kind) {
    case 0:
    case 1:
        if (m <= 1) {
            for (uint32_t i = 0; i < g->n; i++) Proc_Event(g, i, 0, 2);
            if (Sel_NextStage()) {   /* 0x4072d0 */
                uint32_t st = Tod_Stage();
                if (st != 0) {
                    /* the service area after stages 2, 4, 6, 8 and the super special, else on to
                       the next stage at once */
                    if (st == 2 || st == 4 || st == 6 || st == 8 || st == 10) set_mode(2);
                    race_exit(g);
                    return;
                }
                gw8(0x52ea50, 1);   /* 0x4067c0: a new rally (the front end shows it) */
            }
        } else if (R.kind == 0 ? (m == 2 || m == 3 || m == 6 || m == 7 || (m >= 9 && m <= 12)) : (m == 2 || m == 6)) {
            for (uint32_t i = 0; i < g->n; i++) Proc_Event(g, i, 0, 2);
            if (R.kind == 1 && m == 2 && g->n != 1) return;
        } else if (m == 5) {
            for (uint32_t i = 0; i < g->n; i++) Proc_Event(g, i, 0, 2);
            if (Sel_ArcadeNext()) {   /* 0x4074a0: the location's next race, without the front end */
                race_exit(g);
                return;
            }
        } else if (R.kind == 0 && m == 8) {
            for (uint32_t i = 0; i < g->n; i++) Proc_Event(g, i, 0, 2);
            set_mode(Sel_NextStage() ? 2 : 0);
            PENDING_NOTE(0x409ab0, "the network records cleared (0x409ab0, 0x409e30)");
            race_exit(g);
            return;
        } else fe = false;
        break;
    default:
        fe = false;
        break;
    }
    if (!fe) {
        PENDING_NOTE(0x41e8d0, "Race_Results (the championship's, alternation's and the network's ways out)");
        if (!R.skip) return;
        R.skip = false;
        set_mode(2);
        for (uint32_t i = 0; i < g->n; i++) Proc_Event(g, i, 0, 3);
        R.built = false;
        return;
    }
    set_mode(0);
    race_exit(g);
}

/* state 12, 0x41f4e0: back to the front end */
static void s12_quit(ProcGroup *g, uint32_t slot)
{
    (void)g, (void)slot;
    set_mode(0);
}

/* 0x428680 (in the overlays, 0x40f8d0): a player's fade as a rectangle over the view, its alpha the
   level (0x53a06c) of the fade of the car the camera follows, its colour the fade's (0x53a00c), on the
   fade's layer (0x53a20c: the recovery's 2) */
static void fade_overlay(int p, bool paused)
{
    int car = p;   /* 0x422fb0: the camera's car */
    if (paused && Fade_Hold(p)) return;
    if (p >= (int)players() || Fade_LevelOf(car) <= 0) return;
    uint32_t a = (uint32_t)Fade_LevelOf(car) * 0xff >> 16;
    if (a > 0xff) a = 0xff;
    uint32_t c = (uint32_t)Fade_Colour(p);
    uint8_t rgba[4] = {(uint8_t)c, (uint8_t)(c >> 8), (uint8_t)(c >> 16), (uint8_t)a};
    int16_t r[4];
    View_Rect(p, r);
    Rect_Fill(r, rgba, Fade_Arg(car));
}

/* 0x464b10 (with 0x464b60): a player's viewport: the whole screen, or with two players (not in their
   shared replay) the top and bottom halves with 0x52afb4 bit 18, else the left and right */
void View_Rect(int p, int16_t r[4])
{
    int w, h;
    D3d_ScreenSize(&w, &h);
    r[0] = 0, r[1] = 0, r[2] = (int16_t)w, r[3] = (int16_t)h;
    if (Gs_SelBits14() == 1 || (Gs_SelBits14() > 1 && Proc_State(&R.group, 0) == 10) || p < 0 || p > 1) return;
    if (Gs_ModeBit18()) r[1] = (int16_t)(p * (h / 2)), r[3] = (int16_t)(h / 2);
    else r[0] = (int16_t)(p * (w / 2)), r[2] = (int16_t)(w / 2);
}

static void draw(ProcGroup *g, uint32_t slot)
{
    /* state 10's draw 0x411020: the results' page with the view (0x44b330 queues it first) */
    if (Proc_State(g, slot) == 10 && slot == (uint32_t)g->n - 1) Results_Draw();
    if (H->draw) H->draw((int)slot, Proc_State(g, slot));
}

/* 0x40f8d0's split screen part, per view: the finished player's half darkened (0x516ce0) with a
   message while the other still drives (its state 9 against 8, not paused, 100 steps after the
   finish): outside selection bit 26 "0x86 0x87 <the other's name>", or for the head to head's winner
   "0x94 <name>"; with bit 26 only in a race of laps (0x52f2b0 bit 11, kind 0), "0x85 0x87 <the
   other's name>". On the second view the line between the halves (0x516cd8), not in the shared
   replay nor while paused, nor once the second player's results show (0x44a130, taken as never). */
static void split_labels(int p)
{
    bool paused = gm8(0x52af40) != 0;
    if (Gs_SelBits14() < 2 || p > 1) return;
    int o = (p + 1) & 1;
    uint8_t white[4] = {0xff, 0xff, 0xff, 0xff}, dark[4], black[4];
    for (int k = 0; k < 4; k++) dark[k] = (uint8_t)exe_u32(0x516ce0 + (uint32_t)k), black[k] = (uint8_t)exe_u32(0x516cd8 + (uint32_t)k);
    if (Race_State(p) == 9 && Race_State(o) == 8 && !paused && R.start_count == 100) {
        int16_t v[4];
        char t[256];
        bool say = true;
        if (Gs_Mode() == 4) PENDING_NOTE(0x473810, "the championship's names in the split screen");
        if (!Gs_SelBit26()) {
            View_Rect(p, v);
            Rect_Fill(v, dark, 2);
            if (R.winner == -1 || R.winner != p)
                snprintf(t, sizeof t, "%s %s %s", Text_Get(0x86), Text_Get(0x87), Player_Name(o));
            else
                snprintf(t, sizeof t, "%s %s", Text_Get(0x94), Player_Name(p));
        } else if ((gm32(0x52f2b0) >> 0xb & 1) && (gm32(0x52f2b0) & 7) == 0) {
            View_Rect(p, v);
            Rect_Fill(v, dark, 2);
            snprintf(t, sizeof t, "%s %s %s", Text_Get(0x85), Text_Get(0x87), Player_Name(o));
        } else
            say = false;
        if (say) Hud_Message(t, white, p, false, -1, -1);
    }
    bool shared = Gs_SelBits14() > 1 && Race_State(0) == 10;   /* 0x41f3a0 */
    if (!shared && Hud_SplitScreen() && !paused && p == 1 && Race_State(1) != 10) {
        int16_t v[4], l[4];
        View_Rect(1, v);
        if (!Gs_ModeBit18()) l[0] = v[0], l[1] = v[1], l[2] = 1, l[3] = v[3];
        else l[0] = 0, l[1] = v[1], l[2] = v[2], l[3] = 1;
        Rect_Fill(l, black, 2);
    }
}

void Race_DrawOverlays(int player)
{
    Hud_MessageReset();       /* 0x417e60, in the view 0x40f0c0 */
    Dash_Draw(player);        /* 0x446210 */
    Hud_Draw(player);         /* 0x4125f0 */
    Codriver_Notes(player);   /* 0x4177d0 */
    fade_overlay(player, gm8(0x52af40) != 0);
    split_labels(player);
    Intro_Draw(player, Race_State(player));   /* 0x40f8d0 */
}

/* The table at 0x5190b0 in its order; 6 (the intro camera, 0x472e00) and 13 (0x41e5c0) are not ported */
static const ProcState states[14] = {
    {s0_setup, NULL}, {s1_loading, draw}, {s2_mount, draw}, {s3_stage, NULL}, {s4_cars, NULL},
    {s5_line, draw}, {NULL, draw}, {s7_countdown, draw}, {s8_stage, draw}, {s9_finished, draw},
    {s10_replay, draw}, {s11_results, draw}, {s12_quit, NULL}, {NULL, draw},
};

/* Race_Frame's build: the players (0x405d70) and the kind of race; the players into the selection
   (0x407500) */
static uint32_t race_kind(uint32_t *np)
{
    uint32_t n = gm32(0x52afb4) >> 13 & 0xf;   /* 0x405d70: the players */
    uint32_t kind;
    if (Gs_Mode() == 4) {
        PENDING_NOTE(0x408500, "Race_Frame (championship)");
        kind = 2, n = 1;
    } else if (!(Gs_Mode() == 0 && (gm8(0x52f2ac) & 0x1f) == 8 && (gm32(0x52f2ac) & 0x3e0) == 0x160) && 1 < n) {
        /* (0x407270: rally 8 stage 11, the super special, races head to head) */
        if (Gs_Mode() != 3 && n < 3 && !Gs_ModeBit17()) kind = 1, n = 2;
        else kind = 4, n = 1;
    } else
        kind = 0, n = 1;
    gw32(0x52f2ac, (n & 3) << 14 | (gm32(0x52f2ac) & 0xffff3fff));   /* 0x407500 */
    *np = n;
    return kind;
}

void Race_SetPlayers(void) { uint32_t n; race_kind(&n); }

void Race_Begin(const RaceHost *h)
{
    H = h;
    memset(&R, 0, sizeof R);
    R.winner = -1;   /* 0x5191a0's start */
    set_mode(3);
}

int Race_Frame(void)
{
    if (R.built && !R.rebuild && !R.restart) {
        if (Pause_Active()) {   /* 0x404f20 */
            Pause_Frame();   /* 0x404f40 */
            /* 0x405470: each player's view and overlays, the page over them */
            Pause_Draw();
            for (uint32_t i = 0; i < R.group.n; i++)
                if (H->draw) H->draw((int)i, Proc_State(&R.group, i));
            Proc_Advance(&R.group);
            return 0;
        }
        Proc_UpdateAll(&R.group);
        Proc_DrawAll(&R.group);
        Proc_Advance(&R.group);
        return 0;
    }
    if (!R.built) gw8(0x537ef6, 0);   /* 0x41b360 */
    uint32_t n, kind = race_kind(&n);
    R.kind = kind;
    for (uint32_t i = 0; i < n; i++) Proc_InitSlot(&R.slots[i], R.restart ? 5 : 0, kind);
    Proc_Init(&R.group, (uint8_t)n, R.slots, states, exe_data(0x519120, 26 * 4));
    if (R.restart) {
        PENDING_NOTE(0x42b660, "Race_Restart");
        Dash_Reset();   /* 0x445c80, after Sim_InitLists(2) */
    }
    R.built = true;
    R.rebuild = false;
    R.restart = false;
    return 0;
}

void Race_EventAll(uint32_t event, uint32_t prio)
{
    for (uint32_t i = 0; i < R.group.n; i++) Proc_Event(&R.group, i, event, prio);
}

int Race_Cars(void) { return g_all.n; }
int32_t Race_Frac(void) { return R.frac; }
int Race_Base(void) { return gm8(0x537ef6); }   /* 0x41b370 */
int Race_Kind(void) { return (int)R.kind; }   /* 0x41b380 */
bool Race_Exited(void) { return R.exited; }
void Race_EndReplay(void) { end_replay(&R.group); }   /* 0x41f2b0 */
Obj *Race_Camera(int player) { return H && H->camera ? H->camera(player) : NULL; }
