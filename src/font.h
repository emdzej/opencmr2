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

/* The .pcf as Font_Set 0x40b350 reads it: the header, the 256-entry character map at +0x18 and the glyph
   records at +0x218, whose kerning offsets the original relocates; the port checks them when used
   (Font_Glyph). False for no glyphs (the original then leaves the slot as it was). */
bool Font_Parse(Font *f, const uint8_t *pcf, size_t size);
/* The glyph for character c, or false if the font lacks it (map entry -1). */
bool Font_Glyph(const Font *f, unsigned char c, Glyph *g);
/* The measuring of Font_TextWidth 0x40b5b0: the width of s in pixels, up to the first '\n' or '^'
   (font.c). */
int Font_Width(const Font *f, const char *s);

/* The font slots (DAT_0053223c, 0x18 bytes each in the original): the parsed .pcf, its atlas texture. */
typedef struct Tex Tex;
void Font_InitSlots(int n);                       /* 0x40b220 (also the font layer back to 1) */
void Font_FreeSlots(void);                        /* 0x40b290 */
void Font_FreeSlot(int slot);                     /* 0x40b2f0 */
/* Font_Set 0x40b350: pcf (from an archive when in_archive, else owned and freed with the slot) and its
   texture into the slot. */
void Font_Set(const uint8_t *pcf, size_t size, Tex *tex, int slot, bool in_archive);
/* Font_LoadSlot 0x40b430: <dir>\<name>.tga as a texture and <dir>\<name>.pcf from the archive. */
void Font_LoadSlot(const char *dir, const char *name, const void *archive, int slot);
const Font *Font_Get(int slot);                   /* NULL unless loaded */
int Font_TextWidth(int slot, const char *s);      /* 0x40b5b0 */
int Font_TextHeight(int slot, const char *s);     /* 0x40b730: lines * (height + 1) - 1 + the line gap */
int Font_Height(int slot);                        /* 0x40b790 */
void Font_SetLayer(int layer);                    /* 0x40bab0: the sprite layer glyphs go to */
/* Font_Print 0x40b880: s at (x, y) in rgba; flags 4 right-aligned, 2 centred (each line), 0x10 up by the
   first glyph's byte +0x0a, 0x20 up by the height. Draws three numbers from rand() first. */
void Font_Print(int slot, const char *s, int x, int y, const uint8_t rgba[4], uint32_t flags);
void Font_SetCurrent(int slot, const uint8_t rgba[4]);    /* 0x40b580 */
/* 0x40b7c0: one glyph of the current font at (x, y), turning about its middle (angle 0) */
void Font_DrawGlyph(unsigned char ch, int16_t x, int16_t y);
