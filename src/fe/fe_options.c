/* The Options pages' settings (0x4df410-0x4e1d70, 0x4f1c00-0x4f2d90): the graphics and the
   advanced graphics. */
#include "fe/fe.h"
#include "fe/fe_page.h"
#include "fe/fe_strip.h"
#include "fe/pages.h"
#include "font.h"
#include "game/mem.h"
#include "render/d3d7.h"
#include "render/tex.h"
#include "render/twod.h"
#include "exe.h"
#include "text.h"
#include <stdio.h>

static void screen(int *w, int *h) { D3d_ScreenSize(w, h); }

/* the option pages' top, as the Sound page's: centred between 8 + 38 and 384 of 480 */
static int high_top(const Menu *m)
{
    int w, h;
    screen(&w, &h);
    return (h * 8 / 0x1e0 + h * 0x26 / 0x1e0 + h * 0x180 / 0x1e0) / 2 - h * 0x24 / 0x1e0 * m->n / 2;
}

/* a label and its choices after it (10 of 640 apart), the value's in `lit`, the others in `unlit`:
   the k-th choice is text base + k * step */
static void choices_in(const MenuItem *it, const char *label, int base, int step, int x, int y, uint32_t c,
                       uint32_t lit, uint32_t unlit, int w)
{
    const char *prev = label;
    Font_Print(1, prev, x, y, gm(c), 0x11);
    for (int k = 0; k < it->count; k++) {
        x += w * 10 / 0x280 + Font_TextWidth(1, prev);
        prev = Text_Get(base + k * step);
        Font_Print(1, prev, x, y, gm(it->value == k ? lit : unlit), 0x11);
    }
}

/* the usual colours: lit and unlit, an item off all in the third */
static void choices_after(const MenuItem *it, const char *label, int base, int step, int x, int y, uint32_t c, int w)
{
    bool off = c == 0x524970;
    choices_in(it, label, base, step, x, y, c, off ? c : 0x524968, off ? c : 0x52496c, w);
}

static void choices(const MenuItem *it, int base, int step, int x, int y, uint32_t c, int w)
{
    choices_after(it, Text_Get(it->text), base, step, x, y, c, w);
}

/* The port's device (the caps 0x4b7330 reads from Direct3D): more than one texture stage, textures to
   1024, no bump environment mapping and no cube maps (the GLES back end has neither) */
static bool caps_stages(void) { return true; }     /* 0x4b7530: DAT_006e0c48 */
static bool caps_bump(void) { return true; }       /* 0x4b7540: DAT_006e0c4c (the port's device has the bump ops) */
static bool caps_cube(void) { return true; }       /* 0x4b7550: DAT_006e0c54 (the port's device has cube maps) */
static bool caps_tex1024(void) { return true; }    /* 0x4b7560 and 0x4b7590 (0x400) */

/* ---- Graphics (0x81ef38): the split screen's way, the ghost car, the units, the speedo ---- */

static int value_of(Menu *m, int16_t id) { return m->items[Menu_IndexOf(m, id)].value; }
static void set_value(Menu *m, int16_t id, int v) { m->items[Menu_IndexOf(m, id)].value = (uint8_t)v; }

/* 0x4f2c40: from the settings (not when back): the split (0x52afb4 bit 18), the ghost (0x52afb8
   bit 30 clear: On first), the units (bit 31), the speedo (0x52afbc bits 1-2, 0-2 only) */
void fe_4f2c40(Menu *m, bool back)
{
    if (back) return;
    set_value(m, 0, gm32(0x52afb4) >> 0x12 & 1);   /* 0x405dc0 */
    set_value(m, 2, gm32(0x52afb8) >> 0x1e & 1 ? 0 : 1);   /* 0x406310 */
    set_value(m, 3, gm32(0x52afb8) >> 0x1f);   /* 0x405eb0 */
    uint32_t s = gm32(0x52afbc) >> 1 & 3;   /* 0x405ef0 */
    if (s < 3) set_value(m, 4, (int)s);
}

/* 0x4f2d20: OK: the rows into the settings (0x406340, 0x4eab20, 0x405ec0, 0x4eaa40) */
void fe_4f2d20(Menu *m, MenuItem *it)
{
    (void)it;
    gw32(0x52afb4, (gm32(0x52afb4) & ~(1u << 0x12)) | (value_of(m, 0) & 1u) << 0x12);
    gw32(0x52afb8, (gm32(0x52afb8) & ~(1u << 0x1e)) | ((1u - value_of(m, 2)) & 1u) << 0x1e);
    gw32(0x52afb8, (gm32(0x52afb8) & 0x7fffffffu) | (uint32_t)value_of(m, 3) << 0x1f);
    gw32(0x52afbc, (gm32(0x52afbc) & ~6u) | (value_of(m, 4) & 3u) << 1);
}

/* the rows: Vertical / Horizontal, On / Off, KMH / MPH, Default / Digital / Analogue */
static void graphics_row(Menu *m, MenuItem *it, bool two, int x, int y, uint32_t c, int w)
{
    (void)m, (void)two;
    switch (it->id) {
    case 0: choices(it, 0x137, 1, x, y, c, w); break;
    case 2: choices(it, 0x134, -1, x, y, c, w); break;
    case 3: choices(it, 0x135, 1, x, y, c, w); break;
    case 4: choices(it, 0x152, 1, x, y, c, w); break;
    default: Font_Print(1, Text_Get(it->text), x, y, gm(c), 0x11); break;
    }
}

/* 0x4df410 */
void fe_4df410(Menu *m)
{
    int w, h;
    screen(&w, &h);
    Fe_Header();
    Fe_Titles(m, w * 0x18 / 0x280, h * 0x26 / 0x1e0, true, 2, NULL, -1);
    Rows_Draw(m, graphics_row, false, false, high_top(m), Rows_ArrowDy());
    Strip_DrawMain(&pg_81d6d8, false, NULL);
}

/* ---- Advanced Graphics (0x8210f8): the environment map's page, the shadow map, the card's and
   the display mode's pages, the mip map, the draw distance, Detail ---- */

static uint32_t draw_dist_kept;   /* PTR_DAT_00520b74 + 0x3c0: written, never read */

/* 0x4b7520: the device can do the shadow map (DAT_006e0c58, which 0x4b7330 always sets) */
static bool shadow_caps(void) { return true; }

/* 0x4f1c00: the shadow map row on with the caps, On (0) when both its setting (0x52afd0 bit 4) and
   the display's flag 0x40 are; the mip map from the filter (0x52afd0 bits 18-19: 1 bilinear with
   flag 1, 2 trilinear with flag 2, else off); the draw distance (bits 21-24, at most 9) */
void fe_4f1c00(Menu *m, bool back)
{
    (void)back;
    MenuItem *sh = Menu_Item(m, 1);
    if (!shadow_caps()) sh->flags &= (uint8_t)~MIF_ENABLED;
    else {
        sh->value = (gm32(0x52afd0) >> 4 & 1) && (g_render_opts & 0x40) ? 0 : 1;
        sh->flags |= MIF_ENABLED;
    }
    uint32_t f = gm32(0x52afd0) >> 0x12 & 3;
    Menu_Item(m, 4)->value = f == 1 && (g_render_opts & 1) ? 1 : f == 2 && (g_render_opts & 2) ? 0 : 2;
    if ((gm32(0x52afd0) >> 0x15 & 0xf) > 9) gw32(0x52afd0, (gm32(0x52afd0) & 0xfe1fffffu) | 9u << 0x15);
    Menu_Item(m, 5)->value = (uint8_t)(gm32(0x52afd0) >> 0x15 & 0xf);
}

/* 0x4f1e40: the rows into the settings and the display's flags */
void fe_4f1e40(Menu *m, MenuItem *it)
{
    (void)it;
    uint32_t d = gm32(0x52afd0);
    if (shadow_caps()) {
        bool on = Menu_Item(m, 1)->value == 0;
        d = (d & ~0x10u) | (on ? 0x10u : 0);   /* 0x405bb0 */
        g_render_opts = on ? g_render_opts | 0x40 : g_render_opts & ~0x40u;
    }
    switch (Menu_Item(m, 4)->value) {   /* 0x405c80 */
    case 0: d = (d & 0xfff3ffffu) | 2u << 0x12; g_render_opts = (g_render_opts & ~1u) | 2; break;
    case 1: d = (d & 0xfff3ffffu) | 1u << 0x12; g_render_opts = (g_render_opts | 1) & ~2u; break;
    case 2: d &= 0xfff3ffffu; g_render_opts &= ~3u; break;
    }
    uint8_t dist = Menu_Item(m, 5)->value;
    d = (d & 0xfe1fffffu) | (dist & 0xfu) << 0x15;   /* 0x405cb0 */
    gw32(0x52afd0, d);
    draw_dist_kept = dist;
}

/* 0x4f1f70: left other than by back: as OK */
void fe_4f1f70(Menu *m, bool back)
{
    if (!back) fe_4f1e40(m, &m->items[m->sel + 1]);
}

static void adv_row(Menu *m, MenuItem *it, bool two, int x, int y, uint32_t c, int w)
{
    (void)two;
    char b[256];
    switch (it->id) {
    case 0: {
        uint32_t e = gm32(0x52afd0) >> 1 & 3;   /* 0x405b50 */
        snprintf(b, sizeof b, exe_str(0x524dfc), Text_Get(e == 1 ? 0x1cd : e == 2 ? 0x1cc : e == 3 ? 0x1cb : 0x1ce));
        break;
    }
    case 1: choices_after(it, Text_Get(0x1cf), 0x134, -1, x, y, c, w); return;
    case 2:   /* the card's description (0x4a8bf0; the port has its one device) */
        snprintf(b, sizeof b, exe_str(0x524df0), Text_Get(0x1d0), "OpenGL ES 3");
        break;
    case 3: {
        const D3dState *st = D3d_State();
        int sw, sh;
        screen(&sw, &sh);
        snprintf(b, sizeof b, exe_str(0x524ddc), Text_Get(0x1d1), sw, sh, st ? st->bpp : 32);
        break;
    }
    case 4: {
        const char *prev = Text_Get(0x1d2);
        Font_Print(1, prev, x, y, gm(c), 0x11);
        for (int k = 0; k < it->count; k++) {
            x += w * 10 / 0x280 + Font_TextWidth(1, prev);
            prev = Text_Get(k == 0 ? 0x1d6 : k == 1 ? 0x1d5 : 0x133);
            uint32_t kc = c == 0x524970 ? c : it->value == k ? 0x524968 : 0x52496c;
            Font_Print(1, prev, x, y, gm(kc), 0x11);
        }
        return;
    }
    case 5: snprintf(b, sizeof b, exe_str(0x524dd0), Text_Get(0x1d3), Menu_Item(m, 5)->value + 1); break;
    case 6: case 7: case 0xe: snprintf(b, sizeof b, "%s", Text_Get(it->text)); break;
    default: return;
    }
    Font_Print(1, b, x, y, gm(c), 0x11);
}

/* 0x4dfe20 */
void fe_4dfe20(Menu *m)
{
    int w, h;
    screen(&w, &h);
    Fe_Header();
    Fe_Titles(m, w * 0x18 / 0x280, h * 0x26 / 0x1e0, true, 2, NULL, -1);
    Rows_Draw(m, adv_row, false, true, high_top(m), h * 2 / 0x1e0 + h * 0x12 / 0x1e0);
    Strip_DrawMain(&pg_81d6d8, false, NULL);
}

/* ---- Detail (0x81cd78): the effects (0x52afd0 bits 25-26), the track (bits 27-28), the car
   (0x52afd4 bits 0-1) ---- */

/* 0x4f1d00 */
void fe_4f1d00(Menu *m, bool back)
{
    (void)back;
    Menu_Item(m, 8)->value = (uint8_t)(gm32(0x52afd0) >> 0x19 & 3);   /* 0x405cd0 */
    Menu_Item(m, 9)->value = (gm32(0x52afd0) >> 0x1b & 3) ? 1 : 0;   /* 0x405d00 */
    Menu_Item(m, 10)->value = (gm32(0x52afd4) & 3) ? 1 : 0;           /* 0x405d10 */
}

/* 0x4f1d60: without textures to 1024 the car's row can't change */
void fe_4f1d60(Menu *m)
{
    if (caps_tex1024()) return;
    Menu_SetFlags(m, true, m->items[m->sel].id != 10, true, true);
}

/* 0x4f1db0: OK: the rows in; the car's high only with textures to 1024 (else 2) */
void fe_4f1db0(Menu *m, MenuItem *it)
{
    (void)it;
    uint32_t d = gm32(0x52afd0);
    d = (d & 0xf9ffffffu) | (Menu_Item(m, 8)->value & 3u) << 0x19;   /* 0x405ce0 */
    uint8_t t = Menu_Item(m, 9)->value;
    if (t < 2) d = (d & 0xe7ffffffu) | (uint32_t)t << 0x1b;   /* 0x405d20 */
    gw32(0x52afd0, d);
    uint8_t c = Menu_Item(m, 10)->value;
    int car = c == 0 ? (caps_tex1024() ? 0 : 2) : c == 1 ? 2 : -1;
    if (car >= 0) gw32(0x52afd4, (gm32(0x52afd4) & ~3u) | (uint32_t)car);   /* 0x405d40 */
}

/* the rows: the effects High / Low / Off, the track and the car High / Low (the car's Low lit and
   High in the third without textures to 1024) */
static void detail_row(Menu *m, MenuItem *it, bool two, int x, int y, uint32_t c, int w)
{
    (void)m, (void)two;
    bool off = c == 0x524970;   /* not chosen and off: the value unlit, the rest in the third */
    uint32_t lit = off ? 0x52496c : 0x524968, unlit = off ? 0x524970 : 0x52496c;
    switch (it->id) {
    case 8: choices_in(it, Text_Get(it->text), 0x1b9, 1, x, y, c, lit, unlit, w); break;
    case 9: choices_in(it, Text_Get(it->text), 0x1b3, -1, x, y, c, lit, unlit, w); break;
    case 10: {
        const char *prev = Text_Get(it->text);
        Font_Print(1, prev, x, y, gm(c), 0x11);
        for (int k = 0; k < it->count; k++) {
            x += w * 10 / 0x280 + Font_TextWidth(1, prev);
            prev = Text_Get(0x1b3 - k);
            uint32_t kc = !caps_tex1024() ? (k ? 0x524968 : 0x524970) : it->value == k ? lit : unlit;
            Font_Print(1, prev, x, y, gm(kc), 0x11);
        }
        break;
    }
    default: Font_Print(1, Text_Get(it->text), x, y, gm(c), 0x11); break;
    }
}

/* 0x4e0770 */
void fe_4e0770(Menu *m)
{
    int w, h;
    screen(&w, &h);
    Fe_Header();
    Fe_Titles(m, w * 0x18 / 0x280, h * 0x26 / 0x1e0, true, 2, NULL, -1);
    Rows_Draw(m, detail_row, false, true, high_top(m), h * 2 / 0x1e0 + h * 0x12 / 0x1e0);
    Strip_DrawMain(&pg_81d6d8, false, NULL);
}

/* ---- Environment Map (0x81de58): Off, Standard, Bump, Cubic (0x52afd0 bits 1-2; the display's
   flags 8 any, 0x10 bump, 0x80 cubic) ---- */

/* 0x4f1fa0: Off always; Standard with more than one stage, Bump and Cubic with the caps too; the
   current one chosen (the first item's +0x0a set to 4 as well) */
void fe_4f1fa0(Menu *m, bool back)
{
    (void)back;
    m->items[0].count = 4;
    m->items[0].flags |= MIF_ENABLED;
    bool st = caps_stages();
    m->items[1].flags = (uint8_t)((m->items[1].flags & ~MIF_ENABLED) | (st ? MIF_ENABLED : 0));
    m->items[2].flags = (uint8_t)((m->items[2].flags & ~MIF_ENABLED) | (st && caps_bump() ? MIF_ENABLED : 0));
    m->items[3].flags = (uint8_t)((m->items[3].flags & ~MIF_ENABLED) | (st && caps_cube() ? MIF_ENABLED : 0));
    uint32_t e = gm32(0x52afd0) >> 1 & 3;   /* 0x405b50 */
    m->sel = (int8_t)(e <= 3 ? e : 0);
}

/* 0x4f2050: the chosen one in (0x405b60) and back */
void fe_4f2050(Menu *m, MenuItem *it)
{
    (void)it;
    int e = m->sel;
    if (e >= 0 && e <= 3) {
        gw32(0x52afd0, (gm32(0x52afd0) & 0xfffffff9u) | (uint32_t)e << 1);
        g_render_opts &= ~(0x08u | 0x10u | 0x80u);
        if (e) g_render_opts |= 8;
        if (e == 2) g_render_opts |= 0x10;
        if (e == 3) g_render_opts |= 0x80;
    }
    Menu_Goto(m->parent);
}

/* 0x4e1890: the trail with Advanced Graphics, the list, the main menu's strip unlit */
void fe_4e1890(Menu *m)
{
    int w, h;
    screen(&w, &h);
    const char *names[1] = {Text_Get(0x8e)};
    Fe_Header();
    Fe_Titles(m, w * 0x18 / 0x280, h * 0x26 / 0x1e0, true, 2, names, 1);
    List_Draw(m, NULL, -1, -1, 0, true);
    Strip_DrawMain(&pg_81d6d8, false, NULL);
}

/* ---- the graphics card (0x81c7d8) and the display mode (0x81cb98) ----
   The original lists DirectDraw's devices (0x663b18 of them, names at 0x6634d8, 0xa0 apart) and the
   chosen one's modes (0x663b28, at 0x6631f8, 12 bytes each). The port has one device and one mode,
   its 640x480 at 32 bits; choosing either re-opens the same, so the display's reset and the
   textures' reload (0x4a9a50, 0x4a78a0, 0x4d21e0, 0x4a4c40, 0x4f4d80, 0x4ef190) change nothing and
   are left out. */

static const char *device_name(int i) { (void)i; return "OpenGL ES 3"; }   /* 0x4a8bf0 */
static int modes_count(void) { return 1; }                                 /* 0x4a8f10 */
static int mode_current(void) { return 0; }                                /* 0x4a8eb0 */
static void mode_get(int i, int *w, int *h, int *bpp)                      /* 0x4a8f20 */
{
    (void)i;
    screen(w, h);
    const D3dState *st = D3d_State();
    *bpp = st ? st->bpp : 32;
}

/* the graphics settings back to their defaults after a new device or mode (the end of 0x4f2210) */
static void graphics_defaults(void)
{
    uint32_t d = gm32(0x52afd0);
    d &= ~6u;                                    /* 0x405b60(0): no environment map */
    d &= ~8u;                                    /* 0x405b80(0) */
    d &= ~0x10u;                                 /* 0x405bb0(0): no shadow map */
    d &= 0xfff3ffffu;                            /* 0x405c80(0): no mip map */
    d = (d & 0xfe1fffffu) | 1u << 0x15;          /* 0x405cb0(1): draw distance 2 */
    d = (d & 0xf9ffffffu) | 1u << 0x19;          /* 0x405ce0(1): effects low */
    d = (d & 0xe7ffffffu) | 1u << 0x1b;          /* 0x405d20(1): track low */
    gw32(0x52afd0, d);
    gw32(0x52afd4, (gm32(0x52afd4) & ~3u) | 2);  /* 0x405d40(2): car low */
    g_render_opts &= ~(0x08u | 0x10u | 0x80u | 0x20u | 0x40u | 1u | 2u);
    draw_dist_kept = 1;
}

/* 0x4f21c0: a row per device, the current one (0x52afd0 bits 5-8) chosen; those not hardware off */
void fe_4f21c0(Menu *m, bool back)
{
    (void)back;
    m->n = (int8_t)Fe_DisplayModes();
    m->sel = (int8_t)(gm32(0x52afd0) >> 5 & 0xf);
    for (int i = 0; i < m->n; i++) m->items[i].flags |= MIF_ENABLED;   /* 0x4a96c0: the port's is */
}

/* 0x4f2210: the device chosen (0x405be0, 0x4a8bd0), the display reset, the defaults; back */
void fe_4f2210(Menu *m, MenuItem *it)
{
    (void)it;
    gw32(0x52afd0, (gm32(0x52afd0) & 0xfffffe1fu) | ((uint32_t)m->sel & 0xf) << 5);
    int w, h, bpp;
    mode_get(mode_current(), &w, &h, &bpp);
    gw32(0x52afc4, (uint32_t)w), gw32(0x52afc8, (uint32_t)h), gw32(0x52afcc, (uint32_t)bpp);   /* 0x405c20-0x405c60 */
    graphics_defaults();
    Menu_Goto(m->parent);
}

static void card_row(Menu *m, MenuItem *it, bool two, int x, int y, uint32_t c, int w)
{
    (void)two, (void)w;
    Font_Print(1, device_name((int)(it - m->items)), x, y, gm(c), 0x11);
}

/* 0x4e1920 */
void fe_4e1920(Menu *m)
{
    int w, h;
    screen(&w, &h);
    Fe_Header();
    Fe_Titles(m, w * 0x18 / 0x280, h * 0x26 / 0x1e0, true, 2, NULL, -1);
    Rows_Draw(m, card_row, false, true, high_top(m), h * 2 / 0x1e0 + h * 0x12 / 0x1e0);
    Strip_DrawMain(&pg_81d6d8, false, NULL);
}

static int mode_top;   /* DAT_00819128: the first of the ten shown */

/* 0x4f2360: the list's count and the current mode; the first shown keeps it in the ten */
void fe_4f2360(Menu *m, bool back)
{
    (void)back;
    mode_top = mode_current();
    if (modes_count() < mode_top + 10) {
        mode_top = modes_count() - 10;
        if (mode_top < 0) mode_top = 0;
    }
    m->items[0].count = (uint8_t)modes_count();
    m->items[0].value = (uint8_t)mode_current();
    /* 0x4a9b10(0): the display's lost flag cleared (the port's is never set) */
}

/* 0x4f23d0: the ten shown follow the value */
void fe_4f23d0(Menu *m)
{
    int n = modes_count() > 10 ? 10 : modes_count();
    int v = m->items[0].value;
    if (v < mode_top) mode_top--;
    if (n + mode_top <= v) mode_top++;
}

/* 0x4f2430: the mode chosen into the settings (0x405c20-0x405c60), the display reset; back */
void fe_4f2430(Menu *m, MenuItem *it)
{
    (void)it;
    int w, h, bpp;
    mode_get(m->items[0].value, &w, &h, &bpp);
    gw32(0x52afc4, (uint32_t)w), gw32(0x52afc8, (uint32_t)h), gw32(0x52afcc, (uint32_t)bpp);
    Menu_Goto(m->parent);
}

/* 0x4e1d70: the scroll bar (a faint track, the visible part brighter) and up to ten modes
   "WxH:Bbit" down the middle, the chosen lit */
void fe_4e1d70(Menu *m)
{
    int w, h;
    screen(&w, &h);
    static const uint8_t track[4] = {0xff, 0xff, 0xff, 0x20}, thumb[4] = {0xff, 0xff, 0xff, 0x80};
    Fe_Header();
    Fe_Titles(m, w * 0x18 / 0x280, h * 0x26 / 0x1e0, true, 2, NULL, -1);
    int n = modes_count() > 10 ? 10 : modes_count();
    int16_t r[4] = {(int16_t)(w / 2 + 0x50), 0x5f, 0x14, 0xd2};
    Rect_Fill(r, track, 1);
    int16_t t[4] = {(int16_t)(w / 2 + 0x55), (int16_t)((uint32_t)(mode_top * 100) / (uint32_t)modes_count() * 200 / 100 + 100),
                    10, (int16_t)((uint32_t)(n * 100) / (uint32_t)modes_count() * 200 / 100)};
    Rect_Fill(t, thumb, 1);
    for (int i = 0; i < n; i++) {
        int mw, mh, mb;
        char b[64];
        mode_get(i + mode_top, &mw, &mh, &mb);
        snprintf(b, sizeof b, exe_str(0x524e04), mw, mh, mb);
        Font_Print(1, b, w / 2, (i * 5 + 0x19) * 4, gm(i + mode_top == m->items[0].value ? 0x524968 : 0x52496c), 10);
    }
    Strip_DrawMain(&pg_81d6d8, false, NULL);
}
