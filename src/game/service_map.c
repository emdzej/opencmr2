/* The information page's map (see service.h): the rally's map with the day's stages marked, a stage's
   own map zooming out of its mark when chosen, its forecast along a strip and the rally map inset
   (0x503960-0x505e70); the weather legend, the temperature bar, the stage's notes (0x50f650) and the
   rally's (0x50fd30). Positions are the original's 640 x 480. */
#include "game/service_int.h"
#include "exe.h"
#include "fixmath.h"
#include "font.h"
#include "game/gstate.h"
#include "game/mem.h"
#include "game/paths.h"
#include "game/timer.h"
#include "render/d3d7.h"
#include "render/tex.h"
#include "render/twod.h"
#include "text.h"
#include "world/tod.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* a page: a stage's map and its zoom (0x82c6c8, 0x50 bytes each) */
typedef struct {
    Tex *tex;                     /* +0x00 */
    int16_t s[4];                 /* +0x04: its mark on the rally map */
    int16_t c[4];                 /* +0x0c: where it is drawn now */
    int32_t from, to, v, len;     /* +0x14: the zoom, 0 the mark to 1 the whole map; len in hundredths */
    int32_t fade1, fade2;         /* +0x24: the forecast strip, then the inset, fading in */
    int32_t u0, u1, v0, v1;       /* +0x2c: the mark in the rally map's texels (16.16) */
    uint32_t start, t2, t3;       /* +0x3c */
    uint8_t stage;                /* +0x48 */
    int32_t running;              /* +0x4c */
} SvPage;

static SvPage pages[4];
static int npages;                /* DAT_0082c694 */
static int8_t cur = -1;           /* DAT_0082ca1c */
static uint32_t t0;               /* DAT_0082c6c0 */
static int32_t phase;             /* DAT_0082cb44: the mark's pulse, 0-1 every 0.75 s */

static Tex *t_map, *t_piece[4], *t_wx[9], *t_arrow;   /* 0x82cb4c, 0x82cb50, 0x82ca20, 0x82c690 */
static int16_t piece_at[4][2];    /* DAT_0082cb60 */
static uint8_t piece_stage[4];    /* DAT_0082cb70 */
static uint8_t npieces;           /* DAT_0082cb48 */

static Tex *map_tex;              /* DAT_0082c9e8 */
static int16_t big[4];            /* DAT_0082c9ec: the map's box */
static int16_t msrc[4];           /* DAT_0082c9f4: the map's texels */
static int16_t inset[4];          /* DAT_0082c9fc: the rally map inset */

static int legend[7], nlegend;    /* DAT_0082c698, DAT_0082c6bc */
static uint8_t forecast[24];      /* DAT_0082ca04: the weather along the day, 100 not known */
static uint8_t nforecast;         /* DAT_0082ca18 */

static int sw, sh;
static int X(int x) { return sw * x / 0x280; }
static int Y(int y) { return sh * y / 0x1e0; }
static int32_t fmul(int32_t a, int32_t b) { return (int32_t)(((int64_t)a * b) >> 16); }
static int32_t fdiv(int32_t a, int32_t b) { return (int32_t)(((int64_t)a << 32) / ((int64_t)b << 16)); }
static int16_t hi16(int64_t v) { return (int16_t)(v >> 32); }

static Tex *find(BflSlot *a, uint32_t fmt, ...)
{
    char p[260];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(p, sizeof p, exe_str(fmt), ap);
    va_end(ap);
    char *q = p;
    while (*q == '\\') q++;
    return Bfl_FindTexture(a, q, NULL, false, 0);
}

static void sprite(Tex *t, const int16_t s[4], const int16_t d[4], int layer, const uint8_t *c)
{
    if (!t) return;
    Rect16 src = {s[0], s[1], s[2], s[3]}, dst = {d[0], d[1], d[2], d[3]};
    Sprite_Add(&src, &dst, t, layer, 0, NULL, NULL, c, 8);
}

static void full(Tex *t, const int16_t d[4], int layer, const uint8_t *c)
{
    if (!t) return;
    const int16_t s[4] = {0, 0, t->width, t->height};
    sprite(t, s, d, layer, c);
}

static uint8_t *rgba(uint32_t va, uint8_t out[4], int alpha)
{
    memcpy(out, gm(va), 4);
    if (alpha >= 0) out[3] = (uint8_t)alpha;
    return out;
}

/* 0x503ea0: the day's stage maps (two to four, the first two days four), their places on the rally map,
   the rally map, the weather symbols and the arrow */
void Sv_LoadTextures(void)
{
    uint32_t r = Tod_Rally() & 0xff, day = (Tod_Stage() & 0xff) >> 2;
    memset(piece_at, 0, sizeof piece_at);
    memset(t_piece, 0, sizeof t_piece);
    npieces = day < 2 ? 4 : (uint8_t)((r & 1) + 2);
    for (int i = 0; i < 4; i++) {
        if (i >= npieces) continue;
        piece_stage[i] = (uint8_t)(day * 4 + (uint32_t)i);
        uint32_t k = (r * 0xb + piece_stage[i]) * 2;
        piece_at[i][0] = gm8(0x52718c + k), piece_at[i][1] = gm8(0x52718d + k);
    }
    const char *country = exe_str(exe_u32(0x52714c + 4 * r));
    t_map = find(Sv_Day(), 0x527300, Path_SetupRep(), country);
    for (int i = 0; i < npieces; i++)
        t_piece[i] = find(Sv_Day(), 0x5272dc, Path_SetupRep(), country, exe_str(exe_u32(0x52716c + 4 * r)), piece_stage[i] + 1);
    for (uint32_t i = 0; i < 9; i++) t_wx[i] = find(Sv_Common(), 0x5272b4, Path_SetupRep(), 0x280, exe_str(exe_u32(0x527128 + 4 * i)));
    t_arrow = find(Sv_Common(), 0x527290, Path_SetupRep(), 0x280);
}

/* 0x503e00: the legend's weathers (Greece's has the snow, the storm's place) */
static void legend_init(void)
{
    static const int normal[6] = {0, 1, 2, 3, 4, 5}, greece[6] = {0, 1, 2, 6, 7, 8};
    nlegend = 6;
    memcpy(legend, (Tod_Rally() & 0xff) == 3 ? greece : normal, sizeof normal);
}

/* 0x5040f0: the map's boxes and a page for each information page: the stage's map at its mark */
void Sv_StageInit(void)
{
    map_tex = t_map;
    big[0] = 0x1c, big[1] = 0x72, big[2] = 0x100, big[3] = 0xf6;
    msrc[0] = 0, msrc[1] = 0, msrc[2] = t_map ? t_map->width : 0, msrc[3] = 0xf6;
    inset[0] = 0xd8, inset[1] = 0x7c, inset[2] = 0x3b, inset[3] = 0x39;
    npages = Sv_InfoPages();
    for (int i = 0; i < npages; i++) {
        SvPage *p = &pages[i];
        memset(p, 0, sizeof *p);
        int k = (int)(Tod_Stage() & 3) + i;
        p->stage = piece_stage[k];
        p->tex = t_piece[k];
        p->s[0] = (int16_t)(piece_at[k][0] + big[0]), p->s[1] = (int16_t)(piece_at[k][1] + big[1]);
        p->s[2] = 10, p->s[3] = 9;
        p->u0 = (msrc[0] << 16) + fmul(fdiv(p->s[0] - big[0], big[2]), msrc[2] << 16);
        p->u1 = (msrc[0] << 16) + fmul(fdiv(p->s[0] - big[0] + p->s[2], big[2]), msrc[2] << 16);
        p->v0 = (msrc[1] << 16) + fmul(fdiv(p->s[1] - big[1], big[3]), msrc[3] << 16);
        p->v1 = (msrc[1] << 16) + fmul(fdiv(p->s[1] - big[1] + p->s[3], big[3]), msrc[3] << 16);
    }
    cur = 0;
    t0 = Timer_Centis();
    phase = 0;
    legend_init();
}

/* the weather table's base (Tod_Weather's pointer for stage 0: indexed by stage from it) */
static uint32_t weather_base(void)
{
    uint32_t sel = gm32(0x52f2ac);
    if (!(sel >> 24 & 1)) return 0x52f100;
    uint32_t a = sel >> 10 & 3, b = sel >> 12 & 3;
    return sel >> 26 & 1 ? 0x52f36c + ((a * 3 + b) * 3 + Gs_Mode2()) * 4 : 0x52f36c + (a * 3 + b) * 0xc;
}

/* 0x505e70: the forecast from the first page's stage to the last's: the first's start, then each
   one's main weather; 100 for a stage whose weather changes (0x52f240) */
void Sv_ForecastInit(void)
{
    if (npages < 1) return;
    uint32_t first = pages[0].stage, last = pages[npages - 1].stage;
    nforecast = (uint8_t)(last - first + 2);
    forecast[0] = gm32(0x52f240 + 4 * first) ? 100 : gm8(weather_base() + first * 8);
    for (uint32_t s = first, k = 1; s <= last && k < sizeof forecast; s++, k++)
        forecast[k] = gm32(0x52f240 + 4 * s) ? 100 : gm8(0x52f1a0 + 4 * s);
}

/* ---- the zoom ---- */

static int32_t zoom_len(int32_t from, int32_t to)
{
    int32_t a = fmul(from, 0x320000), b = fmul(to, 0x320000);
    return 0x320000 - a < 0 ? a - b : b - a;
}

/* 0x5039d0, 0x503aa0 */
static void zoom_in(SvPage *p)
{
    if (p->v == 0x10000) return;
    p->from = p->v, p->to = 0x10000, p->running = 1;
    p->len = zoom_len(p->from, p->to);
    p->start = Timer_Centis();
}

static void zoom_out(SvPage *p)
{
    if (p->v == 0) return;
    p->from = p->v, p->to = 0, p->running = 1;
    p->len = -fmul(p->from, 0x320000) < 0 ? fmul(p->from, 0x320000) - fmul(p->to, 0x320000) : fmul(p->to, 0x320000) - fmul(p->from, 0x320000);
    p->start = Timer_Centis();
}

/* 0x503960: the shown page's zoom: (0, 0) the Information item chosen, in or out; (0, 1) Back, out;
   (1, x) in */
void Sv_InfoZoom(int a, int b)
{
    if (cur == -1) return;
    SvPage *p = &pages[cur];
    if (a == 0) {
        if (b == 0) {
            if (p->to == 0) zoom_in(p);
            else if (p->to == 0x10000) zoom_out(p);
        } else if (p->to != 0)
            zoom_out(p);
    } else if (p->to != 0x10000)
        zoom_in(p);
}

/* 0x505a60: another page shown, taking over the zoom */
static void show(int v)
{
    if (cur == v) return;
    if (cur != -1 && v != -1) {
        SvPage *a = &pages[cur], *b = &pages[v];
#define SWAP(f) do { __typeof__(a->f) t = a->f; a->f = b->f; b->f = t; } while (0)
        SWAP(running); SWAP(v); SWAP(to); SWAP(fade2); SWAP(from); SWAP(len); SWAP(fade1); SWAP(t3); SWAP(t2); SWAP(start);
#undef SWAP
    }
    t0 = Timer_Centis();
    phase = 0;
    cur = (int8_t)v;
}

/* 0x505b40: the zoom on (square root eased), the shown box; once out the strip then the inset fade in;
   the pulse */
static void animate(void)
{
    if (cur == -1) return;
    SvPage *p = &pages[cur];
    if (p->running) {
        uint32_t k = (uint32_t)(((int64_t)(int32_t)((Timer_Centis() - p->start) << 16) << 16) / p->len);
        k = k ? Fx_Sqrt(k) : 0;
        if ((int32_t)k < 0x10000) p->v = fmul(p->to - p->from, (int32_t)k) + p->from;
        else p->running = 0, p->v = p->to, p->t2 = Timer_Centis();
    }
    bool keep = false;
    if (p->v == 0) {
        memcpy(p->c, p->s, sizeof p->c);
    } else if (p->v == 0x10000) {
        memcpy(p->c, big, sizeof p->c);
        if (!p->running) {
            keep = true;
            if (p->fade1 != 0x10000) {
                p->fade1 = (int32_t)(((int64_t)(int32_t)((Timer_Centis() - p->t2) << 16) << 16) / 0x320000);
                if (p->fade1 > 0xffff) p->fade1 = 0x10000, p->t3 = Timer_Centis();
            }
            if (p->fade1 == 0x10000 && p->fade2 != 0x10000) {
                p->fade2 = (int32_t)(((int64_t)(int32_t)((Timer_Centis() - p->t3) << 16) << 16) / 0x640000);
                if (p->fade2 > 0xffff) p->fade2 = 0x10000;
            }
        }
    } else {
        for (int i = 0; i < 4; i++) p->c[i] = (int16_t)(hi16((int64_t)((big[i] - p->s[i]) << 16) * p->v) + p->s[i]);
    }
    if (!keep) p->fade1 = 0, p->fade2 = 0;
    uint32_t q = (uint32_t)(((int64_t)(int32_t)((Timer_Centis() - t0) << 16) << 16) / 0x4b0000);
    phase = (int32_t)(q & 0xffff);
}

/* 0x5059d0: the page for information value v (the main page's keys off while it zooms) */
static void choose(int v)
{
    if (cur == -1 || !pages[cur].running) {
        if (cur != v) show(v % npages);
        Menu_SetFlags(&sv_main, true, true, true, true);
    } else
        Menu_SetFlags(&sv_main, false, false, false, false);
    animate();
}

/* ---- the draws ---- */

/* 0x503b70: a point of the stage's map (640 x 480 in the big box) where the page is now, on the screen */
static void map_point(const SvPage *p, int16_t *x, int16_t *y)
{
    if (p->v != 0x10000) {
        *x = (int16_t)(hi16((int64_t)fdiv(*x - big[0], big[2]) * (p->c[2] << 16)) + p->c[0]);
        *y = (int16_t)(hi16((int64_t)fdiv(*y - big[1], big[3]) * (p->c[3] << 16)) + p->c[1]);
    }
    *x = (int16_t)X(*x), *y = (int16_t)Y(*y);
}

static void scaled(const int16_t r[4], int16_t o[4]) { o[0] = (int16_t)X(r[0]), o[1] = (int16_t)Y(r[1]), o[2] = (int16_t)X(r[2]), o[3] = (int16_t)Y(r[3]); }

/* 0x504eb0: a box's outline (the bottom at y + h); clipped, the edges whose both corners are inside the
   map box */
static void frame(const int16_t r[4], const uint8_t *c, bool clip)
{
    int16_t e[4];
    if (!clip) {
        e[0] = r[0], e[1] = r[1], e[2] = r[2], e[3] = 1;
        Rect_Fill(e, c, 1);
        e[1] = (int16_t)(e[1] + r[3]);
        Rect_Fill(e, c, 1);
        e[0] = r[0], e[1] = r[1], e[2] = 1, e[3] = r[3];
        Rect_Fill(e, c, 1);
        e[0] = (int16_t)(e[0] + r[2] - 1);
        Rect_Fill(e, c, 1);
        return;
    }
    int x0 = X(big[0]), y0 = Y(big[1]), x1 = X(big[2]) + x0, y1 = Y(big[3]) + y0;
    int px[4] = {r[0], r[0] + r[2], r[0] + r[2], r[0]}, py[4] = {r[1], r[1], r[1] + r[3], r[1] + r[3]};
    bool out[4];
    for (int k = 0; k < 4; k++) {
        out[k] = false;
        if (px[k] < x0) px[k] = x0, out[k] = true;
        else if (x1 < px[k]) px[k] = x1, out[k] = true;
        if (py[k] < y0) py[k] = y0, out[k] = true;
        else if (y1 < py[k]) py[k] = y1, out[k] = true;
    }
    int16_t w = (int16_t)(px[1] - px[0]), h = (int16_t)(py[2] - py[1]);
    e[0] = (int16_t)px[0], e[1] = (int16_t)py[0], e[2] = w, e[3] = 1;
    if (!out[0] || !out[1]) Rect_Fill(e, c, 1);
    e[1] = (int16_t)(e[1] + h);
    if (!out[2] || !out[3]) Rect_Fill(e, c, 1);
    e[0] = (int16_t)px[0], e[1] = (int16_t)py[0], e[2] = 1, e[3] = h;
    if (!out[0] || !out[3]) Rect_Fill(e, c, 1);
    e[0] = (int16_t)(e[0] + w - 1);
    if (!out[1] || !out[2]) Rect_Fill(e, c, 1);
}

/* 0x505590: the rally map, its texels following the shown page's box */
static void draw_map(void)
{
    int16_t s[4];
    if (cur == -1) memcpy(s, msrc, sizeof s);
    else {
        const SvPage *p = &pages[cur];
        int32_t a = fdiv(p->c[0] - big[0], big[2]), b = fdiv(p->c[0] + p->c[2] - big[0], big[2]);
        int32_t su = (int32_t)(((int64_t)(p->u0 - p->u1) << 16) / (a - b));
        int32_t c = fdiv(p->c[1] - big[1], big[3]), d = fdiv(p->c[1] + p->c[3] - big[1], big[3]);
        int32_t sv = (int32_t)(((int64_t)(p->v0 - p->v1) << 16) / (c - d));
        s[0] = (int16_t)((p->u0 - fmul(a, su)) >> 16), s[1] = (int16_t)((p->v0 - fmul(c, sv)) >> 16);
        s[2] = (int16_t)(su >> 16), s[3] = (int16_t)(sv >> 16);
    }
    int16_t d[4];
    scaled(big, d);
    sprite(map_tex, s, d, 3, gm(0x5270e4));
}

/* 0x5044d0: the page's stage map fading in with the zoom; zoomed in its name (Text 0x136), the forecast
   strip in the rally's colour (symbols with arrows between, '?' for a change) and the rally map inset
   with the stage's mark */
static void draw_page(int i)
{
    if (!t_arrow || i == -1) return;
    SvPage *p = &pages[i];
    int16_t d[4];
    scaled(p->c, d);
    uint8_t c1[4], c2[4], col[4];
    rgba(0x5270fc, c1, -1), rgba(0x527100, c2, -1);
    rgba(0x5270e4, col, (int)hi16((int64_t)p->v * 0xff0000));
    if (p->v == 0) return;
    if (p->tex) {
        int16_t s[4] = {0, 0, p->tex->width, msrc[3]};
        sprite(p->tex, s, d, 3, col);
    }
    int x0 = 0x3c;
    int16_t r[4] = {0x29, (int16_t)(inset[3] + inset[1] - 0x28), 0x62, 0x28};
    int mid = r[1] + 0x14;
    if (p->v == 0x10000) Sv_Type1(3, 1, 1, Text_Get(0x136), X(0x29), Y(0x89), c1, c2, 0x11);
    r[0] = (int16_t)X((int16_t)(r[0] + (int16_t)i * 0x3c));
    int32_t f = (int32_t)(((int64_t)p->fade2 * 0x20000) >> 16);
    if (f > 0x10000) f = 0x10000;
    int16_t w = (int16_t)X(r[2]), h = (int16_t)Y(r[3]);
    r[1] = (int16_t)Y(r[1]);
    r[2] = hi16((int64_t)(w << 16) * f), r[3] = hi16((int64_t)(h << 16) * f);
    uint32_t rc = gm32(0x527104 + 4 * (Tod_Rally() & 0xff));
    uint8_t rcol[4] = {(uint8_t)(((rc & 0xff) >> 2) * 3), (uint8_t)(((rc >> 8 & 0xff) >> 2) * 3),
                       (uint8_t)(((rc >> 16 & 0xff) >> 2) * 3), (uint8_t)(rc >> 24)};
    Rect_Fill(r, rcol, 1);
    int32_t sx = fdiv(p->c[2], big[2]);
    if (p->fade1 != 0) {
        uint8_t a = (uint8_t)hi16((int64_t)fmul(p->v, p->fade1) * 0xff0000);
        c1[3] = a, col[3] = a;
        for (int k = 0; k < nforecast; k++) {
            int16_t e[4] = {(int16_t)x0, (int16_t)mid, 0, 0};
            map_point(p, &e[0], &e[1]);
            if (forecast[k] == 100) {
                e[1] = (int16_t)(e[1] + 4);
                Sv_Type1(3, 1, 1, Text_Get(0xfe), e[0], e[1], c1, c2, 0x12);
            } else {
                Tex *t = t_wx[forecast[k] % 9];
                if (t) {
                    e[2] = hi16((int64_t)sx * (t->width << 16)), e[3] = hi16((int64_t)sx * (t->height << 16));
                    e[0] = (int16_t)(e[0] - hi16((int64_t)sx * ((e[2] / 2) << 16)));
                    e[1] = (int16_t)(e[1] - hi16((int64_t)sx * (gm8(0x52723c + forecast[k]) << 16)));
                    full(t, e, 1, col);
                }
            }
            int nx = x0 + 0x1e;
            if (k != nforecast - 1) {
                int16_t g[4] = {(int16_t)nx, (int16_t)mid, 0, 0};
                map_point(p, &g[0], &g[1]);
                g[2] = hi16((int64_t)sx * (t_arrow->width << 16)), g[3] = hi16((int64_t)sx * (t_arrow->height << 16));
                g[0] = (int16_t)(g[0] - g[2] / 2), g[1] = (int16_t)(g[1] - g[3] / 2);
                full(t_arrow, g, 1, col);
                nx = x0 + 0x3c;
            }
            x0 = nx;
        }
    }
    if (p->fade2 == 0) return;
    int32_t g2 = (int32_t)(((int64_t)p->fade2 * 0x20000) >> 16), over = g2 - 0x10000;
    if (g2 > 0x10000) g2 = 0x10000;
    int16_t q[4] = {(int16_t)X(inset[0]), (int16_t)Y(inset[1]), (int16_t)X(fmul(inset[2] << 16, g2) >> 16), (int16_t)Y(fmul(inset[3] << 16, g2) >> 16)};
    if (over >= 0) {
        col[3] = (uint8_t)hi16((int64_t)fmul(p->v, over) * 0xff0000);
        sprite(map_tex, msrc, q, 3, col);
        int32_t ky = fdiv(p->s[1] - big[1], big[3]), kh = fdiv(inset[3], big[3]), kw = fdiv(inset[2], big[2]);
        int16_t m[4] = {(int16_t)X((int16_t)(hi16((int64_t)fdiv(p->s[0] - big[0], big[2]) * (inset[2] << 16)) + inset[0])),
                        (int16_t)Y((int16_t)(hi16((int64_t)ky * (inset[3] << 16)) + inset[1])),
                        (int16_t)X(hi16((int64_t)(p->s[2] << 16) * kw) * 2), (int16_t)Y(hi16((int64_t)(p->s[3] << 16) * kh) * 2)};
        uint8_t mc[4];
        Rect_Fill(m, rgba(0x5270ec, mc, col[3]), 1);
    }
    frame(q, gm(0x5270e8), false);
}

/* 0x5051c0: zooming, the other pages' marks and the shown one's box fading out, a pulse around it */
static void draw_marks(void)
{
    if (cur == -1) return;
    const SvPage *p = &pages[cur];
    int32_t a = 0xff0000 - (int32_t)(((int64_t)p->v * 0xff0000) >> 16);
    if (a == 0) return;
    uint8_t red[4], fill[4], faint[4];
    rgba(0x5270f0, red, (uint8_t)(a >> 16));
    rgba(0x5270ec, fill, (uint8_t)(a >> 16));
    rgba(0x5270f0, faint, (uint8_t)hi16((int64_t)a * 0x4000));
    int32_t kx = fdiv(p->c[2], p->s[2]), ky = fdiv(p->c[3], p->s[3]);
    int16_t r[4];
    for (int j = 0; j < npages; j++) {
        if (j == cur) continue;
        const SvPage *o = &pages[j];
        r[0] = (int16_t)X((int16_t)(hi16((int64_t)((o->s[0] - p->s[0]) << 16) * kx) + p->c[0]));
        r[1] = (int16_t)Y((int16_t)(hi16((int64_t)((o->s[1] - p->s[1]) << 16) * ky) + p->c[1]));
        r[2] = (int16_t)X(hi16((int64_t)(o->s[2] << 16) * kx));
        r[3] = (int16_t)Y(hi16((int64_t)(o->s[3] << 16) * ky));
        frame(r, faint, true);
    }
    scaled(p->c, r);
    Rect_Fill(r, fill, 1);
    frame(r, red, false);
    int16_t s = hi16((int64_t)phase * 0x80000);
    int16_t e[4] = {(int16_t)(p->c[0] - s), (int16_t)(p->c[1] - s), (int16_t)(p->c[2] + s * 2), (int16_t)(p->c[3] + s * 2)};
    scaled(e, r);
    red[3] = (uint8_t)hi16((int64_t)a * (0x10000 - phase));
    frame(r, red, false);
}

/* 0x5057e0: the weather legend, a column of symbols and names (its x by the language) */
static void draw_legend(void)
{
    uint32_t l = gm32(0x52afb4) & 7, r = Tod_Rally() & 0xff;
    int bx = 0x136;
    if (l == 4) bx = r != 3 ? 0x139 : 0x148;
    else if (l == 3) bx = 0x140;
    else if (l == 1 && r == 3) bx = 0x150;
    if (!t_wx[0]) return;
    int x = X(bx);
    int16_t d[4] = {(int16_t)(x - t_wx[0]->width / 2), 0, t_wx[0]->width, t_wx[0]->height};
    uint8_t c1[4], c2[4];
    rgba(0x5270fc, c1, -1), rgba(0x527100, c2, -1);
    int32_t step = (int32_t)(((int64_t)0xea << 32) / ((int64_t)(nlegend - 1) << 16));
    for (int i = 0; i < nlegend; i++) {
        int dy = (int)(((int64_t)(i << 16) * step) >> 32);
        Sv_Type1(3, 1, 1, Text_Get(legend[i] + 0x9d), x, Y(dy + 0x93), c1, c2, 0x22);
        d[1] = (int16_t)(Y(dy + 0x81) - gm8(0x527254 + (uint32_t)legend[i]));
        Tex *t = t_wx[legend[i]];
        if (t) {
            int16_t s[4] = {0, 0, t->width, t->height};
            sprite(t, s, d, 3, gm(0x5270e4));
        }
    }
}

/* 0x503c80: the temperature on a bar from blue to orange, -20 to 50 degrees */
static void draw_temperature(uint32_t stage)
{
    static const uint8_t cols[12] = {0x97, 0xc2, 0xcd, 0, 0xde, 0xd2, 0x74, 0, 0xff, 0x7f, 0, 0};
    int16_t r[4] = {(int16_t)X(0x1c), (int16_t)Y(0x16f), (int16_t)(sw * 0x100 / 0x280), (int16_t)Y(0xc)};
    Sv_Gradient(r, 0xb, cols);
    int t = gms8(0x52f294 + stage);
    int32_t k = fmul((t << 16) + 0x140000, 0x3a9);
    if (k < 0) k = 0;
    else if (k > 0x10000) k = 0x10000;
    char b[32];
    snprintf(b, sizeof b, exe_str(0x527288), (int16_t)t);
    Font_Print(0, b, (int)(((int64_t)(r[2] << 16) * k) >> 32) + r[0], r[3] + r[1], gm(0x5270fc), 0x12);
}

/* 0x50fd30: the rally's notes, four lines */
static void rally_notes(uint32_t r)
{
    for (int k = 0; k < 4; k++)
        Sv_Type1(5, 1, 1, Text_Get((int)(r * 4 + 0xd9) + k), X(0x1c), Y(0x19a + 0xe * k), gm(0x5297ac), gm(0x5297b4), 0x11);
}

/* 0x50f650: the stage's notes: the day and stage of the rally (the stage alone outside a championship
   or rally), its name, start time, length, surfaces and the note (Text 0x13e) */
static void stage_notes(uint32_t r, uint32_t s)
{
    const uint8_t *ca = gm(0x5297ac), *cb = gm(0x5297b4);
    int x = (int)gm32(0x5293a0) * sw / 0x280;
    int16_t bar[4] = {0, (int16_t)Y(0xac), 1, (int16_t)Y(0x18)};
    Sv_TweenGrow(0, bar, false);
    char b[256];
    int tx = x;
    uint32_t m = Gs_Mode();
    if (m != 0 && m != 1) {
        if (s == 10) snprintf(b, sizeof b, exe_str(0x516940), Text_Get(0x9b), Text_Get(0x13d));
        else snprintf(b, sizeof b, exe_str(0x516098), Text_Get(0x9b), (int)s + 1);
    } else {
        int day = s == 10 ? (Gs_Mode2() == 0 ? 2 : Gs_Mode2() == 2 ? 4 : 3) : (int)s / 4 + 1;
        snprintf(b, sizeof b, exe_str(0x516098), Text_Get(0xfa), day);
        Sv_Type1(3, 0, 0, b, x, Y(0xbe), ca, cb, 0x11);
        int sx = Font_TextWidth(0, b) + x + Font_TextWidth(0, exe_str(0x516870));
        bar[0] = (int16_t)sx;
        Rect_Fill(bar, gm(0x5297b8), 3);
        tx = sx + 2 + Font_TextWidth(0, exe_str(0x516870));
        uint32_t d = Gs_Mode2();
        int of = (int)(r & 1) + (d == 0 ? 4 : d == 1 ? 8 : 10);   /* 0x407150 */
        snprintf(b, sizeof b, Text_Get(0x13c), s == 10 ? of : (int)s + 1, of);
    }
    Sv_Type1(3, 0, 0, b, tx, Y(0xbe), ca, cb, 0x11);
    uint32_t idx = s + r * 0xb;
    snprintf(b, sizeof b, exe_str(0x524d38), Text_Get(0xff), exe_str(exe_u32(0x5269d0 + 4 * idx)));
    Sv_Type1(3, 1, 1, b, x, Y(0xd2), ca, cb, 0x11);
    snprintf(b, sizeof b, exe_str(0x5297c4), Text_Get(0xfb), (gm8(0x516188 + (s + r * 12) * 2) * 100u & 0xffff) / 100);   /* 0x4077d0 */
    Sv_Type1(3, 1, 1, b, x, Y(0xe0), ca, cb, 0x11);
    uint32_t info = 0x525d50 + idx * 0x24;
    if (gm8(info + 1) == 0) snprintf(b, sizeof b, exe_str(0x524d2c), Text_Get(0xfc), gm8(info));
    else snprintf(b, sizeof b, exe_str(0x524d20), Text_Get(0xfc), gm8(info), gm8(info + 1));
    strncat(b, exe_str(0x524d1c), sizeof b - strlen(b) - 1);
    Sv_Type1(3, 1, 1, b, x, Y(0xee), ca, cb, 0x11);
    snprintf(b, sizeof b, exe_str(0x524d14), Text_Get(0xfd));
    Sv_Type1(3, 1, 1, b, x, Y(0xfc), ca, cb, 0x11);
    uint32_t n = gm8(info + 2), list = info + 4, first = n > 1 ? 2 : n;
    int widest = 0, y = 0x10a;
    for (uint32_t k = 0; k < first; k++, y += 0xe) {
        snprintf(b, sizeof b, exe_str(0x524d0c), gm8(list + 4 + k * 8), Text_Get((int)gm32(list + k * 8) + 0xd4));
        Sv_Type1(3, 1, 1, b, x, Y(y), ca, cb, 0x11);
        int w = Font_TextWidth(1, b);
        if (widest < w) widest = w;
    }
    int gap = Font_TextWidth(1, exe_str(0x5297bc));
    y = 0x10a;
    for (uint32_t k = first; k < n; k++, y += 0xe) {
        snprintf(b, sizeof b, exe_str(0x524d0c), gm8(list + 4 + k * 8), Text_Get((int)gm32(list + k * 8) + 0xd4));
        Sv_Type1(3, 1, 1, b, gap + widest + x, Y(y), ca, cb, 0x11);
    }
    snprintf(b, sizeof b, "%s", Text_Get(0x13e));
    Sv_Type1(3, 1, 1, b, x, Y(0x17d), ca, cb, 0x21);
}

/* 0x5043b0: the map, the page, the marks, the legend; the notes and temperature of the shown page's
   stage; the map's frame */
static void draw_all(void)
{
    draw_map();
    draw_page(cur);
    draw_marks();
    draw_legend();
    if (cur != -1) {
        uint32_t r = Tod_Rally() & 0xff;
        rally_notes(r);
        stage_notes(r, pages[cur].stage);
        draw_temperature(pages[cur].stage);
    }
    int16_t f[4];
    scaled(big, f);
    frame(f, gm(0x5270e8), false);
}

/* 0x50a880 */
void Sv_InfoMap(int v)
{
    D3d_ScreenSize(&sw, &sh);
    choose(v);
    draw_all();
}
