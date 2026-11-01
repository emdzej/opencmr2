/* The input records (see input.h), ported from 0x49e960 and the menu branch of 0x49f480. */
#include "game/input.h"
#include "game/timer.h"
#include "platform.h"

static InputRecord rec[8];
static bool menu_mode;   /* DAT_0059f8d0 */
/* The repeat delay and interval (DAT_0059f8d8, DAT_0059f8dc), from Windows' keyboard delay and speed
   (0x49f0e0): with the default settings (delay 1, speed 31) 750 and 503 - 13 * 31 = 100 ms. */
enum { REPEAT_DELAY = 750, REPEAT_RATE = 100 };
static const uint32_t repeat_mask = 0xf;   /* DAT_00520868 */
static const bool repeat_adds = true;      /* DAT_0052086c */

void Input_SetMenuMode(bool on) { menu_mode = on; }
InputRecord *Input_Record(int n) { return &rec[n]; }

static uint32_t keyboard(void)
{
    uint8_t k[KEY_COUNT];
    uint32_t h = 0;
    if (plat_keys(k)) {
        if (menu_mode) {
            if (k[0xcb]) h |= 1;        /* Left */
            if (k[0xcd]) h |= 2;        /* Right */
            if (k[0xc8]) h |= 4;        /* Up */
            if (k[0xd0]) h |= 8;        /* Down */
            if (k[0x1c]) h |= 0x10;     /* Enter */
            if (k[0x01]) h |= 0x20;     /* Escape */
            if (k[0x3b]) h |= 0x1000;   /* F1 */
            if (k[0x3c]) h |= 0x2000;   /* F2 */
        }
    }
    uint32_t p = plat_pad(0);
    if (menu_mode) {
        if (p & PAD_LEFT) h |= 1;
        if (p & PAD_RIGHT) h |= 2;
        if (p & PAD_UP) h |= 4;
        if (p & PAD_DOWN) h |= 8;
        if (p & (PAD_A | PAD_START)) h |= 0x10;
        if (p & PAD_B) h |= 0x20;
    }
    return h;
}

static void poll(int n)
{
    InputRecord *r = &rec[n];
    uint32_t old = r->held;
    r->device = n == 0 ? 1 : 0;
    r->held = n == 0 ? keyboard() : 0;
    r->pressed = (r->held ^ old) & r->held;
    if (r->held & 0xfffffff0u) r->held |= 0x10000;
    uint32_t now = Timer_Now();
    uint32_t dirs = r->held & repeat_mask;
    if (!dirs) {
        r->repeat = 0;
        r->due = 0;
    } else if (dirs == (repeat_mask & old)) {
        /* held on: a repeat each interval once the delay is over */
        if ((int32_t)(r->due - now) <= 0) {
            r->repeat = dirs;
            r->due = now + REPEAT_RATE;
        } else r->repeat = 0;
    } else {
        r->repeat = ~old & r->held & repeat_mask;
        r->due = now + REPEAT_DELAY;
    }
    if (repeat_adds) r->pressed |= r->repeat;
}

void Input_PollAll(void)
{
    for (int i = 0; i < 8; i++) poll(i);
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
