/* The selection word's flags from the game mode (0x408020): bits 21-31 of 0x52f2ac, which the race
   reads (Gs_SelBit24-28 and others). Modes: 0-4 rally (0 championship, 1 single rally, 2 single stage,
   3 time trial, 4 challenge), 5-7 arcade, 8-12 network. */
#pragma once

void Sel_ModeFlags(void);   /* 0x408020 */
void Sel_Reset(void);       /* 0x409a30 */
