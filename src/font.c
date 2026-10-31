/* Fonts (see font.h).

   .pcf: +0x00 u32 3, +0x04 u32 glyph count, +0x08 u32 line height, +0x0c u32 space width, +0x10 u32
   tracking (added after every glyph), +0x14 u32 0; +0x18 u16[256] glyph index per character (-1: none);
   +0x218 glyph records of 16 bytes: u16 x, y, w, h in the atlas, u8 character, u8 kerning pair count,
   u16 unused, u32 file offset of the kerning pairs (relocated by Font_Set). The pairs follow the
   records. */
#include "font.h"
#include "bfl.h"
#include "crt.h"
#include "render/tex.h"
#include "render/twod.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

bool Font_Parse(Font *f, const uint8_t *d, size_t size)
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
int Font_Width(const Font *f, const char *str)
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

typedef struct {
    Font f;
    Tex *tex;          /* +0x08 */
    uint8_t *owned;    /* +0x10 when not from an archive */
    bool loaded;       /* +0x14 */
} Slot;

static Slot *slots;    /* DAT_0053223c */
static int nslots;     /* DAT_00532240 */
static int layer = 1;  /* DAT_00532138 */
static const Slot *cur;          /* DAT_00532244 */
static uint8_t cur_rgba[4];      /* DAT_00532248 */

void Font_InitSlots(int n)
{
    layer = 1;
    Font_FreeSlots();
    slots = calloc((size_t)(n & 0xff) ? (size_t)(n & 0xff) : 1, sizeof *slots);
    nslots = slots ? n & 0xff : 0;
}

void Font_FreeSlot(int i)
{
    if (i < 0 || i >= nslots) return;
    free(slots[i].owned);
    slots[i].owned = NULL;
    slots[i].f.data = NULL;
}

void Font_FreeSlots(void)
{
    for (int i = 0; i < nslots; i++) Font_FreeSlot(i);
    free(slots);
    slots = NULL;
    nslots = 0;
}

void Font_Set(const uint8_t *pcf, size_t size, Tex *tex, int i, bool in_archive)
{
    if (i < 0 || i >= nslots || !pcf || !tex) return;
    Slot *s = &slots[i];
    s->tex = tex;
    s->owned = in_archive ? NULL : (uint8_t *)pcf;
    Font f;
    if (Font_Parse(&f, pcf, size)) {
        s->f = f;
        s->loaded = true;
    }
}

void Font_LoadSlot(const char *dir, const char *name, const void *archive, int i)
{
    if (i < 0 || i >= nslots) return;
    char path[260];
    snprintf(path, sizeof path, "%s\\%s.tga", dir, name);
    Tex *t = Bfl_FindTexture(archive, path, NULL, false, 0);
    if (!t) return;
    size_t l = strlen(path);
    memcpy(path + l - 4, ".pcf", 5);
    uint8_t found = 0;
    uint32_t size = 0;
    const uint8_t *p = Bfl_FindFile(archive, path, &found, &size, false);
    if (p) Font_Set(p, size, t, i, found != 0);
}

const Font *Font_Get(int i) { return i >= 0 && i < nslots && slots[i].loaded ? &slots[i].f : NULL; }

int Font_TextWidth(int i, const char *s)
{
    const Font *f = Font_Get(i & 0xff);
    return f ? Font_Width(f, s) : 0;
}

int Font_TextHeight(int i, const char *s)
{
    const Font *f = Font_Get(i & 0xff);
    if (!f) return 0;
    int lines = 1;
    for (; *s; s++)
        if (*s == '\n' || *s == '^') lines++;
    return (f->height + 1) * lines - 1 + (int)rd32(f->data + 0x14);
}

int Font_Height(int i)
{
    const Font *f = Font_Get(i & 0xff);
    return f ? f->height : 0;
}

void Font_SetLayer(int l) { layer = l; }

/* Font_SetCurrent 0x40b580 */
static void set_current(int i, const uint8_t rgba[4])
{
    cur = &slots[i];
    memcpy(cur_rgba, rgba, 4);
}

/* Font_DrawGlyph 0x40b7c0: the glyph's atlas rectangle at (x, y) as a sprite turning about its middle
   (angle 0), flip value 8 (no flip). */
static void draw_glyph(unsigned char ch, int16_t x, int16_t y)
{
    Glyph g;
    if (!Font_Glyph(&cur->f, ch, &g)) return;
    Rect16 src = {(int16_t)g.x, (int16_t)g.y, (int16_t)g.w, (int16_t)g.h};
    Rect16 dst = {x, y, (int16_t)g.w, (int16_t)g.h};
    int32_t centre[3] = {g.w / 2 + x, g.h / 2 + y, 0};
    Sprite_Add(&src, &dst, cur->tex, layer, 0, centre, NULL, cur_rgba, 8);
}

/* x and y are 16-bit in effect: the original adds the advances into registers whose upper halves hold
   leftovers, but only the low halves reach the glyphs. The slot test is "not above the count", one past
   the table; the port treats that as not loaded. */
void Font_Print(int slot, const char *str, int x, int y, const uint8_t rgba[4], uint32_t flags)
{
    crt_rand();
    crt_rand();
    crt_rand();
    int i0 = slot & 0xff;
    const Font *f = Font_Get(i0);
    if (!f) return;
    const unsigned char *s = (const unsigned char *)str;
    int len = (int)strlen(str);
    const uint8_t *h = f->data;
    int16_t cx = (int16_t)x, cy = (int16_t)y;
    if (flags & 6) {
        int w = Font_Width(f, str);
        if (flags & 2) w /= 2;
        cx = (int16_t)(x - w);
    }
    if (flags & 0x10) cy = (int16_t)(cy - h[0x218 + 10]);
    if (flags & 0x20) cy = (int16_t)(cy - (int16_t)rd16(h + 8));
    set_current(i0, rgba);
    for (int i = 0; i < len; i++) {
        unsigned char c = s[i];
        if (c == ' ') cx = (int16_t)(cx + (int16_t)rd16(h + 0x0c));
        else if (c == '\n' || c == '^') {
            cx = (int16_t)x;
            if (flags & 6) {
                int w = Font_Width(f, str + i + 1);
                if (flags & 2) w /= 2;
                cx = (int16_t)(x - w);
            }
            cy = (int16_t)(cy + (int16_t)rd16(h + 0x14) + (int16_t)rd16(h + 8));
        } else {
            Glyph g;
            if (!Font_Glyph(f, c, &g)) continue;
            int16_t adv = (int16_t)rd16(h + 0x10);
            if (i < len - 1 && glyph_index(f, s[i + 1]) >= 0)
                for (int j = 0; j < g.nkern; j++) {
                    const uint8_t *k = g.kern + 4 * j;
                    if (s[i + 1] <= k[0]) {
                        if (k[0] == s[i + 1]) adv = (int16_t)(adv + (int16_t)rd16(k + 2));
                        break;
                    }
                }
            draw_glyph(c, cx, cy);
            cx = (int16_t)(cx + g.w + adv);
        }
    }
}
