/* Stage_Load 0x41f930 (docs/re/world.md#loading-a-stage): the stage's archives and files in the
   original's order. stage: the lower-case id ("fra02"); path, tex: its <stage>hi.bfl and the
   country's <ccc>hit.bfl (the original builds them from the selection). */
#pragma once
#include <stdbool.h>

bool Stage_Load(const char *stage, const char *path, const char *tex);
