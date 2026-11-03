/* The race's pause menu (see pause.h), ported from 0x401000-0x405500. */
#include "net/session.h"
#include "net/records.h"
#include "net/race_msg.h"
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
#include "game/challenge.h"
#include "game/input.h"
#include "game/ctlcfg.h"
#include "game/mem.h"
#include "game/paths.h"
#include "game/race.h"
#include "game/camera.h"
#include "game/dash.h"
#include "game/hud.h"
#include "game/profile.h"
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
static Tex *roc;                   /* DAT_0052af4c: RoCx_640, the challenge's */
static Tex *crt;                   /* DAT_0052af50: crtna640, on the results' times page */
static int32_t keep_music, keep_effects, keep_speech;
static int32_t cam_off[3], cam_dist, keep_off[3], keep_dist;   /* the camera page's (see camera_live) */
static int16_t cam_ang, keep_ang;
static bool cam_live;   /* 0x529ecc, 0x52a488, 0x52ad50 */

static void screen(int *w, int *h) { D3d_ScreenSize(w, h); }
static const uint8_t *C(uint32_t va) { return gm(va); }   /* the colours at 0x516074-0x516090 */
static uint32_t mode(void) { return Gs_Mode(); }

bool Pause_Active(void) { return paused; }
bool Pause_RestartAsked(void) { return ask_restart; }
bool Pause_QuitAsked(void) { return ask_quit; }
bool Pause_ServiceAsked(void) { return ask_service; }
void Pause_ClearAsks(void) { ask_restart = ask_quit = ask_service = false; }
void Pause_ClearRestart(void) { ask_restart = false; }
void Pause_Ask(bool restart, bool service, bool quit)
{
    if (restart) ask_restart = true;
    if (service) ask_service = true;
    if (quit) ask_quit = true;
}
Tex *Pause_ArrowOn(void) { return arrow_on; }    /* 0x4055e0 */
Tex *Pause_ArrowOff(void) { return arrow_off; }  /* 0x4055f0 */
Tex *Pause_Roc(void) { return roc; }             /* 0x405600 */
Tex *Pause_Crt(void) { return crt; }             /* 0x405610 */

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
    cam_live = false;
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
    Camera_LookAround(base, false);
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
    if (Gs_SelBit26()) Music_Resume();   /* 0x4a3180 */
    Pause_End();
}

/* the fades' ends: every race process to the results (event 1), the flag */
void Pause_RestartDone(int ch)   /* 0x401540 */
{
    (void)ch;
    Race_EventAll(1, 3);
    ask_restart = true;
    Pause_End();
}
void Pause_ServiceDone(int ch)   /* 0x4014f0 */
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
    for (int i = 0; i < Race_Cars(); i++) Fade_OutIn(i, i ? NULL : Pause_RestartDone, 1, 0, fade_colour(), true);
    /* the network game's host (not in mode 10): the others' ready cleared, the restart to them */
    if (Gs_ModeBit19() && Net_IsHost() && mode() != 10) {
        NetRec_ClearReady();   /* 0x409bc0 */
        Net_SendRestart();     /* 0x427a50 */
    }
}
static void service_fn(Menu *m, MenuItem *it)
{
    (void)m, (void)it;
    for (int i = 0; i < Race_Cars(); i++) Fade_OutIn(i, i ? NULL : Pause_ServiceDone, 1, 0, (int32_t)gm32(0x516090), true);
    if (Gs_ModeBit19() && Net_IsHost() && mode() != 10) Net_SendService();   /* 0x427a80 */
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

/* 0x402000: the graphics page's rows (those shown, bit 1): the arrow, the name and (but for the back
   row) its description, and a list's values after the name, the chosen one lit: for id 2 the texts
   from 0xa0, for id 4 from 0x9e */
static void draw_options(Menu *m)
{
    int w, h;
    screen(&w, &h);
    Font_SetLayer(2);
    dim();
    header();
    if (!arrow_on || !arrow_off) return;
    int y = h * 0xaa / 0x1e0;
    for (int i = 0; i < m->n; i++) {
        MenuItem *it = &m->items[i];
        if (!(it->flags & MIF_SELECTABLE)) continue;
        bool on = m->sel == i;
        uint32_t c = on ? 0x516074 : 0x516078;
        Tex *t = on ? arrow_on : arrow_off;
        Rect16 src = {0, 0, t->width, t->height};
        Rect16 d = {(int16_t)(w * 0x70 / 0x280), (int16_t)(y - h * 0xd / 0x1e0), arrow_on->width, arrow_on->height};
        Sprite_Add(&src, &d, t, 2, 0, NULL, NULL, C(c), 8);
        const char *vals[3] = {NULL, NULL, NULL};
        bool desc = true;
        switch (it->id) {
        case 1: vals[0] = Text_Get(it->text), vals[1] = Text_Get(it->text + 1); break;
        case 2: for (int k = 0; k < it->count && k < 3; k++) vals[k] = Text_Get(k + 0xa0); break;
        case 4: for (int k = 0; k < it->count && k < 3; k++) vals[k] = Text_Get(k + 0x9e); break;
        case 0: break;
        default: desc = false; break;
        }
        int x = w * 0x86 / 0x280;
        const char *prev = Text_Get(it->text);
        Font_Print(1, prev, x, y, C(c), 0x11);
        if (desc) Font_Print(0, Text_Get(it->text + 1), x, h * 0xf / 0x1e0 + y, C(c), 0x11);
        for (int k = 0; k < it->count && k < 3; k++) {
            x += w * 10 / 0x280 + Font_TextWidth(1, prev);
            Font_Print(1, vals[k], x, y, C(it->value == k ? 0x516074 : 0x516078), 0x11);
            prev = vals[k];
        }
        y += h * 0x36 / 0x1e0;
    }
    Font_SetLayer(2);
}

/* 0x402eb0: the split screen's layout (0x52afb4 bit 18) and the dashboard's units (0x52afb8 bit 31) */
static void gfx_enter(Menu *m, bool back)
{
    if (back) return;
    m->items[Menu_IndexOf(m, 2)].value = (uint8_t)(gm32(0x52afb4) >> 0x12 & 1);   /* 0x405dc0 */
    m->items[Menu_IndexOf(m, 4)].value = (uint8_t)(gm32(0x52afb8) >> 0x1f);       /* 0x405eb0 */
}

/* 0x402f20: kept (0x406340, 0x405ec0; the HUD's bar laid out again, 0x411b20, which Hud_Update does
   on the change) and the dashboard stepped for the player */
static void gfx_back(Menu *m, MenuItem *it)
{
    (void)it;
    uint32_t a = m->items[Menu_IndexOf(m, 2)].value & 1u, b = m->items[Menu_IndexOf(m, 4)].value;
    gw32(0x52afb4, a << 0x12 | (gm32(0x52afb4) & 0xfffbffffu));
    gw32(0x52afb8, b << 0x1f | (gm32(0x52afb8) & 0x7fffffffu));
    Dash_Update(base);   /* 0x445ea0 */
}

static void pick_fn(Menu *m, MenuItem *it) { (void)it; base = (uint8_t)m->sel; }   /* 0x4035d0 */

/* the camera page's settings: the chase camera's offset (0x52aa50, x always 0), distance (0x52aa5c)
   and angle (0x52a86c); as they were (0x52af60, 0x529914, 0x52a48c); the live view on (0x52ad54) */

/* 0x403110: a setting's value of count: (x - from) over its range, rounded */
static uint8_t to_value(const MenuItem *it, int32_t x, int32_t from, int32_t range)
{
    int64_t v = ((int64_t)(int32_t)((uint32_t)(it->count - 1) * (uint32_t)(x - from)) << 16) / range;
    return (uint8_t)((uint32_t)((int32_t)v + 0x8000) >> 16);
}
static void cam_values(Menu *m)
{
    m->items[2].value = to_value(&m->items[2], cam_dist, 0x80000, 0x80000);
    m->items[3].value = to_value(&m->items[3], cam_ang, 0, 0x11c);
    m->items[0].value = to_value(&m->items[0], cam_off[1], 0x13333, 0xcccd);
    m->items[1].value = to_value(&m->items[1], cam_off[2], 0x50000, 0x50000);
}

/* 0x403200: the values back: the distance 8-16, the angle to 0x11c, the height 1.2-2, the length 5-10 */
static void cam_frame(Menu *m)
{
    cam_dist = (int32_t)((uint32_t)m->items[2].value << 0x13) / (m->items[2].count - 1) + 0x80000;
    cam_ang = (int16_t)((int32_t)(m->items[3].value * 0x11c) / (m->items[3].count - 1));
    cam_off[1] = (int32_t)(m->items[0].value * 0xcccdu) / (m->items[0].count - 1) + 0x13333;
    cam_off[2] = (int32_t)(m->items[1].value * 0x50000u) / (m->items[1].count - 1) + 0x50000;
}

/* 0x4036c0: the defaults */
static void cam_default(Menu *m, MenuItem *it)
{
    (void)it;
    cam_dist = 0xe0000, cam_ang = 0x2d, cam_off[1] = 0x18000, cam_off[2] = 0x68000;
    cam_values(m);
}

/* 0x403700: the chase camera's settings now, kept; the live view on. (The original also copies the
   profile's camera record, 0x408930, into two buffers it doesn't read.) */
static void cam_enter(Menu *m, bool back)
{
    (void)back;
    Chase_Raw(base, keep_off);
    keep_dist = Chase_Dist(base);
    keep_ang = Chase_RawAngle(base);
    memcpy(cam_off, keep_off, sizeof cam_off);
    cam_dist = keep_dist, cam_ang = keep_ang;
    cam_values(m);
    cam_live = true;
}

/* 0x4037c0: back, the old settings again; else the new ones to the camera, the options' defaults
   (0x405fa0) and the profile (0x408bd0) */
static void cam_leave(Menu *m, bool back)
{
    (void)m;
    if (back) {
        Chase_SetOffset(base, keep_off);
        Chase_SetDist(base, keep_dist);
        Chase_SetAngle(base, keep_ang);
        return;
    }
    Chase_SetDist(base, cam_dist);
    Chase_SetOffset(base, cam_off);
    Chase_SetAngle(base, cam_ang);
    gw32(0x52b028, (uint32_t)cam_off[1]), gw32(0x52b02c, (uint32_t)cam_off[2]);
    gw16(0x52b034, (uint16_t)cam_ang), gw32(0x52b030, (uint32_t)cam_dist);
    uint32_t v[5] = {0, (uint32_t)cam_off[1], (uint32_t)cam_off[2], (uint16_t)cam_ang, (uint32_t)cam_dist};
    Player_SetCamera(Race_Base() + base, v);
}

/* 0x403ef0: a box's outline */
static void outline(const int16_t r[4], const uint8_t *c)
{
    int16_t a[4] = {r[0], r[1], r[2], 1};
    Rect_Fill(a, c, 2);
    int16_t b[4] = {(int16_t)(r[0] + r[2]), r[1], 1, (int16_t)(r[3] + 1)};
    Rect_Fill(b, c, 2);
    int16_t d[4] = {r[0], (int16_t)(r[1] + r[3]), r[2], 1};
    Rect_Fill(d, c, 2);
    int16_t e[4] = {r[0], r[1], 1, r[3]};
    Rect_Fill(e, c, 2);
}

/* 0x403890: lighter over the view (the dim's alpha 0x73); the four settings each as a bar of its
   values with the value's marker, the defaults row (lit while its value is set) and back, 42 apart */
static void draw_camera(Menu *m)
{
    int w, h;
    screen(&w, &h);
    Font_SetLayer(2);
    uint8_t dc[4];
    memcpy(dc, C(0x51608c), 4);
    dc[3] = 0x73;
    int16_t full[4] = {0, 0, (int16_t)w, (int16_t)h};
    Rect_Fill(full, dc, 2);
    header();
    if (!arrow_on || !arrow_off) return;
    int wide = 0;
    for (int i = 0; i < m->n && m->items[i].text != 0x62; i++) {
        int tw = Font_TextWidth(1, Text_Get(m->items[i].text));
        if (wide < tw) wide = tw;
    }
    for (int i = 0; i < m->n; i++) {
        MenuItem *it = &m->items[i];
        const uint8_t *c = C(m->sel == i ? 0x516074 : 0x516078);
        int y = h * 0xaa / 0x1e0 + (h * 0x2a / 0x1e0) * i, x = w * 0x86 / 0x280;
        if (it->id >= 0 && it->id < 4) {
            Font_Print(1, Text_Get(it->text), x, y, c, 0x11);
            int16_t box[4] = {(int16_t)(w * 5 / 0x280 + wide + x), (int16_t)(y - h * 0xe / 0x1e0),
                              (int16_t)(w * 0xe / 0x280 + (int)it->count * w * 10 / 0x280), (int16_t)(h * 0x10 / 0x1e0)};
            outline(box, c);
            int16_t knob[4] = {(int16_t)(w * 2 / 0x280 + w * 5 / 0x280 + wide + ((int)it->value * w * 10 / 0x280 + w * 0x86 / 0x280)),
                               (int16_t)(y - h * 0xc / 0x1e0), (int16_t)(w * 0x17 / 0x280), (int16_t)(h * 0xe / 0x1e0)};
            Rect_Fill(knob, c, 2);
        } else if (it->id == 4)
            Font_Print(1, Text_Get(0x62), x, y, it->value ? C(0x51607c) : c, 0x11);
        else
            Font_Print(1, Text_Get(it->text), x, y, c, 0x11);
        Tex *t = m->sel == i ? arrow_on : arrow_off;
        Rect16 src = {0, 0, t->width, t->height};
        Rect16 d = {(int16_t)(w * 0x70 / 0x280), (int16_t)(y - h * 0xd / 0x1e0), arrow_on->width, arrow_on->height};
        Sprite_Add(&src, &d, t, 2, 0, NULL, NULL, c, 8);
    }
    Font_SetLayer(2);
}

/* 0x404f40's start on the camera page: the player's camera made the far chase (kind 5, also the
   profile's), its settings eased a tenth of the way to the page's each frame (the page's then the
   eased ones, as the original keeps them); with the live view the camera stepped and framed */
static void camera_live(void)
{
    if (page == &pg_cam) {
        int k = Camera_Kind(base);
        if (k != 5 && k != 8) {
            Camera_SelectAt(base, 5, 0xffff, Camera_Car(base), true);
            /* the profile: in a challenge match its first driver (kind 2), or the pausing player's
               side (kind 3, 0x407830; outside a round the base here, the original's stack value) */
            uint32_t who = (uint32_t)base, other = 0;
            if (Race_Kind() == 2) Challenge_Pair(&who, &other);
            else if (Race_Kind() == 3) {
                if (base == 0) Challenge_Pair(&who, &other);
                else Challenge_Pair(&other, &who);
            } else if (Race_Kind() == 4) who = (uint32_t)(Race_Base() + base);
            Player_SetCameraKind((int)who, 5);
        }
        int32_t raw[3];
        Chase_Raw(base, raw);
        int16_t a = Chase_RawAngle(base);
        cam_off[0] = 0;
        cam_ang = (int16_t)(a + (int16_t)fx_mul((int16_t)(cam_ang - a), 0x1999));
        cam_off[1] = raw[1] + fx_mul(cam_off[1] - raw[1], 0x1999);
        cam_off[2] = raw[2] + fx_mul(cam_off[2] - raw[2], 0x1999);
        Chase_SetDist(base, cam_dist);
        Chase_SetOffset(base, cam_off);
        Chase_SetAngle(base, cam_ang);
    }
    if (cam_live) {
        Race_PausedCamera(base);
        Camera_LookAround(base, page == &pg_cam && pg_cam.sel == 2);   /* 0x423010: on the distance */
    }
}

/* the display page's flags (the profile's +0x574, the challenge record's +0xd): bits 0-1 the
   co-driver's arrows (0x405f00), 2-5 four panels (0x405f40, 0x405f20, 0x405f60, 0x405f80); the page's
   and as they were (0x5298f8, 0x52a850) */
static uint8_t osd, keep_osd;

/* Profile_HudFlags 0x408a00, 0 without a profile */
static uint32_t osd_at(void)
{
    int n = Race_Base() + base;
    if (mode() == 4) return 0x52f3ed + 0xc4u * (uint32_t)n;
    uint32_t t = Player_Word(n) >> 0x12 & 0xf;
    return t == 0xf ? 0 : 0x52ff90 + t * 0x650;
}
static void osd_put(uint8_t v) { uint32_t a = osd_at(); if (a) gw8(a, v); }   /* 0x408990 */

/* the arrows' row (id 4, with the co-driver): with the screen split on (0) or off (1, from bits 0-1
   2), else three values, 2 less the bits */
static void osd_values(Menu *m)
{
    m->items[0].value = osd >> 2 & 1, m->items[1].value = osd >> 3 & 1;
    m->items[2].value = osd >> 4 & 1, m->items[3].value = osd >> 5 & 1;
}

static void osd_enter(Menu *m, bool back)   /* 0x404b80 */
{
    (void)back;
    uint32_t a = osd_at();
    keep_osd = osd = a ? gm8(a) : 0;   /* (the original reads through 0 without a profile) */
    osd_values(m);
    if (!speech_on()) return;
    int k = Menu_IndexOf(m, 4);
    if (Hud_SplitScreen()) m->items[k].value = (osd & 3) == 2 ? 0 : 1;
    else m->items[k].value = (uint8_t)(2 - (osd & 3));
}

/* 0x404d00: the rows into the flags, into the profile at once (the HUD shows them while here) */
static void osd_frame(Menu *m)
{
    osd = (uint8_t)((m->items[3].value & 1) << 5 | (m->items[1].value & 1) << 3 | (m->items[0].value & 1) << 2 | (osd & 0xd3));
    if (speech_on()) {
        uint8_t v = m->items[Menu_IndexOf(m, 4)].value;
        if (!Hud_SplitScreen()) osd = (uint8_t)((osd & 0xfc) | ((uint8_t)(-(int)v - 2) & 3));
        else osd = v == 0 ? (uint8_t)((osd & 0xfe) | 2) : (uint8_t)(osd & 0xfc);
    }
    if (m->items[Menu_IndexOf(m, 2)].value) osd |= 0x10;
    else osd &= 0xef;
    osd_put(osd);
}

/* 0x404130: every panel on, the arrows' bits 0 */
static void osd_default(Menu *m, MenuItem *it)
{
    (void)it;
    osd = (uint8_t)((osd & 0xfc) | 0x3c);
    osd_put(osd);
    osd_values(m);
    if (speech_on()) m->items[Menu_IndexOf(m, 4)].value = Hud_SplitScreen() ? 1 : 2;
}

/* 0x404c50: back, the old flags again; else kept, and the options' defaults too */
static void osd_leave(Menu *m, bool back)
{
    (void)m;
    if (back) {
        osd_put(keep_osd);
        return;
    }
    osd_put(osd);
    uint32_t o = gm32(0x52afb8) & 0xc0ffffffu;
    o |= (uint32_t)(osd & 3) << 0x18 | (uint32_t)(osd >> 2 & 1) << 0x1a | (uint32_t)(osd >> 3 & 1) << 0x1b |
         (uint32_t)(osd >> 4 & 1) << 0x1c | (uint32_t)(osd >> 5 & 1) << 0x1d;
    gw32(0x52afb8, o);
    Player_Touch(Race_Base() + base);
}

/* 0x4041e0: over the view (the dim on layer 4, no header), 42 apart: each row's name and its values
   after it (the chosen lit): 0x9c on; id 2 with more than two values 0x9c then from 0xf0; id 4 from
   0xa4 down, with the screen split 0x9c up; the defaults row (lit while set) and back */
static void draw_osd(Menu *m)
{
    int w, h;
    screen(&w, &h);
    Font_SetLayer(2);
    int16_t full[4] = {0, 0, (int16_t)w, (int16_t)h};
    Rect_Fill(full, C(0x51608c), 4);
    if (!arrow_on || !arrow_off) return;
    for (int i = 0; i < m->n; i++) {
        MenuItem *it = &m->items[i];
        uint32_t c = m->sel == i ? 0x516074 : 0x516078;
        int y = h * 0xaa / 0x1e0 + (h * 0x2a / 0x1e0) * i, x = w * 0x86 / 0x280;
        if (it->id >= 0 && it->id <= 4) {
            const char *prev = Text_Get(it->text);
            Font_Print(1, prev, x, y, C(c), 0x11);
            for (int k = 0; k < it->count; k++) {
                int t = k + 0x9c;
                if (it->id == 2 && it->count != 2) t = k == 0 ? 0x9c : k + 0xef;
                if (it->id == 4) t = Hud_SplitScreen() ? k + 0x9c : 0xa4 - k;
                x += w * 10 / 0x280 + Font_TextWidth(1, prev);
                prev = Text_Get(t);
                Font_Print(1, prev, x, y, C(it->value == k ? 0x516074 : 0x516078), 0x11);
            }
        } else if (it->id == 5)
            Font_Print(1, Text_Get(0x62), x, y, C(it->value ? 0x51607c : c), 0x11);
        else
            Font_Print(1, Text_Get(it->text), x, y, C(c), 0x11);
        Tex *t = m->sel == i ? arrow_on : arrow_off;
        Rect16 src = {0, 0, t->width, t->height};
        Rect16 d = {(int16_t)(w * 0x70 / 0x280), (int16_t)(y - h * 0xd / 0x1e0), arrow_on->width, arrow_on->height};
        Sprite_Add(&src, &d, t, 2, 0, NULL, NULL, C(c), 8);
    }
    Font_SetLayer(2);
}

/* 0x4054b0: paused by player p (0x52af58's byte 1) on the on-screen display page (0x52aa70) */
bool Pause_OsdPage(int p) { return Pause_Active() && base == (uint8_t)p && page == &pg_osd; }

void Pause_Build(void)
{
    paused = false;
    gw8(0x52af40, 0);
    player = base = 0;
    ask_restart = ask_quit = ask_service = false;
    /* 0x4054f0: the arrows and crtna640 from the race's Com archive, in mode 4 RoCx_640 (the
       challenge's screen's box) from the common one (0x455290) */
    char p[260];
    Path_Format(p, sizeof p, 0x5160f4, Path_Root());
    arrow_on = Bfl_FindTexture(Bfl_Com(), p, NULL, false, 0);
    Path_Format(p, sizeof p, 0x5160d8, Path_Root());
    arrow_off = Bfl_FindTexture(Bfl_Com(), p, NULL, false, 0);
    if (Gs_Mode() == 4) {
        Path_Format(p, sizeof p, 0x5160bc, Path_Root());
        roc = Bfl_FindTexture(Bfl_CommonArc(), p, NULL, false, 0);
    }
    Path_Format(p, sizeof p, 0x5160a0, Path_Root());
    crt = Bfl_FindTexture(Bfl_Com(), p, NULL, false, 0);
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
    /* 0x402f90: graphics: the display and camera pages, the split screen's layout (shown only with
       the screen split) and the dashboard's units; back to the main page keeps them */
    Menu_Init(&pg_gfx, NULL, 0, Gs_SelBits14() == 1 ? &pg_main : &pg_gfx_pick, false, NULL, 1, 0, 1);
    Menu_AddPage(&pg_gfx, NULL, 0x33, &pg_osd, NULL, 0);
    Menu_AddPage(&pg_gfx, NULL, 0x5c, &pg_cam, NULL, 1);
    Menu_AddList(&pg_gfx, NULL, 0x35, 2, 0, false, NULL, 2);
    Menu_AddList(&pg_gfx, NULL, 0x39, 2, 0, false, NULL, 4);
    Menu_AddPage(&pg_gfx, NULL, 0x3b, &pg_main, gfx_back, -1);
    Menu_SetCallbacks(&pg_gfx, gfx_enter, NULL, draw_options, NULL);
    Menu_Reset(&pg_gfx);
    if (!Hud_SplitScreen())
        for (int i = 0; i < pg_gfx.n; i++)
            if (pg_gfx.items[i].id == 2) pg_gfx.items[i].flags &= (uint8_t)~(MIF_ENABLED | MIF_SELECTABLE);
    /* 0x403550: which player's (two players) */
    Menu_Init(&pg_gfx_pick, NULL, 0, &pg_main, false, NULL, 1, 0, 1);
    Menu_AddPage(&pg_gfx_pick, NULL, 0x58, &pg_gfx, pick_fn, 0);
    Menu_AddPage(&pg_gfx_pick, NULL, 0x5a, &pg_gfx, pick_fn, 1);
    Menu_SetCallbacks(&pg_gfx_pick, NULL, NULL, draw_main, NULL);
    Menu_Reset(&pg_gfx_pick);
    /* 0x4035e0: the chase camera: height, length, distance, angle (21 values), the defaults, back */
    Menu_Init(&pg_cam, NULL, 0, &pg_gfx, false, NULL, 1, 0, 1);
    Menu_AddList(&pg_cam, NULL, 0x5e, 0x15, 10, false, NULL, 0);
    Menu_AddList(&pg_cam, NULL, 0x5f, 0x15, 10, false, NULL, 1);
    Menu_AddList(&pg_cam, NULL, 0x60, 0x15, 10, false, NULL, 2);
    Menu_AddList(&pg_cam, NULL, 0x61, 0x15, 10, false, NULL, 3);
    Menu_AddAction(&pg_cam, NULL, 0x62, cam_default, 4);
    Menu_AddPage(&pg_cam, NULL, 0x3b, &pg_gfx, NULL, 5);
    Menu_SetCallbacks(&pg_cam, cam_enter, cam_frame, draw_camera, cam_leave);
    Menu_Reset(&pg_cam);
    /* 0x403090: the controller page, never shown (the main page passes over its row) */
    Menu_Init(&pg_ctrl, NULL, 0, &pg_main, false, NULL, 1, 0, 1);
    Menu_AddPage(&pg_ctrl, NULL, 0x3b, &pg_main, NULL, -1);
    Menu_SetCallbacks(&pg_ctrl, NULL, NULL, stub_draw, NULL);
    Menu_Reset(&pg_ctrl);
    /* 0x404000: the display: the four panels (the first named by a circuit's or bit 28's), the
       co-driver's arrows (two values with the screen split, else three), the defaults, back */
    Menu_Init(&pg_osd, NULL, 0, &pg_gfx, false, NULL, 1, 0, 1);
    Menu_AddList(&pg_osd, NULL, Gs_SelBit24() || Gs_SelBit28() ? 0x30 : 100, 2, 0, false, NULL, 0);
    Menu_AddList(&pg_osd, NULL, 0x65, 2, 0, false, NULL, 1);
    Menu_AddList(&pg_osd, NULL, 0x66, 2, 0, false, NULL, 2);
    Menu_AddList(&pg_osd, NULL, 0x67, 2, 0, false, NULL, 3);
    if (speech_on()) Menu_AddList(&pg_osd, NULL, 0x68, Hud_SplitScreen() ? 2 : 3, 0, false, NULL, 4);
    Menu_AddAction(&pg_osd, NULL, 0x62, osd_default, 5);
    Menu_AddPage(&pg_osd, NULL, 0x3b, &pg_gfx, NULL, 6);
    Menu_SetCallbacks(&pg_osd, osd_enter, osd_frame, draw_osd, osd_leave);
    Menu_Reset(&pg_osd);
}

/* 0x404f40: the camera page's live view, then the input on the page. A pause key
   bound on a game controller (its bits kept in 0x52a0b8) leaves it; the keyboard's Escape is the
   menus' back key, so "continue" is the way out of the main page. */
void Pause_Frame(void)
{
    camera_live();
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
