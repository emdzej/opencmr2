/* Driving a car from the keyboard (see drive.h). */
#include "sim/drive.h"
#include "fixmath.h"
#include "game/gstate.h"
#include "sim/road_follow.h"
#include "sim/step.h"
#include "sim/progress.h"
#include <string.h>

void Drive_ReadInput(uint32_t *held, uint32_t *pressed)
{
    static uint32_t last;
    uint8_t k[KEY_COUNT];
    uint32_t h = 0;
    if (plat_keys(k)) {
        if (k[0xcd]) h |= ACT_RIGHT;      /* Right */
        if (k[0xcb]) h |= ACT_LEFT;       /* Left */
        if (k[0xc8]) h |= ACT_THROTTLE;   /* Up */
        if (k[0xd0]) h |= ACT_BRAKE;      /* Down */
        if (k[0x39]) h |= ACT_HANDBRAKE;  /* Space */
        if (k[0x1e]) h |= ACT_GEAR_UP;    /* A */
        if (k[0x2c]) h |= ACT_GEAR_DOWN;  /* Z */
    }
    uint32_t p = plat_pad(0);
    if (p & PAD_RIGHT) h |= ACT_RIGHT;
    if (p & PAD_LEFT) h |= ACT_LEFT;
    if (p & PAD_B) h |= ACT_THROTTLE;
    if (p & PAD_Y) h |= ACT_BRAKE;
    if (p & PAD_A) h |= ACT_HANDBRAKE;
    if (p & PAD_R) h |= ACT_GEAR_UP;
    if (p & PAD_L) h |= ACT_GEAR_DOWN;
    *held = h;
    *pressed = h & ~last;
    last = h;
}

void Car_ControlsFromInput(Car *c, uint32_t held, uint32_t pressed)
{
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

void Car_RaceControls(Car *c, uint32_t held, uint32_t pressed)
{
    CW32(c, 0x1dc, 0);
    CW32(c, 0x1d8, 0);
    memset(c->s + 0x1d0, 0, 5);
    Car_ControlsFromInput(c, held, pressed);
    CW32(c, 0xb9c, 1);   /* (with +0xb48 not manual, 0x43f570 first: not ported) */
}

int Drive_StepsDue(uint32_t frame, double hz)
{
    return (int)((double)(frame + 1) * 25.0 / hz) - (int)((double)frame * 25.0 / hz);
}

void Drive_Step(const int16_t *list, int n)
{
    Sim_StepCars(list, n);
    Sim_CarsPost(list, n);
    for (int i = n - 1; i >= 0; i--) Car_Progress(Car_Get(list[i]));   /* 0x4209f0 in Race_Update */
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
