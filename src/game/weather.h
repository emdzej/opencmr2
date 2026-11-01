/* The rally's weather (0x40d6c0-0x40dc30): before a rally (or stage) the front end picks the weather
   of each of its stages (0x52f100, two words a stage: at the start and at the end), the light level
   of each (0x52f294), the main type around the start's share (0x52f1a0) and the stages with rain
   (0x52f1f0, 0x52f240), from the rally's three tables of ten weather types (0x51627c, 0x78 bytes a
   rally: a stage block of four stages each). A weather chosen on the menus (0x52f290 set, 0x52f0fc
   the type) makes it the same everywhere. */
#pragma once

void Weather_Choose(void);   /* 0x40dc30 */
#include <stdbool.h>
#include <stdint.h>
/* 0x460bf0 / 0x460c10: car n's weather record (0x547ac8 + 0x178 n) wet, and its level; the records
   (Weather_Step 0x45f4a0) are not ported: dry */
bool Weather_Wet(int car);
uint8_t Weather_Kind(int car);   /* 0x460bf0 as the original: the record's type byte (callers test 1 and 2) */
int32_t Weather_Level(int car);
