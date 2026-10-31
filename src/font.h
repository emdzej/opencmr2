/* Fonts (docs/formats/media.md#fonts-pcf-tga): <name>.pcf metrics + <name>.tga atlas, in the front-end and
   in-game archives. Drawing is the renderer's (FUN_0040b880 and friends, step 4); this is the file and
   the measuring. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    const uint8_t *data;    /* the .pcf (kept by the caller) */
    size_t size;
    uint32_t nglyphs;
    int height, space, tracking;
} Font;

typedef struct {
    int x, y, w, h;         /* the glyph's rectangle in the atlas */
    int ch;                 /* its character */
    int nkern;
    const uint8_t *kern;    /* nkern pairs: u8 next character, u8 unused, i16 adjustment; ascending */
} Glyph;

/* Font_Set 0x40b350: the header, the 256-entry character map at +0x18 and the glyph records at +0x218,
   whose kerning offsets the original relocates; the port checks them when used (Font_Glyph). */
bool Font_Set(Font *f, const uint8_t *pcf, size_t size);
/* The glyph for character c, or false if the font lacks it (map entry -1). */
bool Font_Glyph(const Font *f, unsigned char c, Glyph *g);
/* Font_TextWidth 0x40b5b0: the width of s in pixels, up to the first '\n' or '^' (font.c). */
int Font_TextWidth(const Font *f, const char *s);
