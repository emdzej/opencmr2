/* The rally's weather (0x40d6c0-0x40dc30): before a rally (or stage) the front end picks the weather
   of each of its stages (0x52f100, two words a stage: at the start and at the end), the light level
   of each (0x52f294), the main type around the start's share (0x52f1a0) and the stages with rain
   (0x52f1f0, 0x52f240), from the rally's three tables of ten weather types (0x51627c, 0x78 bytes a
   rally: a stage block of four stages each). A weather chosen on the menus (0x52f290 set, 0x52f0fc
   the type) makes it the same everywhere. */
#pragma once

void Weather_Choose(void);   /* 0x40dc30 */
