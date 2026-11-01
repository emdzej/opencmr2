/* The data paths (see paths.h). */
#include "game/paths.h"
#include "exe.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static char tracks[64], cars[64], sounds[64], saves[64], music[64], videos[64], frontend[64], country[64],
    country_cd[64], setuprep[64];

/* the formats (0x516b0c ...), each with the root "" */
static void dir(char *d, uint32_t fmt) { snprintf(d, 64, exe_str(fmt), ""); }

void Paths_Init(void)
{
    dir(tracks, 0x516b0c);
    dir(cars, 0x516afc);
    dir(sounds, 0x516af0);
    dir(saves, 0x516ae0);
    dir(music, 0x516ad0);
    dir(videos, 0x516abc);
    dir(frontend, 0x516ab0);
    dir(country, 0x516a9c);
    dir(country_cd, 0x516a9c);
    dir(setuprep, 0x516a90);
}

const char *Path_Tracks(void) { return tracks; }
const char *Path_Cars(void) { return cars; }
const char *Path_Sounds(void) { return sounds; }
const char *Path_Saves(void) { return saves; }
const char *Path_Music(void) { return music; }
const char *Path_Videos(void) { return videos; }
const char *Path_FrontEnd(void) { return frontend; }
const char *Path_Country(void) { return country; }
const char *Path_CountryCd(void) { return country_cd; }
const char *Path_SetupRep(void) { return setuprep; }
const char *Path_Root(void) { return ""; }

void Path_Format(char *dst, size_t cap, uint32_t fmt, ...)
{
    char b[300];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, exe_str(fmt), ap);
    va_end(ap);
    const char *p = b;
    while (*p == '\\') p++;
    snprintf(dst, cap, "%s", p);
}
