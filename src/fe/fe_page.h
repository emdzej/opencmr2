/* What the pages' callbacks share (0x4d2000-0x4f4000). */
#pragma once
#include "fe/menu.h"
#include <stdbool.h>
#include <stdint.h>

void Fe_SetPage(Menu *m);                 /* DAT_00818abc */
void Fe_SetScrollPos(int i);              /* 0x4ef4a0: DAT_00525398 and DAT_0052539c */
extern const char *g_fe_difficulty[3];    /* 0x81853c: Novice, Intermediate, Expert */
static inline bool Fe_CreditsShown(void) { return true; }   /* 0x4eaa30 */
static inline uint32_t Fe_DisplayModes(void) { return 1; }  /* 0x4a8be0: the display modes found (the port's one) */
static inline int Input_DeviceCount(void) { return 1; }     /* 0x49ef90: the keyboard (and pad) */
uint32_t Fe_MenuTime(void);               /* DAT_00817ff4 */
