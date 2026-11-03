/* The menus' item strip (docs/re/frontend.md#the-strip): a page's items in a row across the screen
   near the bottom, the selected one at a fixed place, sliding there in a quarter of a second. Twelve
   scrollers (0x819140, 0x78 bytes apart) keep each page's item widths and the slide. */
#pragma once
#include "fe/menu.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    int32_t off, off0;      /* +0x00, +0x04: the slide's offset now and at its start */
    int32_t n;              /* +0x08 */
    int32_t width[MENU_ITEMS];   /* +0x0c: the items' text widths (hel_36pt) */
    int32_t gap;            /* +0x64: 24 at 640 */
    int32_t cur, prev;      /* +0x68, +0x6c: the selection, -1 none */
    uint32_t t;             /* +0x70: when the selection changed */
    Menu *page;             /* +0x74 */
} FeScroll;

FeScroll *Fe_Scroll(int k);   /* 0x4f24f0 (0), 0x4f2500 (2), 0x4f2510 (3), 0x4f2520 (4) ... 0x4f2580 (11) */
/* the enter callbacks' measuring (0x4ef300, 0x4f3400, 0x4f3610): the widths of the page's items in
   lower case, the slide at rest on the selection */
void Scroll_Measure(FeScroll *s, Menu *m);
uint32_t Scroll_Update(FeScroll *s);   /* 0x4f37c0 */
void Fe_ScrollWidths(void);                        /* 0x4ef190 */
void Strip_Draw(FeScroll *s, bool lit);            /* 0x4d2cd0 */
void Strip_DrawMain(Menu *m, bool lit, const char *hint);   /* 0x4d45b0 (scroller 0) */
void Fe_Hint(const char *s, bool steady);          /* 0x4d4460 */
/* 0x4d40d0: the page's title after its ancestors' (up to the main menu), from x; the next x */
int Fe_Titles(Menu *m, int x, int y, bool current, int depth, const char *const *names, int k);
void Fe_Header(void);                              /* 0x4d41e0 */
/* 0x4d28c0: an 18 x 12 dot picture (cells index the palette 0x523ef0) waving at (x, y) */
void Fe_DotPicture(int16_t x, int16_t y, const uint8_t *cells);
uint32_t Fe_MainTime(void);                        /* DAT_0081912c */
