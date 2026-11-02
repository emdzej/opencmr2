/* The controller pages (0x4fba80-0x4ff420): Controllers (0x828c80: a player's set-up), the device's
   Configuration (0x829140: the device, analogue steering and pedals, force feedback, the pedal axis
   swapped, steering damping), the actions' Configuration (0x8288c0: a key, button or axis for each of
   the ten), Analogue Setup (0x828aa0: each axis' dead zone and saturation) and the force feedback
   set-up (0x828e60). They work on a copy of the configuration records (0x829448, Ctl_Configure's six)
   and of the players' devices (0x82a7c8), kept when a page is left forward. */
#include "exe.h"
#include "fe/fe.h"
#include "fe/fe_page.h"
#include "fe/fe_res.h"
#include "fe/fe_strip.h"
#include "fe/pages.h"
#include "font.h"
#include "game/ctlcfg.h"
#include "game/input.h"
#include "game/mem.h"
#include "render/d3d7.h"
#include "render/twod.h"
#include "text.h"
#include <stdio.h>
#include <string.h>

enum {
    WORK = 0x829448,    /* the configuration records' copy (0x11a0 bytes) */
    AXES0 = 0x829428,   /* the axes' positions when a capture began (8 words) */
    WAXES = 0x82a5e8,   /* the analogue page's axes (8 of an input axis' five words) */
    BUF = 0x82a788,     /* a binding's text */
    WDEV = 0x82a7c8,    /* the players' devices' copy (8 words) */
    LASTDEV = 0x82a7d8, /* the device item's last value */
    REENUM = 0x82a7dc,  /* a search for devices asked (never set) */
    KEY0 = 0x82a7e0,    /* the key down when a capture began, -1 none */
    CAPTURE = 0x82a7e4, /* an action waiting for its key, button or axis */
    ADJUST = 0x82a7e8,  /* an axis being adjusted */
    PLAYER = 0x82a7ec,  /* the player being set up (a word) */
};
enum { C68 = 0x524968, C6c = 0x52496c, C70 = 0x524970, C74 = 0x524974, C78 = 0x524978, C7c = 0x52497c };

static void screen(int *w, int *h) { D3d_ScreenSize(w, h); }
static int player(void) { return gm16(PLAYER); }                          /* 0x4fba80 */
static int wdev(void) { return gm16(WDEV + 2u * (uint32_t)player()); }
static uint32_t wcfg(int d) { return WORK + CTL_SIZE * (uint32_t)d; }
static InputRecord *wrec(void) { return Input_Record(wdev()); }
static int32_t w32(uint32_t c, uint32_t off) { return (int32_t)gm32(c + off); }

/* 0x4fba90: the two players and OK selectable */
void fe_4fba90(Menu *m, bool back)
{
    (void)back;
    for (int i = 0; i < 8; i++) m->items[i].flags |= 3;
}

/* 0x4fbab0: the device's name */
static const char *device_name(void) { return (const char *)gm(wcfg(wdev()) + 8); }

/* 0x4fbae0: what an action is bound to, as the page shows it: the keyboard's key name; else the
   button's or the hat's name (the bits 1, 2, 4 and 8 a joystick's directions), the axis ("Axis %d")
   when analogue, or the key name kept */
static const char *binding(int a)
{
    InputRecord *r = wrec();
    uint32_t c = wcfg(wdev());
    const char *keyname = (const char *)gm(c + 0x147 + 0x14u * (uint32_t)a);
    if (r->device == DEV_KEYBOARD) return keyname;
    if (gm16(c + 0x128 + 2u * (uint32_t)a) && gm8(c + 0x13e + (uint32_t)a)) return keyname;   /* 0x40c270 */
    uint16_t s = gm16(c + 0x128 + 2u * (uint32_t)a);
    /* a button's name in the record (+0x284 every 0x14; past the twentieth, the hat's) */
    const char *button = NULL;
    int b = Input_ButtonIndex(s);
    if (b != -1) button = (const char *)r + 0x284 + 0x14 * b;
    const char *none = (const char *)gm(0x52ea5c);
    if (!w32(c, 0x210 + 0x14u * (uint32_t)a) || r->device != DEV_PAD) {
        if (r->device == DEV_JOYSTICK) {
            if (s == 1) return Text_Get(0x1f9);
            if (s == 2) return Text_Get(0x1fa);
            if (s == 4) return Text_Get(0x1fb);
            if (s == 8) return Text_Get(0x1fc);
        }
        if (r->device == DEV_MOUSE && (a == 0 || a == 1)) return Text_Get(a == 0 ? 0x1f9 : 0x1fa);
        if (button) return button;
    } else if (!w32(c, 0x110) && (a == 0 || a == 1)) {
        if (button) {
            if (strcmp(button, none)) return button;
            return Text_Get(s == 1 ? 0x1f9 : 0x1fa);
        }
    } else if (w32(c, 0x114) || (a != 2 && a != 3)) {
        snprintf((char *)gm(BUF), 0x40, Text_Get(0x77), gm16(c + 0x2d8 + 2u * (uint32_t)a));
        return (const char *)gm(BUF);
    } else if (button) {
        if (strcmp(button, none)) return button;
        return Text_Get(s == 4 ? 0x1fb : 0x1fc);
    }
    return keyname;
}

/* 0x4fbe60: the analogue page's axis with the device's position now */
static const InputAxis *work_axis(int i)
{
    gw32(WAXES + 0x14u * (uint32_t)i + 0x10, (uint32_t)Input_Record(wdev())->axis[i].value);
    return (const InputAxis *)gm(WAXES + 0x14u * (uint32_t)i);
}

/* 0x4fbea0: the player chosen */
void fe_4fbea0(Menu *m, MenuItem *it)
{
    (void)it;
    gw16(PLAYER, (uint16_t)m->sel);
}

/* 0x4fbec0: an action chosen: the page held while its key, button or axis is waited for (0x4fc070),
   the key down and the axes' positions now kept */
void fe_4fbec0(Menu *m, MenuItem *it)
{
    (void)it;
    gw32(CAPTURE, 1);
    Menu_SetFlags(m, false, false, false, false);
    Input_PollAll();
    Ctl_AxisPresses();
    gw32(KEY0, (uint32_t)Input_KeyDown());
    Input_FlushChars();   /* 0x4b7d40: the key messages */
    InputRecord *r = wrec();
    for (uint32_t i = 0; i < 8; i++) gw32(AXES0 + 4 * i, 0);
    if (r->device == DEV_PAD || r->device == DEV_MOUSE)
        for (uint32_t i = 0; i < 8; i++)
            if (r->axis[i].present) gw32(AXES0 + 4 * i, (uint32_t)r->axis[i].value);
}

/* 0x4fbf60: Left, Right and Pause selectable; Pause not on the keyboard, the mouse or a device of
   fewer than four buttons; the steering not on the mouse */
void fe_4fbf60(Menu *m, bool back)
{
    if (back) return;
    InputRecord *r = wrec();
    m->items[0].flags |= 3;
    m->items[1].flags |= 3;
    m->items[9].flags |= 3;
    if (r->device == DEV_KEYBOARD || r->device == DEV_MOUSE || r->buttons < 4) m->items[9].flags &= 0xfc;
    if (r->device == DEV_MOUSE) m->items[0].flags &= 0xfc, m->items[1].flags &= 0xfc;
}

/* 0x4fbff0: back to the device's page; forward the records kept and the player's keys bound, then a
   game controller's analogue set-up */
void fe_4fbff0(Menu *m, bool back)
{
    (void)m;
    if (back) {
        Menu_Goto(&pg_829140);
        return;
    }
    memcpy(gm(CTL_CONFIGS), gm(WORK), 0x11a0);
    Ctl_Bind(player());
    if (wrec()->device == DEV_PAD) Menu_Goto(&pg_828aa0);
}

/* 0x4fc490: the action is on an axis (the steering's or the pedals', analogue) */
static bool axis_action(const Menu *m)
{
    uint32_t c = wcfg(wdev());
    if (w32(c, 0x110) && (m->sel == 0 || m->sel == 1)) return true;
    return w32(c, 0x114) && (m->sel == 2 || m->sel == 3);
}

/* 0x4fcc50: the action's bits and key taken off the other actions (the loop of ten reads the ninth
   key's byte past them, the first name's) */
static void unbind_others(uint32_t c, int a)
{
    for (int i = 0; i < 10; i++) {
        if (i == a) continue;
        if (gm16(c + 0x128 + 2u * (uint32_t)i) == gm16(c + 0x128 + 2u * (uint32_t)a)) gw16(c + 0x128 + 2u * (uint32_t)i, 0);
        if (gm8(c + 0x13e + (uint32_t)i) == gm8(c + 0x13e + (uint32_t)a)) {
            gw8(c + 0x13e + (uint32_t)i, 0);
            gw8(c + 0x147 + 0x14u * (uint32_t)i, 0);
        }
    }
}

/* 0x4fc070: the capture: a key pressed once none is held (not for an action on an axis), its name
   and key kept (and the bits cleared off the keyboard); an axis moved past 0x4ccc from where it was
   (for one), kept in the action's slot; a single button's bit (not on the keyboard); then the page
   free again */
void fe_4fc070(Menu *m)
{
    if (!gm32(CAPTURE)) return;
    bool done = false;
    Menu_SetFlags(m, false, false, false, false);
    InputRecord *r = wrec();
    int a = m->sel;
    uint32_t c = wcfg(wdev());
    int k = Input_KeyDown();
    int32_t k0 = (int32_t)gm32(KEY0);
    if (k0 == -1 && k != -1 && !axis_action(m)) {
        done = true;
        Input_KeyName(k, (char *)gm(c + 0x147 + 0x14u * (uint32_t)a), 0x14);   /* 0x49ed80 */
        Ctl_SetKey(c, a, (uint8_t)k);
        if (r->device != DEV_KEYBOARD) Ctl_SetBit(c, a, 0);
    } else if (k0 != -1)
        gw32(KEY0, (uint32_t)k);
    bool steer = w32(c, 0x110) && (a == 0 || a == 1);
    if (((w32(c, 0x114) && (a == 2 || a == 3)) || steer) && (r->device == DEV_PAD || r->device == DEV_MOUSE)) {
        for (int i = 0; i < 8; i++) {
            int32_t d = (int32_t)gm32(AXES0 + 4u * (uint32_t)i) - r->axis[i].value;
            if (!r->axis[i].present || (d < 0 ? -d : d) <= 0x4ccc) continue;
            done = true;
            memcpy(gm(c + 0x210 + 0x14u * (uint32_t)a), &r->axis[i], 0x14);
            gw16(c + 0x2d8 + 2u * (uint32_t)a, (uint16_t)i);
            Ctl_SetBit(c, a, 0);
            Ctl_SetKey(c, a, 0);
            gw32(AXES0 + 4u * (uint32_t)i, (uint32_t)r->axis[i].value);
        }
    }
    if (r->device == DEV_PAD || r->device == DEV_MOUSE) {
        if (w32(c, 0x110)) r->pressed &= ~3u;
        if (w32(c, 0x114)) r->pressed &= ~0xcu;
    }
    int bits = 0;
    for (uint32_t p = r->pressed; p; p >>= 1) bits += p & 1;
    if (bits == 1 && r->device != DEV_KEYBOARD && !axis_action(m)) {
        Ctl_SetBit(c, a, (uint16_t)r->pressed);
        Ctl_SetKey(c, a, 0);
    } else if (!done)
        return;
    Input_PollAll();
    Ctl_AxisPresses();
    gw32(CAPTURE, 0);
    Menu_SetFlags(m, true, true, true, true);
    unbind_others(c, a);
}

/* 0x4fc500: the device's axes selectable when there, each with its record's settings (from the slots
   that use it); OK on to the force feedback set-up when it is on */
void fe_4fc500(Menu *m, bool back)
{
    (void)back;
    int d = wdev();
    InputRecord *r = Input_Record(d);
    uint32_t real = CTL_CONFIGS + CTL_SIZE * (uint32_t)d;
    for (int i = 0; i < 8; i++) {
        if (!r->axis[i].present) {
            m->items[i].flags &= 0xfc;
            continue;
        }
        m->items[i].flags |= 3;
        for (uint32_t k = 0; k < 10; k++)
            if (gm16(real + 0x2d8 + 2 * k) == (uint16_t)i && gm32(real + 0x210 + 0x14 * k))
                memcpy(gm(WAXES + 0x14u * (uint32_t)i), gm(real + 0x210 + 0x14 * k), 0x14);
    }
    m->items[8].target = gm32(wcfg(d) + 0x118) ? &pg_828e60 : &pg_828c80;
}

/* 0x4fc620: adjusting an axis with the first keyboard: Return keeps the settings (into the device and
   the slots), Escape takes the axis' back; the arrows move the saturation (left up, right down), with
   shift the dead zone (left down, right up), by 50 within 50-10000 */
void fe_4fc620(Menu *m)
{
    if (gm32(ADJUST)) {
        const InputRecord *k0 = Input_Record(0);
        int d = wdev(), a = m->sel;
        if (k0->pressed & 0x10) {
            uint32_t real = CTL_CONFIGS + CTL_SIZE * (uint32_t)d;
            for (int i = 0; i < 8; i++) {
                uint32_t w = WAXES + 0x14u * (uint32_t)i;
                Input_SetSat(d, i, (int32_t)gm32(w + 0xc));
                Input_SetDead(d, i, (int32_t)gm32(w + 8));
                for (uint32_t k = 0; k < 10; k++)
                    if (gm16(real + 0x2d8 + 2 * k) == (uint16_t)i && gm32(real + 0x210 + 0x14 * k))
                        memcpy(gm(real + 0x210 + 0x14 * k), gm(w), 0x14);
            }
            gw32(ADJUST, 0);
        }
        uint32_t w = WAXES + 0x14u * (uint32_t)a;
        if (k0->pressed & 0x20) {
            const InputRecord *r = Input_Record(d);
            gw32(w + 8, (uint32_t)r->axis[a].dead);
            gw32(ADJUST, 0);
            gw32(w + 0xc, (uint32_t)r->axis[a].sat);
        }
        int32_t sat = (int32_t)gm32(w + 0xc), dead = (int32_t)gm32(w + 8);
        if (!Input_ShiftDown()) {
            if (k0->held & 1) {
                if (sat < 0x26de) gw32(w + 0xc, (uint32_t)(sat + 0x32));
            } else if ((k0->held & 2) && sat > 0x32)
                gw32(w + 0xc, (uint32_t)(sat - 0x32));
        } else if (k0->held & 1) {
            if (dead > 0x32) gw32(w + 8, (uint32_t)(dead - 0x32));
        } else if ((k0->held & 2) && dead < 0x26de)
            gw32(w + 8, (uint32_t)(dead + 0x32));
        if (gm32(ADJUST)) return;
    }
    Menu_SetFlags(m, true, true, true, true);
}

/* 0x4fc850: an axis chosen: the page held while it is adjusted */
void fe_4fc850(Menu *m, MenuItem *it)
{
    (void)it;
    gw32(ADJUST, 1);
    Menu_SetFlags(m, false, false, false, false);
    Input_PollAll();
    Ctl_AxisPresses();
}

/* 0x4fc880: the force feedback page from the player's device: the two forces in tenths, the quick
   update (0 on) */
void fe_4fc880(Menu *m, bool back)
{
    (void)back;
    int p = player();
    m->items[0].value = (uint8_t)((Ctl_Setting1(p) * 10) / 0x10000);
    m->items[1].value = (uint8_t)((Ctl_Setting0(p) * 10) / 0x10000);
    m->items[2].value = Ctl_QuickUpdate(p) ? 0 : 1;
}

/* 0x4fc8f0 */
void fe_4fc8f0(Menu *m, bool back)
{
    if (back) return;
    int p = player();
    Ctl_SetSetting1(p, (int32_t)(((uint32_t)m->items[0].value << 16) / 10));
    Ctl_SetSetting0(p, (int32_t)(((uint32_t)m->items[1].value << 16) / 10));
    Ctl_SetQuickUpdate(p, 1 - m->items[2].value);
}

/* 0x4fc970: forward, the players' devices and the records kept */
void fe_4fc970(Menu *m, bool back)
{
    (void)m;
    if (back) return;
    for (int p = 0; p < 8; p++) Ctl_SetDevice(p, gm16(WDEV + 2u * (uint32_t)p));
    memcpy(gm(CTL_CONFIGS), gm(WORK), 0x11a0);
}

/* 0x4fcb30: the device's page from the player's device (its own when the device has no record): the
   analogue items and the swap and damping only for a game controller, force feedback when it has it */
static void device_items(void)
{
    Menu *pg = &pg_829140;
    int p = player(), d = wdev();
    pg->items[0].value = (uint8_t)d;
    InputRecord *r = Input_Poll(d);
    if (!r) {
        gw16(WDEV + 2u * (uint32_t)p, (uint16_t)p);
        r = Input_Poll(p);
        d = p;
    }
    bool pad = r->device == DEV_PAD;
    uint32_t c = wcfg(d);
    for (int i = 1; i <= 5; i++)
        if (i != 3) pg->items[i].flags = (uint8_t)((pg->items[i].flags & ~1u) | pad);
    pg->items[1].value = gm8(c + 0x110);
    pg->items[2].value = gm8(c + 0x114);
    pg->items[3].flags = (uint8_t)((pg->items[3].flags & ~1u) | (r->ffb != 0));
    pg->items[3].value = gm32(c + 0x118) ? 0 : 1;
    pg->items[4].value = gm32(c + 0x120) ? 0 : 1;
    pg->items[5].value = gm32(c + 0x124) ? 0 : 1;
}

/* 0x4fc9b0: the devices configured, the players' devices and the records copied */
void fe_4fc9b0(Menu *m, bool back)
{
    (void)m, (void)back;
    Ctl_Configure();
    for (int p = 0; p < 8; p++) gw16(WDEV + 2u * (uint32_t)p, (uint16_t)Ctl_Device(p));
    memcpy(gm(WORK), gm(CTL_CONFIGS), 0x11a0);
    device_items();
}

/* 0x4fc9f0: no devices, back to the options; the items into the record while the device stays; a
   device changed: the first keyboard only the first player's and the second only the second's */
void fe_4fc9f0(Menu *m)
{
    if (Ctl_Count() == 0) {
        Menu_Goto(&pg_81f118);
        return;
    }
    if (!gm32(0x663db4)) {
        if (gm32(REENUM)) {
            Input_Reinit();   /* 0x49eb50 */
            Ctl_Configure();
            fe_4fc9b0(m, false);
            gw32(REENUM, 0);
        }
    } else if (gm32(REENUM))
        return;
    uint8_t v = m->items[0].value;
    if (v == gm16(LASTDEV)) {
        uint32_t c = wcfg(wdev());
        gw32(c + 0x110, m->items[1].value);
        gw32(c + 0x114, m->items[2].value);
        gw32(c + 0x118, 1u - m->items[3].value);
        gw32(c + 0x120, 1u - m->items[4].value);
        gw32(c + 0x124, 1u - m->items[5].value);
        return;
    }
    if (v == 0 && player() == 1) m->items[0].value = 1;
    if (m->items[0].value == 1 && player() == 0) m->items[0].value = gm16(LASTDEV) ? 0 : 2;
    gw16(WDEV + 2u * (uint32_t)player(), m->items[0].value);
    device_items();
    gw16(LASTDEV, m->items[0].value);
}

/* ---- the draws ---- */

static void sep(int16_t *r, uint32_t hi, uint32_t lo)
{
    Rect_Fill(r, gm(hi), 1);
    r[1]++;
    Rect_Fill(r, gm(lo), 1);
}

static void arrow(bool on, int16_t y, uint32_t c)
{
    int w, h;
    screen(&w, &h);
    Tex *t = on ? g_fe.arrow_a : g_fe.arrow_d;
    if (!t) return;
    int16_t ah = g_fe.arrow_a ? (int16_t)g_fe.arrow_a->height : 0, aw = g_fe.arrow_a ? (int16_t)g_fe.arrow_a->width : 0;
    Rect16 src = {0, 0, t->width, t->height};
    Rect16 dst = {(int16_t)(w * 100 / 0x280), (int16_t)(y - ah / 2), aw, ah};
    Sprite_Add(&src, &dst, t, 1, 0, NULL, NULL, gm(c), 8);
}

/* "Player n Controller" and the page's title */
static void title_row(const Menu *m)
{
    int w, h;
    screen(&w, &h);
    char b[64];
    snprintf(b, sizeof b, Text_Get(100), player() + 1);
    const char *names[2] = {b, Text_Get(m->text)};
    Fe_TitleRow(w * 0x18 / 0x280, h * 0x26 / 0x1e0, names, 2);
}

/* 0x4fccb0: the Controllers page: the selectable rows, "Player n Controller" for the two players,
   OK */
void fe_4fccb0(Menu *m)
{
    int w, h;
    screen(&w, &h);
    Fe_Header();
    Fe_Titles(m, w * 0x18 / 0x280, h * 0x26 / 0x1e0, true, 1, NULL, -1);
    int top = h * 0xaa / 0x1e0, row = h * 0x24 / 0x1e0;
    int16_t r[4] = {(int16_t)(w * 99 / 0x280), (int16_t)top, (int16_t)(w * 0x11a / 0x280), 1};
    sep(r, m->sel == 0 ? C74 : C78, m->sel == 0 ? C68 : C6c);
    for (int i = 0, k = 0; i < m->n; i++) {
        if (!(m->items[i].flags & 2)) continue;
        bool on = m->sel == i;
        uint32_t c = on ? C68 : C6c;
        arrow(on, (int16_t)(h * 0x14 / 0x1e0 + top + row * k), c);
        char b[64];
        if (i < 2) snprintf(b, sizeof b, Text_Get(100), i + 1);
        else snprintf(b, sizeof b, "%s", Text_Get(0x67));
        Font_Print(1, b, w * 0x7a / 0x280, h * 0x18 / 0x1e0 + r[1], gm(c), 0x11);
        bool lit = m->sel == i || m->sel == i + 1;
        r[1] = (int16_t)(row * (k + 1) + top);
        sep(r, lit ? C74 : C78, lit ? C68 : C6c);
        k++;
    }
    Strip_DrawMain(&pg_81d6d8, false, NULL);   /* 0x4f8410 */
}

/* 0x4fd080: the actions and what each is bound to ("action - binding"; the one waiting alone) */
void fe_4fd080(Menu *m)
{
    int w, h;
    screen(&w, &h);
    Fe_Header();
    title_row(m);
    int top = h * 0x41 / 0x1e0, row = h * 0x24 / 0x1e0;
    int16_t r[4] = {(int16_t)(w * 99 / 0x280), (int16_t)top, (int16_t)(w * 0x11a / 0x280), 1};
    if (m->sel == 0) sep(r, C74, C68);
    else if (!(m->items[0].flags & 1)) sep(r, C7c, C70);
    else sep(r, C78, C6c);
    for (int i = 0, k = 0; i < m->n; i++) {
        MenuItem *it = &m->items[i];
        if (!(it->flags & 1)) continue;
        bool on = m->sel == i;
        uint32_t c = on ? C68 : C6c;
        arrow(on, (int16_t)(h * 0x14 / 0x1e0 + top + row * k), c);
        char b[160];
        if (i < 10 && !(gm32(CAPTURE) && on)) snprintf(b, sizeof b, exe_str(0x517dd8), Text_Get(it->text), binding(i));
        else snprintf(b, sizeof b, "%s", Text_Get(it->text));
        Font_Print(1, b, w * 0x7a / 0x280, h * 0x18 / 0x1e0 + r[1], gm(c), 0x11);
        bool lit = m->sel == i || m->sel == i + 1;
        r[1] = (int16_t)(row * (k + 1) + top);
        sep(r, lit ? C74 : C78, lit ? C68 : C6c);
        k++;
    }
}

/* 0x4ff0f0: an axis' gauge: a bar 200 of 640 wide centred on x; the dead zone's and the saturation's
   marks either side, the position's */
static void gauge(int x, int y, uint32_t colour, const InputAxis *a)
{
    int w, h;
    screen(&w, &h);
    int bw = w * 200 / 0x280, bh = h * 10 / 0x1e0;
    int16_t r[4] = {(int16_t)(x - w * 200 / 0x500), (int16_t)(y - h * 10 / 0x3c0), (int16_t)bw, (int16_t)bh};
    Rect_Fill(r, (const uint8_t *)&colour, 1);
    if (!a) return;
    int16_t my = (int16_t)(y - h * 10 / 0x3c0 - 2), mh = (int16_t)(bh + 4);
    int dz = a->dead * bw / 10000 / 2, sat = a->sat * bw / 10000 / 2;
    int v = (int)(((int64_t)bw * a->value / 2) / 0x10000);
    int xs[5] = {x - dz, x + dz, x - sat, x + sat, x + v};
    for (int i = 0; i < 5; i++) {
        int16_t q[4] = {(int16_t)xs[i], my, 2, mh};
        Rect_Fill(q, gm(C68), 1);
    }
}

/* 0x4ff060: the gauge of item i: grey when not selectable; the one adjusted red */
static void item_gauge(int x, int y, const Menu *m, int i)
{
    const InputAxis *a = NULL;
    uint32_t c;
    if (!(m->items[i].flags & 2))
        c = gm32(C70);
    else {
        a = work_axis(i);
        if (m->sel == i) c = gm32(ADJUST) ? 0xff1414f0u : gm32(C68);
        else {
            c = gm32(C6c);
            if (!(m->items[i].flags & 1)) return;
        }
    }
    gauge(x, y, c, a);
}

/* 0x4fd480: Analogue Setup: "Axis n" and its gauge for each axis, OK; the help at the bottom */
void fe_4fd480(Menu *m)
{
    int w, h;
    screen(&w, &h);
    Fe_Header();
    title_row(m);
    int top = h * 0x4b / 0x1e0;
    static int16_t r[4];   /* DAT_0082af70 */
    r[0] = (int16_t)(w * 99 / 0x280), r[1] = (int16_t)top, r[2] = (int16_t)(w * 0x11a / 0x280), r[3] = 1;
    sep(r, m->sel == 0 ? C74 : C78, m->sel == 0 ? C68 : C6c);
    int widest = 0;
    char b[64];
    for (int i = 0; i < m->n; i++) {
        snprintf(b, sizeof b, Text_Get(0x77), i);
        int tw = Font_TextWidth(1, b);
        if (widest < tw) widest = tw;
    }
    int ah = g_fe.arrow_a ? g_fe.arrow_a->height : 0;
    for (int i = 0; i < m->n; i++) {
        MenuItem *it = &m->items[i];
        int ay = h * 2 / 0x1e0 + h * 0x12 / 0x1e0 + top + (h * 0x24 / 0x1e0) * i - ah / 2;
        bool on = m->sel == i;
        uint32_t c = on ? C68 : (it->flags & 1) ? C6c : C70;
        arrow(on, (int16_t)(ay + ah / 2), c);
        int y = h * 0x18 / 0x1e0 + r[1];
        if (i < m->n - 1) {
            snprintf(b, sizeof b, Text_Get(0x77), i);
            Font_Print(1, b, w * 0x7a / 0x280, y, gm(c), 0x11);
            item_gauge(w * 0x7a / 0x280 + widest + w * 5 / 0x280 + w * 200 / 0x500, h * 10 / 0x1e0 + ay, m, i);
        } else
            Font_Print(1, Text_Get(it->text), w * 0x7a / 0x280, y, gm(c), 0x11);
        bool lit = m->sel == i + 1 || m->sel == i;
        r[1] = (int16_t)((h * 0x24 / 0x1e0) * (i + 1) + top);
        sep(r, lit ? C74 : C78, lit ? C68 : C6c);
    }
    int x = w * 0x18 / 0x280;
    if (gm32(ADJUST)) {
        Font_Print(1, Text_Get(0x207), x, h * 0x1a4 / 0x1e0, gm(C6c), 9);
        Font_Print(1, Text_Get(0x208), x, h * 0x1b8 / 0x1e0, gm(C6c), 9);
        Font_Print(1, Text_Get(0x209), x, h * 0x1cc / 0x1e0, gm(C6c), 9);
    } else
        Font_Print(1, Text_Get(0x20a), x, h * 0x1a4 / 0x1e0, gm(C6c), 9);
}

/* a row's two choices after its label: the item's value 0 the first lit */
static void choices(const MenuItem *it, int x, int y, int first, int second, uint32_t lit, uint32_t other)
{
    int w, h;
    screen(&w, &h);
    Font_Print(1, Text_Get(first), x, y, gm(it->value == 0 ? lit : other), 0x11);
    int x2 = w * 10 / 0x280 + x + Font_TextWidth(1, Text_Get(first));
    Font_Print(1, Text_Get(second), x2, y, gm(it->value == 0 ? other : lit), 0x11);
}

/* the rows' colours: the arrow and label's, the value chosen's, the other's */
static void row_colours(const Menu *m, int i, uint32_t *label, uint32_t *lit, uint32_t *other)
{
    if (m->sel == i) *label = C68, *lit = C68, *other = C6c;
    else if (!(m->items[i].flags & 1)) *label = *lit = *other = C70;
    else *label = C6c, *lit = C68, *other = C6c;
}

/* 0x4fdb10: the force feedback page: the two forces ("name n%"), the quick update's On and Off, OK */
void fe_4fdb10(Menu *m)
{
    int w, h;
    screen(&w, &h);
    Fe_Header();
    title_row(m);
    int row = h * 0x24 / 0x1e0;
    int top = (h * 8 / 0x1e0 + h * 0x26 / 0x1e0 + h * 0x180 / 0x1e0) / 2 - row * m->n / 2;
    static int16_t r[4];
    r[0] = (int16_t)(w * 99 / 0x280), r[1] = (int16_t)top, r[2] = (int16_t)(w * 0x11a / 0x280), r[3] = 1;
    sep(r, m->sel == 0 ? C74 : C78, m->sel == 0 ? C68 : C6c);
    for (int i = 0; i < m->n; i++) {
        MenuItem *it = &m->items[i];
        uint32_t label, lit, other;
        row_colours(m, i, &label, &lit, &other);
        arrow(m->sel == i, (int16_t)(h * 2 / 0x1e0 + h * 0x12 / 0x1e0 + top + (h * 0x24 / 0x1e0) * i), label);
        int x = w * 0x7a / 0x280, y = h * 0x18 / 0x1e0 + r[1];
        char b[96];
        switch (it->id) {
        case 0:
        case 1:
            snprintf(b, sizeof b, exe_str(0x526ec8), Text_Get(it->text), it->value * 10);
            Font_Print(1, b, x, y, gm(label), 0x11);
            break;
        case 2:
            Font_Print(1, Text_Get(it->text), x, y, gm(label), 0x11);
            choices(it, w * 10 / 0x280 + x + Font_TextWidth(1, Text_Get(it->text)), y, 0x134, 0x133, lit, other);
            break;
        case 3:
            Font_Print(1, Text_Get(it->text), x, y, gm(label), 0x11);
            break;
        }
        bool l = m->sel == i + 1 || m->sel == i;
        r[1] = (int16_t)(row * (i + 1) + top);
        sep(r, l ? C74 : C78, l ? C68 : C6c);
    }
    Strip_DrawMain(&pg_81d6d8, false, NULL);
}

/* 0x4fe240: the device's page: "< name >", Steering and Accelerate / Brake (Digital, Analogue), Force
   Feedback (On, Off), the swap and damping (Yes, No), OK */
void fe_4fe240(Menu *m)
{
    int w, h;
    screen(&w, &h);
    Fe_Header();
    title_row(m);
    int top = h * 100 / 0x1e0, row = h * 0x24 / 0x1e0;
    int16_t r[4] = {(int16_t)(w * 99 / 0x280), (int16_t)top, (int16_t)(w * 0x11a / 0x280), 1};
    if (m->sel == 0) sep(r, C74, C68);
    else if (!(m->items[0].flags & 1)) sep(r, C7c, C70);
    else sep(r, C78, C6c);
    for (int i = 0; i < m->n; i++) {
        MenuItem *it = &m->items[i];
        uint32_t label, lit, other;
        row_colours(m, i, &label, &lit, &other);
        arrow(m->sel == i, (int16_t)(h * 0x14 / 0x1e0 + top + row * i), label);
        int x = w * 0x7a / 0x280, y = h * 0x18 / 0x1e0 + r[1];
        char b[300];
        const char *s = NULL;
        switch (it->id) {
        case 0:
            snprintf(b, sizeof b, exe_str(0x526ed8), device_name());
            Font_Print(1, b, x, y, gm(label), 0x11);
            break;
        case 1:
        case 2:
            if (it->id == 1) s = Text_Get(0x197);
            else snprintf(b, sizeof b, exe_str(0x526ed0), Text_Get(0x6d), Text_Get(0x6e)), s = b;
            Font_Print(1, s, x, y, gm(label), 0x11);
            choices(it, Font_TextWidth(1, s) + w * 10 / 0x280 + x, y, 0x69, 0x68, lit, other);
            break;
        case 3:
        case 4:
        case 5:
            Font_Print(1, Text_Get(it->text), x, y, gm(label), 0x11);
            choices(it, Font_TextWidth(1, Text_Get(it->text)) + w * 10 / 0x280 + x, y, it->id == 3 ? 0x134 : 5,
                    it->id == 3 ? 0x133 : 4, lit, other);
            break;
        case 6:
            Font_Print(1, Text_Get(it->text), x, y, gm(label), 0x11);
            break;
        }
        bool l = m->sel == i || m->sel == i + 1;
        r[1] = (int16_t)(row * (i + 1) + top);
        sep(r, l ? C74 : C78, l ? C68 : C6c);
    }
    Strip_DrawMain(&pg_81d6d8, false, NULL);
}
