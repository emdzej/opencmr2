/* The race (docs/re/architecture.md#modes-and-processes): Race_Frame 0x41b060, the top-level mode 3,
   runs one process per player through the race states (table 0x5190b0, transitions 0x519120): 0 set-up,
   1 the loading screen, 2-4 the loads, 5 the cars on the line, 7 the countdown, 8 the stage, 9 after
   the finish, 10 the replay, 11 the results, 12 back to the front end. Race_Update 0x41c5a0 runs the
   steps due in each of 5 and 7-10. Ported for one player in a rally; the loads, the cameras, the HUD
   and the results screen are the host's (the port's stand-ins) until they are ported. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "world/obj.h"
#include "game/input.h"

typedef struct {
    bool (*load_stage)(void);   /* state 3: Stage_Load 0x41f930 */
    bool (*load_cars)(void);    /* state 4: 0x420150 (Cars_Load 0x456d90) */
    void (*place_cars)(void);   /* state 5: 0x424ed0, 0x424c50 */
    /* after each step's poll (0x49eab0): a host's changes to the player's input record (the tests') */
    void (*read_input)(int player, InputRecord *r);
    void (*pose)(int32_t frac); /* Race_PoseInterp 0x41d060 */
    void (*draw)(int player, int state);   /* the states' draws: the view (0x49d3f0) and the HUD */
    Obj *(*camera)(int player);            /* the player's camera object (0x538e2c) */
} RaceHost;

/* The port: the race with the host, the top-level mode set to 3 (0x4057e0). */
void Race_Begin(const RaceHost *h);
/* the build's players into the selection (0x407500): the original builds the race before its
   state 3 loads the stage */
void Race_SetPlayers(void);
int Race_Frame(void);              /* 0x41b060 */
Obj *Race_Camera(int player);      /* the host's camera object (0x538e2c[player]), NULL without one */
int Race_State(int player);        /* the player's process state */
uint32_t Race_ProcWord(int player);   /* the process's state word (0x41b390's slots), 0 past them */
int32_t Race_Clock(void);          /* 0x41d290: the steps run since the start, in 1/100 s */
int32_t Race_StageTime(int player);   /* 0x4781c0 */
int32_t Race_Countdown(void);      /* DAT_00537fc0: 1/100 s into the countdown */
bool Race_Finished(int player);    /* 0x4582b0 */
void Race_Skip(void);              /* 0x4d0590: the key that ends the replay and the results */
uint32_t Game_Mode(void);
/* The overlays' part of 0x40f8d0 ported so far: the fade (0x428680). The host calls it in its draw. */
void Race_DrawOverlays(int player);          /* 0x4057d0 */
void Race_EventAll(uint32_t event, uint32_t prio);   /* 0x49c1c0 on every race process */
int Race_Cars(void);           /* 0x42b700: the cars in the race */
int32_t Race_Frac(void);       /* 0x41f270: car 0's step fraction (DAT_00537f60) */
void View_Rect(int player, int16_t r[4]);   /* 0x464b10 */
int Race_Base(void);           /* 0x41b370: the first player of the race (DAT_00537ef6) */
void Race_EndReplay(void);   /* 0x41f2b0: every process on from the replay */
bool Race_Exited(void);      /* torn down by the results for the next stage (0x41e670) */
int Race_Winner(void);             /* 0x41f380: the head to head's winner by touching, -1 none */
void Race_RequestRestart(void);    /* 0x41b310: the processes built again from state 5 */
bool Race_Restarted(void);         /* 0x41b320 */
void Race_ClearRestarted(void);    /* 0x41b330 */
void Race_FadeOverlay(int player, bool paused);   /* 0x428680 */
int Race_Reentry(void);        /* the port: 0 a new race, 1 the next player (state 3), 2 the next round's cars (state 4) */
int Race_Kind(void);           /* 0x41b380: DAT_00537df0 */
