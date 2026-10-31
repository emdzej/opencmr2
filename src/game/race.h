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

typedef struct {
    bool (*load_stage)(void);   /* state 3: Stage_Load 0x41f930 */
    bool (*load_cars)(void);    /* state 4: 0x420150 (Cars_Load 0x456d90) */
    void (*place_cars)(void);   /* state 5: 0x424ed0, 0x424c50 */
    void (*read_input)(int player, uint32_t *held, uint32_t *pressed);   /* the input record */
    void (*pose)(int32_t frac); /* Race_PoseInterp 0x41d060 */
    void (*draw)(int player, int state);   /* the states' draws: the view (0x49d3f0) and the HUD */
    Obj *(*camera)(int player);            /* the player's camera object (0x538e2c) */
} RaceHost;

/* The port: the race with the host, the top-level mode set to 3 (0x4057e0). */
void Race_Begin(const RaceHost *h);
int Race_Frame(void);              /* 0x41b060 */
int Race_State(int player);        /* the player's process state */
int32_t Race_Clock(void);          /* 0x41d290: the steps run since the start, in 1/100 s */
int32_t Race_StageTime(int player);   /* 0x4781c0 */
int32_t Race_Countdown(void);      /* DAT_00537fc0: 1/100 s into the countdown */
bool Race_Finished(int player);    /* 0x4582b0 */
void Race_Skip(void);              /* 0x4d0590: the key that ends the replay and the results */
uint32_t Game_Mode(void);          /* 0x4057d0 */
