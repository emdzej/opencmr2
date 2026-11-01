/* Stage_Load 0x41f930 (docs/re/world.md#loading-a-stage): the stage's archives and files in the
   original's order. stage: the lower-case id ("fra02"); path, tex: its <stage>hi.bfl and the
   country's <ccc>hit.bfl (the original builds them from the selection). */
#pragma once
#include <stdbool.h>
#include <stddef.h>

bool Stage_Load(const char *stage, const char *path, const char *tex);
bool Stage_Find(const char *stage, char *path, char *tex, size_t cap);   /* the port */
void Stage_Name(char *dst, size_t cap);   /* the port: "fra02" from the selection */
