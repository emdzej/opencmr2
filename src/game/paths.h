/* The data paths (Paths_Init 0x40e8d0): the directories the game builds from the registry's hard disk
   and CD paths and the install type, each "%s\<dir>" with one of them. The port has one root, the CD
   (or its image), so every directory is "\<dir>"; Path_Format drops the leading backslash when a path
   is used. */
#pragma once
#include <stddef.h>
#include <stdint.h>

void Paths_Init(void);
const char *Path_Tracks(void);       /* 0x5361a8 */
const char *Path_Cars(void);         /* 0x5364b4 */
const char *Path_Sounds(void);       /* 0x535fa0 */
const char *Path_Saves(void);        /* 0x5365b8 */
const char *Path_Music(void);        /* 0x40ed70 (0x5368c4) */
const char *Path_Videos(void);       /* 0x40ed80 (0x5360a4) */
const char *Path_FrontEnd(void);     /* 0x40ed90 (0x5366bc) */
const char *Path_Country(void);      /* 0x40eda0 (0x535b90) */
const char *Path_CountryCd(void);    /* 0x40edd0 (0x535d98) */
const char *Path_SetupRep(void);     /* 0x535e9c */
const char *Path_Root(void);         /* 0x4aa710 / 0x4aa700: the CD and hard disk roots, "" */
/* sprintf with the exe's format at fmt, then without the leading backslashes */
void Path_Format(char *dst, size_t cap, uint32_t fmt, ...);
