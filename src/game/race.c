/* The race (see race.h). The original keeps the race's variables at 0x537dd0-0x538124; they are the
   fields of R, named by address where the meaning is open. */
#include "game/race.h"
#include "game/proc.h"
#include "game/gstate.h"
#include "game/mem.h"
#include "game/timer.h"
#include "sim/sim.h"
#include "sim/drive.h"
#include "sim/fade.h"
#include "sim/progress.h"
#include "exe.h"
#include "pending.h"
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
    uint8_t race_done[CAR_MAX];   /* 0x542e92 + 0x1c n: past the finish */
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
bool Race_Finished(int p) { return R.race_done[p]; }

/* the stage timers 0x478130-0x4781c0: started, run, cleared, read */
static void timer_start(int p) { R.t_start[p] = Race_Clock(); }
static void timer_run(int p) { R.t_elapsed[p] = Race_Clock() - R.t_start[p]; }
static void timer_clear(int p) { R.t_elapsed[p] = 0; }

/* 0x4581d0 after the cars' progress: the sectors (0x458e00, the 0x1c-byte records at 0x542e78) count
   each car through the stage and flag it past the finish. Not ported: the port flags a car once its
   share of the stage (Car_StageShare) reaches 1. */
static void sectors(void)
{
    PENDING_NOTE(0x4581d0, "Race_Sectors");
    for (int i = 0; i < g_all.n; i++)
        if (!R.race_done[i] && Car_StageShare(Car_Get(g_all.car[i])) >= 0x10000) R.race_done[i] = 1;
}

static void Race_Update(uint8_t state, bool flag)
{
    uint32_t np = players();
    PENDING_NOTE(0x445a20, "Replay_Playing");   /* a replay: no controls, Sim_Step only on its frames */
    R.steps = Sim_StepCount(&R.frac);
    for (uint32_t k = 0; k < R.steps; k++) {
        bool car0 = Car_Get(0)->s[0xb43] != 0;
        if (car0) {
            R.clock++;
            if (flag && state == 10) PENDING_NOTE(0x448e70, "Replay_Step");
        }
        for (int i = 0; i < g_all.n; i++) {
            Car *c = Car_Get(i);
            if (!c->s[0xb43]) continue;
            uint8_t kind;
            uint32_t held = 0, pressed = 0;
            if ((uint32_t)i < np) {
                kind = Proc_State(&R.group, (uint32_t)i);
                if (H->read_input) H->read_input(i, &held, &pressed);
            } else
                kind = Proc_State(&R.group, 0) < 8 ? 7 : 8;   /* CPU cars (0x407ea0, 0x407e90 cases open) */
            switch (kind) {
            case 5: case 7:
                Car_StartLineControls(c, held, pressed);
                break;
            case 8: case 13:
                Car_RaceControls(c, held, pressed);
                break;
            case 9: case 10:
                /* after the finish: braking to a stop, the player's input read first once finished */
                if (R.finished[i] && (uint32_t)i < np) Car_FinishedPlayerControls(c, held, pressed);
                else Car_FinishedControls(c);
                break;
            }
            if ((uint32_t)i < np) {
                PENDING_NOTE(0x445ea0, "Replay_Record");
                PENDING_NOTE(0x411f70, "Hud_Gauges");
                if (state == 8 || state == 9) PENDING_NOTE(0x417090, "Codriver_Step");
                else if (state == 7) PENDING_NOTE(0x416f70, "Codriver_Start");
                PENDING_NOTE(0x404e10, "Pause_Pressed");
            }
        }
        for (uint32_t i = 0; i < np; i++)
            if (Car_Get((int)i)->s[0xb43] && flag) PENDING_NOTE(0x41d0c0, "Race_PlayerStep");
        Sim_Step();
        if (car0) PENDING_NOTE(0x466520, "Damage_Step");
        for (int i = 0; i < g_all.n; i++)
            if (Car_Get(i)->s[0xb43]) Fade_Tick(i, false);
        if (car0) {
            for (int i = 0; i < g_all.n; i++) Car_Progress(Car_Get(g_all.car[i]));   /* 0x4209f0 */
            sectors();
            PENDING_NOTE(0x448120, "Race_Step 0x448120, 0x45e7f0");
        }
        for (uint32_t i = 0; i < np; i++)
            if (Car_Get((int)i)->s[0xb43]) PENDING_NOTE(0x4219b0, "Split_Check");
        if (car0) PENDING_NOTE(0x4b0110, "Skids_Step");
        Sim_StepDone(&g_all);
    }
    if (H->pose) H->pose(R.frac);   /* Race_PoseInterp 0x41d060 with the fraction */
    PENDING_NOTE(0x422140, "Camera_Update");
    for (int i = 0; i < g_all.n; i++) Fade_Level(i, R.frac, false);
    PENDING_NOTE(0x464c60, "Car_Sounds 0x464c60, 0x45def0, 0x45af00");
}

/* state 0, 0x41bf80: the race's variables cleared; for the first player the stage's selection, the
   common archives, the car tables */
static void s0_setup(ProcGroup *g, uint32_t slot)
{
    R.loading = true;
    R.start_count = 0;
    if (slot == 0) PENDING_NOTE(0x427c10, "Race_Setup 0x427c10, 0x41f560, 0x41f260, Bfl_MountCommon, 0x478b50");
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
    Proc_Event(g, slot, 0, 2);
}

static void s4_cars(ProcGroup *g, uint32_t slot)
{
    if (!load_or_quit(g, slot, H->load_cars)) return;
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
            R.place_count = 1;
            R.place = 0;
        }
        PENDING_NOTE(0x422fe0, "Camera_Set");
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
        PENDING_NOTE(0x469a80, "Damage_Finish");
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
    for (uint32_t i = 0; i < g->n; i++) Proc_Event(g, i, 0, 2);
    R.first = true;
}

/* state 10, 0x41e350: the replay (not ported: the cars run on after the finish) until the key
   (0x4d0580), then 0x41f2b0: every process on to the results */
static void s10_replay(ProcGroup *g, uint32_t slot)
{
    if (R.skip) {
        R.skip = false;
        for (uint32_t i = 0; i < g->n; i++) Proc_Event(g, i, 0, 2);
    }
    if (slot != 0) return;
    PENDING_NOTE(0x46cce0, "Replay_Play");
    Race_Update(Proc_State(g, 0), true);
    R.first = true;
}

/* state 11, 0x41e8d0: the results until the key; in a rally on to the next stage through the world
   mode (2: the service area and the loading screens) */
static void s11_results(ProcGroup *g, uint32_t slot)
{
    PENDING_NOTE(0x41e8d0, "Race_Results");
    if (!R.skip) return;
    R.skip = false;
    set_mode(2);
    for (uint32_t i = 0; i < g->n; i++) Proc_Event(g, i, 0, 3);
    (void)slot;
}

/* state 12, 0x41f4e0: back to the front end */
static void s12_quit(ProcGroup *g, uint32_t slot)
{
    (void)g, (void)slot;
    set_mode(0);
}

static void draw(ProcGroup *g, uint32_t slot)
{
    if (H->draw) H->draw((int)slot, Proc_State(g, slot));
}

/* The table at 0x5190b0 in its order; 6 (the intro camera, 0x472e00) and 13 (0x41e5c0) are not ported */
static const ProcState states[14] = {
    {s0_setup, NULL}, {s1_loading, draw}, {s2_mount, draw}, {s3_stage, NULL}, {s4_cars, NULL},
    {s5_line, draw}, {NULL, draw}, {s7_countdown, draw}, {s8_stage, draw}, {s9_finished, draw},
    {s10_replay, draw}, {s11_results, draw}, {s12_quit, NULL}, {NULL, draw},
};

void Race_Begin(const RaceHost *h)
{
    H = h;
    memset(&R, 0, sizeof R);
    set_mode(3);
}

int Race_Frame(void)
{
    if (R.built && !R.rebuild && !R.restart) {
        if (gm8(0x52af40)) {   /* 0x404f20: paused */
            PENDING_NOTE(0x404f40, "Pause_Menu");
            Proc_Advance(&R.group);
            return 0;
        }
        Proc_UpdateAll(&R.group);
        Proc_DrawAll(&R.group);
        Proc_Advance(&R.group);
        return 0;
    }
    if (!R.built) gw8(0x537ef6, 0);   /* 0x41b360 */
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
    R.kind = kind;
    for (uint32_t i = 0; i < n; i++) Proc_InitSlot(&R.slots[i], R.restart ? 5 : 0, kind);
    Proc_Init(&R.group, (uint8_t)n, R.slots, states, exe_data(0x519120, 26 * 4));
    if (R.restart) PENDING_NOTE(0x42b660, "Race_Restart");
    R.built = true;
    R.rebuild = false;
    R.restart = false;
    return 0;
}
