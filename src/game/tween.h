/* The tweens (0x4bc0c0-0x4bc470): 32 slots of 0x30 bytes at 0x521138, each moving a value from a start
   to an end over a number of steps, a step at most every 16 ms. A caller keeps a byte handle (the slot)
   in the game state; a slot is its handle's while it records that handle's address (+0x20). Slot
   layout: +0 the curve (0 linear, 1 linear with a bounce at the end, 2 easing in, 3 easing out, 4 easing
   in with the bounce; 5 free or done), +1 the direction (+1 or -1), +4 the steps, +8 the base, +0xc
   the end, +0x10 the start, +0x14 three steps more before it counts as done, +0x18 an OR of the
   slot's first bytes (written, never read), +0x1c the rate (20.12), +0x20 the handle's address, +0x24
   the steps to wait first, +0x28 the step, +0x2c the time of the last step. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

bool Tween_Active(uint32_t handle);   /* 0x4bc0c0: still moving (the slot still the handle's) */
int32_t Tween_Value(uint8_t slot);    /* 0x4bc110: the value now, the step advanced by the clock */
/* 0x4bc290: a tween from `from` to `to` over `steps` (curve 0-4), after `delay` steps at `from`;
   `hold`: three steps more before it is done */
void Tween_Start(uint32_t handle, int curve, int steps, int delay, int32_t from, int32_t to, bool hold);
void Tween_ClearAll(void);            /* 0x4bc440 */
void Tween_Stop(uint32_t handle);     /* 0x4bc470 */
