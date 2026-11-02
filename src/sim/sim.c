/* The world step (see sim.h). */
#include "sim/sim.h"
#include "sim/damage.h"
#include "sim/knock.h"
#include "sim/step.h"
#include "sim/pose.h"
#include "sim/parts.h"
#include "sim/walls.h"
#include "sim/road_follow.h"
#include "game/gstate.h"
#include "game/replay.h"
#include "game/mem.h"
#include "game/timer.h"
#include "pending.h"
#include "exe.h"
#include <math.h>
#include <string.h>

CarList g_all, g_stepped, g_listA, g_simple, g_second, g_collide;

static uint8_t latched;                  /* DAT_0053b530: which of the three times are set */
static uint32_t t_origin, t_now, t_last; /* DAT_0053bd64, DAT_0053bd40, DAT_0053a374 */
static bool reset_flag;                  /* DAT_00538114 */

void Sim_ResetClock(void) { reset_flag = true; }

void Sim_InitLists(int n)
{
    memset(&g_all, 0, sizeof g_all);
    for (int i = 0; i < n && i < CAR_MAX; i++) g_all.car[i] = g_listA.car[i] = g_simple.car[i] = (int16_t)i;
    g_all.n = g_simple.n = g_collide.n = g_listA.n = (int16_t)n;
    /* 0x40bbc0 for the two players' slots (0x53acc8), the pose buffers (0x42bcd0): not ported */
    Pose_Init(n);   /* 0x42bcd0 */
}

/* the step rate, a float at +0xa98 */
static float rate(const Car *c)
{
    uint32_t u = (uint32_t)CI32(c, 0xa98);
    float f;
    memcpy(&f, &u, 4);
    return f;
}

uint32_t Sim_StepCount(int32_t *frac)
{
    if (!(latched & 1)) latched |= 1, t_origin = Timer_Now();
    if (!(latched & 2)) latched |= 2, t_now = Timer_Now();
    if (!(latched & 4)) latched |= 4, t_last = Timer_Now();
    t_now = Timer_Now();
    if (gm8(0x52af41)) t_last = t_now;   /* 0x404f30: paused */
    if (reset_flag) reset_flag = false, t_last = t_now;
    gw8(0x52af41, 0);                    /* 0x404f10 */
    uint32_t most = 0;
    Car *c0 = Car_Get(0);
    float k;
    uint32_t ku = exe_u32(0x5113a0);   /* 0.001f */
    memcpy(&k, &ku, 4);
    for (int i = 0; i < g_all.n; i++) {
        Car *c = Car_Get(g_all.car[i]);
        uint32_t due;
        if (g_all.car[i] < 1 || rate(c) != rate(c0)) {
            /* (x87: the products kept at extended precision, rounded to nearest) */
            double last = (double)(uint32_t)(t_last - t_origin) * (double)k;
            double now = (double)(uint32_t)(t_now - t_origin) * (double)k * (double)rate(c);
            int64_t s = llrint(now);
            CW32(c, 0xa90, (int32_t)llrint((now - (double)s) * 65536.0));
            int64_t was = llrint(last * (double)rate(c));
            due = (uint32_t)(s - was);
            if ((int32_t)due > 5) due = 5;
        } else {
            CW32(c, 0xa90, CI32(c0, 0xa90));
            due = c0->s[0xb43];
        }
        c->s[0xb43] = (uint8_t)due;
        if (most < (due & 0xff)) most = due & 0xff;
    }
    *frac = CI32(c0, 0xa90);
    t_last = t_now;
    return most;
}

void Sim_StepDone(const CarList *l)
{
    for (int i = l->n - 1; i >= 0; i--) {
        Car *c = Car_Get(l->car[i]);
        if (c->s[0xb43]) c->s[0xb43]--;
    }
}

/* 0x428810: the step's lists from each car's steps due; the network modes' list of the players (8, 9,
   11, 12) is not ported */
static void build_lists(void)
{
    uint32_t m = Gs_Mode();
    if (m == 8 || m == 9 || m == 11 || m == 12) PENDING_NOTE(0x428810, "Sim_BuildLists (network)");
    g_second.n = g_stepped.n = g_listA.n = g_simple.n = g_collide.n = 0;
    for (int i = 0; i < g_all.n; i++) {
        int16_t k = g_all.car[i];
        Car *c = Car_Get(k);
        CW32(c, 0xb70, 0), CW32(c, 0xb6c, 0), CW32(c, 0xb68, 0);
        if (!CI32(c, 0xc18)) {
            if (c->s[0xb43]) {
                g_stepped.car[g_stepped.n++] = k;
                g_listA.car[g_listA.n++] = k;
                CW32(c, 0xb64, 0);
                g_collide.car[g_collide.n++] = k;
            }
        } else if (c->s[0xb43])
            g_collide.car[g_collide.n++] = k;
        if (CI32(c, 0xc18) && c->s[0xb43]) g_simple.car[g_simple.n++] = k;
    }
    /* 0x428a00 per stepped car: +0xb68.. from the players' views, arcade only (0x407e70) */
    if (Gs_SelBit26()) PENDING_NOTE(0x428a00, "Car_OutOfView");
}

void Sim_Step(void)
{
    PENDING_NOTE(0x427890, "Net_Messages");
    build_lists();
    Replay_RecordStep();   /* 0x46c8e0 */
    Replay_PlayStep();     /* 0x46d270 */
    Sim_StepCars(g_stepped.car, g_stepped.n);
    if (g_second.n) PENDING_NOTE(0x43f630, "Sim_SecondCars");
    if (g_simple.n) PENDING_NOTE(0x425950, "Sim_SimpleCars");
    Sim_Collide(g_collide.car, g_collide.n);
    Knock_Step();   /* 0x470580 */
    Sim_CarsPost(g_stepped.car, g_stepped.n);
    PENDING_NOTE(0x426fc0, "Sim_Step 0x426fc0, 0x46d5e0 (the replays' poses), 0x444c10");
    Pose_Spin(&g_listA);
    Pose_Spin(&g_simple);
    Pose_Capture(&g_listA);
    Pose_Capture(&g_simple);
    PENDING_NOTE(0x46d510, "Sim_Step 0x46d510 (the replays' poses recorded)");
    Parts_Step(&g_listA);   /* 0x480bb0 */
    Damage_Scrapes(g_stepped.car, g_stepped.n);   /* 0x469e40 */
    /* 0x424710: the steering wheel's force feedback (DirectInput effects; gasm has none, left out) */
    PENDING_NOTE(0x4258e0, "Net_SendCars (the network game's car states, with 0x539cc8 set by 0x427640)");
}
