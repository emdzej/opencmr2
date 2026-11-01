/* The menu engine (0x49ff50-0x4a0c50, docs/re/frontend.md#the-menu-engine): a page is a fixed record of
   a title, up to 22 items, callbacks and the selection; the front end builds every page at start-up
   (0x4e9f70) and runs the current one each frame with Menu_Input 0x4a0570, which moves the selection,
   changes values, follows sub-pages and goes back. The original's records are 0x1e0 bytes in the BSS
   (the main menu at 0x81d6d8); the port keeps the fields, with real pointers. Offsets are the
   original's. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct Menu Menu;
typedef struct MenuItem MenuItem;
typedef void (*MenuItemFn)(Menu *m, MenuItem *it);

/* The item types (+0x07) */
enum {
    MI_BACK = 1,     /* back to the parent (0x4a0270) */
    MI_PAGE = 2,     /* to the target page (0x4a02e0) */
    MI_LIST = 3,     /* a value of count (0x4a00a0) */
    MI_ACTION = 4,   /* its function only (0x4a0200) */
    MI_LIST6 = 6,    /* a value kept in the page's +0x10 as well (0x4a0150) */
};

/* The item flags (+0x06) */
enum { MIF_ENABLED = 1, MIF_SELECTABLE = 2, MIF_WRAP = 4, MIF_SELECT_STEPS = 8 };

struct MenuItem {
    const char *label;    /* +0x00: shown instead of the text when set */
    int16_t text;         /* +0x04: Text_Get number, -1 none */
    uint8_t flags;        /* +0x06 */
    uint8_t type;         /* +0x07 */
    int16_t id;           /* +0x08 */
    uint8_t count;        /* +0x0a: a list's values */
    uint8_t value;        /* +0x0b */
    Menu *target;         /* +0x0c: a page item's page */
    MenuItemFn fn;        /* +0x10: run when the item is chosen */
};

/* The page callbacks (+0x1dc): after a move (moved false at an end), after a value change, on
   choosing a page item, on going back. */
typedef struct {
    void (*moved)(Menu *m, MenuItem *it, bool moved);
    void (*changed)(Menu *m, MenuItem *it, bool changed);
    void (*chosen)(Menu *m, MenuItem *it, bool b);
    void (*backed)(Menu *m, MenuItem *it, bool b);
} MenuHooks;

/* The page flags (+0x0e) */
enum {
    MF_NAV = 1,       /* the selection moves */
    MF_VALUES = 2,    /* list values change */
    MF_SELECT = 4,    /* the select button works */
    MF_BACK = 8,      /* the back button works */
    MF_WRAP = 0x10,   /* the selection wraps */
    MF_ROOT = 0x40,   /* built without a parent */
};

enum { MENU_ITEMS = 22 };

struct Menu {
    const char *title;          /* +0x00 */
    int16_t text;               /* +0x04 */
    int8_t n;                   /* +0x06 */
    int8_t sel;                 /* +0x07 */
    Menu *parent;               /* +0x08 */
    int8_t first;               /* +0x0c: the selection Menu_Reset starts at, -1 none */
    uint8_t across;             /* +0x0d: 1 the items run up and down (the selection moves with up and
                                   down, values with left and right), 0 across (the other way) */
    uint8_t flags;              /* +0x0e */
    uint8_t dir;                /* +0x0f: bit 0 the last move went back, bit 1 the last value went up */
    uint8_t value;              /* +0x10: low 7 bits the shared value of type 6 items */
    MenuItem items[MENU_ITEMS]; /* +0x14 */
    void (*enter)(Menu *m, bool back);   /* +0x1cc */
    void (*frame)(Menu *m);              /* +0x1d0 */
    void (*draw)(Menu *m);               /* +0x1d4 */
    void (*leave)(Menu *m, bool back);   /* +0x1d8 */
    const MenuHooks *hooks;              /* +0x1dc */
};

/* The input bits Menu_Input reads (the input record's +0x08 in the menus' key mode, 0x49f480) */
enum { MB_LEFT = 1, MB_RIGHT = 2, MB_UP = 4, MB_DOWN = 8, MB_SELECT = 0x10, MB_BACK = 0x20 };

/* 0x49ff80: the sound numbers for a move, a choice, going back, (unused here) and a value change; -1
   none */
void Menu_SetSounds(int move, int choose, int back, int other, int value);
void Menu_SetVolume(int32_t v);                     /* 0x49ffc0 */
void Menu_PlaySound(int id);                        /* 0x49ff50 */
/* 0x49ffd0: an empty page; parent NULL is the original's -1 (MF_ROOT) */
void Menu_Init(Menu *m, const char *title, int16_t text, Menu *parent, bool root, const MenuHooks *hooks,
               bool wrap, int8_t sel, uint8_t across);
void Menu_AddList(Menu *m, const char *label, int16_t text, uint8_t count, uint8_t value, bool wrap,
                  MenuItemFn fn, int16_t id);           /* 0x4a00a0 */
void Menu_AddList6(Menu *m, const char *label, int16_t text, uint8_t count, uint8_t value, bool wrap,
                   MenuItemFn fn, int16_t id);          /* 0x4a0150 */
void Menu_AddAction(Menu *m, const char *label, int16_t text, MenuItemFn fn, int16_t id);   /* 0x4a0200 */
void Menu_AddBack(Menu *m, const char *label, int16_t text, MenuItemFn fn, int16_t id);     /* 0x4a0270 */
void Menu_AddPage(Menu *m, const char *label, int16_t text, Menu *target, MenuItemFn fn, int16_t id);   /* 0x4a02e0 */
void Menu_SetSel(Menu *m, int8_t sel);              /* 0x4a0360 */
void Menu_SetParent(Menu *m, Menu *parent);         /* 0x4a0370 */
int Menu_IndexOf(const Menu *m, int16_t id);        /* 0x4a0380: -1 none */
MenuItem *Menu_Item(Menu *m, int16_t id);           /* 0x4a03b0 */
void Menu_SetTarget(Menu *m, int16_t id, Menu *t);  /* 0x4a03e0 */
void Menu_SelectId(Menu *m, int16_t id);            /* 0x4a0410 */
void Menu_SetCallbacks(Menu *m, void (*enter)(Menu *, bool), void (*frame)(Menu *), void (*draw)(Menu *),
                       void (*leave)(Menu *, bool));   /* 0x4a0430 */
void Menu_SetFlags(Menu *m, bool nav, bool values, bool select, bool back);   /* 0x4a0460 */
void Menu_Reset(Menu *m);                           /* 0x4a04a0: the first selectable item from +0x0c */
void Menu_Enter(Menu *m);                           /* 0x4a04f0 */
void Menu_Leave(Menu *m);                           /* 0x4a0530 */
void Menu_Draw(Menu *m);                            /* 0x4a0550 */
/* 0x4a0570: one frame of the page with the input bits; the page to go to, or NULL */
Menu *Menu_Input(Menu *m, uint32_t buttons);
void Menu_Goto(Menu *target);                       /* 0x4a0ad0 */
void Menu_Back(Menu *m);                            /* 0x4a0b30 */
void Menu_Sound(int which);                         /* 0x4a0bc0: 0-4 the set's, else that number */
void Menu_Start(void);                              /* 0x4a0ba0 */
void Menu_SoundsOn(bool on);                        /* 0x4a0c40 */
void Menu_AllowDiagonal(bool on);                   /* 0x4a0c50 */
bool Menu_WentBack(void);                           /* DAT_0059fa16 */
