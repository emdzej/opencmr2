/* The selection word's flags from the game mode (0x408020): bits 21-31 of 0x52f2ac, which the race
   reads (Gs_SelBit24-28 and others). Modes: 0-4 rally (0 championship, 1 single rally, 2 single stage,
   3 time trial, 4 challenge), 5-7 arcade, 8-12 network. */
#pragma once

void Sel_ModeFlags(void);   /* 0x408020 */
void Sel_Reset(void);       /* 0x409a30 */
#include <stdbool.h>
bool Sel_NextStage(void);   /* 0x4072d0 */
bool Sel_ArcadeNext(void);  /* 0x4074a0 */

/* 0x4069a0: the CPU cars of an arcade race (0x52f2a9's low four bits); 0x40d640 sets them */
int Sel_CpuCount(void);
void Sel_SetCpuCount(int n);
/* 0x40dfa0 (the arcade's car chosen): the six CPU cars at 0x52ea98 by the first player's car's class:
   its own car and one or two others of the same group (0x40e180, 0x40e210), the cheat's fixed sets */
void Sel_CpuCars(void);
int Sel_CpuCar(int i);   /* 0x52ea98 + 4 i */
/* 0x4069c0: the challenge's draw from its depth (0x52f2b4 bits 0-2) and the opponents' level */
void Sel_ChallengeDraw(void);
