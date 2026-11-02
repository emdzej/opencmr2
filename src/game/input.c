/* The input records (see input.h), ported from 0x49e930-0x49ff50. DirectInput's part is the port's:
   the keyboard is the platform's (DIK codes), the game controllers gasm's gamepads in their four slots,
   each made a DirectInput controller (DIJOYSTATE): with the standard mapping as DirectInput shows an
   XInput pad (X and Y the left stick, Z the triggers, Rx and Ry the right stick; buttons A, B, X, Y,
   the shoulders, back, start and the stick clicks; the d-pad the hat), else its axes and buttons in
   order. The mouse device (0x49f060, 0x49f360) never gets a record and is not ported. */
#include "game/input.h"
#include "game/mem.h"
#include "game/timer.h"
#include "exe.h"
#include "platform.h"
#include <stdio.h>
#include <string.h>

enum {
    RECORDS = 0x59ce48, KEYSTATE = 0x59f7c8,   /* the keyboard's state, 0x80 a key down (0x49f300) */
    COUNTS = 0x59f8cc,   /* bytes: the next record, the keyboard players (2), the next controller, the
                            controllers counted (0x49ef90) */
    MENU = 0x59f8d0, MENU2 = 0x520870, DELAY = 0x59f8d8, RATE = 0x59f8dc, REPEAT_MASK = 0x520868,
    REPEAT_ADDS = 0x52086c, BUTTON_BITS = 0x520808,
};

InputRecord *Input_Record(int n) { return (InputRecord *)gm(RECORDS + 0x50cu * (uint32_t)n); }   /* 0x49ead0 */

/* 0x49e930, 0x49ea90 */
void Input_SetMenuMode(bool on)
{
    gw32(MENU2, on);
    gw32(MENU, on);
}

/* ---- the controllers: DirectInput's view of a gasm gamepad ---- */

typedef struct {
    int32_t axis[8];
    uint32_t pov;
    uint8_t button[32];
} JoyState;

/* the standard mapping's buttons in DirectInput's order for an XInput pad */
static const uint8_t std_buttons[10] = {0, 1, 2, 3, 4, 5, 8, 9, 10, 11};

static int joy_axes(const PlatGamepad *g, float out[8], bool have[8])
{
    memset(have, 0, 8);
    if (g->standard) {
        out[0] = g->axis[0], out[1] = g->axis[1];
        out[2] = g->button[6] - g->button[7];
        out[3] = g->axis[2], out[4] = g->axis[3];
        out[5] = out[6] = out[7] = 0;
        for (int i = 0; i < 5; i++) have[i] = true;
        return 5;
    }
    int n = g->naxes < 8 ? g->naxes : 8;
    for (int i = 0; i < 8; i++) {
        out[i] = i < n ? g->axis[i] : 0;
        have[i] = i < n;
    }
    return n;
}

static int joy_buttons(const PlatGamepad *g)
{
    /* Port: at most 20 buttons (the names' room in the record; the original writes past it) */
    if (g->standard) return 10;
    return g->nbuttons < 20 ? g->nbuttons : 20;
}

/* DirectInput's scaling: inside the dead zone 0, past the saturation the end of the range */
static int32_t scale(float f, const InputAxis *a)
{
    float v = f < 0 ? -f : f, d = (float)a->dead / 10000.0f, s = (float)a->sat / 10000.0f;
    if (v <= d) return 0;
    v = s > d ? (v - d) / (s - d) : 1.0f;
    if (v > 1) v = 1;
    int32_t r = (int32_t)(v * (float)a->range + 0.5f);
    return f < 0 ? -r : r;
}

static bool joy_state(int slot, const InputRecord *r, JoyState *st)
{
    PlatGamepad g;
    memset(st, 0, sizeof *st);
    st->pov = 0xffffffffu;
    if (!plat_gamepad(slot, &g)) return false;
    float ax[8];
    bool have[8];
    joy_axes(&g, ax, have);
    for (int i = 0; i < 8; i++) st->axis[i] = have[i] ? scale(ax[i], &r->axis[i]) : 0;
    if (g.standard) {
        for (int i = 0; i < 10; i++) st->button[i] = g.button[std_buttons[i]] > 0.5f ? 0x80 : 0;
        bool u = g.button[12] > 0.5f, d = g.button[13] > 0.5f, l = g.button[14] > 0.5f, rt = g.button[15] > 0.5f;
        int y = u ? -1 : d ? 1 : 0, x = l ? -1 : rt ? 1 : 0;
        static const uint32_t angle[3][3] = {{31500, 0, 4500}, {27000, 0xffffffffu, 9000}, {22500, 18000, 13500}};
        st->pov = angle[y + 1][x + 1];
    } else
        for (int i = 0; i < joy_buttons(&g); i++) st->button[i] = g.button[i] > 0.5f ? 0x80 : 0;
    return true;
}

/* 0x49fad0: the eight axes' ranges tried; each there gets 0x10000, a dead zone of 200 and a
   saturation of 10000 */
static void joy_axes_init(InputRecord *r, int slot)
{
    PlatGamepad g;
    float ax[8];
    bool have[8] = {0};
    if (plat_gamepad(slot, &g)) joy_axes(&g, ax, have);
    r->naxes = 0;
    for (int i = 0; i < 8; i++) {
        InputAxis *a = &r->axis[i];
        a->present = have[i];
        if (have[i]) {
            a->dead = 200;
            a->range = 0x10000;
            a->sat = 10000;
            r->naxes++;
        }
    }
}

/* 0x49ee10, 0x49ee90, 0x49ef10: the device's property and the record's copy */
void Input_SetRange(int n, int axis, int32_t range) { Input_Record(n)->axis[axis].range = range; }
void Input_SetDead(int n, int axis, int32_t dead) { Input_Record(n)->axis[axis].dead = dead; }
void Input_SetSat(int n, int axis, int32_t sat) { Input_Record(n)->axis[axis].sat = sat; }

/* 0x49f6d0: a game controller found (DirectInput's enumeration, in the slots' order): the next record
   for it, a traditional joystick 0 and any other 3 (the port's are 3), its names, axes, force feedback
   (none in the port), buttons and hat */
static void joy_found(int slot)
{
    uint32_t c = gm32(COUNTS);
    int n = (int)(c & 0xff), j = (int)(c >> 16 & 0xff);
    InputRecord *r = Input_Record(n);
    PlatGamepad g;
    if (n >= 8 || j >= 4 || !plat_gamepad(slot, &g)) return;
    r->device = DEV_PAD;
    char name[0x104];
    if (!plat_gamepad_name(slot, name, sizeof name) || !name[0]) snprintf(name, sizeof name, "Controller %d", slot + 1);
    snprintf(r->name, sizeof r->name, "%s", name);
    snprintf(r->product, sizeof r->product, "%s", name);
    r->joy = j;
    joy_axes_init(r, slot);
    for (int i = 0, k = 0; k < r->naxes && i < 8; i++)
        if (r->axis[i].present) {
            Input_SetRange(n, i, r->axis[i].range);
            Input_SetDead(n, i, r->axis[i].dead);
            Input_SetSat(n, i, r->axis[i].sat);
            k++;
        }
    r->ffb = 0;
    /* the buttons' names (Port: DirectInput's names for a HID device), then the hat's four */
    r->buttons = 0;
    int nb = joy_buttons(&g);
    for (int i = 0; i < nb; i++) snprintf(r->button_name[r->buttons++], 0x14, "Button %d", i);
    if (g.standard) {
        static const uint32_t suffix[4] = {0x52088c, 0x520888, 0x520884, 0x520880};   /* " L" " R" " U" " D" */
        for (int i = 0; i < 4; i++) snprintf(r->pov_name[i], 0x14, "Hat Switch%s", exe_str(suffix[i]));
    }
    for (int i = 0; i < r->buttons && i < 24; i++) r->button_bit[i] = 1u << i;
    gw32(COUNTS, (c & 0xff00ff00u) | (uint32_t)(j + 1) << 16 | (uint32_t)(n + 1));
    /* the slot is the device: 0x59f6b0[j] */
    gw32(0x59f6b0 + 4u * (uint32_t)j, (uint32_t)slot + 1);
}

static int joy_slot(int j) { return (int)gm32(0x59f6b0 + 4u * (uint32_t)j) - 1; }

/* 0x49f690: the controllers there (DirectInput's attached game controllers) */
static void joy_enum(void)
{
    for (int s = 0; s < 4; s++) {
        PlatGamepad g;
        if (plat_gamepad(s, &g)) joy_found(s);
    }
}

/* 0x49ef90 */
int Input_DeviceCount(void)
{
    int n = 0;
    for (int s = 0; s < 4; s++) {
        PlatGamepad g;
        if (plat_gamepad(s, &g)) n++;
    }
    gw8(COUNTS + 3, (uint8_t)n);
    return gm8(COUNTS + 1) + n;
}

/* 0x49f0e0: the two keyboard players' default keys, and the keys' repeat from Windows' settings (the
   port takes the defaults: a delay of 1, 750 ms, and a speed of 31, 503 - 13 * 31 = 100 ms) */
static void keyboard_defaults(void)
{
    static const uint8_t keys[2][9] = {
        {0xcb, 0xcd, 0xc8, 0xd0, 0x39, 0x1b, 0x1a, 0x2e, 0x13},   /* arrows, space, ] [ C R */
        {0x4b, 0x4d, 0x48, 0x50, 0x51, 0xc9, 0xd1, 0x49, 0x47},   /* the keypad's 4 6 8 2 3, page up and down, 9 7 */
    };
    for (int p = 0; p < 2; p++) {
        int n = gm8(COUNTS);
        InputRecord *r = Input_Record(n);
        r->device = DEV_KEYBOARD;
        r->joy = 0;
        snprintf(r->name, sizeof r->name, "%s", exe_str(0x520874));
        snprintf(r->product, sizeof r->product, "%s", exe_str(0x520874));
        r->ffb = 0;
        memcpy(r->key, keys[p], 9);
        r->key[9] = 0;
        r->key[10] = 0x01;   /* Escape */
        r->key[11] = 0;
        r->key[12] = 0x3b;   /* F1 */
        r->key[13] = 0x3c;   /* F2 */
        r->key[14] = 0x3e;   /* F4 */
        r->key[15] = 0;
        gw8(COUNTS, (uint8_t)(n + 1));
    }
    gw32(DELAY, 750);
    gw32(RATE, 100);
}

static void clear_records(void)
{
    for (int i = 0; i < 8; i++) {
        Input_Record(i)->device = (uint32_t)DEV_NONE;
        Input_Record(i)->joy = -1;
    }
    for (int j = 0; j < 4; j++) gw32(0x59f6b0 + 4u * (uint32_t)j, 0);
}

/* 0x49eaf0 */
void Input_Init(void)
{
    gw32(COUNTS, (gm32(COUNTS) & 0xff000000u) | 0x200);
    clear_records();
    keyboard_defaults();
    joy_enum();
}

/* 0x49eb50 (0x49fd90 lets the devices go) */
void Input_Reinit(void)
{
    gw8(COUNTS + 1, 2);
    gw8(COUNTS, 0);
    gw8(COUNTS + 2, 0);
    clear_records();
    keyboard_defaults();
    joy_enum();
}

/* 0x49eb90: an action bit's key (single bits only) */
void Input_BindKey(int n, uint32_t bit, uint8_t key)
{
    for (int i = 0; i < 16; i++)
        if (bit == 1u << i) Input_Record(n)->key[i] = key;
}

/* 0x49f300: the keyboard's state */
static void keyboard_read(void)
{
    uint8_t k[KEY_COUNT];
    if (!plat_keys(k)) memset(k, 0, sizeof k);
    for (int i = 0; i < KEY_COUNT; i++) gw8(KEYSTATE + (uint32_t)i, k[i] ? 0x80 : 0);
}

static bool down(int code) { return (gm8(KEYSTATE + (uint32_t)code) & 0x80) != 0; }

int Input_KeyDown(void)   /* 0x49edd0 */
{
    for (int i = 0; i < 0xdd; i++)
        if (down(i)) return i;
    return -1;
}

bool Input_ShiftDown(void) { return down(0x2a) || down(0x36); }   /* 0x49edf0 */

int Input_ButtonIndex(uint32_t bit)   /* 0x49fd00 */
{
    int i = 0;
    for (; i < 24; i++)
        if (exe_u32(BUTTON_BITS + 4u * (uint32_t)i) & bit) break;
    return i == 24 ? -1 : i;
}

void Input_KeyName(int code, char *dst, int cap)
{
    static const char *const names[0xe0] = {
        [0x01] = "Esc", [0x02] = "1", [0x03] = "2", [0x04] = "3", [0x05] = "4", [0x06] = "5", [0x07] = "6",
        [0x08] = "7", [0x09] = "8", [0x0a] = "9", [0x0b] = "0", [0x0c] = "-", [0x0d] = "=", [0x0e] = "Backspace",
        [0x0f] = "Tab", [0x10] = "Q", [0x11] = "W", [0x12] = "E", [0x13] = "R", [0x14] = "T", [0x15] = "Y",
        [0x16] = "U", [0x17] = "I", [0x18] = "O", [0x19] = "P", [0x1a] = "[", [0x1b] = "]", [0x1c] = "Enter",
        [0x1d] = "Ctrl", [0x1e] = "A", [0x1f] = "S", [0x20] = "D", [0x21] = "F", [0x22] = "G", [0x23] = "H",
        [0x24] = "J", [0x25] = "K", [0x26] = "L", [0x27] = ";", [0x28] = "'", [0x29] = "`", [0x2a] = "Shift",
        [0x2b] = "\\", [0x2c] = "Z", [0x2d] = "X", [0x2e] = "C", [0x2f] = "V", [0x30] = "B", [0x31] = "N",
        [0x32] = "M", [0x33] = ",", [0x34] = ".", [0x35] = "/", [0x36] = "Right Shift", [0x37] = "Num *",
        [0x38] = "Alt", [0x39] = "Space", [0x3a] = "Caps Lock", [0x3b] = "F1", [0x3c] = "F2", [0x3d] = "F3",
        [0x3e] = "F4", [0x3f] = "F5", [0x40] = "F6", [0x41] = "F7", [0x42] = "F8", [0x43] = "F9", [0x44] = "F10",
        [0x45] = "Pause", [0x46] = "Scroll Lock", [0x47] = "Num 7", [0x48] = "Num 8", [0x49] = "Num 9",
        [0x4a] = "Num -", [0x4b] = "Num 4", [0x4c] = "Num 5", [0x4d] = "Num 6", [0x4e] = "Num +", [0x4f] = "Num 1",
        [0x50] = "Num 2", [0x51] = "Num 3", [0x52] = "Num 0", [0x53] = "Num Del", [0x57] = "F11", [0x58] = "F12",
        [0x9c] = "Num Enter", [0x9d] = "Right Ctrl", [0xb5] = "Num /", [0xb8] = "Right Alt", [0xc5] = "Num Lock",
        [0xc7] = "Home", [0xc8] = "Up", [0xc9] = "Page Up", [0xcb] = "Left", [0xcd] = "Right", [0xcf] = "End",
        [0xd0] = "Down", [0xd1] = "Page Down", [0xd2] = "Insert", [0xd3] = "Delete",
    };
    if (cap <= 0) return;
    const char *n = code >= 0 && code < 0xe0 ? names[code] : NULL;
    snprintf(dst, (size_t)cap, "%s", n ? n : "");
}

/* 0x49f480 */
static void keyboard(InputRecord *r)
{
    r->held = 0;
    if (!gm32(MENU)) {
        for (int i = 0; i < 16; i++)
            if (down(r->key[i])) r->held |= 1u << i;
        return;
    }
    if (down(0xcb)) r->held |= 1;        /* Left */
    if (down(0xcd)) r->held |= 2;        /* Right */
    if (down(0xc8)) r->held |= 4;        /* Up */
    if (down(0xd0)) r->held |= 8;        /* Down */
    if (down(0x1c)) r->held |= 0x10;     /* Enter */
    if (down(0x01)) r->held |= 0x20;     /* Escape */
    if (down(0x3b)) r->held |= 0x1000;   /* F1 */
    if (down(0x3c)) r->held |= 0x2000;   /* F2 */
}

/* 0x49fb70: the state into the axes (+0x47c, every 0x14), the directions, the hat's and the buttons'
   bits */
static void joystick(InputRecord *r)
{
    JoyState st;
    r->held = 0;
    if (r->joy < 0 || !joy_state(joy_slot(r->joy), r, &st)) return;   /* (the original tries again 5 times) */
    for (int i = 0; i < 8; i++) r->axis[i].value = st.axis[i];
    int32_t x = st.axis[0], y = st.axis[1];
    if (x < -0x9999) r->held |= 1;
    else if (x > 0x9999) r->held |= 2;
    if (y < -0x9999) r->held |= 4;
    else if (y > 0x9999) r->held |= 8;
    uint32_t pov = st.pov;
    if ((uint16_t)pov != 0xffff) {
        if (pov >= 4500 && pov < 13501) r->held |= 2;
        if (pov >= 13500 && pov < 22501) r->held |= 8;
        if (pov >= 22500 && pov < 31501) r->held |= 1;
        if (pov <= 4500 || pov >= 31500) r->held |= 4;
    }
    for (int i = 0; i < r->buttons && i < 24; i++)
        if (st.button[i] & 0x80) r->held |= exe_u32(BUTTON_BITS + 4u * (uint32_t)i);
}

/* 0x49e960 */
InputRecord *Input_Poll(int n)
{
    if (n < 0 || n >= 8) return NULL;
    InputRecord *r = Input_Record(n);
    if (n >= 2 && !(r->joy >= 0 && r->joy < 4)) return NULL;
    uint32_t old = r->held;
    switch ((int32_t)r->device) {
    case DEV_JOYSTICK:
    case DEV_PAD:
        joystick(r);
        break;
    case DEV_KEYBOARD:
        if (n == 0) keyboard_read();
        keyboard(r);
        break;
    }
    r->pressed = (r->held ^ old) & r->held;
    if (r->held & 0xfffffff0u) r->held |= 0x10000;
    uint32_t now = Timer_Now(), mask = gm32(REPEAT_MASK);
    uint32_t dirs = r->held & mask;
    if (!dirs) {
        r->repeat = 0;
        r->due = 0;
    } else if (dirs == (mask & old)) {
        /* held on: a repeat each interval once the delay is over */
        if ((int32_t)(r->due - now) <= 0) {
            r->repeat = dirs;
            r->due = now + gm32(RATE);
        } else r->repeat = 0;
    } else {
        r->repeat = ~old & r->held & mask;
        r->due = now + gm32(DELAY);
    }
    if (gm32(REPEAT_ADDS)) r->pressed |= r->repeat;
    return r;
}

void Input_PollAll(void)   /* 0x49eab0 */
{
    for (int i = 0; i < 8; i++) Input_Poll(i);
}

uint32_t Input_HeldAll(void)   /* 0x49e940 */
{
    uint32_t h = 0;
    for (int i = 0; i < 8; i++)
        if (Input_Record(i)->device != (uint32_t)DEV_NONE) h |= Input_Record(i)->held;
    return h;
}

static int chars[30], nchars;

static void take_presses(void)
{
    static const char map[0x40] = {
        0, 0, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', 0, 0, 8, 0,
        'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', 0, 0, 0, 0, 'a', 's',
        'd', 'f', 'g', 'h', 'j', 'k', 'l', 0, 0, 0, 0, 0, 'z', 'x', 'c', 'v',
        'b', 'n', 'm', 0, '.', 0, 0, 0, 0, ' ', 0, 0, 0, 0, 0, 0};
    uint8_t codes[32];
    int n = plat_key_presses(codes, 32);
    for (int i = 0; i < n; i++)
        if (codes[i] < 0x40 && map[codes[i]] && nchars < 30) chars[nchars++] = map[codes[i]];
}

bool Input_NextChar(int *c)
{
    take_presses();
    if (!nchars) return false;
    *c = chars[0];
    for (int i = 1; i < nchars; i++) chars[i - 1] = chars[i];
    nchars--;
    return true;
}

void Input_FlushChars(void)
{
    take_presses();
    nchars = 0;
}
