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
/* The rows of the option pages (0x4d8ed0's layout, drawn inline by 0x4da710, 0x4daf90, 0x4d7750,
   0x4e2610 and others): the lines between (lit about the chosen), the arrows Ar_640A lit, Ar_640D
   (or, when the item is off and `third`, the third colour), each label by `row_fn` at x 122 of 640
   and 24 of 480 under its line. Rows_Top is the usual top (the rows centred between 56 and 374 of
   480), Rows_ArrowDy the usual arrow offset (20 of 480). */
typedef void RowFn(Menu *m, MenuItem *it, bool two, int x, int y, uint32_t c, int w);
void Rows_Draw(Menu *m, RowFn *row_fn, bool two, bool third, int top, int arrow_dy);
int Rows_Top(const Menu *m);
int Rows_ArrowDy(void);
/* 0x4d3fa0: n titles in a row, the last lit */
void Fe_TitleRow(int x, int y, const char *const *names, int n);
void Fe_SetPlayersPage(Menu *m);          /* 0x4f1bc0: DAT_00819870 */
void Fe_SetToSet(int n);                  /* 0x4f1bb0 */
int Fe_RallyWeathers(void);               /* 0x4081d0: the rally's distinct weathers (0x52ea74), their count */
int Fe_ToSet(void);                       /* DAT_00819048: the players still to set up */
int Fe_SetupPlayer(void);                 /* players - DAT_00819048 - 1: the one being set up */
void Fe_PlayerTitle(char *dst, size_t cap);   /* "Player n" for that player */
void Fe_Leave(bool game, bool stage, bool race);   /* 0x4d2070 */
void Fe_SetCar(int player, int car);                /* 0x408600 */
void Fe_SetPlayerName(int player, const char *s);   /* 0x4eae90 */
/* The attract-mode demo (0x52ea52): its set-up (0x4e9e40, after the selection kept by 0x406470), one
   of four in turn (0x52f3d8): three single stages from the disc's runs, an arcade race; and the
   selection back (0x4ead10) */
void Fe_DemoSetup(void);
void Fe_DemoRestore(void);
void Fe_SetAutoBox(int player, uint8_t v);         /* 0x4eb0c0: the profile's automatic box (+0x54 bit 5) */
void Fe_ChampTables(void);                /* 0x4cf060 */
void Fe_OptionsMode(void);                /* 0x4eabc0 */
void Fe_CarSetups(void);                  /* 0x406820 */
void Fe_AgeProfiles(void);                /* 0x4ec1a0 */
void Fe_Teardown(void);                   /* 0x49c0f0 to the mark, 0x4a5be0, 0x4a5d10 */
void Fe_ClearGuests(void);                /* DAT_00819744 = 0 */
void Fe_LettersInit(void);                 /* 0x4f3dd0 */
void Fe_LettersUpdate(void);               /* 0x4f4050 */
void Fe_LettersDraw(void);                 /* 0x4f45a0 */
void Fe_LettersTrail(void);                /* 0x4f4650 */
void Fe_LettersTrails(void);               /* 0x4f4760 */
int Fe_ScrollPrev(void);                   /* 0x4f25b0: DAT_00525398 */
