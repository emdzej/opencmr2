/* The race's pause menu (see pause.h), ported from 0x401000-0x405500. */
#include "game/pause.h"
#include "audio/codriver.h"
#include "audio/crash.h"
#include "fixmath.h"
#include "audio/engine.h"
#include "audio/music.h"
#include "fe/menu.h"
#include "font.h"
#include "game/common.h"
#include "game/gstate.h"
#include "game/input.h"
#include "game/ctlcfg.h"
#include "game/mem.h"
#include "game/paths.h"
#include "game/race.h"
#include "exe.h"
#include "pending.h"
#include "platform.h"
#include "render/d3d7.h"
#include "render/tex.h"
#include "render/twod.h"
#include "sim/fade.h"
#include "text.h"
#include <stdio.h>
#include <string.h>

static Menu pg_main, pg_sound, pg_restart, pg_quit_net, pg_exit, pg_gfx, pg_gfx_pick, pg_ctrl, pg_cam, pg_osd;
static Menu *page, *next;          /* DAT_0052af44, DAT_0052af48 */
static uint8_t player, base;       /* DAT_0052af58: the pausing player, and its base (byte 1) */
static bool paused;                /* DAT_0052af40 */
static int prev_sel;               /* DAT_0052af6c */
static bool ask_restart, ask_quit, ask_service;   /* 0x53810c, 0x53810d, 0x537ffa */
static Tex *arrow_on, *arrow_off;  /* DAT_0052aa60, DAT_0052aa68 */
static int32_t keep_music, keep_effects, keep_speech;   /* 0x529ecc, 0x52a488, 0x52ad50 */

static void screen(int *w, int *h) { D3d_ScreenSize(w, h); }
static const uint8_t *C(uint32_t va) { return gm(va); }   /* the colours at 0x516074-0x516090 */
static uint32_t mode(void) { return Gs_Mode(); }

bool Pause_Active(void) { return paused; }
bool Pause_RestartAsked(void) { return ask_restart; }
bool Pause_QuitAsked(void) { return ask_quit; }
bool Pause_ServiceAsked(void) { return ask_service; }
void Pause_ClearAsks(void) { ask_restart = ask_quit = ask_service = false; }
void Pause_Ask(bool restart, bool service, bool quit)
{
    if (restart) ask_restart = true;
    if (service) ask_service = true;
    if (quit) ask_quit = true;
}
Tex *Pause_ArrowOn(void) { return arrow_on; }    /* 0x4055e0 */
Tex *Pause_ArrowOff(void) { return arrow_off; }  /* 0x4055f0 */

/* 0x404e10: the pause key: on the keyboard (or the mouse) the bit 0x400 (Escape); on a game
   controller its pause action's bits (kept in 0x52a0b8 to leave the pause with), or the first
   keyboard's Escape */
bool Pause_Pressed(int p)
{
    const InputRecord *r = Input_Record(Ctl_Device(p));
    if (r->device == DEV_KEYBOARD || r->device == DEV_MOUSE) return (r->pressed & 0x400) != 0;
    uint16_t m = Ctl_Mask(p, 9);
    if (r->pressed & m) {
        gw16(0x52a0b8, m);
        return true;
    }
    return (Input_Record(0)->pressed & 0x400) != 0;
}

/* 0x404ea0 */
void Pause_Begin(int p)
{
    player = (uint8_t)p;
    paused = true;
    gw8(0x52af40, 1);
    gw32(0x52ad54, 0);
    Input_SetMenuMode(true);
    gw8(0x52af41, 1);
    page = NULL;
    next = &pg_main;
    Menu_Start();
}

/* 0x404ef0 */
void Pause_End(void)
{
    Input_SetMenuMode(false);
    paused = false;
    gw8(0x52af40, 0);
}

/* 0x401b60: "player n" and "paused" at the top left with a separator */
static void header(void)
{
    int w, h;
    screen(&w, &h);
    char b[64];
    int x = w * 0x1f / 0x280, y = h * 0x45 / 0x1e0;
    snprintf(b, sizeof b, Text_Get(0x98), base + player + 1);
    Font_Print(2, b, x, y, C(0x516074), 0x11);
    int sx = h * 10 / 0x1e0 + x + Font_TextWidth(2, b);   /* (the gap taken from the height) */
    int16_t r[4] = {(int16_t)sx, (int16_t)(h * 0x25 / 0x1e0), 1, (int16_t)(h * 0x2d / 0x1e0)};
    Rect_Fill(r, C(0x516074), 2);
    r[0]++;
    Rect_Fill(r, C(0x516084), 2);
    Font_Print(2, Text_Get(0x18), sx + h * 10 / 0x1e0, y, C(0x516074), 0x11);
}

static void dim(void)
{
    int w, h;
    screen(&w, &h);
    int16_t r[4] = {0, 0, (int16_t)w, (int16_t)h};
    Rect_Fill(r, C(0x51608c), 2);
}

/* the rows of 0x401870 and 0x401d20: each item's name and its description (the next text), an arrow
   beside, 54 apart from 170 of 480; skip is the row left out (the main page's controller) */
static void rows(Menu *m, int skip)
{
    int w, h;
    screen(&w, &h);
    if (!arrow_on || !arrow_off) return;
    int y = h * 0xaa / 0x1e0;
    for (int i = 0; i < m->n; i++) {
        if (i == skip) continue;
        MenuItem *it = &m->items[i];
        const char *a = it->label, *b = it->label;
        if (!a) a = Text_Get(it->text), b = Text_Get(it->text + 1);
        bool on = m->sel == i;
        uint32_t c = on ? 0x516074 : 0x516078;
        Font_Print(1, a, w * 0x86 / 0x280, y, C(c), 0x11);
        Font_Print(0, b, w * 0x86 / 0x280, h * 0xf / 0x1e0 + y, C(c), 0x11);
        Tex *t = on ? arrow_on : arrow_off;
        Rect16 src = {0, 0, t->width, t->height};
        Rect16 d = {(int16_t)(w * 0x70 / 0x280), (int16_t)(y - h * 0xd / 0x1e0), arrow_on->width, arrow_on->height};
        Sprite_Add(&src, &d, t, 2, 0, NULL, NULL, C(c), 8);
        y += h * 0x36 / 0x1e0;
    }
}

/* 0x401870 */
static void draw_main(Menu *m)
{
    Font_SetLayer(2);
    dim();
    header();
    rows(m, 2);
    Font_SetLayer(2);
}

/* 0x401d20 */
static void draw_confirm(Menu *m)
{
    Font_SetLayer(2);
    dim();
    header();
    rows(m, -1);
    Font_SetLayer(2);
}

/* 0x4028d0: the volumes as "<name> <percent>" and the ok row */
static void draw_sound(Menu *m)
{
    int w, h;
    screen(&w, &h);
    Font_SetLayer(2);
    dim();
    header();
    if (!arrow_on || !arrow_off) return;
    for (int i = 0; i < m->n; i++) {
        MenuItem *it = &m->items[i];
        uint32_t c = m->sel == i ? 0x516074 : 0x516078;
        int y = h * 0xaa / 0x1e0 + (h * 0x36 / 0x1e0) * i;
        if (it->id >= 0 && it->id < 3) {
            char b[96];
            snprintf(b, sizeof b, exe_str(0x516098), Text_Get(it->text), it->value * 10);
            Font_Print(1, b, w * 0x86 / 0x280, y, C(c), 0x11);
        } else if (it->id == 4)
            Font_Print(1, Text_Get(it->text), w * 0x86 / 0x280, y, C(c), 0x11);
        Tex *t = m->sel == i ? arrow_on : arrow_off;
        Rect16 src = {0, 0, t->width, t->height};
        Rect16 d = {(int16_t)(w * 0x70 / 0x280), (int16_t)(y - h * 0xd / 0x1e0), arrow_on->width, arrow_on->height};
        Sprite_Add(&src, &d, t, 2, 0, NULL, NULL, C(c), 8);
    }
    Font_SetLayer(2);
}

/* 0x402bb0: the main page at "continue", or where it was */
static void main_enter(Menu *m, bool back)
{
    if (!back) m->sel = (int8_t)Menu_IndexOf(m, 7);
    prev_sel = m->sel;
}

/* 0x402bf0: the controller row is passed over (to sound from below, to restart from above) */
static void main_frame(Menu *m)
{
    if (m->sel == 2) m->sel = prev_sel > 1 ? 1 : 3;
    prev_sel = m->sel;
}

static void continue_fn(Menu *m, MenuItem *it)   /* 0x401850 */
{
    (void)m, (void)it;
    if (Gs_SelBit26()) PENDING_NOTE(0x4a3180, "the music's resume");
    Pause_End();
}

/* the fades' ends: every race process to the results (event 1), the flag */
static void restart_done(int ch)   /* 0x401540 */
{
    (void)ch;
    Race_EventAll(1, 3);
    ask_restart = true;
    Pause_End();
}
static void service_done(int ch)   /* 0x4014f0 */
{
    (void)ch;
    Race_EventAll(1, 3);
    ask_restart = true;
    ask_service = true;
    Pause_End();
}
static void quit_done(int ch)   /* 0x4016b0 */
{
    (void)ch;
    Race_EventAll(1, 3);
    if (mode() == 4) gw32(0x52f2b0, gm32(0x52f2b0) | 0x800000);   /* 0x407820 */
    ask_quit = true;
    Pause_End();
}

static int32_t fade_colour(void)
{
    uint32_t m = mode();
    return (int32_t)(m == 8 || m == 1 || m == 0 ? gm32(0x516090) : gm32(0x52af54));
}

/* 0x401590, 0x401630, 0x401780: each car's fade out and in, the first calling back */
static void restart_fn(Menu *m, MenuItem *it)
{
    (void)m, (void)it;
    for (int i = 0; i < Race_Cars(); i++) Fade_OutIn(i, i ? NULL : restart_done, 1, 0, fade_colour(), true);
}
static void service_fn(Menu *m, MenuItem *it)
{
    (void)m, (void)it;
    for (int i = 0; i < Race_Cars(); i++) Fade_OutIn(i, i ? NULL : service_done, 1, 0, (int32_t)gm32(0x516090), true);
}
static void quit_fn(Menu *m, MenuItem *it)
{
    (void)m, (void)it;
    for (int i = 0; i < Race_Cars(); i++) Fade_OutIn(i, i ? NULL : quit_done, 1, 0, (int32_t)gm32(0x516090), true);
}

static void last_enter(Menu *m, bool back) { (void)back; m->sel = (int8_t)(m->n - 1); }   /* 0x403360 */

/* the volumes (0x52afb8: music bits 0-6, effects 7-13, speech 14-20) */
static uint32_t vol(int shift) { return gm32(0x52afb8) >> shift & 0x7f; }
static void set_vol(int shift, uint32_t v)
{
    gw32(0x52afb8, (gm32(0x52afb8) & ~(0x7fu << shift)) | (v & 0x7f) << shift);
    if (shift == 0) Music_SetVolume((int)(v & 0x7f));   /* 0x405e10 */
}
static bool speech_on(void) { return !Gs_SelBit24(); }   /* 0x4174d0 */

static void apply(Menu *m)
{
    set_vol(0, m->items[Menu_IndexOf(m, 0)].value * 10u);
    set_vol(7, m->items[Menu_IndexOf(m, 1)].value * 10u);
    if (speech_on()) set_vol(14, m->items[Menu_IndexOf(m, 2)].value * 10u);
    int32_t v = (int32_t)(vol(7) << 16) / 100;
    Menu_SetVolume(v / 4);
}

/* 0x4012d0: the values from the options, kept for a cancel */
static void sound_enter(Menu *m, bool back)
{
    (void)back;
    keep_music = (int32_t)vol(0), keep_effects = (int32_t)vol(7), keep_speech = (int32_t)vol(14);
    m->items[Menu_IndexOf(m, 0)].value = (uint8_t)(keep_music / 10);
    m->items[Menu_IndexOf(m, 1)].value = (uint8_t)(keep_effects / 10);
    if (speech_on()) m->items[Menu_IndexOf(m, 2)].value = (uint8_t)(keep_speech / 10);
}
static void sound_frame(Menu *m) { apply(m); }   /* 0x401380 */
/* 0x401420: kept, or back to the old values; the race's volumes again (0x427c10) */
static void sound_leave(Menu *m, bool back)
{
    if (!back) apply(m);
    else {
        set_vol(0, (uint32_t)keep_music), set_vol(7, (uint32_t)keep_effects), set_vol(14, (uint32_t)keep_speech);
        int32_t v = (int32_t)(vol(7) << 16) / 100;
        Menu_SetVolume(v / 4);
    }
    Engine_Volume((int32_t)vol(7));
    Codriver_Volume((int32_t)vol(14));
    Crash_Volume(fx_mul((int32_t)vol(7) * 0x10000 / 100, 0xaaaa));   /* 0x418d20 */
}

static void stub_draw(Menu *m) { draw_confirm(m); }

/* 0x4054b0: paused by player p (0x52af58's byte 1) on the on-screen display page (0x52aa70) */
bool Pause_OsdPage(int p) { return Pause_Active() && base == (uint8_t)p && page == &pg_osd; }

void Pause_Build(void)
{
    paused = false;
    gw8(0x52af40, 0);
    player = base = 0;
    ask_restart = ask_quit = ask_service = false;
    /* 0x4054f0: the arrows from the race's Com archive */
    char p[260];
    Path_Format(p, sizeof p, 0x5160f4, Path_Root());
    arrow_on = Bfl_FindTexture(Bfl_Com(), p, NULL, false, 0);
    Path_Format(p, sizeof p, 0x5160d8, Path_Root());
    arrow_off = Bfl_FindTexture(Bfl_Com(), p, NULL, false, 0);
    /* 0x403370: restart: the stage (the text by the mode, below), at the service area, cancel */
    Menu_Init(&pg_restart, NULL, 0, &pg_main, false, NULL, 1, 0, 1);
    Menu_AddAction(&pg_restart, NULL, 0xf, restart_fn, 3);
    Menu_AddAction(&pg_restart, NULL, 0x8b, service_fn, 4);
    Menu_AddBack(&pg_restart, NULL, 0xa6, NULL, -1);
    Menu_SetCallbacks(&pg_restart, last_enter, NULL, draw_confirm, NULL);
    Menu_Reset(&pg_restart);
    /* 0x403420: quit to the waiting room (network) */
    Menu_Init(&pg_quit_net, NULL, 0, &pg_main, false, NULL, 1, 0, 1);
    Menu_AddAction(&pg_quit_net, NULL, 0xf6, NULL, 3);
    Menu_AddBack(&pg_quit_net, NULL, 0xa6, NULL, -1);
    Menu_SetCallbacks(&pg_quit_net, last_enter, NULL, draw_confirm, NULL);
    Menu_Reset(&pg_quit_net);
    /* 0x403490: exit */
    Menu_Init(&pg_exit, NULL, 0, &pg_main, false, NULL, 1, 0, 1);
    Menu_AddAction(&pg_exit, NULL, 0xa8, quit_fn, 3);
    Menu_AddBack(&pg_exit, NULL, 0xaa, NULL, -1);
    Menu_SetCallbacks(&pg_exit, last_enter, NULL, draw_confirm, NULL);
    Menu_Reset(&pg_exit);
    /* 0x402c90: the main page */
    Menu_Init(&pg_main, NULL, 0, NULL, false, NULL, 1, 0, 1);
    Menu_AddPage(&pg_main, NULL, 9, Gs_SelBits14() == 1 ? &pg_gfx : &pg_gfx_pick, NULL, 0);
    Menu_AddPage(&pg_main, NULL, 0xb, &pg_sound, NULL, 1);
    Menu_AddPage(&pg_main, NULL, 0xd, &pg_ctrl, NULL, 2);
    if (!Gs_ModeBit19()) Menu_AddPage(&pg_main, NULL, 0xf, &pg_restart, NULL, 3);
    else PENDING_NOTE(0x402c90, "the network game's pause pages");
    Menu_AddPage(&pg_main, NULL, 0x11, &pg_exit, NULL, 6);
    Menu_AddAction(&pg_main, NULL, 0x13, continue_fn, 7);
    Menu_SetCallbacks(&pg_main, main_enter, main_frame, draw_main, NULL);
    Menu_Reset(&pg_main);
    /* the restart's name by the mode: stage, rally, challenge, championship or race */
    int16_t t;
    switch (mode()) {
    case 0: case 1: case 8: t = 0x25; break;
    case 2: case 3: case 9: case 10: t = 0x23; break;
    case 4: t = 0x27; break;
    case 5: t = 0x2b; break;
    default: t = 0x29; break;
    }
    pg_main.items[Menu_IndexOf(&pg_main, 3)].text = t;
    pg_restart.items[Menu_IndexOf(&pg_restart, 3)].text = t;
    /* 0x4032a0: the volumes (0 to 100 in tens) */
    Menu_Init(&pg_sound, NULL, 0, &pg_main, false, NULL, 1, 0, 1);
    Menu_AddList(&pg_sound, NULL, 0x19, 0xb, 10, false, NULL, 0);
    Menu_AddList(&pg_sound, NULL, 0x1a, 0xb, 10, false, NULL, 1);
    if (speech_on()) Menu_AddList(&pg_sound, NULL, 0x1b, 0xb, 10, false, NULL, 2);
    Menu_AddPage(&pg_sound, NULL, 0x3b, &pg_main, NULL, 4);
    Menu_SetCallbacks(&pg_sound, sound_enter, sound_frame, draw_sound, sound_leave);
    Menu_Reset(&pg_sound);
    /* the graphics, camera, display and controller pages (0x402f90, 0x403550, 0x4035e0, 0x404000,
       0x403090): their settings are not ported; they show their rows and go back */
    PENDING_NOTE(0x402f90, "the pause menu's graphics, camera and controller pages");
    Menu_Init(&pg_gfx, NULL, 0, &pg_main, false, NULL, 1, 0, 1);
    Menu_AddPage(&pg_gfx, NULL, 0x33, &pg_osd, NULL, 0);
    Menu_AddPage(&pg_gfx, NULL, 0x5c, &pg_cam, NULL, 1);
    Menu_AddPage(&pg_gfx, NULL, 0x3b, &pg_main, NULL, -1);
    Menu_SetCallbacks(&pg_gfx, NULL, NULL, stub_draw, NULL);
    Menu_Reset(&pg_gfx);
    Menu_Init(&pg_gfx_pick, NULL, 0, &pg_main, false, NULL, 1, 0, 1);
    Menu_AddPage(&pg_gfx_pick, NULL, 0x58, &pg_gfx, NULL, 0);
    Menu_AddPage(&pg_gfx_pick, NULL, 0x5a, &pg_gfx, NULL, 1);
    Menu_SetCallbacks(&pg_gfx_pick, NULL, NULL, draw_main, NULL);
    Menu_Reset(&pg_gfx_pick);
    Menu_Init(&pg_ctrl, NULL, 0, &pg_main, false, NULL, 1, 0, 1);
    Menu_AddPage(&pg_ctrl, NULL, 0x3b, &pg_main, NULL, -1);
    Menu_SetCallbacks(&pg_ctrl, NULL, NULL, stub_draw, NULL);
    Menu_Reset(&pg_ctrl);
    Menu_Init(&pg_cam, NULL, 0, &pg_gfx, false, NULL, 1, 0, 1);
    Menu_AddPage(&pg_cam, NULL, 0x3b, &pg_gfx, NULL, 5);
    Menu_SetCallbacks(&pg_cam, NULL, NULL, stub_draw, NULL);
    Menu_Reset(&pg_cam);
    Menu_Init(&pg_osd, NULL, 0, &pg_gfx, false, NULL, 1, 0, 1);
    Menu_AddPage(&pg_osd, NULL, 0x3b, &pg_gfx, NULL, 6);
    Menu_SetCallbacks(&pg_osd, NULL, NULL, stub_draw, NULL);
    Menu_Reset(&pg_osd);
}

/* 0x404f40's menu part (its camera page's live view not ported): the input on the page. A pause key
   bound on a game controller (its bits kept in 0x52a0b8) leaves it; the keyboard's Escape is the
   menus' back key, so "continue" is the way out of the main page. */
void Pause_Frame(void)
{
    page = next;
    Input_PollAll();   /* 0x49eab0 */
    Ctl_AxisPresses();   /* 0x40bad0 */
    InputRecord *r = Input_Record(player);
    Ctl_Merge(player, r);
    if (!(r->pressed & gm16(0x52a0b8))) {
        if (page) Menu_SetFlags(page, true, true, true, true);
        Menu *to = Menu_Input(page, r->pressed);
        if (to) next = to;
    } else {
        gw32(0x52a0b8, gm32(0x52a0b8) & 0xffff0000u);
        if (page) Menu_Leave(page);
        Pause_End();
    }
    /* the fades run on while paused (a restart's or an exit's) */
    for (int i = 0; i < Race_Cars(); i++) {
        Fade_Tick(i, true);   /* 0x4284d0 */
        Fade_Level(i, Race_Frac(), true);   /* 0x4285b0 with 0x41f270 */
    }
}

void Pause_Draw(void) { Menu_Draw(page); }
