/* The stage set-up and service screens (see service.h): the process and its states (0x500c80-0x501920),
   the scene (0x5013a0), the archives, text, fonts and sounds (0x50f240-0x50f4f0), the pages
   (0x502240-0x5024a0, 0x50f180) and the main page's items (0x4ff4e0-0x500210). */
#include "game/service.h"
#include "game/service_int.h"
#include "game/saves.h"
#include "audio/music.h"
#include "audio/snd.h"
#include "bfl.h"
#include "exe.h"
#include "fe/menu.h"
#include "font.h"
#include "game/gstate.h"
#include "game/input.h"
#include "game/mem.h"
#include "game/paths.h"
#include "game/proc.h"
#include "game/timer.h"
#include "game/weather.h"
#include "pending.h"
#include "render/d3d7.h"
#include "render/dlight.h"
#include "render/rstate.h"
#include "render/sprite3d.h"
#include "render/view.h"
#include "text.h"
#include "world/frame.h"
#include "world/obj.h"
#include "world/relight.h"
#include <stdio.h>
#include <string.h>

void Fe_CarSetups(void);   /* 0x406820 (fe) */

SvState S;

static ProcGroup group;   /* DAT_0082a7f0 */
static ProcSlot slot;     /* DAT_0082a800 */
static bool started;      /* DAT_0082a908 */

Menu sv_main, sv_setup, sv_info, sv_msg, sv_save;   /* 0x82b668, 0x82b848, 0x82bc08, 0x82ba28, 0x82b488 */
static Menu *cur, *next;  /* DAT_00831778, DAT_0083177c */
static BflSlot common, day, text_arc;   /* 0x831a90, 0x831aa0, 0x831ab0 */
static uint8_t *text_file;   /* DAT_00831880 */

BflSlot *Sv_Common(void) { return &common; }   /* 0x50f620 */
BflSlot *Sv_Day(void) { return &day; }         /* 0x50f640 */
ProcGroup *Sv_Group(void) { return &group; }   /* 0x4ff440 */

static void screen(int *w, int *h) { D3d_ScreenSize(w, h); }
static uint32_t rally(void) { return gm32(0x52f2ac) & 0x1f; }        /* Tod_Rally 0x406910 */
static uint32_t stage(void) { return gm32(0x52f2ac) >> 5 & 0x1f; }   /* Tod_Stage 0x406930 */
static bool networked(void) { return gm32(0x52afb4) >> 0x13 & 1; }   /* 0x405e00 */

/* 0x5011b0: the player being set up: the players less those still to go */
int Sv_Player(void) { return (int)Gs_Players() - S.to_go; }

/* ---- the tweens (0x82b2c0, five words each: the value, the length, the start, the state, the curve) ---- */

/* 0x501cc0: tween i from now over len hundredths, curve 0 straight, 1 squared, 2 square root */
void Sv_TweenStart(int i, int len, int curve)
{
    S.tw[i].start = Timer_Centis();
    S.tw[i].len = len;
    S.tw[i].v = 0;
    S.tw[i].state = 1;
    S.tw[i].curve = curve;
}

void Sv_TweenClear(int i) { memset(&S.tw[i], 0, sizeof S.tw[i]); }   /* 0x501d00 (the curve kept) */

/* 0x501d20 */
static void tweens_init(int n)
{
    S.ntw = n;
    for (int i = 0; i < n; i++) {
        int c = S.tw[i].curve;
        Sv_TweenClear(i);
        S.tw[i].curve = c;
    }
}

/* 0x5021e0: every tween at its end */
static void tweens_finish(void)
{
    for (int i = 0; i < S.ntw; i++) S.tw[i].v = 0x10000, S.tw[i].state = 2;
}

/* 0x501ac0: the pulsing grey (0x82b1b8): a second's sine from 128 to 255 */
static void pulse(void)
{
    uint32_t t = Timer_Centis();
    int64_t f = (int64_t)(int32_t)(((uint64_t)(t % 0x3c) << 32) / 0x3c0000) * 0x1680000;
    uint32_t deg = (uint32_t)(f >> 16);
    double k;
    memcpy(&k, exe_data(0x511300, 8), 8);
    uint16_t a = (uint16_t)(int64_t)__builtin_nearbyint((double)deg * k);
    int64_t s = (int64_t)(g_sin[a & 0xfff] + 0x10000) * 0x8000;
    int32_t v = (int32_t)(s >> 16);
    if (v < 0) v = 0;
    else if (v > 0x10000) v = 0x10000;
    uint8_t g = (uint8_t)(((int32_t)(((int64_t)v * 0x7f0000) >> 16) + 0x800000) >> 16);
    S.pulse[0] = S.pulse[1] = S.pulse[2] = g, S.pulse[3] = 0xff;
}

/* 0x501b90: the running tweens on; the pulse */
static void tweens_step(void)
{
    for (int i = 0; i < S.ntw; i++) {
        SvTween *t = &S.tw[i];
        if (t->state != 1) continue;
        int32_t e = (int32_t)(Timer_Centis() - t->start);
        if (e >= t->len) {
            t->state = 2;
            t->v = 0x10000;
            continue;
        }
        uint32_t v = (uint32_t)(((int64_t)(int32_t)(e << 16) << 16) / ((int64_t)t->len << 16));
        t->v = (int32_t)v;
        if (t->curve == 1) t->v = (int32_t)(((int64_t)(int32_t)v * (int32_t)v) >> 16);
        else if (t->curve == 2) t->v = (int32_t)Fx_Sqrt(v);
        else continue;
    }
    pulse();
}

int Sv_TweenState(int i) { return S.tw[i].state; }   /* 0x5021c0 */

/* 0x500ec0: the main page's pieces coming in, in turn */
static void tweens_queue(void)
{
    if (!Sv_TweenState(2)) Sv_TweenStart(2, 0x28, 0);
    if (!Sv_TweenState(6)) Sv_TweenStart(6, 0xf, 0);
    if (!Sv_TweenState(7)) Sv_TweenStart(7, 0xf, 0);
    if (!Sv_TweenState(4)) Sv_TweenStart(4, 0x1e, 0);
    if (!Sv_TweenState(5)) Sv_TweenStart(5, 0x1e, 1);
    if (!Sv_TweenState(0)) {
        Sv_TweenStart(0, 0x14, 1);
        return;
    }
    if (Sv_TweenState(0) == 2) {
        if (!Sv_TweenState(1)) Sv_TweenStart(1, 0x14, 0);
        if (!Sv_TweenState(3)) Sv_TweenStart(3, 0x14, 0);
    }
}

/* ---- the scene (0x5013a0): a world node, the camera and a directional light ---- */

static Obj *world, *camera;   /* DAT_0082b1b4, DAT_0082b1b0 */

Obj *Sv_World(void) { return world; }
Obj *Sv_Camera(void) { return camera; }

static void scene_view(int16_t v[4])
{
    int w, h;
    screen(&w, &h);
    v[0] = 0, v[1] = 0, v[2] = (int16_t)(w * 2 / 3), v[3] = (int16_t)(h * 2 / 3);
}

static void make_scene(void)
{
    static const int32_t cam_pos[3] = {0x23d7, -0x170a, -0x50000}, light_pos[3] = {0x50000, 0x50000, -0x50000};
    static const uint16_t zero[3] = {0, 0, 0};
    world = Obj_New(NULL);   /* 0x4ac760: a node whose children keep their matrices */
    world->type = (world->type & ~0xffu) | 0xfd;
    camera = Obj_New(world);   /* 0x4adfa0 */
    camera->kind = 2;
    Obj_Place(camera, cam_pos, zero);
    for (Obj *o = world; o; o = o->parent) o->dirty = 1;
    static const uint8_t low[4] = {200, 200, 200, 0xff};
    Light_SetLow(low, false);
    Dlight_Add(2, 0x10000, 0x10000, 0x10000, light_pos, zero, world);
    Proj_Set(0x30978, 0x4326e, 0xfa0000, 0x10000);
    Obj_Frame(world, camera);
}

/* the three draws of the scene: 0x501780 (states 1: the clear colour from the stage's 0x52704c to the
   service's grey over half a second), 0x5015d0 (3) and 0x501920 (4, back) */
static void draw_scene(void)
{
    int16_t v[4];
    scene_view(v);
    Render_ClearTarget();
    Render_ClearZ(false);
    Proj_Set(0x30978, 0x4326e, 0xfa0000, 0x10000);
    Obj_Frame(world, camera);
    World_Render(camera, v, 0, true);
}

static void fade_clear(bool in)
{
    uint32_t k = (Timer_Centis() * 1000 - S.fade_at * 1000) / 0x32, c = gm32(0x52704c);
    int r, g, b;
    if (in) {
        int a = 1000 - (int)k;
        r = ((int)(c & 0xff) * a + (int)k * 0x8d) / 1000;
        g = ((int)(c >> 8 & 0xff) * a + (int)k * 0x97) / 1000;
        b = ((int)(c >> 16 & 0xff) * a + (int)k * 0x9f) / 1000;
    } else {
        r = (((int)(c & 0xff) - 0x8d) * (int)k + 0x226c8) / 1000;
        g = (((int)(c >> 8 & 0xff) - 0x97) * (int)k + 0x24dd8) / 1000;
        b = (((int)(c >> 16 & 0xff) - 0x9f) * (int)k + 0x26d18) / 1000;
    }
    Render_SetClearColour((uint8_t)r, (uint8_t)g, (uint8_t)b);
}

/* ---- the archives (0x50f4f0), text (0x50f240), fonts (0x50f370), sounds (0x50f3c0, 0x50f420) ---- */

static void mount(void)
{
    char p[260];
    uint32_t res = 0x280;   /* 1,024 with the texture memory: the port's device stays at 640 */
    snprintf(p, sizeof p, exe_str(0x529794), Path_SetupRep(), res);
    Bfl_Mount(&common, p);
    uint32_t day_n = stage() >> 2;
    /* 0x4a9700, compressed textures: not the port's; the plain archive */
    snprintf(p, sizeof p, exe_str(0x52976c), Path_SetupRep(), res, exe_str(exe_u32(0x5296f8 + 4 * rally())), day_n + 1);
    Bfl_Mount(&day, p);
    uint32_t lang = gm32(0x52ea54), slotn = gm32(0x52afb4) & 7;
    snprintf(p, sizeof p, exe_str(0x51a100), Path_Country(), exe_str(exe_u32(0x516124 + 4 * lang)),
             exe_str(exe_u32(0x52971c + 4 * (lang * 5 + slotn))));
    Bfl_Mount(&text_arc, p);
}

static bool load_text(void)
{
    static const uint32_t europe[5] = {0x529664, 0x52965c, 0x529650, 0x529644, 0x52963c};
    static const uint32_t america[3] = {0x529634, 0x52965c, 0x529650};
    static const uint32_t other[2] = {0x529664, 0x52962c};
    const uint32_t *names = NULL;
    switch (gm32(0x52ea54)) {
    case 0: names = europe; break;
    case 1: names = america; break;
    case 2: names = other; break;
    case 3: names = other + 1; break;
    }
    if (!names) return false;
    char p[64];
    snprintf(p, sizeof p, exe_str(0x51e948), exe_str(names[gm32(0x52afb4) & 7]));
    uint32_t sz = 0;
    const uint8_t *f = Bfl_FindFile(&text_arc, p, NULL, &sz, false);
    if (!f) return false;
    size_t n = sz;
    Text_Build(1, 0x146, &f, &n);
    text_file = NULL;
    return true;
}

static void load_fonts(void)
{
    static const uint32_t names[4] = {0x529670, 0x529684, 0x529698, 0x5296ac};
    Font_InitSlots(4);
    for (int i = 0; i < 4; i++) {
        const char *n = exe_str(names[i]), *b = strrchr(n, '\\');
        char dir[32];
        size_t l = b ? (size_t)(b - n) : 0;
        if (l >= sizeof dir) l = sizeof dir - 1;
        memcpy(dir, n, l);
        dir[l] = 0;
        Font_LoadSlot(dir, b ? b + 1 : n, &common, i);
    }
}

static int snd_base;   /* DAT_00831888 */

static bool load_sounds(void)
{
    bool ok = true;
    snd_base = 0;
    for (uint32_t a = 0x5296c4; a < 0x5296e7; a += 7) {
        char p[260];
        snprintf(p, sizeof p, exe_str(0x5296e8), Path_Sounds(), exe_str(a));
        if (!Snd_LoadNamed(p[0] == '\\' ? p + 1 : p, &common)) ok = false;
    }
    return ok;
}

static void menu_sounds(void)
{
    S.sound_at = (uint32_t)-1;   /* 0x83188c */
    int v = (int)(gm32(0x52afb8) >> 7 & 0x7f);   /* 0x405e70 */
    v = (v << 16) / 100;
    Menu_SetVolume((v + (v >> 31 & 3)) >> 2);
    Menu_SetSounds(snd_base, snd_base + 1, snd_base + 2, snd_base + 3, snd_base + 4);
    Menu_SoundsOn(true);
}

/* ---- the pages (0x50f180) ---- */

static void build_pages(void)
{
    Menu_Start();
    /* 0x502310: the main page */
    Menu_Init(&sv_main, NULL, -1, &sv_main, false, NULL, true, 0, 0);
    Menu_AddList(&sv_main, NULL, 0x48, (uint8_t)Sv_InfoPages(), 0, false, sv_info_go, 0);
    Menu_AddList(&sv_main, NULL, 0x47, 7, 0, true, sv_setup_go, 1);
    Menu_AddList(&sv_main, NULL, 0x46, 0xb, 0, true, sv_repair_go, 2);
    Menu_AddList(&sv_main, NULL, 0x3c, 1, 0, false, sv_race_go, 3);
    if (Gs_Mode() == 0) Menu_AddList(&sv_main, NULL, 0xd3, 2, 0, false, sv_save_go, 4);
    Menu_AddList(&sv_main, NULL, 0x88, 2, 1, true, sv_quit_go, 5);
    Menu_SetCallbacks(&sv_main, sv_main_enter, sv_main_frame, sv_main_draw, sv_main_leave);
    Menu_Reset(&sv_main);
    Sv_MainReset();
    Sv_Textures();
    Sv_CarsInit();
    Menu_SetFlags(&sv_main, false, false, false, false);
    /* 0x5024a0: a set-up value */
    Menu_Init(&sv_setup, NULL, -1, &sv_main, false, NULL, true, 0, 1);
    Menu_AddList(&sv_setup, NULL, -1, 0x65, 0, false, sv_setup_choose, 0);
    Menu_SetCallbacks(&sv_setup, NULL, NULL, sv_setup_draw, NULL);
    Menu_Reset(&sv_setup);
    /* 0x502440: the information pages */
    Menu_Init(&sv_info, NULL, -1, &sv_main, false, NULL, true, 0, 0);
    Menu_SetCallbacks(&sv_info, NULL, NULL, sv_info_draw, sv_main_leave);
    Menu_Reset(&sv_info);
    Menu_SetFlags(&sv_info, false, false, false, true);
    /* 0x502240: a message */
    Menu_Init(&sv_msg, NULL, -1, &sv_main, false, NULL, true, 0, 0);
    Menu_AddBack(&sv_msg, NULL, -1, NULL, -1);
    Menu_SetCallbacks(&sv_msg, sv_msg_enter, NULL, sv_msg_draw, NULL);
    Menu_Reset(&sv_msg);
    /* 0x5022a0: save the championship? */
    Menu_Init(&sv_save, NULL, -1, &sv_main, false, NULL, false, 0, 1);
    Menu_AddBack(&sv_save, NULL, 0x100, sv_save_yes, -1);
    Menu_AddBack(&sv_save, NULL, 0x101, NULL, -1);
    Menu_SetCallbacks(&sv_save, sv_save_enter, NULL, sv_save_draw, NULL);
    Menu_Reset(&sv_save);
    cur = NULL;
    next = &sv_main;
}

void Sv_Goto(Menu *m) { Menu_Goto(m); }

/* 0x50f1d0: the page's frame with the input (the second player's own in split screen, not alternate,
   0x405da0) */
static void menu_frame(void)
{
    cur = next;   /* 0x50f1c0 */
    Input_PollAll();   /* 0x4a2fe0 (the music's keep-alive) and 0x49eab0 */
    int rec = Sv_Player() == 1 && !Gs_ModeBit17() ? 1 : 0;   /* split screen: the second pad */
    PENDING_NOTE(0x40bd60, "the other players' keys on the shared pad (0x40bad0, 0x40bd60)");
    Menu *m = Menu_Input(cur, Input_Record(rec)->pressed);
    if (m) next = m;
}

/* ---- the states ---- */

/* 0x500c00: the weather and the set-ups again when the rally is not the one they were made for */
static void weather_check(void)
{
    uint32_t r = rally();
    if ((uint16_t)r == (uint16_t)(int16_t)(int8_t)gm8(0x52f2a8)) return;
    uint32_t m = Gs_Mode();
    if (m != 2 && (m < 8 || m > 12)) Weather_Choose();
    Fe_CarSetups();
    for (uint32_t a = 0x52ebc0; a < 0x52ebc0 + 0x148 * 4; a += 4) gw32(a, 0);   /* 0x408290 */
    gw8(0x52f2a8, (uint8_t)r);
}

/* 0x502d50: each player's set-up (0x52ebc0, 0x148 bytes) kept at 0x82c070, the repairs at 0x82c040
   cleared; 0x502570: the four tyre choices (0x52f0e0, 7 bytes) kept twice (0x82bf04, 0x82bee8) */
static void keep_setups(void)
{
    for (int p = 0; p < (int)Gs_Players(); p++) {
        memcpy(gm(0x82c070 + 0x148u * (uint32_t)p), gm(0x52ebc0 + 0x148u * (uint32_t)p), 0x148);
        for (uint32_t k = 0; k < 12; k++) gw8(0x82c040 + 12u * (uint32_t)p + k, 0);
    }
    for (uint32_t i = 0; i < 4; i++) {
        memcpy(gm(0x82bf04 + 7 * i), gm(0x52f0e0 + 7 * i), 7);
        memcpy(gm(0x82bee8 + 7 * i), gm(0x82bf04 + 7 * i), 7);
        gw32(0x82bf20 + 7 * i, 0), gw16(0x82bf24 + 7 * i, 0), gw8(0x82bf26 + 7 * i, 0);
    }
}

/* 0x502db0, 0x5029b0: the set-ups and tyres back */
static void restore_setups(void)
{
    for (int p = 0; p < (int)Gs_Players(); p++)
        memcpy(gm(0x52ebc0 + 0x148u * (uint32_t)p), gm(0x82c070 + 0x148u * (uint32_t)p), 0x148);
    for (uint32_t i = 0; i < 4; i++) memcpy(gm(0x52f0e0 + 7 * i), gm(0x82bee8 + 7 * i), 7);
}

static void s0_init(ProcGroup *g, uint32_t s)
{
    memset(&S, 0, sizeof S);
    S.race_to_fe = false;   /* 0x82b0ac */
    Input_SetMenuMode(true);
    if (networked()) PENDING_NOTE(0x40ac40, "the network's service message");
    Menu_SoundsOn(false);
    Menu_SetSounds(-1, -1, -1, -1, -1);
    S.to_go = (int)Gs_Players();   /* 0x82b0a8 */
    weather_check();
    keep_setups();
    /* 0x501520: the clear colour, the font layer, the scene, the archives, the 3D sprites, the cull, the
       weather and map textures (0x503ea0), the stage panel (0x5040f0), the tyre counts (0x505e70), the
       environment map */
    Render_SetClearColour(0x8d, 0x97, 0x9f);
    Font_SetLayer(1);
    make_scene();
    mount();
    Sprite3d_Init();
    Render_SetDefaultCull(3);
    Sv_LoadTextures();
    Sv_StageInit();
    Sv_ForecastInit();
    PENDING_NOTE(0x4b2970, "the service area's environment map (option 15, 16)");
    Champs_Scan();   /* 0x4f4ef0 */
    load_sounds();
    menu_sounds();
    load_fonts();
    load_text();
    build_pages();
    uint32_t m = Gs_Mode();
    S.first_of_day = !((m == 0 || m == 1) && (stage() & 3));   /* DAT_00527000 */
    for (int p = 0; p < (int)Gs_Players(); p++) S.time[p] = 360000, S.time_lim[p] = 0x3c;   /* 0x82af90, 0x82af78 */
    char p[260];
    snprintf(p, sizeof p, exe_str(0x52703c), Path_Music());
    Music_Open(p[0] == '\\' ? p + 1 : p);
    Music_SetVolume((int)(gm32(0x52afb8) & 0x7f));
    Music_Update();
    Proc_Event(g, s, 0, 2);
    S.fresh = true;   /* 0x82af8c */
}

/* 0x5012e0: the clock sampled the first time; every player's car hidden; after half a second on */
static void s1_wait(ProcGroup *g, uint32_t s)
{
    (void)s;
    if (S.fresh) {
        S.fade_at = Timer_Centis();
        S.fresh = false;
    }
    for (int p = 0; p < (int)Gs_Players(); p++) Sv_CarShow(p, false);
    if (Timer_Centis() - S.fade_at > 0x32) Proc_Event(g, 0, 0, 2);
}

static void d1_wait(ProcGroup *g, uint32_t s)
{
    (void)g, (void)s;
    fade_clear(true);
    draw_scene();
}

/* 0x500df0: the player's turn: the main page's own car shown (its set-up view and its tyres), the
   others hidden */
static void s2_player(ProcGroup *g, uint32_t s)
{
    Sv_MainTurn();
    for (int p = 0; p < (int)Gs_Players(); p++) {
        if (p == Sv_Player()) {
            Sv_CarShow(p, true);
            Sv_CarView(p, Sv_SetupView(0), false);
            Sv_CarLines(p, Sv_RepairView(0), true);
        } else
            Sv_CarShow(p, false);
    }
    Proc_Event(g, s, 0, 2);
    S.turn_at = Timer_Centis();
}

/* 0x500f80: the loop. With the skip option straight on (after a networked game's minute); otherwise
   the tweens, the page and the car */
static void s3_service(ProcGroup *g, uint32_t s)
{
    bool skip = gm8(0x817574) != 0;   /* Key_Skip 0x4d0580 */
    if (!skip && Timer_Centis() - S.turn_at > 0x17d4 && networked() && Gs_Mode() != 10) skip = true;
    if (!skip) {
        tweens_queue();
        tweens_step();
        menu_frame();
        Sv_CarTurn();
    } else {
        Proc_Event(g, s, 0, 2);
        S.fade_at = Timer_Centis();
    }
    if (networked()) PENDING_NOTE(0x500ba0, "the network's service messages");
}

static void d3_service(ProcGroup *g, uint32_t s)
{
    (void)g, (void)s;
    draw_scene();
    Menu_Draw(cur);   /* 0x50f230 */
}

/* 0x501350: half a second, then the race (or the front end after a quit) */
static void s4_leave(ProcGroup *g, uint32_t s)
{
    (void)s;
    if (Timer_Centis() - S.fade_at <= 0x32) return;
    Proc_Event(g, 0, S.quit ? 2 : 0, 2);
}

static void d4_leave(ProcGroup *g, uint32_t s)
{
    (void)g, (void)s;
    fade_clear(false);
    draw_scene();
}

static void teardown(void)
{
    Music_Stop(false);   /* 0x4a2b50 */
    Render_SetClearColour(0x9c, 0xb4, 0xac);
    Render_ClearTarget();
    Render_ClearZ(false);
    Sv_Free();
    Obj_FreeAll();
    Text_Free();
    Bfl_Unmount(&common), Bfl_Unmount(&day), Bfl_Unmount(&text_arc);
    Voices_StopAll();
    Snd_Truncate(0);
}

/* 0x5010a0: to the race (or the front end), the set-ups back */
static void s5_race(ProcGroup *g, uint32_t s)
{
    teardown();
    restore_setups();
    Proc_Event(g, s, 0, 2);
    gw32(0x52ea4c, S.race_to_fe ? 0 : 3);
    Input_SetMenuMode(false);
    started = false;
}

/* 0x501130: to the front end */
static void s6_quit(ProcGroup *g, uint32_t s)
{
    teardown();
    Proc_Event(g, s, 0, 2);
    gw32(0x52ea4c, 0);
    Input_SetMenuMode(false);
    started = false;
}

static const ProcState states[7] = {
    {s0_init, NULL}, {s1_wait, d1_wait}, {s2_player, NULL}, {s3_service, d3_service},
    {s4_leave, d4_leave}, {s5_race, NULL}, {s6_quit, NULL},
};

int World_Frame(void)
{
    if (started) {
        Proc_UpdateAll(&group);
        Proc_DrawAll(&group);
        Proc_Advance(&group);
        return 0;
    }
    Proc_InitSlot(&slot, 0, 0xff);
    Proc_Init(&group, 1, &slot, states, exe_data(0x526f18, 16 * 4));
    started = true;
    return 0;
}

/* ---- the main page's items ---- */

/* 0x500130: Race: the next player's turn (event 1), or with every player done the race (event 0) */
void Sv_RaceNow(void)
{
    if (S.to_go - 1 != 0) {
        Proc_Event(&group, 0, 1, 2);
        S.to_go--;
        return;
    }
    Proc_Event(&group, 0, 0, 2);
    S.quit = false;
    S.fade_at = Timer_Centis();
    S.to_go = 1;
}

/* 0x5001c0: Quit: the leaderboards' entries cleared (0x4cf260), to the front end */
void Sv_QuitNow(bool keep_net)
{
    PENDING_NOTE(0x4cf260, "the stage's leaderboard entries on a quit");
    S.quit = true;
    S.fade_at = Timer_Centis();
    Proc_Event(&group, 0, 0, 2);
    if (networked() && !keep_net) PENDING_NOTE(0x4a1a90, "the network game left on a quit");
}

/* 0x500110: Race chosen: the tweens done, the race two frames on (0x4ffab0) */
void sv_race_go(Menu *m, MenuItem *it)
{
    (void)m, (void)it;
    tweens_finish();
    S.race_in = 1;
}

/* 0x500190: Quit chosen with Yes (value 0) */
void sv_quit_go(Menu *m, MenuItem *it)
{
    (void)it;
    if (m->items[Menu_IndexOf(m, 5)].value == 0) {
        tweens_finish();
        S.quit_in = 1;
    }
}

/* 0x500210: leaving the main page back: the player before (event 1), not past the first */
void sv_main_leave(Menu *m, bool back)
{
    (void)m;
    if (!back) return;
    int n = S.to_go;
    if ((int)Gs_Players() < n + 1) {
        S.to_go = (int)Gs_Players();
        return;
    }
    Proc_Event(&group, 0, 1, 2);
    S.to_go = n + 1;
}

/* 0x4ff4e0: the main page's state: the panels' selections, the tweens */
void Sv_MainReset(void)
{
    S.setup_last = 0, S.repair_last = 0, S.info_last = 0;   /* 0x526f48-0x526f50 */
    S.shown_time = 0;   /* 0x82a92c */
    S.blink = 0, S.blink_at = 0;   /* 0x82ac58, 0x82ac5c */
    S.panel[0] = 0;
    S.main_sel = 2;    /* 0x526f44 */
    for (int i = 1; i < 6; i++) S.panel[i] = 3;   /* 0x82a910-0x82a920 */
    tweens_init(8);
}

void Sv_TweensFinish(void) { tweens_finish(); }
Menu *Sv_Current(void) { return cur; }
