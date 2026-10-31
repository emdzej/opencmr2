/* The string table (see text.h). */
#include "text.h"
#include "exe.h"
#include <stdlib.h>
#include <string.h>

static char **table;         /* DAT_0065aa6c: nfiles * count string pointers */
static char **copies;        /* the port's copies of the files */
static int nfiles;           /* DAT_0065aa71 */
static int per_file;         /* DAT_0065aa78 */
static int first;            /* DAT_0065aa74: the selected file's first entry */

/* Text_Next 0x4a3d50: the string ends at the first byte below 0x20, which becomes NUL; the next one starts
   at the next byte of 0x20 or more. So empty lines are skipped, not counted: a file's strings are its
   non-empty lines. The original has no end test (strings past the end of a file run into whatever
   follows it in memory); the port's copy ends in NUL padding and the walk stops there, giving "". */
static char *text_next(char *s, const char *end, int more)
{
    while (s < end && (unsigned char)*s > 0x1f) s++;
    if (s < end) *s = 0;
    if (!more || s >= end) return NULL;
    do s++;
    while (s < end && (unsigned char)*s < 0x20);
    return s;
}

void Text_Build(int n, int count, const uint8_t *const *files, const size_t *sizes)
{
    Text_Free();
    nfiles = n;
    per_file = count;
    first = 0;
    table = calloc((size_t)(n * count ? n * count : 1), sizeof *table);
    copies = calloc((size_t)(n ? n : 1), sizeof *copies);
    if (!table || !copies) return;
    static char empty[1];
    for (int f = 0; f < n; f++) {
        char *s = NULL, *end = NULL;
        if (files[f] && (copies[f] = malloc(sizes[f] + 1))) {
            memcpy(copies[f], files[f], sizes[f]);
            copies[f][sizes[f]] = 0;
            s = copies[f];
            end = s + sizes[f];
        }
        for (int i = 0; i < count; i++) {
            table[f * count + i] = s ? s : empty;
            if (s) s = text_next(s, end, i < count - 1);
        }
    }
}

void Text_Select(int f)
{
    if (f >= 0 && f < nfiles) first = per_file * f;   /* the original lets a negative file through */
}

const char *Text_Get(int i)
{
    if (table && i >= 0 && i < per_file) return table[first + i];
    return exe_str(0x520b00);   /* the "invalid" message */
}

void Text_Free(void)
{
    for (int f = 0; copies && f < nfiles; f++) free(copies[f]);
    free(copies);
    free(table);
    copies = NULL;
    table = NULL;
    nfiles = per_file = first = 0;
}

/* Text_ToFontCase 0x4a9f90: upper case in place, a-z and the Latin-1 letters 0xe0-0xff (so also 0xf7
   and 0xff), then a few symbols moved onto the font's glyph slots; the switch looks at the byte before
   the case change. */
char *Text_ToFontCase(char *str)
{
    for (unsigned char *p = (unsigned char *)str; *p; p++) {
        unsigned char c = *p;
        if ((c > '`' && c < '{') || c >= 0xe0) *p = (unsigned char)(c - 0x20);
        switch (c) {
        case 0xa3: *p = 0x7e; break;
        case 0xa9: *p = 0x24; break;
        case 0xae: *p = 0x2a; break;
        case 0xb1: *p = 0x7c; break;
        case 0xbc: *p = 0xf7; break;
        case 0xbd: *p = 0x7b; break;
        case 0xbe: *p = 0x7d; break;
        case 0xd7: *p = 0x99; break;
        case 0xf3: *p = 0xd3; break;
        }
    }
    return str;
}
