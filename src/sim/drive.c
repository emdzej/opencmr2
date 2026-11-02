/* Driving a car from the keyboard (see drive.h). */
#include "sim/drive.h"
#include "sim/knock.h"
#include "sim/walls.h"
#include "sim/fade.h"
#include "pending.h"
#include "fixmath.h"
#include "game/gstate.h"
#include "game/mem.h"
#include "game/camera.h"
#include "game/race.h"
#include "game/ai.h"
#include "crt.h"
#include "sim/road_follow.h"
#include "sim/step.h"
#include "sim/progress.h"
#include "game/input.h"
#include "game/ctlcfg.h"
#include "game/pause.h"
#include "game/replay.h"
#include "game/cockpit.h"
#include <string.h>

/* 0x408760: the player's profile keeps the camera chosen */
static void profile_camera(int player, int kind)
{
    uint32_t t = gm32(0x531350 + 0x30u * (uint32_t)player) >> 0x12 & 0xf;
    if (Gs_Mode() == 4) {
        gw8(0x52f3e4 + 0xc4u * (uint32_t)player, (uint8_t)kind);
        gw8(0x52f3ec + 0xc4u * (uint32_t)player, (uint8_t)(gm8(0x52f3ec + 0xc4u * (uint32_t)player) | 2));
        return;
    }
    if (t == 0xf) return;
    if (gm32(0x52fa1c + t * 0x650) & 0x200000) PENDING_NOTE(0x405fd0, "Profile_Camera (0x405fd0)");
    gw32(0x52fa60 + t * 0x650, (uint32_t)kind);
    gw8(0x52fa00 + t, 1);
}

/* 0x47bca0: the player's action bits (0x51f4b0-0x51f4c0, a word each) and whether the steering and
   the pedals are analogue (0x58e0a8-0x58e0aa) */
static void action_masks(int player)
{
    for (int a = 0; a < 9; a++) gw16(0x51f4b0 + 2u * (uint32_t)a, Ctl_Mask(player, a));
    gw8(0x58e0a8, Ctl_AnalogSteer(player) && Ctl_Axis(player, 0) != -1);
    gw8(0x58e0a9, Ctl_AnalogPedals(player) && Ctl_Axis(player, 2) != -1);
    gw8(0x58e0aa, Ctl_AnalogPedals(player) && Ctl_Axis(player, 3) != -1);
}

static uint32_t act(int a) { return gm16(0x51f4b0 + 2u * (uint32_t)a); }

/* the camera key's next camera (Camera_Key 0x47b970 for the race kinds of one profile a player) */
static void camera_key(int player)
{
    int state = Race_State(player);
    int now = Camera_Next(player, state);
    if ((state == 7 || state == 8) && now >= 1 && now <= 5) profile_camera(player, now);
}

/* 0x408800 for the race's kinds 0, 1 and 4 (2 and 3 pair the players, 0x407830) */
static int profile_kind(int player)
{
    int k = Race_Kind(), p = player;
    if (k == 4) p = Race_Base() + player;
    else if (k == 2 || k == 3) PENDING_NOTE(0x407830, "the rear view's profile in a paired race");
    if (Gs_Mode() == 4) return gm8(0x52f3e4 + 0xc4u * (uint32_t)p);
    uint32_t t = gm32(0x531350 + 0x30u * (uint32_t)p) >> 0x12 & 0xf;
    return t != 0xf ? (int)gm32(0x52fa60 + t * 0x650) : 0;
}

/* 0x4232a0 */
static bool camera_allowed(int p, int kind)
{
    return kind == 1 || kind == 2 || (kind == 3 && Cockpit_Ready(p)) || (kind >= 4 && kind <= 6) || kind == 10;
}

/* 0x47bad0: on the line and on the stage, the rear view (10) while its key is held, then the profile's
   camera again (the chase, 4, when it cannot be had) */
static void rear_view(uint32_t held, int player, const Car *c)
{
    int st = Race_State(player), p = CI8(c, 0xb1a);
    if (st != 7 && st != 8) return;
    if ((held & act(8)) && Camera_Kind(player) != 10) Camera_SelectAt(p, 10, 0xffff, Camera_Car(p), false);
    if (!(held & act(8)) && Camera_Kind(p) == 10) {
        int kind = profile_kind(player);
        Camera_SelectAt(p, camera_allowed(p, kind) ? kind : 4, 0xffff, Camera_Car(p), false);
    }
}

/* the analogue positions: the byte of |v| 63 / 0x10000; a pedal's from its own axis, full at -0x10000 */
static uint8_t amount(int32_t v)
{
    int64_t a = v < 0 ? -(int64_t)v : v;
    return (uint8_t)((a * 0x3f0000) >> 32);
}

static uint8_t pedal(int32_t v)
{
    int32_t h = v / 2, p = h + 0x8000;
    if (p < 0) p = -0x8000 - h;
    return (uint8_t)(0x3f - (uint8_t)(((int64_t)p * 0x3f0000) >> 32));
}

void Car_ControlsFromInput(Car *c, int rec)
{
    int player = CI8(c, 0xb1a);
    if (Pause_Active()) return;   /* 0x404f20 */
    if (!Gs_ModeBit19()) {
        if ((int)Gs_SelBits14() <= player) return;
    } else if (player > 0) return;
    action_masks(player);
    InputRecord *r = Input_Record(rec);
    if (r->device != DEV_KEYBOARD) {
        /* the player's keyboard record too (a controller's actions bound to keys, 0x40c440) */
        const InputRecord *k = Input_Record(player);
        r->held |= k->held;
        r->pressed |= k->pressed;
        r->repeat |= k->repeat;
    }
    /* the camera key: not from the trackside (7) or behind (10) views, nor in the replay's state (but
       for 0x44a130, the second player's results, taken as never) */
    if (Race_State(player) != 10) {
        int kind = Camera_Kind(player);
        if (kind != 10 && kind != 7 && (r->pressed & act(7))) camera_key(player);
    }
    if (!Replay_Playing(Race_Replay(player))) {   /* 0x41f3d0 */
        if (!(r->pressed & act(7)) && Camera_Kind(player) != 8) rear_view(r->held, player, c);
        if (r->device == DEV_PAD || r->device == DEV_MOUSE) {
            int sa = gm8(0x58e0a8) ? Ctl_Axis(player, 0) : -1;
            int aa = -1, ba = -1;
            if (gm8(0x58e0a9)) aa = Ctl_Axis(player, 2), ba = Ctl_Axis(player, 3);
            if (sa == -1) {
                if (r->held & act(0)) c->s[0x1d0] = 0x3f;
                else if (r->held & act(1)) c->s[0x1d1] = 0x3f;
            } else {
                CW32(c, 0xb88, Ctl_Damping(CI8(c, 0xb1a)) ? 1 : 2);
                int32_t v = r->axis[sa].value;
                c->s[v < 0 ? 0x1d0 : 0x1d1] = amount(v);
            }
            if (aa == -1) {
                CW32(c, 0xb8c, 0);
                if (r->held & act(2)) c->s[0x1d2] = 0x3f;
            } else {
                CW32(c, 0xb8c, 1);
                int32_t v = r->axis[aa].value;
                uint8_t m = amount(v);
                if (ba != aa) c->s[0x1d2] = pedal(v);
                /* one axis: up accelerates, down brakes (swapped with +0x120; the original asks with
                   the record's number for the player's) */
                else if ((v < 1) != Ctl_Swap(rec)) c->s[0x1d2] = m;
                else CW32(c, 0xb90, 1), c->s[0x1d3] = m;
            }
            if (ba == -1) {
                CW32(c, 0xb90, 0);
                if (r->held & act(3)) c->s[0x1d3] = 0x3f;
            } else if (ba != aa) {
                CW32(c, 0xb90, 1);
                uint8_t m = pedal(r->axis[ba].value);
                if (m) c->s[0x1d3] = m;
            }
        } else {
            CW32(c, 0xb8c, 0);
            CW32(c, 0xb90, 0);
            CW32(c, 0xb88, 0);
            if (r->held & act(2)) c->s[0x1d2] = 0x3f;
            if (r->held & act(3)) c->s[0x1d3] = 0x3f;
            if (r->held & act(0)) c->s[0x1d0] = 0x3f;
            else if (r->held & act(1)) c->s[0x1d1] = 0x3f;
        }
        if (r->held & act(4)) CW32(c, 0x1d8, 1);
        if (CI32(c, 0xb9c)) {
            if (r->pressed & act(5)) c->s[0x1d4] = 1;
            if (r->pressed & act(6)) c->s[0x1d4] = 0xff;
        }
    }
    if (Gs_Option(2)) {
        uint8_t t = c->s[0x1d0];
        c->s[0x1d0] = c->s[0x1d1];
        c->s[0x1d1] = t;
    }
}

/* 0x43f570: the launch when the automatic box first takes over: first gear, the driven wheels' spins
   (+0x860-+0x86c, by the drive split +0x7b4: rear, all four, or front) at the revs through first gear,
   the clutch counter +0xb1f at 10. The last line uses the car being worked on (0x53cc18), not c. */
void Car_Launch(Car *c)
{
    c->s[0xb1e] = 1;
    int32_t w = fx_mul(CI32(c, 0x7a4), CI32(c, 0x7e0)), split = CI32(c, 0x7b4);
    if (split != 0x10000) CW32(c, 0x86c, w), CW32(c, 0x868, w);
    if (split != 0) CW32(c, 0x864, w), CW32(c, 0x860, w);
    c->s[0xb1f] = 10;
    if (g_car) CW32(g_car, 0x7ac, CI32(g_car, 0x7a4));
}

void Car_RaceControls(Car *c, int rec)
{
    CW32(c, 0x1dc, 0);
    CW32(c, 0x1d8, 0);
    memset(c->s + 0x1d0, 0, 5);
    Car_ControlsFromInput(c, rec);
    if (!CI32(c, 0xb9c) && CI32(c, 0xb48) != 1) Car_Launch(c);
    CW32(c, 0xb9c, 1);
}

/* 0x47b000 for a car without a pad: 0x47b620, the computer driver (Ai_Drive 0x47bdd0) with
   selection bit 26 */
void Car_RaceControlsCpu(Car *c, int n)
{
    CW32(c, 0x1dc, 0);
    CW32(c, 0x1d8, 0);
    memset(c->s + 0x1d0, 0, 5);
    if (Gs_SelBit26()) Ai_Drive(c, n, false);
    if (!CI32(c, 0xb9c) && CI32(c, 0xb48) != 1) Car_Launch(c);
    CW32(c, 0xb9c, 1);
}

/* 0x47b640 for a car without a pad: the revs blipped, the counter +0xb47 counting 5-14 steps up on
   the throttle (from rand()), then as many down off it */
void Car_StartLineControlsCpu(Car *c)
{
    CW32(c, 0x1e0, 0);
    CW32(c, 0x1dc, 0);
    CW32(c, 0x1d8, 0);
    memset(c->s + 0x1d0, 0, 5);
    int8_t k = CI8(c, 0xb47);
    if (k < 0) {
        c->s[0xb47] = (uint8_t)(k + 1);
        if (!c->s[0xb47]) c->s[0xb47] = (uint8_t)(crt_rand() % 10 + 5);
    } else if (k < 1)
        c->s[0xb47] = (uint8_t)(crt_rand() % 10 + 5);
    else {
        c->s[0xb47] = (uint8_t)(k - 1);
        if (!c->s[0xb47]) c->s[0xb47] = (uint8_t)(-5 - crt_rand() % 10);
    }
    c->s[0x1d2] = CI8(c, 0xb47) < 1 ? 0 : 0x3f;
    CW32(c, 0x1d8, 1);
    CW32(c, 0x1dc, 0);
    CW32(c, 0xb9c, 0);
}

void Car_StartLineControls(Car *c, int rec)
{
    CW32(c, 0x1e0, 0);
    CW32(c, 0x1dc, 0);
    CW32(c, 0x1d8, 0);
    memset(c->s + 0x1d0, 0, 5);
    /* a player's controls from the input (a car without a pad: Car_StartLineControlsCpu) */
    Car_ControlsFromInput(c, rec);
    CW32(c, 0x1d8, 1);
    CW32(c, 0x1dc, 0);
    CW32(c, 0xb9c, 0);
}

/* 0x47b870 and 0x47b7b0 (the latter after the race state's reader): brake unless +0xb94, no
   throttle, handbrake, automatic box off, the velocity times 0.97 */
static void brake_to_stop(Car *c)
{
    c->s[0x1d3] = CI32(c, 0xb94) ? 0 : 1;
    c->s[0x1d2] = 0;
    CW32(c, 0x1d8, 1);
    CW32(c, 0xb9c, 0);
    CW32(c, 0x1e4, 0);
    for (uint32_t k = 0; k < 3; k++) CW32(c, 0x408 + 4 * k, fx_mul(CI32(c, 0x408 + 4 * k), 0xf851));
}

void Car_FinishedControls(Car *c)
{
    CW32(c, 0x1dc, 0);
    CW32(c, 0x1d8, 0);
    memset(c->s + 0x1d0, 0, 5);
    brake_to_stop(c);
}

void Car_FinishedPlayerControls(Car *c, int rec)
{
    Car_RaceControls(c, rec);
    brake_to_stop(c);
}

int Drive_StepsDue(uint32_t frame, double hz)
{
    return (int)((double)(frame + 1) * 25.0 / hz) - (int)((double)frame * 25.0 / hz);
}

static int16_t cars[CAR_MAX];
static int ncars;

const int16_t *Sim_Cars(int *n)
{
    *n = ncars;
    return cars;
}

void Drive_Step(const int16_t *list, int n)
{
    ncars = n > CAR_MAX ? CAR_MAX : n;
    for (int i = 0; i < ncars; i++) cars[i] = list[i];
    Sim_StepCars(list, n);
    /* (Sim_Step's 0x43f630 and 0x425950: an empty list and the network game's cars) */
    Sim_Collide(list, n);
    Knock_Step();   /* 0x470580 */
    Sim_CarsPost(list, n);
    for (int i = n - 1; i >= 0; i--) Car_Progress(Car_Get(list[i]));   /* 0x4209f0 in Race_Update */
    /* Race_Update after Sim_Step: each car with a step due this frame (+0xb43) ticks its fade */
    for (int i = 0; i < n; i++) Fade_Tick(list[i], false);
}

void Car_PoseSimple(Car *c)
{
    Obj *o = c->main;
    if (!o) return;
    for (int r = 0; r < 3; r++)
        for (int k = 0; k < 3; k++) o->local.m[4 * r + k] = CI32(c, 0x360 + 12 * (uint32_t)r + 4 * (uint32_t)k);
    for (int k = 0; k < 3; k++) o->local.m[12 + k] = CI32(c, 0x2d0 + 4 * (uint32_t)k);
    o->inherit = 0;
    for (Obj *p = o; p; p = p->parent) p->dirty = 1;
    if (c->second) {
        Obj *s = c->second;
        for (int i = 0; i < 16; i++) s->local.m[i] = o->local.m[i];
        for (Obj *p = s; p; p = p->parent) p->dirty = 1;
    }
}
