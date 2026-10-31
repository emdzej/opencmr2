/* Fonts (see font.h).

   .pcf: +0x00 u32 3, +0x04 u32 glyph count, +0x08 u32 line height, +0x0c u32 space width, +0x10 u32
   tracking (added after every glyph), +0x14 u32 0; +0x18 u16[256] glyph index per character (-1: none);
   +0x218 glyph records of 16 bytes: u16 x, y, w, h in the atlas, u8 character, u8 kerning pair count,
   u16 unused, u32 file offset of the kerning pairs (relocated by Font_Set). The pairs follow the
   records. */
#include "font.h"

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

bool Font_Set(Font *f, const uint8_t *d, size_t size)
{
    if (!d || size < 0x218) return false;
    f->data = d;
    f->size = size;
    f->nglyphs = rd32(d + 4);
    f->height = (int)rd32(d + 8);
    f->space = (int)rd32(d + 0x0c);
    f->tracking = (int)rd32(d + 0x10);
    /* the original marks a font with no glyphs as not loaded (Font_TextWidth then gives 0) */
    if (!f->nglyphs || (size - 0x218) / 16 < f->nglyphs) return false;
    for (int c = 0; c < 256; c++) {
        uint16_t gi = rd16(d + 0x18 + 2 * c);
        if (gi != 0xffff && gi >= f->nglyphs) return false;
    }
    return true;
}

static int glyph_index(const Font *f, unsigned char c) { return (int16_t)rd16(f->data + 0x18 + 2 * c); }

bool Font_Glyph(const Font *f, unsigned char c, Glyph *g)
{
    int i = glyph_index(f, c);
    if (i < 0) return false;
    const uint8_t *r = f->data + 0x218 + (size_t)i * 16;
    g->x = rd16(r);
    g->y = rd16(r + 2);
    g->w = (int16_t)rd16(r + 4);
    g->h = (int16_t)rd16(r + 6);
    g->ch = r[8];
    g->nkern = r[9];
    uint32_t k = rd32(r + 12);
    /* com1024(C).bfl hel_20pt: the glyph for 0xcd is a record of 0xcd bytes, kerning offset included;
       the original would read 0xcdcdcdcd past the file (and crash) when measuring 0xcd before another
       glyph. The port gives such a glyph no kerning. */
    if (k > f->size || (f->size - k) / 4 < (size_t)g->nkern) g->nkern = 0, k = 0;
    g->kern = f->data + k;
    return true;
}

static bool line_end(unsigned char c) { return c == 0 || c == '\n' || c == '^'; }

/* Despite the loop over the string, the walk stops at the first '\n' or '^': the width is the first
   line's. A space adds the space width (no tracking); a glyph adds its width and the tracking, or the
   kerning adjustment plus the tracking when the next character has a glyph and a pair (the pairs are
   searched in order up to the first one not below the next character). Characters without a glyph add
   nothing. The sums are 16-bit, as in the original. */
int Font_TextWidth(const Font *f, const char *str)
{
    const unsigned char *s = (const unsigned char *)str;
    int16_t x = 0, widest = 0, track = (int16_t)f->tracking;
    for (size_t i = 0; s[i]; i++) {
        unsigned char c = s[i], next = s[i + 1];
        if (c == '\n' || c == '^') break;
        if (c == ' ') x = (int16_t)(x + (int16_t)f->space);
        else {
            Glyph g;
            if (Font_Glyph(f, c, &g)) {
                int16_t adv = track;
                if (!line_end(next) && glyph_index(f, next) >= 0)
                    for (int j = 0; j < g.nkern; j++) {
                        const uint8_t *k = g.kern + 4 * j;
                        if (next <= k[0]) {
                            if (k[0] == next) adv = (int16_t)((int16_t)rd16(k + 2) + track);
                            break;
                        }
                    }
                x = (int16_t)(x + g.w + adv);
            }
        }
        if (line_end(next) && widest < x) widest = x;
    }
    return widest;
}
