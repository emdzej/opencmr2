/* The item strip, the titles and the header (see fe_strip.h), ported from 0x4d2cd0, 0x4d28c0,
   0x4d3e80-0x4d45b0 and 0x4f37c0. */
#include "fe/fe_strip.h"
#include "fe/fe.h"
#include "fe/fe_page.h"
#include "fe/fe_res.h"
#include "fe/pages.h"
#include "exe.h"
#include "fixmath.h"
#include "font.h"
#include "game/mem.h"
#include "game/timer.h"
#include "render/d3d7.h"
#include "render/twod.h"
#include "text.h"
#include <stdio.h>
#include <string.h>

static FeScroll scroll[12];
static int scroll_prev, scroll_cur;   /* DAT_00525398, DAT_0052539c */
static uint32_t main_time;            /* DAT_0081912c */
static int8_t pulse;                  /* DAT_00818270 */
static bool pulse_up = true;          /* DAT_00524b8c */

FeScroll *Fe_Scroll(int k) { return &scroll[k]; }
void Fe_SetScrollPos(int i) { scroll_prev = scroll_cur = i; }
void Fe_SetScrollPrev(int i) { scroll_prev = i; }
int Fe_ScrollPrev(void) { return scroll_prev; }
uint32_t Fe_MainTime(void) { return main_time; }

static void screen(int *w, int *h) { D3d_ScreenSize(w, h); }
static const uint8_t *col(uint32_t va) { return gm(va); }   /* the colours at 0x524968-0x52497c */
enum { C68 = 0x524968, C6c = 0x52496c, C70 = 0x524970, C74 = 0x524974, C78 = 0x524978, C7c = 0x52497c };

static const char *item_text(const Menu *m, int i) { return Text_Get(m->items[i].text); }

void Scroll_Measure(FeScroll *s, Menu *m)
{
    int w, h;
    screen(&w, &h);
    s->off = s->off0 = 0;
    s->cur = s->prev = m->sel;
    s->gap = w * 0x18 / 0x280;
    s->n = m->n;
    for (int i = 0; i < m->n; i++) {
        char b[256];
        snprintf(b, sizeof b, "%s", item_text(m, i));
        s->width[i] = Font_TextWidth(2, Text_Lower(b));
    }
    s->t = Fe_Now();
    s->page = m;
}

uint32_t Scroll_Update(FeScroll *s)
{
    Menu *m = s->page;
    if (m->sel != s->cur) {
        if (s->cur != -1) {
            if (m == &pg_81d6d8) {
                main_time = Fe_Now();
                scroll_prev = scroll_cur;
                scroll_cur = m->sel;
            }
            int now = m->sel, old = s->cur, back, fwd;
            /* the shorter way round from the old selection to the new, a width and a gap a step */
            if (old < now) back = m->n - now + old, fwd = now - old;
            else back = old - now, fwd = m->n - old + now;
            if (back < fwd) {
                for (int k = back; k > 0; k--) {
                    old--;
                    s->off -= s->width[old < 0 ? s->n + old : old] + s->gap;
                }
            } else if (fwd < back) {
                for (int k = fwd; k > 0; k--, old++) s->off += s->width[old >= s->n ? old - s->n : old] + s->gap;
            } else if (!(m->dir & 1)) {
                s->off += s->width[old >= s->n ? old - s->n : old] + s->gap;
            } else {
                int i = old - 1;
                if (i < 0) i += s->n;
                s->off -= s->width[i] + s->gap;
            }
            s->off0 = s->off;
        }
        s->t = Fe_Now();
        s->prev = s->cur;
        s->cur = m->sel;
    }
    if (s->cur == -1) return 0;
    uint32_t dt = Fe_Now() - s->t;
    if (dt > 250) {
        s->off = s->off0 = 0;
        return dt;
    }
    int32_t o = s->off0;
    if (o > 0) s->off = o - (int32_t)((dt * (uint32_t)o) / 250);
    else s->off = (int32_t)((uint32_t)(-(int32_t)(dt * (uint32_t)o)) / 250) + o;
    return dt;
}

/* the strip's text line and its separators (1 wide, 45 high at 384 of 480) */
static int text_y(int h) { return h * 0x20 / 0x1e0 + h * 0x180 / 0x1e0; }
static void bar(int x, int h, uint32_t c)
{
    int16_t r[4] = {(int16_t)x, (int16_t)(h * 0x180 / 0x1e0), 1, (int16_t)(h * 0x2d / 0x1e0)};
    Rect_Fill(r, col(c), 1);
}

static void print2(const char *s, int x, int y, uint32_t c, bool lower)
{
    char b[256];
    snprintf(b, sizeof b, "%s", s);
    Font_Print(2, lower ? Text_Lower(b) : b, x, y, col(c), 0x11);
}

void Strip_Draw(FeScroll *s, bool lit)
{
    Menu *m = s->page;
    int w, h;
    screen(&w, &h);
    int y = text_y(h), x0 = w * 0x66 / 0x280 + s->off, n = m->n;
    print2(item_text(m, m->sel), x0, y, s->off == 0 && lit ? C68 : C6c, true);
    bar(s->width[m->sel] + s->gap / 2 + x0, h, lit ? C68 : C6c);
    bar(s->width[m->sel] + s->gap / 2 + x0 + 1, h, lit ? C74 : C78);
    /* to the right, until off the screen */
    uint32_t on = lit ? C6c : C70, faint = lit ? C78 : C7c;
    int x = s->width[m->sel] + x0 + s->gap, i = m->sel;
    while (x < w) {
        if (++i >= n) i = 0;
        bool en = m->items[i].flags & MIF_ENABLED;
        print2(item_text(m, i), x, y, en ? on : C70, true);
        bar(s->gap / 2 + s->width[i] + x, h, en ? on : C70);
        bar(s->gap / 2 + s->width[i] + x + 1, h, en ? faint : C7c);
        x += s->width[i] + s->gap;
    }
    /* to the left; after the first the separators take the plain colours */
    i = m->sel - 1;
    if (i < 0) i = n - 1;
    x = x0 - s->width[i] - s->gap;
    uint32_t c14 = lit ? C6c : C70, c10 = lit ? C68 : C6c, c4 = lit ? C74 : C78;
    while (s->width[i] + s->gap + x >= 1) {
        if (!(m->items[i].flags & MIF_ENABLED)) c14 = C70;
        print2(item_text(m, i), x, y, c14, true);
        int j = i + 1 >= n ? 0 : i + 1;
        if (!(m->items[j].flags & MIF_ENABLED)) {
            bar(s->gap / 2 + s->width[i] + x, h, C70);
            c4 = C7c;
        } else
            bar(s->gap / 2 + s->width[i] + x, h, c10);
        bar(s->gap / 2 + s->width[i] + x + 1, h, c4);
        if (--i < 0) i = n - 1;
        x -= s->width[i] + s->gap;
        c4 = lit ? C78 : C7c;
        c14 = c10 = lit ? C6c : C70;
    }
}

void Fe_Hint(const char *s, bool steady)
{
    if (!s) return;
    uint8_t c[4];
    memcpy(c, col(C6c), 4);
    if (!steady) {
        /* the colour breathes between -32 and +32 */
        if (!pulse_up) {
            int8_t p = (int8_t)(pulse - 1);
            if (p < -0x20) pulse_up = true;
            else pulse = p;
        } else {
            int8_t p = (int8_t)(pulse + 1);
            if (p > 0x20) pulse_up = false;
            else pulse = p;
        }
        c[0] = (uint8_t)(c[0] + pulse), c[1] = (uint8_t)(c[1] + pulse), c[2] = (uint8_t)(c[2] + pulse);
    } else {
        pulse = 0;
        pulse_up = true;
    }
    memcpy(gm(0x524b88), c, 4);
    char b[300];
    snprintf(b, sizeof b, "%s", s);
    /* 0x40bac0 (a key name substitution) returns 0 */
    int w, h;
    screen(&w, &h);
    Font_Print(1, b, w * 0x66 / 0x280, h * 0x40 / 0x1e0 + h * 0x180 / 0x1e0, c, 0x11);
}

void Strip_DrawMain(Menu *m, bool lit, const char *hint)
{
    FeScroll *s = &scroll[0];
    int w, h;
    screen(&w, &h);
    Fe_Hint(hint ? hint : Text_Get(0x57), true);
    int y = text_y(h), x0 = w * 0x66 / 0x280 + s->off, n = m->n;
    Font_Print(2, item_text(m, m->sel), x0, y, col(s->off == 0 && lit ? C68 : C6c), 0x11);
    bar(s->width[m->sel] + s->gap / 2 + x0, h, lit ? C68 : C6c);
    bar(s->width[m->sel] + s->gap / 2 + x0 + 1, h, lit ? C74 : C78);
    uint32_t on = lit ? C6c : C70, faint = lit ? C78 : C7c;
    int x = s->width[m->sel] + x0 + s->gap, i = m->sel;
    while (x < w) {
        if (++i >= n) i = 0;
        Font_Print(2, item_text(m, i), x, y, col(m->items[i].flags & MIF_ENABLED ? on : faint), 0x11);
        bar(s->gap / 2 + s->width[i] + x, h, on);
        bar(s->gap / 2 + s->width[i] + x + 1, h, faint);
        x += s->width[i] + s->gap;
    }
    /* to the left, placed by the measured width of each text */
    i = m->sel - 1;
    if (i < 0) i = n - 1;
    x = x0 - s->gap - Font_TextWidth(2, item_text(m, i));
    uint32_t c9 = lit ? C6c : C70, c18 = lit ? C78 : C7c, c10 = lit ? C68 : C6c, cc = lit ? C74 : C78;
    for (int e = Font_TextWidth(2, item_text(m, i)) + x; e > 0; e = Font_TextWidth(2, item_text(m, i)) + x) {
        Font_Print(2, item_text(m, i), x, y, col(m->items[i].flags & MIF_ENABLED ? c9 : c18), 0x11);
        bar(s->width[i] + s->gap / 2 + x, h, c10);
        bar(s->width[i] + s->gap / 2 + x + 1, h, cc);
        if (--i < 0) i = n - 1;
        x -= s->width[i] + s->gap;
        c9 = c10 = lit ? C6c : C70;
        c18 = cc = lit ? C78 : C7c;
    }
}

/* 0x4d3e80: a title in hel_15pt; one that isn't the current page gets a separator after it */
static int title(int x, int y, uint32_t c, bool current, const char *s)
{
    int w, h;
    screen(&w, &h);
    Font_Print(1, s, x, y, col(c), 0x11);
    if (current) return x;
    int nx = w * 5 / 0x280 + x + Font_TextWidth(1, s);
    int16_t r[4] = {(int16_t)nx, (int16_t)(h * 0x26 / 0x1e0 - h * 0xf / 0x1e0), 1, (int16_t)(h * 0x17 / 0x1e0)};
    Rect_Fill(r, col(C6c), 1);
    return nx + w * 5 / 0x280;
}

int Fe_Titles(Menu *m, int x, int y, bool current, int depth, const char *const *names, int k)
{
    if (depth < 1 && depth != -1) return x;
    if (m->parent && m != &pg_81d6d8) {
        int kk = current ? k : k - 1;
        x = Fe_Titles(m->parent, x, y, false, depth == -1 ? -1 : depth - 1, names, kk);
    }
    const char *s;
    char b[256];
    if (names && k >= 1 && !current) s = names[k - 1];
    else if (m->title) s = m->title;
    else s = Text_Get(m->text);
    snprintf(b, sizeof b, "%s", s ? s : "");
    return title(x, y, current ? C68 : C6c, current, b);
}

/* 0x4d41e0: the time on the menus (hours, minutes, seconds in the dot font) and "Time" at the top right, and
   "Automode" when the demo option (0x52e890 bit 17) is on */
void Fe_Header(void)
{
    int w, h;
    screen(&w, &h);
    uint32_t t = Fe_MenuTime(), m = t / 1000 / 60;
    char b[32];
    snprintf(b, sizeof b, exe_str(0x516e3c), m / 60, m % 60, t / 1000 % 60);
    Font_Print(3, b, w * 0x21b / 0x280, h * 0x26 / 0x1e0, col(C6c), 0x11);
    Font_Print(1, Text_Get(0x4e), w * 0x217 / 0x280, h * 0x26 / 0x1e0, col(C6c), 0x14);
    if (gm32(0x52e890) >> 0x11 & 1) Font_Print(1, exe_str(0x524c6c), w * 0x27b / 0x280, h * 5 / 0x1e0, col(C6c), 0xc);
}

/* 0x4d2bd0: a sine wave rippling out from p with the period, at phase; the distance in 16.16 */
static int32_t ripple(const int32_t p[2], int32_t x, int32_t y, uint32_t phase, int32_t period)
{
    int32_t dx = x - p[0], dy = y - p[1];
    int32_t d = (int32_t)Fx_Sqrt((uint32_t)fx_mul(dx, dx) + (uint32_t)fx_mul(dy, dy)) % period;
    return g_sin[((int32_t)(((int64_t)d << 16) / period) + (int32_t)phase) & 0xfff];
}

void Fe_DotPicture(int16_t px, int16_t py, const uint8_t *cells)
{
    Tex *t = g_fe.lg_matrix;
    if (!t) return;
    int w, h;
    screen(&w, &h);
    /* two ripples, one from the top left, one from (640, 0) taken as 16.16 (so just right of it) */
    static const int32_t a[2] = {0, 0};
    int32_t b[2] = {w, 0};
    uint32_t c = Timer_Centis();
    uint16_t ph1 = (uint16_t)((((uint64_t)(uint32_t)((c + 1) * (uint32_t)-0x6000) * 0x16c16c16dULL) >> 33) >> 8);
    c = Timer_Centis();
    uint16_t ph2 = (uint16_t)((((uint64_t)(uint32_t)((c + 1) * (uint32_t)-0x3000) * 0x16c16c16dULL) >> 33) >> 8);
    Rect16 src = {0, 0, t->width, t->height};
    for (int i = 0; i < 0x12; i++)
        for (int j = 0; j < 0xc; j++) {
            Rect16 d = {(int16_t)((w * 0x12 / 0x280) * i + px), (int16_t)((h * 0x12 / 0x1e0) * j + py), t->width, t->height};
            int32_t fx = (int32_t)(((int64_t)d.x << 16) / w), fy = (int32_t)(((int64_t)d.y << 16) / w);
            int32_t v = (ripple(a, fx, fy, ph1, 0x20000) + ripple(b, fx, fy, ph2, 0x140000)) / 2;
            int16_t sh = (int16_t)(((int64_t)v * 0x30000) >> 32);
            d.x = (int16_t)(d.x - sh), d.y = (int16_t)(d.y - sh);
            uint32_t pc = exe_u32(0x523ef0 + 4u * cells[i + j * 0x12]);
            int32_t k = v / 4 + 0xc000;   /* the brightness, 0.5 to 1 */
            uint8_t rgba[4] = {(uint8_t)(((int64_t)((pc & 0xff) << 16) * k) >> 32), (uint8_t)(((int64_t)((pc >> 8 & 0xff) << 16) * k) >> 32),
                               (uint8_t)(((int64_t)((pc >> 16 & 0xff) << 16) * k) >> 32), (uint8_t)(pc >> 24)};
            Sprite_Add(&src, &d, t, 1, 0, NULL, NULL, rgba, 8);
        }
}

void Fe_TitleRow(int x, int y, const char *const *names, int n)
{
    for (int i = 0; i < n; i++)
        if (names[i]) {
            char b[256];
            snprintf(b, sizeof b, "%s", names[i]);
            x = title(x, y, i == n - 1 ? C68 : C6c, i == n - 1, b);
        }
}

void List_Draw(Menu *m, const char *hdr, int cy, int xo, int first, bool lit)
{
    Tex *on = g_fe.arrow_a, *off = g_fe.arrow_d;
    if (!on || !off) return;
    int w, h;
    screen(&w, &h);
    int cnt = 0;
    for (int i = 0; i < m->n; i++) cnt += (m->items[i].flags & MIF_SELECTABLE) != 0;
    int tx, ax;
    if (xo == -1) tx = w * 0x7a / 0x280, ax = w * 100 / 0x280;
    else tx = (w * 0x7a / 0x280 - w * 0x18 / 0x280) + xo, ax = (w * 100 / 0x280 - w * 0x18 / 0x280) + xo;
    if (cy == -1) cy = (h * 8 / 0x1e0 + h * 0x26 / 0x1e0 + h * 0x180 / 0x1e0) / 2;
    int row = h * 0x24 / 0x1e0, rows = cnt - first + (hdr != NULL);
    int top = cy - row * rows / 2;
    int lx = xo == -1 ? w * 99 / 0x280 : (w * 99 / 0x280 - w * 0x18 / 0x280) + xo;
    int16_t r[4] = {(int16_t)lx, (int16_t)(hdr ? top + row : top), (int16_t)(w * 0x11a / 0x280), 1};
    uint32_t c1 = m->sel == first ? C68 : C6c, c2 = m->sel == first ? C74 : C78;
    Rect_Fill(r, col(c2), 1);
    r[1]++;
    Rect_Fill(r, col(c1), 1);
    for (int k = 0; k < rows; k++) {
        while (!(m->items[first].flags & MIF_SELECTABLE)) first++;
        int16_t ay = (int16_t)(h * 0x14 / 0x1e0 + top + row * k - off->height / 2);
        if (!hdr || k != 0) {
            MenuItem *it = &m->items[first];
            Tex *t;
            uint32_t c;
            if (m->sel == first && lit) c = C68, t = on;
            else t = off, c = (it->flags & MIF_ENABLED) && lit ? C6c : C70;
            Rect16 src = {0, 0, t->width, t->height}, dst = {(int16_t)ax, ay, on->width, on->height};
            Sprite_Add(&src, &dst, t, 1, 0, NULL, NULL, col(c), 8);
            const char *s = it->text == -1 ? it->label : Text_Get(it->text);
            Font_Print(1, s ? s : "", tx, h * 0x18 / 0x1e0 + r[1], col(c), 0x11);
            uint32_t a, b;
            if (m->sel == first || m->sel == first + 1) a = C68, b = C74;
            else if (!(it->flags & MIF_ENABLED) && !(m->items[first + 1].flags & MIF_ENABLED)) a = C70, b = C7c;
            else a = C6c, b = C78;
            r[1] = (int16_t)(row * (k + 1) + top);
            Rect_Fill(r, col(b), 1);
            r[1]++;
            Rect_Fill(r, col(a), 1);
            first++;
        } else {
            int16_t band[4] = {r[0], (int16_t)top, r[2], (int16_t)row};
            Rect_Fill(band, col(C78), 4);
            Font_Print(1, hdr, tx, h * 0x18 / 0x1e0 + top, gm(0x524984), 0x11);
            r[1] = (int16_t)(row + top);
        }
    }
}
