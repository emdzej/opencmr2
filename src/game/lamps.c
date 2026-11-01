/* The cars' lamps (see lamps.h). */
#include "game/shadow.h"
#include "game/lamps.h"
#include "game/weather.h"
#include "game/car_load.h"
#include "game/common.h"
#include "game/mem.h"
#include "game/timer.h"
#include "render/dlight.h"
#include "render/light.h"
#include "render/tex.h"
#include "sim/damage.h"
#include "sim/sim.h"
#include "world/glow.h"
#include "world/relight.h"
#include "world/stage.h"
#include "c3d.h"
#include "exe.h"
#include "fixmath.h"
#include "pending.h"
#include <stdio.h>
#include <string.h>

enum { LAMPS = 20, REC = 0x28, GROUPS = 5 };

/* the lamps' sprite and shaft textures by the lamp's +0x21: brake, reversing, hazard, hazard, head
   (0x547fac and 0x547ff8, five each) */
static Tex *sprite_tex[GROUPS], *shaft_tex[GROUPS];
static Glow *glow[CAR_MAX][LAMPS];   /* 0x547d00 + 0x50 n */
static int32_t hazard[CAR_MAX];      /* 0x547ce0: the hazards' clock */
/* 0x543580 / 0x5431c0 (0x45a150, 0x45b530): the kind 9 lamps (the exhausts)' records and glows, two
   a car (read by the flames 0x45af90 and 0x45d2d0, effects.c) */
static const uint8_t *k9_rec[16];
/* the headlights' lights (0x547fec, 0x547ff0) and their offsets in the car (0x547fe0, 0x547fa0); the
   attenuation last set is 0x547ff4 */
static Obj *head_light[2];
static int32_t ahead[2][3];
static Glow *k9_glow[16];

/* A lens (0x58d6d0 + 0x48 n): the textures of the second model's and the interior's lenses, the
   levels of each group and side (+0x08 left, +0x12 right), the levels last put into the textures
   (+0x1c, +0x26), the fades (+0x30, +0x3a), the groups on per side (+0x44, +0x45) and whether the side
   lamps also light the brake lenses (+0x46). */
static struct {
    Tex *tex[2];
    int16_t level[2][GROUPS], last[2][GROUPS], fade[2][GROUPS];
    uint8_t bits[2];
    uint8_t head_on_brakes;
} lens[CAR_MAX];

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static int32_t ri32(const uint8_t *p) { return (int32_t)rd32(p); }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static void wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8); }
static int car_of(const Car *c) { int n = CI8(c, 0xb1a); return n >= 0 && n < CAR_MAX ? n : 0; }

/* the car's lamp table, .cin section 0: a count, then 0x28 bytes a lamp: +0x00 the place, +0x0c the
   direction, +0x18 the size, +0x1c the brightness, +0x20 the kind (the level it follows), +0x21 the
   texture, +0x22 the loose part it is on (-1: none), +0x24 / +0x26 the damage slot and vertex nearest
   (written by Lamps_Setup) */
static uint8_t *table(int n, int *count)
{
    uint8_t *s = (uint8_t *)Car_CinSection(n, 0);
    int k = s ? ri32(s) : 0;
    *count = k < 0 ? 0 : k > LAMPS ? LAMPS : k;
    return s ? s + 4 : NULL;
}

int Lamps_Count(int car)
{
    int k;
    table(car, &k);
    return k;
}

Tex *Lamps_SpriteTex(int k) { return k >= 0 && k < GROUPS ? sprite_tex[k] : NULL; }
Tex *Lens_Tex(int i) { return i >= 0 && i < CAR_MAX ? lens[i].tex[0] : NULL; }
bool Lamps_On(int car, int k) { return car >= 0 && car < CAR_MAX && k >= 0 && k < LAMPS && glow[car][k] && glow[car][k]->on; }
int32_t Lamps_Glow(int car, int k) { return Lamps_On(car, k) ? glow[car][k]->level : 0; }

void Lamps_Load(void)
{
    Glow_Clear();   /* (and at exit) */
    /* the order of the loads: hazard, reversing, head, brake, hazard, for both tables */
    static const uint32_t name[5] = {0x51b9d4, 0x51b9bc, 0x51b9a4, 0x51b98c, 0x51b9d4};
    static const int at[5] = {2, 1, 4, 0, 3};
    for (int t = 0; t < 2; t++)
        for (int i = 0; i < 5; i++) {
            char p[260];
            snprintf(p, sizeof p, "%s%s", exe_str(0x516a50), exe_str(name[i]));   /* 0x40ed50's \Textures */
            uint8_t found;
            Tex *x = Bfl_FindTexture(Bfl_CommonArc(), p, &found, false, 0);
            (t ? shaft_tex : sprite_tex)[at[i]] = x;
        }
}

void Lamps_Setup(Car *c)
{
    int n = car_of(c);
    if (n == 0) {
        /* the headlights' two point lights, grey 20, ahead of the car by 30 and 15, under the world */
        static const int32_t zero[3];
        static const uint16_t none[3];
        ahead[0][0] = 0x1e0000, ahead[0][1] = ahead[0][2] = 0;
        ahead[1][0] = 0xf0000, ahead[1][1] = ahead[1][2] = 0;
        head_light[0] = Dlight_Add(0, 0x140000, 0x140000, 0x140000, zero, none, &g_world);
        head_light[1] = Dlight_Add(0, 0x140000, 0x140000, 0x140000, zero, none, &g_world);
    }
    int count;
    uint8_t *lamp = table(n, &count);
    int32_t best[LAMPS];
    int k9 = 0;
    for (int i = 0; i < count; i++) {
        uint8_t *r = lamp + REC * i;
        Obj *parent = c->second;
        if (Damage_Dents(n) && (int8_t)r[0x22] != -1) {
            parent = Damage_PartObj(n, (int8_t)r[0x22]);
            if (!parent) parent = c->second;
        }
        int t = r[0x21] < GROUPS ? r[0x21] : 0;
        Tex_SetBlend(sprite_tex[t], 1);
        Tex_SetBlend(shaft_tex[t], 1);
        int32_t pos[3] = {ri32(r), ri32(r + 4), ri32(r + 8)}, dir[3] = {ri32(r + 0xc), ri32(r + 0x10), ri32(r + 0x14)};
        glow[n][i] = Glow_Add(2, pos, dir, ri32(r + 0x18), ri32(r + 0x18), sprite_tex[t], shaft_tex[t], ri32(r + 0x1c),
                              parent, r[0x20] == 9 ? 0xff : 0, 0x10000);
        Glow_Show(glow[n][i], true);
        best[i] = 0x3e80000;   /* 1000 */
        if (r[0x20] == 9) {
            if (k9 + 2 * n < 16) k9_rec[k9 + 2 * n] = r, k9_glow[k9 + 2 * n] = glow[n][i];
            k9++;
        }
    }
    /* with dents the lamps follow the nearest vertex of the damage slots (the squared distance, the
       first of equals kept) */
    if (Damage_Dents(n))
        for (int s = 0; s < Damage_Slots(n); s++)
            for (int v = 0; v < Damage_SlotVerts(n, s); v++) {
                int32_t p[3];
                if (!Damage_RestVert(n, s, v, p)) continue;
                for (int i = 0; i < count; i++) {
                    uint8_t *r = lamp + REC * i;
                    int32_t d[3] = {ri32(r) - p[0], ri32(r + 4) - p[1], ri32(r + 8) - p[2]};
                    int32_t dd = fx_mul(d[0], d[0]) + fx_mul(d[1], d[1]) + fx_mul(d[2], d[2]);
                    if (dd < best[i]) {
                        best[i] = dd;
                        wr16(r + 0x24, (uint16_t)s);
                        wr16(r + 0x26, (uint16_t)v);
                    }
                }
            }
    gw32(0x547ff4, 0xffff0000);   /* the headlights' last attenuation */
    memset(hazard, 0, sizeof hazard);
}

const uint8_t *Lamps_Exhaust(int car, int i) { return car >= 0 && car < 8 && i >= 0 && i < 2 ? k9_rec[2 * car + i] : NULL; }
Glow *Lamps_ExhaustGlow(int car, int i) { return car >= 0 && car < 8 && i >= 0 && i < 2 ? k9_glow[2 * car + i] : NULL; }

void Lamps_Place(void)
{
    for (int i = 0; i < g_all.n; i++) {
        if (2 * i + 1 < 16) k9_rec[2 * i] = k9_rec[2 * i + 1] = NULL, k9_glow[2 * i] = k9_glow[2 * i + 1] = NULL;
        Car *c = Car_Get(g_all.car[i]);
        if (CI32(c, 0xc0c)) continue;
        Lamps_Setup(c);
        uint8_t cls = c->s[0xb1b];
        Lens_Setup(i, !(cls == 6 || cls == 7 || cls == 10));
    }
}

static void lens_reset(int i)   /* 0x477ac0 */
{
    memset(lens[i].level, 0, sizeof lens[i].level);
    memset(lens[i].fade, 0, sizeof lens[i].fade);
    memset(lens[i].last, 0xff, sizeof lens[i].last);
    lens[i].bits[0] = lens[i].bits[1] = 0;
}

void Lens_Setup(int i, bool head_on_brakes)
{
    if (i < 0 || i >= CAR_MAX) return;
    lens[i].head_on_brakes = head_on_brakes;
    lens[i].tex[0] = lens[i].tex[1] = NULL;
    lens_reset(i);
    /* the lens: the second model's type 0xe object, its first polygon's texture, blend type 2 (its +0x2c
       zeroed by 0x4b2e40); the interior model's (+0x724) the same, which the port does not load */
    Obj *o = Obj_FindType(Car_Get(i)->second, 0xe);
    if (!o || !o->mesh || !o->model) return;
    uint8_t *m = o->mesh;
    m[0x2c] = m[0x2d] = m[0x2e] = m[0x2f] = 0;
    const uint8_t *poly = C3d_Ref(o->model, m + 0x24, o->model->polys);
    int32_t t = poly ? ri32(poly + 4) : -1;
    lens[i].tex[0] = t >= 0 && t < TEX_MAX ? g_tex[t] : NULL;
    Tex_SetBlend(lens[i].tex[0], 2);
}

void Lens_Set(int i, bool left, bool right, uint8_t bit)
{
    if (i < 0 || i >= CAR_MAX) return;
    lens[i].bits[0] = left ? lens[i].bits[0] | bit : lens[i].bits[0] & (uint8_t)~bit;
    lens[i].bits[1] = right ? lens[i].bits[1] | bit : lens[i].bits[1] & (uint8_t)~bit;
}

/* a level as a 16.16 fraction: the value in the top bytes, the low byte from the division (0 here) */
static int32_t frac(int16_t v) { int32_t x = v; return (int32_t)((uint32_t)x * 0x10000u + (uint32_t)((x / 0x8001 + (x >> 31)) & 0xff)) >> 8; }

void Lens_Get(int i, int32_t *left, int32_t *right, int k)
{
    if (i < 0 || i >= CAR_MAX || k < 0 || k >= GROUPS) return;
    if (left) *left = frac(lens[i].level[0][k]);
    if (right) *right = frac(lens[i].level[1][k]);
}

/* the fades: up by 128 a frame at 25 frames a second, down by 80, within 0-255 */
static int16_t fade(int16_t v, bool on)
{
    int32_t x = on ? v + fx_mul(Frame_Scale(), 0x80) : v - fx_mul(Frame_Scale(), 0x50);
    return (int16_t)(on ? (x > 0xff ? 0xff : x) : (x < 0 ? 0 : x));
}

void Lens_Step(int i)
{
    if (i < 0 || i >= CAR_MAX) return;
    for (int k = 0; k < GROUPS; k++)
        for (int s = 0; s < 2; s++) {
            lens[i].fade[s][k] = fade(lens[i].fade[s][k], lens[i].bits[s] >> k & 1);
            lens[i].level[s][k] = lens[i].fade[s][k];
        }
    /* the side lamps: two thirds of them added to the brakes, or only the two thirds */
    for (int s = 0; s < 2; s++) {
        int16_t *l = lens[i].level[s];
        if (!lens[i].head_on_brakes) l[0] = (int16_t)(l[0] / 3 * 2);
        else {
            int32_t x = l[1] + l[0] / 3 * 2;
            l[1] = (int16_t)((x > 0xff ? 0xff : x) & 0xff);
        }
    }
    bool changed = false;
    int16_t m[GROUPS];
    for (int k = 0; k < GROUPS; k++) {
        int16_t a = lens[i].level[0][k], b = lens[i].level[1][k];
        if (b != lens[i].last[1][k] || a != lens[i].last[0][k]) changed = true;
        m[k] = a < b ? b : a;
    }
    if (!changed) return;
    /* the lens texture from its copy, then its alpha keys set to the levels (the brighter side's) */
    for (int t = 0; t < 2; t++) {
        Tex *x = lens[i].tex[t];
        if (!x) continue;
        Tex_RestoreCopy(x, i);
        Tex_KeyAlpha(x, 0xbf, (uint8_t)m[1], 0x40, (uint8_t)m[3], 0x80, (uint8_t)m[2], i);
        Tex_KeyAlpha(x, 0x60, (uint8_t)m[0], 0xe0, (uint8_t)m[4], 0xe0, (uint8_t)m[4], i);
    }
    memcpy(lens[i].last, lens[i].level, sizeof lens[i].last);
}

/* 0x4789d0: the shine of a surface (0x51e888, u16 per surface), towards its wet surface's (0x51e8e8)
   by the weather value */
static int32_t shine(int s, int32_t wet)
{
    uint32_t a = gm16(0x51e888 + 2 * (uint32_t)s), b = gm16(0x51e888 + 2 * (uint32_t)gm8(0x51e8e8 + (uint32_t)s));
    return fx_mul((int32_t)(b - a), wet) + (int32_t)a;
}

void Lamps_Step(Car *c)
{
    int n = car_of(c);
    const uint8_t *d = Damage_Get(n);
    int count;
    uint8_t *lamp = table(n, &count);
    if (CI32(c, 0xb70)) {
        /* off the stage: the brakes and reversing off, every lamp hidden */
        Lens_Set(n, false, false, 2);
        Lens_Set(n, false, false, 8);
        for (int i = 0; i < count; i++) Glow_Show(glow[n][i], false);
        return;
    }
    /* 0x466870: the lamps broken on each side (the record's +0x4a0, +0x4a4) */
    bool bl = DI32(d, 0x4a0) != 0, br = DI32(d, 0x4a4) != 0;
    bool brake = CI32(c, 0xb54) != 0, reverse = CI32(c, 0xb5c) != 0, head = CI32(c, 0xb58) != 0;
    Lens_Set(n, brake && !bl, brake && !br, 2);
    Lens_Set(n, reverse && !bl, reverse && !br, 8);
    Lens_Set(n, head && !bl, head && !br, 1);
    Lens_Set(n, head, head, 0x10);
    /* a broken engine (panel 23 over 0.3): the hazards on for the second half of each 20 frames */
    if (DI32(d, 0x29c) > 0x4ccc) {
        int32_t was = hazard[n];
        hazard[n] = was + Frame_Scale();
        if (hazard[n] > 0x140000) hazard[n] = 0;
        if (hazard[n] == 0) Lens_Set(n, false, false, 4);
        if (was < 0xa0000 && hazard[n] >= 0xa0000) Lens_Set(n, !bl, !br, 4);
    }
    /* the levels a lamp's kind picks: brakes, reversing, hazards, side lamps (left, right), the
       headlights (the right side's), and two never lit (kinds 9 and 10) */
    int32_t v[11] = {0};
    Lens_Get(n, &v[0], &v[1], 1);
    Lens_Get(n, &v[2], &v[3], 3);
    Lens_Get(n, &v[4], &v[5], 2);
    Lens_Get(n, &v[6], &v[7], 0);
    Lens_Get(n, &v[8], &v[8], 4);
    int32_t at[3] = {CI32(c, 0x270), CI32(c, 0x8dc), CI32(c, 0x278)};
    int32_t up[3] = {CI32(c, 0x48c), CI32(c, 0x490), CI32(c, 0x494)};
    for (int i = 0; i < count; i++) {
        uint8_t *r = lamp + REC * i;
        Glow *g = glow[n][i];
        if (!g) continue;
        int32_t on = r[0x20] < 11 ? v[r[0x20]] : 0;
        if (!on) Glow_Show(g, false);
        else {
            Glow_Show(g, true);
            Glow_Level(g, fx_mul(on, ri32(r + 0x1c)));
            /* a colour by the texture (brake red, reversing and head white, hazard amber) scaled by half
               the level goes to 0x4ae410, which does nothing */
        }
        /* the shaft on the ground under the wheel on the lamp's corner */
        int w = (g->pos[0] < 0 ? 2 : 0) + (g->pos[2] < 0 ? 1 : 0);
        int16_t surf = (int16_t)(c->s[0xaae + 2 * w] | c->s[0xaaf + 2 * w] << 8);
        Glow_Plane(g, at, up, shine(surf, Weather_CarWet(c)));   /* 0x460c80 */
        if (Damage_Dents(n)) {
            /* the lamp moved with its vertex (0x46b440) */
            int s = rd16(r + 0x24), x = rd16(r + 0x26);
            int32_t rest[3], now[3];
            if (Damage_RestVert(n, s, x, rest) && Damage_Vert(n, s, x, now)) {
                int32_t p[3] = {now[0] - rest[0] + ri32(r), now[1] - rest[1] + ri32(r + 4), now[2] - rest[2] + ri32(r + 8)};
                int32_t dir[3] = {ri32(r + 0xc), ri32(r + 0x10), ri32(r + 0x14)};
                Glow_Move(g, p, dir);
            }
        }
    }
}

void Lamps_Frame(const int16_t *cars, int n)
{
    for (int i = 0; i < n; i++) {
        Car *c = Car_Get(cars[i]);
        if (CI32(c, 0xc0c)) continue;
        if (!CI32(c, 0xb70)) Shadow_Build(c);   /* 0x496e00 */
        Lens_Step(i);   /* by the place in the list, as the original */
        Lamps_Step(c);
    }
    Weather_Step();   /* 0x45f4a0 */
}

/* 0x4648f0: the daylight from the high colour, (R + G + B - 70) / 550, 0 to 1 (a 16-bit difference) */
static int32_t daylight(void)
{
    uint8_t c[4];
    Light_GetHigh(c);
    int16_t d = (int16_t)(c[0] + c[1] + c[2] - 0x46);
    int32_t v = (int32_t)(((int64_t)d << 32) / 0x2260000);
    return v < 0 ? 0 : v > 0x10000 ? 0x10000 : v;
}

void Lamps_Lights(int car)
{
    Car *c = Car_Get(car);
    int32_t day = daylight();
    CW32(c, 0xb58, day != 0x10000);
    int32_t a = fx_mul(day, 0x4c0000);
    int32_t l;
    Lens_Get(car, &l, &l, 4);
    if (l == 0) a = 0x3e80000;
    g_lights_keep = l != 0;   /* 0x4b6ef0 */
    l = 0x10000 - l;
    l = l < 0 ? 0 : l > 0x10000 ? 0x10000 : l;
    a += fx_mul(l, 0x190000);
    if (a != gms32(0x547ff4)) {
        Dlight_SetAtten(head_light[0], a);
        Dlight_SetAtten(head_light[1], a);
        gw32(0x547ff4, (uint32_t)a);
    }
    if (!c->main) return;
    const int32_t *m = c->main->local.m;   /* +0x98 */
    for (int i = 0; i < 2; i++) {
        int32_t p[3];
        for (int k = 0; k < 3; k++)   /* 0x4b9c20, then the car's place */
            p[k] = fx_mul(m[8 + k], ahead[i][2]) + fx_mul(m[4 + k], ahead[i][1]) + fx_mul(m[k], ahead[i][0]) + m[12 + k];
        Obj_SetPosition(head_light[i], p);
    }
}
