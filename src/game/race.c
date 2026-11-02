/* The race (see race.h). The original keeps the race's variables at 0x537dd0-0x538124; they are the
   fields of R, named by address where the meaning is open. */
#include "game/balls.h"
#include "game/shadow.h"
#include "game/race.h"
#include "sim/knock.h"
#include "game/ai.h"
#include "game/fireworks.h"
#include "game/weather.h"
#include "game/skids.h"
#include "game/proc.h"
#include "game/camera.h"
#include "game/common.h"
#include "game/hud.h"
#include "game/sectors.h"
#include "audio/crash.h"
#include "audio/engine.h"
#include "audio/codriver.h"
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
#include "fixmath.h"
#include "render/d3d7.h"
#include "render/twod.h"
#include "pending.h"
#include "game/pause.h"
#include "game/results.h"
#include "game/opponents.h"
#include "game/car_load.h"
#include "audio/codriver.h"
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
    bool skip;              /* DAT_00817574 */
    int32_t t_start[4], t_elapsed[4];   /* 0x58da10, 0x58da30 */
} R;

uint32_t Game_Mode(void) { return gm32(0x52ea4c); }
static void set_mode(uint32_t m) { gw32(0x52ea4c, m); }   /* 0x4057e0 */
static uint32_t players(void) { return Gs_SelBits14(); }
void Race_Skip(void) { R.skip = true; }
int Race_State(int p) { return p < R.group.n ? Proc_State(&R.group, (uint32_t)p) : -1; }
int32_t Race_Clock(void) { return R.clock << 2; }
int32_t Race_StageTime(int p) { return R.t_elapsed[p]; }
int32_t Race_Countdown(void) { return R.countdown; }
bool Race_Finished(int p) { return Sectors_Finished(p); }

/* the stage timers 0x478130-0x4781c0: started, run, cleared, read */
static void timer_start(int p) { R.t_start[p] = Race_Clock(); }
static void timer_run(int p) { R.t_elapsed[p] = Race_Clock() - R.t_start[p]; }
static void timer_clear(int p) { R.t_elapsed[p] = 0; }

static void Race_Update(uint8_t state, bool flag)
{
    uint32_t np = players();
    PENDING_NOTE(0x445a20, "Replay_Playing");   /* a replay: no controls, Sim_Step only on its frames */
    Frame_RateStep();   /* 0x42b790 */
    R.steps = Sim_StepCount(&R.frac);
    for (uint32_t k = 0; k < R.steps; k++) {
        bool car0 = Car_Get(0)->s[0xb43] != 0;
        if (car0) {
            R.clock++;
            if (flag && state == 10) Results_Step();   /* 0x448e70 */
        }
        for (int i = 0; i < g_all.n; i++) {
            Car *c = Car_Get(i);
            if (!c->s[0xb43]) continue;
            uint8_t kind;
            uint32_t held = 0, pressed = 0;
            if ((uint32_t)i < np) {
                kind = Proc_State(&R.group, (uint32_t)i);
                if (H->read_input) H->read_input(i, &held, &pressed);
            } else {
                /* a CPU car: on the line until player 0 races, then racing; head to head (bit 27, not
                   networked) stopped once it has finished. (Bit 28's ghost, 0x466090, is open.) */
                kind = Proc_State(&R.group, 0) < 8 ? 7 : 8;
                if (kind == 8 && Gs_SelBit28() && Gs_Bit30()) PENDING_NOTE(0x466090, "the ghost's controls");
                else if (kind == 8 && Gs_SelBit27() && !Gs_ModeBit19() && Race_Finished(i)) kind = 9;
            }
            bool cpu = (uint32_t)i >= np;   /* no pad (0x42b710 -1) */
            switch (kind) {
            case 5: case 7:
                if (cpu) Car_StartLineControlsCpu(c);
                else Car_StartLineControls(c, held, pressed);
                break;
            case 8: case 13:
                if (cpu) Car_RaceControlsCpu(c, i);
                else Car_RaceControls(c, held, pressed);
                break;
            case 9:
                /* after the finish: with the computer drivers (bit 26) a player's car driven on
                   (0x47d330), a CPU car still racing; else braking to a stop, the player's input read
                   first once finished */
                if (Gs_SelBit26()) {
                    if (!cpu) Ai_AutoDrive(i, false);
                    else Car_RaceControlsCpu(c, i);
                } else if (R.finished[i] && !cpu) Car_FinishedPlayerControls(c, held, pressed);
                else Car_FinishedControls(c);
                break;
            case 10:
                if (Gs_SelBit26() && !cpu) Ai_AutoDrive(i, false);
                else Car_FinishedControls(c);
                break;
            }
            if ((uint32_t)i < np) {
                PENDING_NOTE(0x445ea0, "Replay_Record");
                Hud_Update(i, kind == 5 || kind == 7);   /* 0x411f70 */
                if (state == 8 || state == 9) Codriver_Step(i);   /* 0x417090 */
                else if (state == 7) Codriver_Start(i);           /* 0x416f70 */
                /* 0x404e10: the pause, on the line, in the countdown (not in a network game) and on the
                   stage, not while a fade runs or in the demo (0x41f3d0's check not ported) */
                if ((state == 8 || state == 5 || (state == 7 && !Gs_ModeBit19())) && Pause_Pressed((int)i) &&
                    !Fade_Active((int)i) && !gm8(0x52ea52)) {
                    Pause_Begin((int)i);   /* 0x404ea0 */
                    PENDING_NOTE(0x478c40, "the sounds' pause (0x478c40, 0x418ee0)");
                    Crash_StopSounds();   /* 0x418780 */
                }
            }
        }
        for (uint32_t i = 0; i < np; i++)
            if (Car_Get((int)i)->s[0xb43] && flag) PENDING_NOTE(0x41d0c0, "Race_PlayerStep");
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
        /* the players' sounds once the countdown runs, not while paused (0x47a710, 0x41af60 not ported,
           0x418b00) */
        if (car0 && state > 6 && !gm8(0x52af40))
            for (uint32_t i = 0; i < np; i++) Engine_Update((int)i, (int)i), Crash_UpdateSounds();
        Sim_StepDone(&g_all);
    }
    Knock_Interp(R.frac);   /* 0x471950, first in Race_PoseInterp */
    Car_PoseInterp();   /* Race_PoseInterp 0x41d060 (its other effects not ported) */
    if (Gs_Option(0)) Balls_Frame(R.frac);   /* 0x47e1e0, in 0x41d060 */
    Parts_Interp(R.frac);   /* 0x484d30 */
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

/* state 0, 0x41bf80: the race's variables cleared; for the first player the stage's selection, the
   common archives, the car tables */
static void s0_setup(ProcGroup *g, uint32_t slot)
{
    R.loading = true;
    R.start_count = 0;
    if (slot == 0) {
        /* 0x427c10: the engines at a third of the effects volume, the collisions at two thirds (the
           other volume, 0x41b040, not ported) */
        Engine_Volume((int32_t)(gm32(0x52afb8) >> 7 & 0x7f));
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
        Lamps_Load();    /* 0x463d60 */
        Lights_Load();
        Weather_Load();
        Effects_LoadTextures();
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
        PENDING_NOTE(0x418ff0, "Race_LoadSounds (the surfaces')");
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
            if (Gs_SelBit26()) Ai_Init();   /* 0x48ca70 -> 0x47bda0 */
            Times_Reset();     /* 0x447f70 */
            Sectors_Start();   /* 0x458100, in 0x424c50 */
            Hud_Init(1);       /* 0x411450 */
            R.place_count = 1;
            R.place = 0;
        }
        /* 0x422fe0 per player: the profile's camera (0x408800), the chase (4) when it cannot be had */
        for (uint32_t i = 0; i < players(); i++) {
            uint32_t t = gm32(0x531350 + 0x30 * i) >> 0x12 & 0xf;
            int kind = Gs_Mode() == 4 ? gm8(0x52f3e4 + 0xc4 * i) : t != 0xf ? (int)gm32(0x52fa60 + t * 0x650) : 0;
            if (!(kind == 1 || kind == 2 || (kind >= 4 && kind <= 6) || kind == 10)) kind = 4;   /* 0x4232a0 */
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
    R.finished[slot] = 0;
    R.shown[slot] = 0;
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
    PENDING_NOTE(0x44a120, "Countdown_Sounds");
    if (R.countdown >= 500 || R.skip) {
        Proc_Event(g, slot, 0, 2);
        PENDING_NOTE(0x46c750, "Replay_Start");
        Times_Clear();            /* 0x448100 */
        if (Gs_SelBit22() && !Gs_SelBit25() && !Gs_ModeBit19()) {
            PENDING_NOTE(0x42b800, "the CPU cars' states");
            if (Gs_SelBit26()) Ai_Go(false);   /* 0x47bdc0 */
        }
        timer_start((int)slot);   /* the stage time runs from the go */
    }
}

/* state 8, 0x41d7a0: the stage: steps until the player is past the finish (0x4582b0), the finish's
   time noted */
static void s8_stage(ProcGroup *g, uint32_t slot)
{
    timer_run((int)slot);
    bool fin = Race_Finished((int)slot);
    if (fin) {
        R.nfinished++;
        R.finished[slot] = 1;
        Damage_Finish((int)slot);   /* 0x469a80 */
        R.finish_at[slot] = Timer_Centis();
        Proc_Event(g, slot, 0, 2);
    }
    if (slot == 0) Race_Update(Proc_State(g, 0), R.nfinished == players());
}

/* state 9, 0x41db10: after the finish, four seconds (by the clock) before the finish is counted shown;
   once every player's is and no fade runs, on to the replay */
static void s9_finished(ProcGroup *g, uint32_t slot)
{
    if (R.start_count < 100) R.start_count++;
    if (400 <= Timer_Centis() - R.finish_at[slot] && !R.shown[slot]) {
        R.shown[slot] = 1;
        R.done++;
        PENDING_NOTE(0x46cc60, "Replay_Save");
    }
    int fading = 0;
    for (uint32_t i = 0; i < players(); i++) fading += Fade_Active((int)i);
    bool all = R.done == players() && !fading;
    Race_Update(Proc_State(g, 0), true);
    if (!all && !R.skip) {
        R.first = true;
        return;
    }
    Results_Start(false);   /* 0x44a1b0 */
    Skids_Reset();          /* 0x465530 */
    for (uint32_t i = 0; i < g->n; i++) Proc_Event(g, i, 0, 2);
    R.first = true;
}

/* state 10, 0x41e350: the replay (not ported: the cars run on after the finish) with the results'
   pages over it (Results_Step in the steps); the demo's skip (0x4d0580) ends it, 0x41f2b0: every process
   on to the results state (the results' choices go there through their fades) */
static void s10_replay(ProcGroup *g, uint32_t slot)
{
    if (R.skip || gm8(0x817574)) {
        R.skip = false;
        for (int i = 0; i < (int)players(); i++) Damage_Store(i);   /* 0x469b50 */
        for (uint32_t i = 0; i < g->n; i++) Proc_Event(g, i, 0, 2);
    }
    if (slot != 0) return;
    PENDING_NOTE(0x46cce0, "Replay_Play");
    /* the super special: a rocket each frame while the replay runs (its record 0x537f3c +4, taken as
       clear) */
    if (Gs_SuperSpecial()) Fireworks_Launch();   /* 0x480380 */
    Race_Update(Proc_State(g, 0), true);
    R.first = true;
}

/* state 11, 0x41e8d0: the results until the key; in a rally on to the next stage through the world
   mode (2: the service area and the loading screens) */
static void s11_results(ProcGroup *g, uint32_t slot)
{
    (void)slot;
    Results_Stop();
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
        /* the players' carried damage dropped (0x408290) in the modes the original's switch lists */
        if (m <= 5 || (m >= 8 && m <= 10)) memset(gm(0x52ebc0), 0, 0x148 * 4);
        if ((m == 2 || m == 3 || m == 9 || m == 10) && !service && R.kind != 4) {
            /* 0x41e6b0: the cars back on the line, the co-driver, sectors and timers again, then
               every process to the line (event 4) */
            for (int i = 0; i < g_all.n; i++) Car_Replace(g_all.car[i]);
            for (int i = 0; i < g_all.n; i++) Car_ProgressStart(Car_Get(g_all.car[i]));
            Codriver_Reset();   /* 0x416670 */
            if (Gs_SelBit26()) Ai_Go(true);   /* 0x47bdc0 */
            Times_Reset();      /* 0x447f70 */
            Opp_Times(false);   /* 0x455470 */
            Engine_Reset();
            Times_Clear();
            Hud_Init(gm8(0x53810c));   /* 0x411450 */
            Skids_Reset();   /* 0x465530 */
            Knock_Restore();   /* 0x4702a0 */
            Lights_Reset();   /* 0x463d00 */
            PENDING_NOTE(0x41e6b0, "Race_Restart's splits and replay (0x4584d0, 0x42b800)");
            R.nfinished = 0, R.done = 0;
            memset(R.finished, 0, sizeof R.finished);
            memset(R.shown, 0, sizeof R.shown);
            R.place = 1;
            R.first = true;
            for (uint32_t i = 0; i < g->n; i++) Proc_Event(g, i, 4, 2);
            return;
        }
        /* a rally's restart and the service area's go through mode 2 */
        set_mode(2);
        for (uint32_t i = 0; i < g->n; i++) Proc_Event(g, i, 0, 3);
        R.built = false;
        return;
    }
    PENDING_NOTE(0x41e8d0, "Race_Results");
    if (!R.skip) return;
    R.skip = false;
    set_mode(2);
    for (uint32_t i = 0; i < g->n; i++) Proc_Event(g, i, 0, 3);
    R.built = false;
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
    int w, h;
    D3d_ScreenSize(&w, &h);
    const int16_t r[4] = {0, 0, (int16_t)w, (int16_t)h};   /* 0x464b10: the one player's viewport */
    Rect_Fill(r, rgba, Fade_Arg(car));
}

static void draw(ProcGroup *g, uint32_t slot)
{
    /* state 10's draw 0x411020: the results' page with the view (0x44b330 queues it first) */
    if (Proc_State(g, slot) == 10 && slot == (uint32_t)g->n - 1) Results_Draw();
    if (H->draw) H->draw((int)slot, Proc_State(g, slot));
}

void Race_DrawOverlays(int player)
{
    Hud_Draw(player);         /* 0x4125f0, in the view 0x40f0c0 */
    Codriver_Notes(player);   /* 0x4177d0 */
    fade_overlay(player, gm8(0x52af40) != 0);
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
    if (R.restart) PENDING_NOTE(0x42b660, "Race_Restart");
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
Obj *Race_Camera(int player) { return H && H->camera ? H->camera(player) : NULL; }
