/* The controller configuration (see ctlcfg.h), ported from 0x40bad0-0x40cc60. */
#include "game/ctlcfg.h"
#include "game/mem.h"
#include "text.h"
#include "exe.h"
#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { DEVICES = 0x5168f4, COUNT = 0x516904, LOADED = 0x5334f0, PRESSES = 0x5334f4 };

static uint32_t cfg(int d) { return CTL_CONFIGS + CTL_SIZE * (uint32_t)d; }

int Ctl_Count(void) { return gm16(COUNT); }                                         /* 0x40bba0 */
int Ctl_Device(int player) { return gm16(DEVICES + 2u * (uint32_t)(player & 0xffff)); }   /* 0x40bbc0 */
void Ctl_SetDevice(int player, int d) { gw16(DEVICES + 2u * (uint32_t)(player & 0xffff), (uint16_t)d); }   /* 0x40bbe0 */
uint32_t Ctl_Config(int player) { return cfg(Ctl_Device(player)); }
int32_t Ctl_Setting0(int player) { return (int32_t)gm32(Ctl_Config(player)); }               /* 0x40bc00 */
int32_t Ctl_Setting1(int player) { return (int32_t)gm32(Ctl_Config(player) + 4); }           /* 0x40bc30 */
void Ctl_SetSetting0(int player, int32_t v) { gw32(Ctl_Config(player), (uint32_t)v); }        /* 0x40bc60 */
void Ctl_SetSetting1(int player, int32_t v) { gw32(Ctl_Config(player) + 4, (uint32_t)v); }    /* 0x40bd00 */
void Ctl_SetQuickUpdate(int player, uint32_t on) { gw32(Ctl_Config(player) + 0x11c, on); }   /* 0x40bc90 */
bool Ctl_QuickUpdate(int player) { return gm32(Ctl_Config(player) + 0x11c) != 0; }           /* 0x40bcd0 */
bool Ctl_ForceFeedback(int player) { return gm32(Ctl_Config(player) + 0x118) != 0; }         /* 0x40bd30 */
bool Ctl_AnalogSteer(int player) { return gm32(Ctl_Config(player) + 0x110) != 0; }           /* 0x40bdd0 */
bool Ctl_AnalogPedals(int player) { return gm32(Ctl_Config(player) + 0x114) != 0; }          /* 0x40be00 */
bool Ctl_Swap(int player) { return gm32(Ctl_Config(player) + 0x120) != 0; }                  /* 0x40be30 */
bool Ctl_Damping(int player) { return gm32(Ctl_Config(player) + 0x124) != 0; }               /* 0x40be60 */

/* 0x40bd60: a player on a game controller (a device past the two keyboard players, of input type 0
   or 3) gets its bits added to the record */
void Ctl_Merge(int player, InputRecord *r)
{
    int d = Ctl_Device(player);
    if (d == 0 || d == 1) return;
    const InputRecord *j = Input_Record(d);
    if (j->device != DEV_JOYSTICK && j->device != DEV_PAD) return;
    r->pressed |= j->pressed;
    r->held |= j->held;
    r->repeat |= j->repeat;
}

/* 0x40c530: the first bit clear */
static int first_clear(uint32_t m)
{
    int i = 0;
    while (i < 0x20 && (m & 1)) m >>= 1, i++;
    return i;
}

/* 0x40c440: on a game controller's record, each action with a key but no bit gets the first bit no
   action has (nor 0x400), its key bound to that bit in the player's record and taken off its own */
static void spare_bits(int player, uint32_t c)
{
    for (int i = 0; i < 10; i++) {
        if (gm16(c + 0x128 + 2u * (uint32_t)i) || !gm8(c + 0x13e + (uint32_t)i)) continue;
        /* (the original's union leaves out the rear view's word, +0x138) */
        uint32_t used = 0x400;
        for (int k = 0; k < 10; k++)
            if (k != 8) used |= gm16(c + 0x128 + 2u * (uint32_t)k);
        uint32_t bit = 1u << (first_clear(used) & 0x1f);
        Input_BindKey(player, bit & 0xffff, gm8(c + 0x13e + (uint32_t)i));
        Input_BindKey(player, (1u << i) & 0xffff, 0);
        gw16(c + 0x128 + 2u * (uint32_t)i, (uint16_t)bit);
    }
}

/* 0x40be90 */
void Ctl_Bind(int player)
{
    int d = Ctl_Device(player);
    uint32_t c = cfg(d);
    if (d > 1) spare_bits(player, c);
    for (int k = 0; k < 9; k++) Input_BindKey(player & 0xffff, 1u << k, gm8(c + 0x13e + (uint32_t)k));
}

/* 0x40bf70: Configuration\Controller.rcf, taken when it is 0x11a4 bytes: the six records and the two
   players' devices; then the players' keys bound */
void Ctl_Load(void)
{
    gw32(LOADED, 0);
    size_t n = 0;
    uint8_t *f = plat_load_user_file("Configuration\\Controller.rcf", &n);
    if (!f) return;
    if (n == 0x11a4) {
        for (uint32_t i = 0; i < 0x11a0; i++) gw8(CTL_CONFIGS + i, f[i]);
        for (uint32_t i = 0; i < 4; i++) gw8(DEVICES + i, f[0x11a0 + i]);
        gw32(LOADED, 1);
        Ctl_Bind(0);
        Ctl_Bind(1);
    }
    free(f);
}

/* 0x40bff0 */
void Ctl_Save(void)
{
    static uint8_t b[0x11a4];
    for (uint32_t i = 0; i < 0x11a0; i++) b[i] = gm8(CTL_CONFIGS + i);
    for (uint32_t i = 0; i < 4; i++) b[0x11a0 + i] = gm8(DEVICES + i);
    plat_save_user_file("Configuration\\Controller.rcf", b, sizeof b);
}

static void put_name(uint32_t c, int k, const char *s)
{
    char *d = (char *)gm(c + 0x147 + 0x14u * (uint32_t)k);
    snprintf(d, 0x14, "%s", s);
}

static void copy_axis(uint32_t c, int slot, const InputAxis *a, int axis)
{
    memcpy(gm(c + 0x210 + 0x14u * (uint32_t)slot), a, 0x14);
    gw16(c + 0x2d8 + 2u * (uint32_t)slot, (uint16_t)axis);
}

/* 0x40c610: a record's defaults from its input record */
static void defaults(const InputRecord *r, int i)
{
    uint32_t c = cfg(i);
    gw8(c + 0x2ec, (uint8_t)i);
    /* (the original passes the name to sprintf as the format) */
    snprintf((char *)gm(c + 8), 0x104, "%s", r->name);
    bool pad = r->device == DEV_PAD || r->device == DEV_MOUSE;
    gw32(c + 0x10c, pad);
    gw32(c + 0x110, pad);
    gw32(c + 0x114, pad);
    gw32(c + 0x118, (uint32_t)r->ffb);
    gw32(c + 4, 0x8000);
    gw32(c, 0x8000);
    if (r->device == DEV_KEYBOARD) {
        for (int k = 0; k < 9; k++) gw8(c + 0x13e + (uint32_t)k, r->key[k]);
        if (i == 0) {
            for (int k = 0; k < 5; k++) put_name(c, k, Text_Get(0x1f9 + k));   /* Left, Right, Up, Down, Space */
            static const uint32_t s[4] = {0x516958, 0x516954, 0x516950, 0x51694c};   /* ] [ C R */
            for (int k = 0; k < 4; k++) put_name(c, 5 + k, exe_str(s[k]));
            return;
        }
        /* the keypad's: Numpad 4, 6, 8, 2, 3, Page Up, Page Down, Numpad 9, 7 */
        static const uint32_t pad_keys[9] = {0x516948, 0x51693c, 0x516938, 0x516934, 0x516930, 0, 0, 0x51692c, 0x516928};
        for (int k = 0; k < 9; k++) {
            char b[0x40];
            if (k == 5 || k == 6) snprintf(b, sizeof b, "%s", Text_Get(k == 5 ? 0x204 : 0x205));
            else snprintf(b, sizeof b, exe_str(0x516940), Text_Get(0x203), exe_str(pad_keys[k]));   /* "%s %s" */
            put_name(c, k, b);
        }
        return;
    }
    if (r->device == DEV_MOUSE) return;   /* (no record is ever the mouse's; its branch not ported) */
    static const uint16_t joystick[10] = {1, 2, 0x10, 0x20, 0x40, 0x80, 0x100, 0x200, 0x800, 0x400};
    static const uint16_t controller[10] = {1, 2, 4, 8, 0x10, 0x20, 0x40, 0x80, 0x100, 0x200};
    if (!gm32(c + 0x10c)) {
        /* a traditional joystick: no axes, the buttons for the pedals */
        memset(gm(c + 0x210), 0, 0xc8);
        for (int k = 0; k < 10; k++) gw16(c + 0x128 + 2u * (uint32_t)k, joystick[k]);
        return;
    }
    /* another controller: X steers (slots 0 and 1), Y the pedals (2 and 3), when they are there */
    for (int k = 0; k < 10; k++) gw16(c + 0x128 + 2u * (uint32_t)k, controller[k]);
    if (r->axis[0].present) copy_axis(c, 0, &r->axis[0], 0), copy_axis(c, 1, &r->axis[0], 0);
    if (r->axis[1].present) copy_axis(c, 2, &r->axis[1], 1), copy_axis(c, 3, &r->axis[1], 1);
}

/* 0x40c050: the devices counted; each gets its defaults unless Controller.rcf had a record of the same
   name in its place; the records past them cleared */
void Ctl_Configure(void)
{
    int n = Input_DeviceCount();
    gw16(COUNT, (uint16_t)n);
    for (int i = 0; i < n; i++) {
        const InputRecord *r = Input_Poll(i);
        if (!r) continue;   /* Port: a controller there since the records were made (the original reads 0) */
        if (!gm32(LOADED) || strcmp((const char *)gm(cfg(i) + 8), r->name)) defaults(r, i);
    }
    if (n < 6) memset(gm(cfg(n)), 0, CTL_SIZE * (size_t)(6 - n));
    gw32(LOADED, 1);
}

void Ctl_SetBit(uint32_t c, int action, uint16_t bit)   /* 0x40c130 */
{
    if (action >= 0 && action < 10) gw16(c + 0x128 + 2u * (uint32_t)action, bit);
}

void Ctl_SetKey(uint32_t c, int action, uint8_t key)   /* 0x40c550 */
{
    if (action >= 0 && action < 9) gw8(c + 0x13e + (uint32_t)action, key);
}

int Ctl_Axis(int player, int slot)   /* 0x40c210 */
{
    uint32_t c = Ctl_Config(player);
    if (gm32(c + 0x210 + 0x14u * (uint32_t)slot)) return gm16(c + 0x2d8 + 2u * (uint32_t)slot);
    return -1;
}

/* 0x40c2a0: the record's word for the action; none on the keyboard players' devices the bit 1 << action;
   an action with a key as well (0x40c270) the bit too (its key is in the keyboard record) */
uint16_t Ctl_Mask(int player, int action)
{
    uint32_t c = Ctl_Config(player);
    uint16_t s = action >= 0 && action < 10 ? gm16(c + 0x128 + 2u * (uint32_t)action) : 0;
    bool keyed = s && gm8(c + 0x13e + (uint32_t)action);   /* 0x40c270 */
    if ((!s && (player == 0 || player == 1)) || keyed) return action >= 0 && action < 10 ? (uint16_t)(1u << action) : 0;
    return s;
}

/* an input record's axis position (+0x47c every 0x14; no axis, -1, reads +0x468) */
static int32_t axis_value(int rec, int axis)
{
    return (int32_t)gm32((uint32_t)((int32_t)(0x59ce48 + 0x50c * rec + 0x47c) + 0x14 * axis));
}

/* 0x40bad0: on each game controller (type 3) with separate pedal axes (player 0's slots, whichever
   the record), an axis pushed below 0x3333 from above it gives the press 4 (accelerate) or 8 */
void Ctl_AxisPresses(void)
{
    for (int i = 0; i < 8; i++) {
        InputRecord *r = Input_Record(i);
        uint32_t st = PRESSES + 8u * (uint32_t)i;
        if (r->device != DEV_PAD || Ctl_Axis(0, 3) == Ctl_Axis(0, 2)) continue;
        r->pressed &= ~0xcu;
        if (axis_value(i, Ctl_Axis(0, 2)) < 0x3333) {
            if (!gm32(st)) r->pressed |= 4, gw32(st, 1);
        } else gw32(st, 0);
        if (axis_value(i, Ctl_Axis(0, 3)) < 0x3333) {
            if (!gm32(st + 4)) r->pressed |= 8, gw32(st + 4, 1);
        } else gw32(st + 4, 0);
    }
}
