/* --param frametime=1: where a slow frame's time went (platform_gasm.c logs the sections with each frame
   slower than the frame rate). A port tool: nothing of the original's. */
#pragma once
#include "platform.h"
#include <stdbool.h>

enum { PROF_SIM, PROF_WORLD, PROF_CUBE, PROF_GHOST, PROF_POLY, PROF_VBUP, PROF_TEXUP, PROF_PRESENT, PROF_N };
extern bool g_prof_on;
extern double g_prof[PROF_N];
extern unsigned g_prof_count[PROF_N];

#define PROF_BEGIN(s) double prof_t0_##s = g_prof_on ? plat_ms() : 0
#define PROF_END(s) do { if (g_prof_on) g_prof[s] += plat_ms() - prof_t0_##s, g_prof_count[s]++; } while (0)
