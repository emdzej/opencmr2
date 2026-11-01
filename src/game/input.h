/* The input records (0x49e960-0x49fb70, DAT_0059ce48, 0x50c bytes each): per player the device, the
   held action bits (+0x04), those newly pressed this frame (+0x08, with the auto-repeats) and the
   repeats (+0x0c). In the menus' key mode (0x49ea90(1)) the keyboard gives fixed bits (0x49f480): the
   arrows 1 left, 2 right, 4 up, 8 down, Enter 0x10, Escape 0x20, F1 0x1000, F2 0x2000. Ported for the
   keyboard player 0 in that mode; the pad adds the same bits (the port). */
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t device;    /* +0x00: 1 the keyboard */
    uint32_t held;      /* +0x04 */
    uint32_t pressed;   /* +0x08 */
    uint32_t repeat;    /* +0x0c */
    uint32_t due;       /* +0x10: the time of the next repeat */
} InputRecord;

void Input_SetMenuMode(bool on);     /* 0x49ea90 */
void Input_PollAll(void);            /* 0x49eab0: 0x49e960 for each record */
InputRecord *Input_Record(int n);    /* 0x49ead0 */

/* The typed characters (WM_CHAR into the queue 0x6ed3f4, 30 deep): 0x4b7cd0 takes the next one, false
   when none; 0x4b7c80 empties the queue. The port makes them from the key presses (letters lower
   case, digits, space, '.', backspace 8). */
bool Input_NextChar(int *c);
void Input_FlushChars(void);
