/* The screen's colour test (see readback.h). */
#include "render/readback.h"
#include "render/d3d7.h"
#include <stdlib.h>

static Rect16 rect;      /* 0x8164d0 */
static uint8_t col[3];   /* 0x8164d8 */
static uint8_t share;    /* 0x8164dc */
static uint8_t tol;      /* 0x8164dd */

uint8_t Screen_ColourQuery(const Rect16 *r, const uint8_t c[3], uint8_t t)
{
    rect = *r;
    col[0] = c[0], col[1] = c[1], col[2] = c[2];
    tol = t;
    return share;
}

/* The rectangle clipped to the screen (one that starts past an edge, or lies wholly beyond the left or
   top, measures nothing). The back buffer is copied to an offscreen surface (0x4bca70 made it) and
   locked; a pixel counts when its red is within the tolerance of the colour's byte 0, its green of
   byte 1 and its blue of byte 2. The bytes are the disc's colour as stored, B, G, R: so the red is
   tested against blue and the blue against red (kept). The share is of the unclipped area, so the part
   off the screen counts as not matching. The original reads a 16-bit mode's channels with their low
   bits zero; the port's back buffer is 32-bit. */
void Screen_ColourMeasure(void)
{
    int area = rect.w * rect.h;
    if (!area) return;
    int sw, sh;
    D3d_ScreenSize(&sw, &sh);
    int16_t x = rect.x, y = rect.y, w = rect.w, h = rect.h;
    if (x < 0) {
        if (-x < w) w = (int16_t)(w + x);
        else w = 0;
        x = 0;
    }
    if (y < 0) {
        if (-y < h) h = (int16_t)(h + y);
        else h = 0;
        y = 0;
    }
    if (x >= sw) w = 0, x = 0;
    if (y >= sh) h = 0, y = 0;
    if (w + x >= sw) w = (int16_t)(sw - x);
    if (h + y >= sh) h = (int16_t)(sh - y);
    if (w <= 0 || h <= 0) return;
    uint8_t *px = malloc((size_t)w * (size_t)h * 4);
    if (!px) return;
    D3d_ReadBack(x, y, w, h, px);
    int n = 0;
    for (int i = 0; i < w * h; i++) {
        const uint8_t *p = px + 4 * i;
        bool in = true;
        for (int k = 0; k < 3; k++) in = in && p[k] >= col[k] - tol && p[k] <= col[k] + tol;
        n += in;
    }
    free(px);
    share = (uint8_t)(n * 100 / area);
}
