/* The menu engine (see menu.h), ported from 0x49ff50-0x4a0c50. */
#include "fe/menu.h"
#include "audio/snd.h"
#include <string.h>

static int snd_move = -1, snd_choose = -1, snd_back = -1, snd_other = -1, snd_value = -1;   /* 0x59f8f8, 0x59f910, 0x59f8f4, 0x59f8f0, 0x59f90c */
static int32_t volume;          /* DAT_0059f900 */
static bool entering;           /* DAT_0059f904: run the page's enter first */
static Menu *pending;           /* DAT_0059f908: the page to go to */
static bool backed;             /* DAT_0059f8fc: this frame went back */
static bool sounds;             /* DAT_0059fa14 */
static bool diagonal;           /* DAT_0059fa15: a move and a value change in one frame */
static bool went_back;          /* DAT_0059fa16 */
static bool back_called;        /* DAT_0059fa17: Menu_Back was called */
static uint32_t last_buttons;   /* DAT_0059fa18 */

void Menu_SetSounds(int move, int choose, int back, int other, int value)
{
    snd_move = move, snd_choose = choose, snd_back = back, snd_other = other, snd_value = value;
}

void Menu_SetVolume(int32_t v) { volume = v; }

/* 22,500 Hz, from the start, once, not 3D */
void Menu_PlaySound(int id)
{
    if (id >= 0) Voice_Play(id, volume, 0x57e4, 0, false, false);
}

void Menu_Init(Menu *m, const char *title, int16_t text, Menu *parent, bool root, const MenuHooks *hooks,
               bool wrap, int8_t sel, uint8_t across)
{
    m->title = title;
    m->text = text;
    m->sel = sel;
    m->parent = root ? NULL : parent;
    m->n = 0;
    m->enter = NULL, m->frame = NULL, m->draw = NULL, m->leave = NULL;
    m->across = across;
    m->first = -1;
    /* bits 0-3 and 5 set, 4 the wrap, 6 no parent; bit 7 kept */
    m->flags = (uint8_t)((m->flags & 0x80) | 0x2f | (wrap ? MF_WRAP : 0) | (root ? MF_ROOT : 0));
    m->value &= 0x80;
    m->hooks = hooks;
}

/* 0x4a0060: the next item cleared: no text, enabled and selectable, nothing else */
static MenuItem *new_item(Menu *m)
{
    MenuItem *it = &m->items[m->n];
    it->label = NULL;
    it->type = 0;
    it->id = 0;
    it->fn = NULL;
    it->count = it->value = 0;
    it->target = NULL;
    it->text = -1;
    it->flags = (uint8_t)((it->flags & 0xf3) | MIF_ENABLED | MIF_SELECTABLE);
    return it;
}

static void add_list(Menu *m, uint8_t type, const char *label, int16_t text, uint8_t count, uint8_t value,
                     bool wrap, MenuItemFn fn, int16_t id)
{
    MenuItem *it = new_item(m);
    it->label = label;
    it->text = text;
    it->type = type;
    it->count = count;
    it->value = value;
    it->flags = (uint8_t)((it->flags & ~MIF_WRAP) | (wrap ? MIF_WRAP : 0));
    it->id = id;
    it->fn = fn;
    m->n++;
}

void Menu_AddList(Menu *m, const char *label, int16_t text, uint8_t count, uint8_t value, bool wrap,
                  MenuItemFn fn, int16_t id)
{
    add_list(m, MI_LIST, label, text, count, value, wrap, fn, id);
}

void Menu_AddList6(Menu *m, const char *label, int16_t text, uint8_t count, uint8_t value, bool wrap,
                   MenuItemFn fn, int16_t id)
{
    add_list(m, MI_LIST6, label, text, count, value, wrap, fn, id);
}

static void add_simple(Menu *m, uint8_t type, const char *label, int16_t text, MenuItemFn fn, int16_t id)
{
    MenuItem *it = new_item(m);
    it->label = label;
    it->text = text;
    it->type = type;
    it->id = id;
    it->fn = fn;
    m->n++;
}

void Menu_AddAction(Menu *m, const char *label, int16_t text, MenuItemFn fn, int16_t id) { add_simple(m, MI_ACTION, label, text, fn, id); }
void Menu_AddBack(Menu *m, const char *label, int16_t text, MenuItemFn fn, int16_t id) { add_simple(m, MI_BACK, label, text, fn, id); }

void Menu_AddPage(Menu *m, const char *label, int16_t text, Menu *target, MenuItemFn fn, int16_t id)
{
    MenuItem *it = new_item(m);
    it->label = label;
    it->text = text;
    it->type = MI_PAGE;
    it->target = target;
    it->id = id;
    it->fn = fn;
    m->n++;
}

void Menu_SetSel(Menu *m, int8_t sel) { m->sel = sel; }
void Menu_SetParent(Menu *m, Menu *parent) { m->parent = parent; }

int Menu_IndexOf(const Menu *m, int16_t id)
{
    for (int i = 0; i < m->n; i++)
        if (m->items[i].id == id) return i;
    return -1;
}

MenuItem *Menu_Item(Menu *m, int16_t id)
{
    int i = Menu_IndexOf(m, id);
    return i < 0 ? NULL : &m->items[i];
}

void Menu_SetTarget(Menu *m, int16_t id, Menu *t)
{
    MenuItem *it = Menu_Item(m, id);
    if (it && it->type == MI_PAGE) it->target = t;
}

void Menu_SelectId(Menu *m, int16_t id) { m->sel = (int8_t)Menu_IndexOf(m, id); }

void Menu_SetCallbacks(Menu *m, void (*enter)(Menu *, bool), void (*frame)(Menu *), void (*draw)(Menu *),
                       void (*leave)(Menu *, bool))
{
    m->enter = enter, m->frame = frame, m->draw = draw, m->leave = leave;
}

/* The original computes the low nibble as an exclusive or of nav with the other three shifted in;
   with the old bits masked off first it is the plain or. */
void Menu_SetFlags(Menu *m, bool nav, bool values, bool select, bool back)
{
    m->flags = (uint8_t)((m->flags & 0xf0) | (nav ? MF_NAV : 0) | (values ? MF_VALUES : 0) | (select ? MF_SELECT : 0) |
                         (back ? MF_BACK : 0));
}

static bool selectable(const MenuItem *it) { return (it->flags & MIF_ENABLED) && (it->flags & MIF_SELECTABLE); }

void Menu_Reset(Menu *m)
{
    if (m->first >= 0 && m->first < m->n) m->sel = m->first;
    int n = m->n;
    for (int k = 0, i = m->sel; k < n; k++, i++)
        if (selectable(&m->items[i % n])) {
            m->sel = (int8_t)(i % n);
            return;
        }
}

void Menu_Enter(Menu *m)
{
    if (m->enter) m->enter(m, went_back || back_called);
}

void Menu_Leave(Menu *m)
{
    if (m->leave) m->leave(m, false);
}

void Menu_Draw(Menu *m)
{
    if (m && m->draw) m->draw(m);
}

/* the selection one step on (dir 1) or back (-1) to the next selectable item; false when it came
   round to where it started */
static bool step(Menu *m, int dir)
{
    int n = m->n, count = 0;
    bool full = true;
    do {
        int s = m->sel + dir;
        if (s >= n) s = 0;
        else if (s < 0) s += n;
        m->sel = (int8_t)s;
        if (++count >= n) full = false;
    } while (!selectable(&m->items[m->sel]));
    return full;
}

Menu *Menu_Input(Menu *m, uint32_t buttons)
{
    uint32_t b = buttons;
    if (!diagonal && (b & (MB_UP | MB_DOWN)) && (b & (MB_LEFT | MB_RIGHT))) b = 0;
    last_buttons = b;
    backed = false;
    if (!m) return NULL;
    if (entering) {
        Menu_Enter(m);
        entering = false;
        back_called = false;
    }
    bool back = false;
    if (!pending) {
        uint8_t fl = m->flags;
        bool select = (fl & MF_SELECT) && (b & MB_SELECT);
        back = (fl & MF_BACK) && (b & MB_BACK);
        /* values: left and right on a page whose items run up and down, else up and down; both is up */
        bool dec = false, inc = false;
        if (fl & MF_VALUES) {
            dec = m->across == 1 ? (b & MB_LEFT) != 0 : (b & MB_UP) != 0;
            inc = m->across == 1 ? (b & MB_RIGHT) != 0 : (b & MB_DOWN) != 0;
            if (dec && inc) dec = false;
        }
        /* the selection: the other pair, not while a value changes; both is on */
        bool prev = false, next = false;
        if ((fl & MF_NAV) && !dec && !inc) {
            prev = m->across == 1 ? (b & MB_UP) != 0 : (b & MB_LEFT) != 0;
            next = m->across == 1 ? (b & MB_DOWN) != 0 : (b & MB_RIGHT) != 0;
            if (prev && next) prev = false;
        }
        /* off a disabled item first (looks at the enabled bit only) */
        for (int k = m->n; k > 0 && !(m->items[m->sel].flags & MIF_ENABLED); k--)
            if (++m->sel >= m->n) m->sel = 0;
        if (prev) {
            bool moved = true;
            if (m->sel < 1 && !(m->flags & MF_WRAP)) moved = false;
            else {
                bool full = step(m, -1);
                m->dir |= 1;
                if (full && sounds) Menu_PlaySound(snd_move);
            }
            if (m->hooks && m->hooks->moved) m->hooks->moved(m, &m->items[m->sel], moved);
        } else if (next) {
            bool moved = true;
            if (m->sel < m->n - 1 || ((m->flags & MF_WRAP) && m->n)) {
                bool full = step(m, 1);
                m->dir &= (uint8_t)~1;
                if (full && sounds) Menu_PlaySound(snd_move);
            } else moved = false;
            if (m->hooks && m->hooks->moved) m->hooks->moved(m, &m->items[m->sel], moved);
        }
        MenuItem *it = &m->items[m->sel];
        bool chosen = select && (it->flags & MIF_ENABLED);
        if ((prev || next) && it->type == MI_LIST6) {
            if (it->count <= (m->value & 0x7f)) m->value = (uint8_t)((m->value & 0x80) | ((it->count - 1) & 0x7f));
            it->value = m->value & 0x7f;
        }
        if (it->type == MI_LIST || it->type == MI_LIST6) {
            if (dec) {
                bool changed = true;
                if (it->value == 0 && !(it->flags & MIF_WRAP)) changed = false;
                else {
                    if (--it->value == 0xff) it->value = (uint8_t)(it->count - 1);
                    if (sounds) Menu_PlaySound(snd_value);
                    m->dir &= (uint8_t)~2;
                }
                if (m->hooks && m->hooks->changed) m->hooks->changed(m, &m->items[m->sel], changed);
            } else if (inc || (chosen && (it->flags & MIF_SELECT_STEPS))) {
                bool changed = true;
                if (it->value < it->count - 1 || (it->flags & MIF_WRAP) || (chosen && (it->flags & MIF_SELECT_STEPS))) {
                    /* a chosen item with a function leaves its value to the function */
                    if ((!chosen || !it->fn) && ++it->value >= it->count) it->value = 0;
                    if (sounds) Menu_PlaySound(snd_value);
                    m->dir |= 2;
                } else changed = false;
                if (m->hooks && m->hooks->changed) m->hooks->changed(m, &m->items[m->sel], changed);
            }
            m->value = (uint8_t)((m->value & 0x80) | (it->value & 0x7f));
        }
        if (chosen) {
            if (it->fn) it->fn(m, it);
            if (back || it->type == MI_BACK) goto go_back;
            if (it->type == MI_PAGE) {
                pending = it->target;
                if (m->hooks && m->hooks->chosen) m->hooks->chosen(m, &m->items[m->sel], true);
                if (sounds) Menu_PlaySound(snd_choose);
            }
        } else if (back) {
        go_back:
            pending = m->parent;
            backed = true;
            if (m->hooks && m->hooks->backed) m->hooks->backed(m, &m->items[m->sel], true);
            if (sounds) Menu_PlaySound(snd_back);
        }
        if (m->frame) m->frame(m);
        if (!pending) return NULL;
    }
    /* pending set (before, or above): going back when the back button did it or a back item is
       selected */
    went_back = back || m->items[m->sel].type == MI_BACK;
    if (m->leave) m->leave(m, went_back);
    if (pending) entering = true;
    Menu *r = pending;
    pending = NULL;
    return r;
}

void Menu_Goto(Menu *target)
{
    if (!backed && sounds) Menu_PlaySound(snd_choose);
    went_back = false;
    pending = target;
}

void Menu_Back(Menu *m)
{
    if (!backed && sounds) Menu_PlaySound(snd_choose);
    back_called = true;
    pending = m->parent;
}

void Menu_Sound(int which)
{
    switch (which) {
    case 0: Menu_PlaySound(snd_move); break;
    case 1: Menu_PlaySound(snd_choose); break;
    case 2: Menu_PlaySound(snd_back); break;
    case 3: Menu_PlaySound(snd_other); break;
    case 4: Menu_PlaySound(snd_value); break;
    default: Menu_PlaySound(which); break;
    }
}

void Menu_Start(void)
{
    entering = true;
    diagonal = false;
    went_back = false;
    pending = NULL;
}

void Menu_SoundsOn(bool on) { sounds = on; }
void Menu_AllowDiagonal(bool on) { diagonal = on; }
bool Menu_WentBack(void) { return went_back; }
