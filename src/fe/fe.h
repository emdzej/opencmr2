/* The front end (docs/re/frontend.md): the top-level mode 0 of Game_Frame 0x4d0780, one process
   (group 0x817da0, states 0x523bc8, transitions 0x523c18) through the boot screens, the menus and the
   way out to a race: 0 the start-up (0x4d15e0), 1 the copyright screen, 2 the Bink credit, 3 and 4 the
   Codemasters and intro movies, 5 the title, 6 the menus, 7 the rally's movie, 8 a pause, 9 out to the
   stage set-up (mode 2) or the race (mode 3). */
#pragma once
#include "fe/menu.h"
#include "bfl.h"
#include "render/tex.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

int Game_Frame(void);        /* 0x4d0780 */
void Game_SetMode(uint32_t m);   /* 0x4057e0 */

/* the front end's archives: Res640.bfl (0x818250, 0x4d21a0), sFERes640_c.bfl (0x818260, 0x4d21b0),
   Common.bfl (0x818240, 0x4d2190), the languages' text archives (0x818200, 12 bytes apart, 0x4d21c0) */
BflSlot *Fe_Res(void);
BflSlot *Fe_ResLang(void);
BflSlot *Fe_Common(void);
BflSlot *Fe_Text(int lang);

/* "%s<rest>" path formats in the exe with the original's root (0x4aa710; the port's paths are
   relative, so "" and the leading backslash dropped) */
void Fe_Path(char *dst, size_t cap, uint32_t fmt, const char *arg);
Tex *Fe_Texture(const BflSlot *arc, uint32_t fmt, const char *arg);

uint32_t Fe_Now(void);        /* 0x4d20e0: the frame's time (DAT_00817fe4) */
void Fe_Stamp(void);          /* 0x4ea9f0: DAT_0052af90 = now */
uint32_t Fe_Stamped(void);    /* 0x4eaa00 */

/* The current page (DAT_00818abc) and the one to show next (DAT_00818ac0) */
Menu *Fe_Page(void);
void Fe_SetNextPage(Menu *m);
/* 0x49c1c0 on the front end's process */
void Fe_Event(uint32_t event, uint32_t prio);

int Fe_Player(void);          /* 0x4ea500: DAT_00818ac8, the player whose input runs the menus */
