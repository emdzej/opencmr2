/* The glows (see glow.h). */
#include "world/glow.h"
#include "render/polyq.h"
#include "render/sprite3d.h"
#include "fixmath.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static Glow *pool;           /* DAT_006a2a98 */
static int npool, used;      /* DAT_006a2bcc, DAT_006a2bc8 */

int Glow_Used(void) { return used; }

void Glow_Alloc(int n)
{
    Glow_Free();
    npool = n & 0xff;
    pool = calloc((size_t)(npool ? npool : 1), sizeof *pool);
}

void Glow_Free(void)
{
    free(pool);
    pool = NULL;
    npool = 0;
}

void Glow_Clear(void)
{
    for (int i = 0; i < npool; i++) pool[i].kind = 0;
    used = 0;
}

Glow *Glow_Add(uint32_t kind, const int32_t pos[3], const int32_t *dir, int32_t w, int32_t h, Tex *tex,
               Tex *shaft_tex, int32_t level, Obj *parent, uint8_t pool_level, int32_t size)
{
    if (used >= npool) return NULL;
    int i = 0;
    while (pool[i].kind != 0)
        if (++i >= npool) return NULL;
    Glow *g = &pool[i];
    g->kind = kind;
    memcpy(g->pos, pos, sizeof g->pos);
    if (dir) memcpy(g->dir, dir, sizeof g->dir);
    else memset(g->dir, 0, sizeof g->dir);
    g->w = w, g->h = h;
    g->tex = tex, g->shaft_tex = shaft_tex;
    g->level = level;
    g->parent = parent;
    g->on = 1;
    g->pool = pool_level;
    g->size = size;
    used++;
    return g;
}

void Glow_Show(Glow *g, bool on) { if (g) g->on = on; }
void Glow_Level(Glow *g, int32_t level) { if (g) g->level = level; }

void Glow_Move(Glow *g, const int32_t *pos, const int32_t *dir)
{
    if (!g) return;
    if (pos) memcpy(g->pos, pos, sizeof g->pos);
    if (dir) memcpy(g->dir, dir, sizeof g->dir);
}

void Glow_Plane(Glow *g, const int32_t at[3], const int32_t n[3], int32_t shaft)
{
    if (!g) return;
    memcpy(g->at, at, sizeof g->at);
    memcpy(g->normal, n, sizeof g->normal);
    g->shaft = shaft;
}

/* 0x4b9c20: v times the 3 x 3 of a matrix (row vector on the left) */
static void vmul(int32_t o[3], const int32_t v[3], const int32_t m[16])
{
    for (int k = 0; k < 3; k++) o[k] = fx_mul(m[8 + k], v[2]) + fx_mul(m[4 + k], v[1]) + fx_mul(m[k], v[0]);
}

static int32_t dot(const int32_t a[3], const int32_t b[3]) { return fx_mul(a[2], b[2]) + fx_mul(a[0], b[0]) + fx_mul(a[1], b[1]); }
static uint32_t len(const int32_t v[3]) { return Fx_Sqrt((uint32_t)fx_mul(v[0], v[0]) + (uint32_t)fx_mul(v[1], v[1]) + (uint32_t)fx_mul(v[2], v[2])); }
static int32_t recip(int32_t v) { return (int32_t)(0x100000000LL / v); }

static void vert(PolyVert *p, const int32_t v[3], uint8_t grey, int32_t u, int32_t t)
{
    p->x = v[0], p->y = v[1], p->z = v[2];
    p->rgba[0] = p->rgba[1] = p->rgba[2] = grey, p->rgba[3] = 0xff;
    p->u = u, p->v = t;
}

static void quad(const int32_t c[4][3], uint8_t grey, Tex *tex, uint32_t flags)
{
    PolyVert v[4];
    vert(&v[0], c[0], grey, 0, 0);
    vert(&v[1], c[1], grey, 0xfff9, 0);
    vert(&v[2], c[2], grey, 0xfff9, 0xfff9);
    vert(&v[3], c[3], grey, 0, 0xfff9);
    Poly_Add(&v[0], &v[1], &v[2], tex, flags);
    Poly_Add(&v[0], &v[2], &v[3], tex, flags);
}

static int32_t side[3];                              /* 0x521010: the camera's level right */
static const int32_t up[3] = {0, 0x10000, 0};        /* 0x521004 */

/* 0x4ae470: the shaft, an upright quad from the glow down to its plane, its width 0.6 w, moved
   towards the camera by its distance less 3 (at most 10) and shrunk to keep its size; it fades out
   from 2 above the plane to 2.5 */
static void shaft(const Glow *g, const int32_t p[3], int32_t w, uint8_t grey, const int32_t cam[3])
{
    int32_t rel[3] = {p[0] - g->at[0], p[1] - g->at[1], p[2] - g->at[2]};
    int32_t h = dot(g->normal, rel), f[3], c[3];
    for (int i = 0; i < 3; i++) f[i] = p[i] - fx_mul(g->normal[i], h);
    int32_t hh = fx_mul(h, 0x20000), sw = fx_mul(-w, 0x9999);
    for (int i = 0; i < 3; i++) c[i] = cam[i] - f[i];
    int32_t d = (int32_t)len(c);
    if (!d) return;   /* the original divides by zero */
    int32_t inv = recip(d), l = d - 0x30000;
    if (l > 0xa0000) l = 0xa0000;
    for (int i = 0; i < 3; i++) f[i] += fx_mul(fx_mul(c[i], inv), l);
    int32_t k = 0x10000 - (int32_t)(((int64_t)l << 16) / d);
    int32_t u[3], s[3], b[3], q[4][3];
    for (int i = 0; i < 3; i++) {
        u[i] = fx_mul(fx_mul(up[i], hh), k);
        s[i] = fx_mul(fx_mul(side[i], sw), k);
        b[i] = f[i] - u[i];
        q[0][i] = f[i] - s[i], q[1][i] = f[i] + s[i], q[2][i] = b[i] + s[i], q[3][i] = b[i] - s[i];
    }
    int32_t a = h - 0x20000;
    if (a < 0) a = 0;
    a = fx_mul(a, 0x20000);
    if (a > 0x10000) a = 0x10000;
    int32_t lv = fx_mul(0x10000 - a, g->shaft);
    quad(q, (uint8_t)(((int64_t)lv * (int32_t)((uint32_t)grey << 16)) >> 32), g->shaft_tex, 0x16);
}

/* 0x4ae950: the pool, a square on the plane under the glow (side size x w), fading out from 0.4 to
   1.0 above the plane, moved a unit towards the camera and shrunk to keep its size */
static void pool_of(const Glow *g, const int32_t p[3], const int32_t cam[3])
{
    const int32_t *n = g->normal;
    int32_t rel[3] = {p[0] - g->at[0], p[1] - g->at[1], p[2] - g->at[2]};
    int32_t d = dot(n, rel), f[3];
    for (int i = 0; i < 3; i++) f[i] = p[i] - fx_mul(n[i], d);
    int32_t t = fx_mul((d < 0 ? -d : d) - 0x6666, 0x1aaac);
    t = t > 0x10000 ? 0x10000 : t < 0 ? 0 : t;
    int32_t fade = 0x10000 - t, nx = fx_mul(n[0], 0x10000);
    int32_t a[3] = {0x10000 - fx_mul(n[0], nx), -fx_mul(n[1], nx), -fx_mul(n[2], nx)};
    Vec_Unit(a);
    int32_t b[3] = {fx_mul(a[1], n[2]) - fx_mul(a[2], n[1]), fx_mul(a[2], n[0]) - fx_mul(a[0], n[2]),
                    fx_mul(a[0], n[1]) - fx_mul(a[1], n[0])};
    Vec_Unit(b);
    int32_t sz = fx_mul(g->size, g->w);
    for (int i = 0; i < 3; i++) a[i] = fx_mul(a[i], sz), b[i] = fx_mul(b[i], sz);
    int32_t c[3] = {cam[0] - f[0], cam[1] - f[1], cam[2] - f[2]};
    int32_t l = (int32_t)len(c);
    if (l > 0xffff) {
        int32_t inv = recip(l), k;
        for (int i = 0; i < 3; i++) f[i] += fx_mul(c[i], inv);
        k = 0x10000 - inv;
        for (int i = 0; i < 3; i++) a[i] = fx_mul(a[i], k), b[i] = fx_mul(b[i], k);
    }
    int32_t q[4][3];
    for (int i = 0; i < 3; i++) {
        q[0][i] = a[i] + f[i] - b[i];
        q[1][i] = a[i] + f[i] + b[i];
        q[2][i] = f[i] - a[i] + b[i];
        q[3][i] = f[i] - a[i] - b[i];
    }
    quad(q, (uint8_t)((uint32_t)g->pool * (uint32_t)fx_mul(fade, g->level) >> 16), g->tex, 0xe);
}

/* The view's angle to a kind 2 or 3 glow: its direction against the unit vector from the camera (the
   difference first divided by its largest part), as degrees in 16.16 through the arcsine table */
static int32_t view_angle(const int32_t dir[3], const int32_t p[3], const int32_t cam[3])
{
    int32_t v[3] = {p[0] - cam[0], p[1] - cam[1], p[2] - cam[2]};
    int32_t ax = abs(v[0]), ay = abs(v[1]), az = abs(v[2]), m;
    if (ax > ay && ax > az) m = ax;
    else if (ay > ax && ay > az) m = ay;
    else m = az;
    if (m) {
        int32_t r = recip(m);
        for (int i = 0; i < 3; i++) v[i] = fx_mul(v[i], r);
    }
    if (!v[0] && !v[1] && !v[2]) v[0] = 0x10000;
    Vec_Unit(v);
    int32_t d = fx_mul(dir[2], v[2]) + fx_mul(dir[0], v[0]) + fx_mul(dir[1], v[1]);
    int32_t ad = d < 0 ? -d : d;
    if (ad > 0xfd6f) return d < 1 ? 0xb40000 : 0;
    int16_t s;
    if (ad > 0x10000) s = (int16_t)g_asin[4095];
    else {
        int64_t k = llrint((double)ad * (1.0 / 65536.0) * -4095.0);
        s = (int16_t)g_asin[-k];
        if (d < 0) s = (int16_t)-s;
    }
    return (0x400 - s) * 0x1680;
}

void Glow_Draw(const Obj *camera, uint32_t pass)
{
    const int32_t *m = camera->world.m;
    const int32_t cam[3] = {m[12], m[13], m[14]};
    uint8_t mask = (uint8_t)(1u << (pass & 0x1f));
    int32_t fx = m[8], fz = m[10];
    int32_t l = (int32_t)Fx_Sqrt((uint32_t)fx_mul(fx, fx) + (uint32_t)fx_mul(fz, fz));
    if (!l) fx = fz = 0;
    else {
        int32_t r = recip(l);
        fx = fx_mul(fx, r), fz = fx_mul(fz, r);
    }
    side[0] = -fz, side[1] = 0, side[2] = fx;
    for (int i = 0; i < npool; i++) {
        Glow *g = &pool[i];
        if (!g->kind || !g->on || g->level <= 0) continue;
        int32_t dir[3], p[3];
        if (g->parent) {
            const Obj *o = g->parent;
            if (!(mask & o->mask)) continue;
            if (o->parent && !(mask & o->parent->mask)) continue;
            vmul(dir, g->dir, o->world.m);
            vmul(p, g->pos, o->world.m);
            for (int k = 0; k < 3; k++) p[k] += o->world.m[12 + k];
        } else {
            memcpy(dir, g->dir, sizeof dir);
            memcpy(p, g->pos, sizeof p);
        }
        bool sprite = true;
        int32_t f = 0x10000;
        if (g->kind == 2 || g->kind == 3) {
            int32_t a = view_angle(dir, p, cam);
            if (a > 0x5a0000) f = fx_mul(a - 0x5a0000, 0x3d7);
            else if (a < 0x5a0000 && g->kind == 3) f = fx_mul(0x5a0000 - a, 0x3d7);
            else sprite = false;
            if (sprite) {
                if (f > 0x10000) f = 0x10000;
                else if (f < 1) sprite = false;
            }
        }
        if (sprite) {
            int32_t lv = fx_mul(f, g->level);
            uint8_t grey = (uint8_t)((lv * 0xfe) >> 16);
            Sprite3d s = {{p[0], p[1], p[2]}, {-g->w, g->h, g->w, -g->h}, {grey, grey, grey, 0xff}, 0, 0, 0};
            int32_t t[3] = {cam[0] - p[0], cam[1] - p[1], cam[2] - p[2]};
            int32_t d = (int32_t)len(t);
            if (d > 0xffff) {
                int32_t inv = recip(d), k = 0x10000 - inv;
                for (int j = 0; j < 3; j++) s.pos[j] += fx_mul(t[j], inv);
                for (int j = 0; j < 4; j++) s.e[j] = fx_mul(s.e[j], k);
            }
            Sprite3d_Add(&s, g->tex);
            if (g->shaft) shaft(g, p, g->w, grey, cam);
        }
        if (g->pool) pool_of(g, p, cam);
    }
}
