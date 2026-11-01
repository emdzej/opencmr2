/* The game's string table (docs/formats/media.md#text): one table at a time, built from one or more
   text files (one per language) of a fixed number of strings each; strings are fetched by number from the
   selected file. The front end builds it from f<language>.txt (533 strings), the stage screens from
   s<language>.txt (326), the race from g<language>.txt (260); each build replaces the table. */
#pragma once
#include <stddef.h>
#include <stdint.h>

/* Text_Build 0x4a3c90: nfiles files of count strings each. The port copies the files (the original cuts
   the archive members in place). */
void Text_Build(int nfiles, int count, const uint8_t *const *files, const size_t *sizes);
void Text_Select(int file);       /* Text_Select 0x4a3c30 */
const char *Text_Get(int i);      /* Text_Get 0x4a3c60 */
void Text_Free(void);             /* Text_Free 0x4a3d80 */
char *Text_ToFontCase(char *s);   /* Text_ToFontCase 0x4a9f90, in place */
char *Text_Lower(char *s);       /* 0x4aa090: lower case for the fonts, and their stand-ins for a few signs; in place */
