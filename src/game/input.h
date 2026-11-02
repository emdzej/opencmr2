/* The input records (0x49e930-0x49ff50; DAT_0059ce48, eight of 0x50c bytes, in the game memory): one
   per device, the two keyboard players first (0x49f0e0), then the game controllers in their slots
   (0x49f6d0). Each poll (0x49e960) makes the held action bits (+0x04), those newly pressed (+0x08,
   with the auto-repeats of the bits in 0x520868) and the repeats (+0x0c). The keyboard's bits are its
   sixteen bound keys (+0x468, 0x49f480), or in the menus' key mode (0x49ea90(1)) the fixed keys: the
   arrows 1 left, 2 right, 4 up, 8 down, Enter 0x10, Escape 0x20, F1 0x1000, F2 0x2000. A controller's
   (0x49fb70): its X and Y axes past 0x9999 the four directions, the hat the same, button i the bit
   0x520808[i] (0x10 up to button 19). */
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    int32_t present;   /* +0x00: the axis is there (its range could be set, 0x49fad0) */
    int32_t range;     /* +0x04: -range..range (0x49ee10), 0x10000 */
    int32_t dead;      /* +0x08: the dead zone in 1/10000 of the range (0x49ee90), 200 */
    int32_t sat;       /* +0x0c: the saturation (0x49ef10), 10000 */
    int32_t value;     /* +0x10: the position */
} InputAxis;

enum { DEV_JOYSTICK = 0, DEV_KEYBOARD = 1, DEV_MOUSE = 2, DEV_PAD = 3, DEV_NONE = -1 };

typedef struct {
    uint32_t device;            /* +0x000: DEV_* (a traditional joystick 0, another controller 3) */
    uint32_t held;              /* +0x004 */
    uint32_t pressed;           /* +0x008 */
    uint32_t repeat;            /* +0x00c */
    uint32_t due;               /* +0x010: the time of the next repeat */
    int32_t buttons;            /* +0x014 */
    int32_t joy;                /* +0x018: the controller's slot (0x59f6b0), -1 none; the keyboard's 0 */
    uint32_t button_bit[24];    /* +0x01c: 1 << i for each button */
    char name[0x104];           /* +0x07c: the instance's name */
    char product[0x104];        /* +0x180 */
    char button_name[20][0x14]; /* +0x284 */
    char pov_name[4][0x14];     /* +0x414: the hat's left, right, up and down */
    int32_t ffb;                /* +0x464: force feedback */
    union {
        uint8_t key[16];        /* +0x468: the keyboard's key (DIK code) for action bits 0-15 */
        struct {
            int32_t naxes;      /* +0x468 */
            InputAxis axis[8];  /* +0x46c: X, Y, Z, Rx, Ry, Rz, the two sliders */
        };
    };
} InputRecord;
_Static_assert(sizeof(InputRecord) == 0x50c, "the original's input record");

/* 0x49eaf0: the records cleared, the keyboard players (0x49f0e0) and the controllers (0x49f690);
   0x49eb50 again after letting the devices go (the controller page's search, 0x4fc9f0) */
void Input_Init(void);
void Input_Reinit(void);
int Input_DeviceCount(void);         /* 0x49ef90: the two keyboard players and the controllers there now */
void Input_SetMenuMode(bool on);     /* 0x49ea90 */
void Input_PollAll(void);            /* 0x49eab0: 0x49e960 for each record */
InputRecord *Input_Poll(int n);      /* 0x49e960: NULL for a record without a device */
InputRecord *Input_Record(int n);    /* 0x49ead0 */
uint32_t Input_HeldAll(void);        /* 0x49e940: every record's held bits */
void Input_BindKey(int n, uint32_t bit, uint8_t key);   /* 0x49eb90: the key for one action bit */
void Input_SetRange(int n, int axis, int32_t range);    /* 0x49ee10 */
void Input_SetDead(int n, int axis, int32_t dead);      /* 0x49ee90 */
void Input_SetSat(int n, int axis, int32_t sat);        /* 0x49ef10 */
int Input_KeyDown(void);             /* 0x49edd0: the first key held (below 0xdd), -1 none */
bool Input_ShiftDown(void);          /* 0x49edf0: either shift key */
int Input_ButtonIndex(uint32_t bit); /* 0x49fd00: the button of a bit (0x520808), -1 none */
/* 0x49ed80: a key's name (Windows' GetKeyNameText for the key last pressed; the port's names are
   Windows' for a US keyboard, by DIK code) */
void Input_KeyName(int code, char *dst, int cap);

/* The typed characters (WM_CHAR into the queue 0x6ed3f4, 30 deep): 0x4b7cd0 takes the next one, false
   when none; 0x4b7c80 empties the queue. The port makes them from the key presses (letters lower
   case, digits, space, '.', backspace 8). */
bool Input_NextChar(int *c);
void Input_FlushChars(void);
