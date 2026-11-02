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
#include <string.h>

void Drive_ReadInputFor(int player, uint32_t *held, uint32_t *pressed)
{
    static uint32_t last[2];
    uint8_t k[KEY_COUNT];
    uint32_t h = 0;
    player &= 1;
    if (player == 0 && plat_keys(k)) {
        if (k[0xcd]) h |= ACT_RIGHT;      /* Right */
        if (k[0xcb]) h |= ACT_LEFT;       /* Left */
        if (k[0xc8]) h |= ACT_THROTTLE;   /* Up */
        if (k[0xd0]) h |= ACT_BRAKE;      /* Down */
        if (k[0x39]) h |= ACT_HANDBRAKE;  /* Space */
        if (k[0x1e]) h |= ACT_GEAR_UP;    /* A */
        if (k[0x2c]) h |= ACT_GEAR_DOWN;  /* Z */
        if (k[0x2e]) h |= ACT_CAMERA;     /* C */
    }
    uint32_t p = plat_pad(player);
    if (p & PAD_RIGHT) h |= ACT_RIGHT;
    if (p & PAD_LEFT) h |= ACT_LEFT;
    if (p & PAD_B) h |= ACT_THROTTLE;
    if (p & PAD_Y) h |= ACT_BRAKE;
    if (p & PAD_A) h |= ACT_HANDBRAKE;
    if (p & PAD_R) h |= ACT_GEAR_UP;
    if (p & PAD_L) h |= ACT_GEAR_DOWN;
    if (p & PAD_X) h |= ACT_CAMERA;
    *held = h;
    *pressed = h & ~last[player];
    last[player] = h;
}

void Drive_ReadInput(uint32_t *held, uint32_t *pressed) { Drive_ReadInputFor(0, held, pressed); }

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
    gw32(0x52fa00 + 4 * t, 1);
}

void Car_ControlsFromInput(Car *c, uint32_t held, uint32_t pressed)
{
    /* the camera key (0x51f4be's mask): the next camera (0x47b970), kept in the profile in the race's
       line and stage states; not from the trackside (7) or behind (10) views */
    int player = CI8(c, 0xb1a), kind = Camera_Kind(player);
    if (kind != 10 && kind != 7 && (pressed & ACT_CAMERA) && player < (int)Gs_SelBits14()) {
        int state = Race_State(player);
        int now = Camera_Next(player, state);
        if ((state == 7 || state == 8) && now >= 1 && now <= 5) profile_camera(player, now);
    }
    CW32(c, 0xb8c, 0);
    CW32(c, 0xb90, 0);
    CW32(c, 0xb88, 0);
    if (held & ACT_THROTTLE) c->s[0x1d2] = 0x3f;
    if (held & ACT_BRAKE) c->s[0x1d3] = 0x3f;
    if (held & ACT_RIGHT) c->s[0x1d0] = 0x3f;
    else if (held & ACT_LEFT) c->s[0x1d1] = 0x3f;
    if (held & ACT_HANDBRAKE) CW32(c, 0x1d8, 1);
    if (CI32(c, 0xb9c)) {
        if (pressed & ACT_GEAR_UP) c->s[0x1d4] = 1;
        if (pressed & ACT_GEAR_DOWN) c->s[0x1d4] = 0xff;
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

void Car_RaceControls(Car *c, uint32_t held, uint32_t pressed)
{
    CW32(c, 0x1dc, 0);
    CW32(c, 0x1d8, 0);
    memset(c->s + 0x1d0, 0, 5);
    Car_ControlsFromInput(c, held, pressed);
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

void Car_StartLineControls(Car *c, uint32_t held, uint32_t pressed)
{
    CW32(c, 0x1e0, 0);
    CW32(c, 0x1dc, 0);
    CW32(c, 0x1d8, 0);
    memset(c->s + 0x1d0, 0, 5);
    /* a player's controls from the input (a car without a pad: Car_StartLineControlsCpu) */
    Car_ControlsFromInput(c, held, pressed);
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

void Car_FinishedPlayerControls(Car *c, uint32_t held, uint32_t pressed)
{
    Car_RaceControls(c, held, pressed);
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
    PENDING_NOTE(0x43f630, "Sim_Step entities 0x43f630, 0x425950");
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
