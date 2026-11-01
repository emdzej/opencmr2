/* What the pages' callbacks share (0x4d2000-0x4f4000). */
#pragma once
#include "fe/menu.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void Fe_SetPage(Menu *m);                 /* DAT_00818abc */
void Fe_SetScrollPos(int i);              /* 0x4ef4a0: DAT_00525398 and DAT_0052539c */
extern const char *g_fe_difficulty[3];    /* 0x81853c: Novice, Intermediate, Expert */
static inline bool Fe_CreditsShown(void) { return true; }   /* 0x4eaa30 */
static inline uint32_t Fe_DisplayModes(void) { return 1; }  /* 0x4a8be0: the display modes found (the port's one) */
static inline int Input_DeviceCount(void) { return 1; }     /* 0x49ef90: the keyboard (and pad) */
uint32_t Fe_MenuTime(void);               /* DAT_00817ff4 */
void Fe_SetScrollPrev(int i);             /* DAT_00525398 */
void Fe_DifficultyNames(void);            /* 0x4d2790 */
void Fe_BuildControllers(void);           /* 0x4fa4d0 */
void Fe_SetMode(uint32_t m);              /* 0x4ea8e0: the mode word's bits 3-9 (and 0x52e920 for the network) */
void Fe_SetPlayer(int n);                 /* 0x4ea480: the player whose input runs the menus (1-3 when
                                             not alternating and not a challenge, else 0) */
/* 0x4d3360: the page's items as a column of arrowed rows (the selected lit), from item `first`,
   centred on cy (-1: the screen's middle above the strip) at x (-1: 122 of 640); with hdr a header
   row on a band first */
void List_Draw(Menu *m, const char *hdr, int cy, int x, int first, bool lit);
/* 0x4d3fa0: n titles in a row, the last lit */
void Fe_TitleRow(int x, int y, const char *const *names, int n);
int Fe_ToSet(void);                       /* DAT_00819048: the players still to set up */
int Fe_SetupPlayer(void);                 /* players - DAT_00819048 - 1: the one being set up */
void Fe_PlayerTitle(char *dst, size_t cap);   /* "Player n" for that player */
