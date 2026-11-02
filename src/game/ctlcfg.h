/* The controller configuration (0x40bad0-0x40cc60): six records of 0x2f0 bytes at 0x532250, one per
   input device (record i made from input record i, 0x40c610), kept in Configuration\Controller.rcf
   with the two players' devices (0x5168f4: a word per player, the device's record). A record:
     +0x000, +0x004  two settings, 0x8000 (0x40bc00, 0x40bc30)
     +0x008          the device's name (0x104)
     +0x10c          a game controller (input types 2 and 3); +0x110 analogue steering; +0x114 analogue
                     pedals (both set with it)
     +0x118          force feedback on (the device's own at first); +0x11c the quick force update
     +0x120          accelerate and brake swapped on a shared axis; +0x124 steering damping
     +0x128          the ten actions' bits (words): left, right, accelerate, brake, handbrake, gear up,
                     gear down, camera, rear view, pause; 0 for the keyboard (the bit 1 << action)
     +0x13e          the first nine actions' keys (bytes); +0x147 their names (0x14 each)
     +0x210          ten axis slots (an input axis' five words, 0x14): steering, steering, accelerate,
                     brake; +0x2d8 each slot's axis (words)
     +0x2ec          the record's index (a byte) */
#pragma once
#include "game/input.h"
#include <stdbool.h>
#include <stdint.h>

enum { CTL_CONFIGS = 0x532250, CTL_SIZE = 0x2f0 };

int Ctl_Count(void);                       /* 0x40bba0: the devices at the last 0x40c050 */
int Ctl_Device(int player);                /* 0x40bbc0 */
void Ctl_SetDevice(int player, int d);     /* 0x40bbe0 */
uint32_t Ctl_Config(int player);           /* the player's device's record's address */
int32_t Ctl_Setting0(int player);          /* 0x40bc00 */
int32_t Ctl_Setting1(int player);          /* 0x40bc30 */
void Ctl_SetSetting0(int player, int32_t v);   /* 0x40bc60 */
void Ctl_SetSetting1(int player, int32_t v);   /* 0x40bd00 */
void Ctl_SetQuickUpdate(int player, uint32_t on);   /* 0x40bc90 (and the effects' 0x4aaf50: no force feedback) */
bool Ctl_QuickUpdate(int player);          /* 0x40bcd0 */
bool Ctl_ForceFeedback(int player);        /* 0x40bd30 */
void Ctl_Merge(int player, InputRecord *r);   /* 0x40bd60: a game controller's bits added */
bool Ctl_AnalogSteer(int player);          /* 0x40bdd0 */
bool Ctl_AnalogPedals(int player);         /* 0x40be00 */
bool Ctl_Swap(int player);                 /* 0x40be30 */
bool Ctl_Damping(int player);              /* 0x40be60 */
void Ctl_Bind(int player);                 /* 0x40be90: the device's keys into the player's record */
void Ctl_Load(void);                       /* 0x40bf70 */
void Ctl_Save(void);                       /* 0x40bff0 */
void Ctl_Configure(void);                  /* 0x40c050: a record for each device there */
void Ctl_SetBit(uint32_t cfg, int action, uint16_t bit);   /* 0x40c130 */
void Ctl_SetKey(uint32_t cfg, int action, uint8_t key);    /* 0x40c550 */
int Ctl_Axis(int player, int slot);        /* 0x40c210: the slot's axis, -1 none */
uint16_t Ctl_Mask(int player, int action); /* 0x40c2a0: the action's bits in the player's record */
void Ctl_AxisPresses(void);                /* 0x40bad0: the pedal axes as up and down presses */
