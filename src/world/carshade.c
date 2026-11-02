/* The trees' shadows on the cars (see carshade.h). */
#include "world/carshade.h"
#include "world/relight.h"
#include "world/section.h"
#include "world/tre.h"
#include "render/d3d7.h"
#include "render/rstate.h"
#include "render/tex.h"
#include "c3d.h"
#include "fixmath.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

enum { OBJS = 0x1d, CYLS = 9, ITEMS = 99, VB_VERTS = 0x7080, RUNS = 300, CYL_VERTS = 0x16, CYL_POLYS = 0x14 };

/* A mesh's vertices (0x30 bytes: position, normal, colour, specular, two coordinates) and polygons
   (0x4c bytes, the corners' indices at +0x40), resolved */
typedef struct {
    const uint8_t *verts, *polys;
    int nv, np;
} Src;

/* A mesh's entry (0x58 bytes in the original): the sun (+0x00) and the three axes (+0x0c-+0x24) in the
   object's frame, the mesh (+0x30) and its object (+0x34), the normals (+0x38), the positions a little
   out along them (+0x3c), the normals on the sun (+0x40), the chosen vertices' positions (+0x44), their
   copies to draw (+0x48) and the triangles' corners among them (+0x4c), the counts (+0x50, +0x52) and
   the dirty mark (+0x54) */
typedef struct {
    float axes[12];
    Src src;
    const void *key;
    const Obj *obj;
    float *nrm, *pos, *dot, *sel;
    uint8_t *out;
    uint16_t *tri;
    uint16_t ntri, nsel;
    bool dirty;
} Entry;

/* A listed object (0x14 bytes): the object, its entries and their count, the "again" mark (+0x0c) and
   the "shaded this frame" one (+0x10) */
typedef struct {
    const Obj *obj;
    Entry *e;
    uint8_t n;
    bool reset, used;
} Listed;

/* A cylinder standing in for a mesh (0x6e01c4, at most nine): the source mesh's name, its record */
typedef struct {
    char name[0x10];
    uint8_t verts[CYL_VERTS * C3D_VERTEX];
    uint8_t polys[CYL_POLYS * C3D_POLY];
} Cyl;

typedef struct { uint32_t start, count, tex; uint8_t passes; } Run;   /* 0x6dead0, 16 bytes each */

static Listed *list[OBJS];        /* 0x6e0124 */
static uint8_t nlist;             /* 0x6deab8 */
static Cyl *cyl[CYLS];            /* 0x6e01c4 */
static uint8_t ncyl;              /* 0x6e0b99 */
static const uint8_t *items[ITEMS];   /* 0x6dfe14 */
static uint16_t nitems;           /* 0x6e01c0 */
static Run runs[RUNS];
static int run;                   /* the cursor 0x6e01f0 */
static uint32_t nverts;           /* 0x6e0b94 */
static uint8_t nruns;             /* 0x6e0b98 */
static uint32_t last_tex = 0xffffffffu, last_pass = 0xffffffffu;   /* 0x5210d4, 0x5210d8 */
static D3dVB *vb;                 /* PTR_DAT_00520b78 + 0x33c */
static uint8_t *vbp;              /* the locked buffer, 0x6e0b90 */

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static void wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8); }
static float fget(const uint8_t *p) { float f; memcpy(&f, p, 4); return f; }
static void fput(uint8_t *p, float f) { memcpy(p, &f, 4); }
static float fx2f(int32_t v) { return (float)v * (float)(1.0 / 65536.0); }
static const float *wm(const Obj *o) { return &o->worldf.m[0][0]; }

static Src src_of(const Obj *o)
{
    Src s = {0};
    if (!o->model || !o->mesh) return s;
    s.verts = C3d_Ref(o->model, o->mesh + 0x0c, o->model->verts);
    s.polys = C3d_Ref(o->model, o->mesh + 0x24, o->model->polys);
    s.nv = s.verts ? (int)rd32(o->mesh + 0x10) : 0;
    s.np = s.polys ? (int)rd32(o->mesh + 0x28) : 0;
    return s;
}

static bool usable(const Obj *o)
{
    uint32_t t = o->type & 0xff;
    return o->kind == 0 && o->mesh && t != 6 && t != 0xf && t != 0xe;
}

/* 0x4b67f0: a cylinder of the mesh's size (cached by the mesh's name): ten sides of radius the
   farthest from the axis of the corners alone on their side of z = 0 in a polygon, from the lowest z
   to the highest (both at least 0 away), its ends fans from the axis (normals -z and +z) */
static const Cyl *cylinder(const Obj *o, const Src *s)
{
    const char *name = (const char *)o->mesh;
    for (int i = 0; i < ncyl; i++)
        if (!strncmp(cyl[i]->name, name, sizeof cyl[i]->name)) return cyl[i];
    if (ncyl >= CYLS) return NULL;
    float zmax = 0, zmin = 0, r = 0;
    for (int i = 0; i < s->nv; i++) {
        float z = fget(s->verts + (size_t)i * C3D_VERTEX + 8);
        if (zmax < z) zmax = z;
        if (z < zmin) zmin = z;
    }
    for (int p = 0; p < s->np; p++) {
        const uint8_t *q = s->polys + (size_t)p * C3D_POLY;
        const uint8_t *a = s->verts + (size_t)rd16(q + 0x40) * C3D_VERTEX, *b = s->verts + (size_t)rd16(q + 0x42) * C3D_VERTEX,
                      *c = s->verts + (size_t)rd16(q + 0x44) * C3D_VERTEX;
        float az = fget(a + 8), bz = fget(b + 8), cz = fget(c + 8);
        const uint8_t *k = NULL;
        if ((az < 0 && 0 < bz && 0 < cz) || (0 < az && bz < 0 && cz < 0)) k = a;
        if ((bz < 0 && 0 < az && 0 < cz) || (0 < bz && az < 0 && cz < 0)) k = b;
        if ((cz < 0 && 0 < bz && 0 < az) || (0 < cz && bz < 0 && az < 0)) k = c;
        if (k) {
            float x = fget(k), y = fget(k + 4);
            float d = (float)sqrt((double)y * y + (double)x * x);
            if (r < d) r = d;
        }
    }
    Cyl *n = calloc(1, sizeof *n);
    if (!n) return NULL;
    strncpy(n->name, name, sizeof n->name - 1);
    cyl[ncyl++] = n;
    for (int i = 0; i < 10; i++) {
        uint16_t a = (uint16_t)llrint((double)(i * 0x240000) * (1.0 / 5760.0));
        uint8_t *v = n->verts + (size_t)(i + 1) * C3D_VERTEX;
        fput(v, (float)g_sin[a & 0xfff] * (float)(1.0 / 65536.0) * r);
        fput(v + 4, (float)g_sin[(a + 0x400) & 0xfff] * (float)(1.0 / 65536.0) * r);
    }
    for (int i = 0; i < 11; i++) {
        uint8_t *v = n->verts + (size_t)i * C3D_VERTEX;
        fput(v + 8, zmin), fput(v + 0x14, -1.0f);
    }
    for (int i = 11; i < CYL_VERTS; i++) {
        uint8_t *v = n->verts + (size_t)i * C3D_VERTEX;
        memcpy(v, v - 11 * C3D_VERTEX, C3D_VERTEX);
        fput(v + 8, zmax), fput(v + 0xc, 0), fput(v + 0x10, 0), fput(v + 0x14, 1.0f);
    }
    for (int i = 0; i < 10; i++) {
        uint8_t *q = n->polys + (size_t)i * C3D_POLY;
        wr16(q + 0x40, 0), wr16(q + 0x42, (uint16_t)(i + 1)), wr16(q + 0x44, (uint16_t)(i == 9 ? 1 : i + 2));
    }
    for (int i = 10; i < CYL_POLYS; i++)
        for (int k = 0; k < 3; k++)
            wr16(n->polys + (size_t)i * C3D_POLY + 0x40 + 2 * k, (uint16_t)(rd16(n->polys + (size_t)(i - 10) * C3D_POLY + 0x40 + 2 * k) + 11));
    return n;
}

static bool entry_init(Entry *e, const Obj *x, const Obj *root, bool keep)
{
    e->src = src_of(x);
    e->key = x->mesh;
    if (!keep && (root->type & 0xff) < 5) {
        const Cyl *c = cylinder(x, &e->src);
        if (c) e->src = (Src){c->verts, c->polys, CYL_VERTS, CYL_POLYS}, e->key = c;
    }
    int nv = e->src.nv, np = e->src.np;
    e->nrm = calloc((size_t)nv * 3 + 1, sizeof(float));
    e->dot = calloc((size_t)nv + 1, sizeof(float));
    e->pos = calloc((size_t)nv * 3 + 1, sizeof(float));
    e->tri = calloc((size_t)np * 3 + 1, sizeof(uint16_t));
    e->out = calloc((size_t)nv + 1, C3D_VERTEX);
    e->sel = calloc((size_t)nv * 3 + 1, sizeof(float));
    if (!e->nrm || !e->dot || !e->pos || !e->tri || !e->out || !e->sel) return false;
    for (int i = 0; i < nv; i++) memcpy(e->out + (size_t)i * C3D_VERTEX + 0x1c, &(uint32_t){0xff000000u}, 4);
    e->dirty = true;
    e->obj = x;
    e->ntri = e->nsel = 0;
    return true;
}

void CarShade_Add(Obj *o, bool keep)
{
    if (!o || nlist >= OBJS) return;
    Listed *l = calloc(1, sizeof *l);
    if (!l) return;
    list[nlist++] = l;
    l->obj = o;
    l->reset = true;
    int n = usable(o) ? 1 : 0;
    for (const Obj *c = o->child; c; c = c->next)
        for (const Obj *x = c; x; x = x->child)
            if (usable(x)) n++;
    l->e = calloc((size_t)n + 1, sizeof *l->e);
    if (!l->e) return;
    int k = 0;
    if (usable(o) && entry_init(&l->e[k], o, o, keep)) k++;
    for (const Obj *c = o->child; c; c = c->next)
        for (const Obj *x = c; x; x = x->child)
            if (usable(x) && entry_init(&l->e[k], x, o, keep)) k++;
    l->n = (uint8_t)k;
}

void CarShade_Free(void)
{
    for (int i = 0; i < OBJS; i++) {
        Listed *l = list[i];
        if (!l) continue;
        for (int k = 0; l->e && k < l->n; k++) {
            Entry *e = &l->e[k];
            free(e->nrm), free(e->dot), free(e->pos), free(e->tri), free(e->out), free(e->sel);
        }
        free(l->e);
        free(l);
        list[i] = NULL;
    }
    for (int i = 0; i < CYLS; i++) free(cyl[i]), cyl[i] = NULL;
    nlist = 0, ncyl = 0, nverts = 0, nruns = 0;
    if (vb) D3dVB_Free(vb), vb = NULL;
}

static Listed *find(const Obj *o)
{
    for (int i = 0; i < nlist; i++)
        if (list[i]->obj == o) return list[i];
    return NULL;
}

void CarShade_Mark(const Obj *o, const uint8_t *mesh)
{
    if (!g_tre_have) return;
    Listed *l = find(o);
    if (!l) return;
    for (int k = 0; k < l->n; k++)
        if (l->e[k].key == mesh) {
            l->e[k].dirty = true;
            l->reset = true;
            return;
        }
}

void CarShade_Begin(void)
{
    if (!g_tre_have) return;
    if (!vb) vb = D3dVB_Create(FVF_MODEL, VB_VERTS);
    vbp = vb ? D3dVB_Lock(vb) : NULL;   /* (flags 0x21) */
    nverts = 0, nruns = 0;
    last_tex = last_pass = 0xffffffffu;
    run = 0;
}

void CarShade_End(void)
{
    if (!g_tre_have) return;
    runs[run].count = nverts - runs[run].start;
    if (vb && vbp) D3dVB_Unlock(vb);
    vbp = NULL;
}

void CarShade_Gather(const Obj *o, int32_t reach, int16_t cell)
{
    nitems = 0;
    const int32_t *h = Tre_Header();
    if (!o || !o->mask || !h || !find(o)) return;
    int32_t x = o->world.m[12], z = o->world.m[14];
    int w = g_sec_row;
    int16_t cells[9] = {cell, (int16_t)(cell - 1), (int16_t)(cell + 1), (int16_t)(cell - w), (int16_t)(cell - w - 1),
                        (int16_t)(cell - w + 1), (int16_t)(cell + w), (int16_t)(cell + w - 1), (int16_t)(cell + w + 1)};
    /* the shadows' axes on the ground: the header's first and third, flat (0x6e0b28, 0x6dfda0) */
    int32_t ax = h[3], az = h[5], bx = h[9], bz = h[11];
    for (int i = 0; i < 9; i++) {
        int c = cells[i];
        if (c < 0 || c >= g_nsec || g_tre_node[c] == -1) continue;
        int n;
        const uint8_t *it = Tre_NodeItems(g_tre_node[c], &n);
        for (int j = 0; it && j < n; j++) {
            const uint8_t *item = it + (size_t)j * 0x30;
            if (!Tre_ItemScenery(item)) continue;
            int32_t dx = x - (int32_t)rd32(item + 8), dz = z - (int32_t)rd32(item + 0xc);
            int32_t a = fx_mul(dz, az) + fx_mul(dx, ax), b = fx_mul(dz, bz) + fx_mul(dx, bx);
            if (a < 0) a = -a;
            if (b < 0) b = -b;
            if (a - reach <= (int32_t)rd32(item + 0x10) && b - reach <= (int32_t)rd32(item + 0x14) && nitems < ITEMS)
                items[nitems++] = item;
        }
    }
}

/* 0x4b9ec0: v by the matrix's rows (into the object's frame); 0x4b9d40: by its columns (out of it) */
static void into(float o[3], const float v[3], const float *m)
{
    float x = v[0] * m[0] + m[1] * v[1] + m[2] * v[2], y = m[5] * v[1] + m[6] * v[2] + m[4] * v[0],
          z = m[9] * v[1] + m[10] * v[2] + m[8] * v[0];
    o[0] = x, o[1] = y, o[2] = z;
}
static void outof(float o[3], const float v[3], const float *m)
{
    o[0] = v[0] * m[0] + m[4] * v[1] + m[8] * v[2];
    o[1] = m[1] * v[0] + m[5] * v[1] + m[9] * v[2];
    o[2] = m[2] * v[0] + m[6] * v[1] + m[10] * v[2];
}

/* 0x4b4180: the triangles with a corner facing the sun copied: each corner once, at its position out
   along its normal in the world, the shadow colour with the shadow alpha by the strength (at most 1.0),
   none facing away, fading in over the first tenth */
static void shade(Entry *e, int32_t k)
{
    const int32_t *h = Tre_Header();
    if (!h) return;
    if (k > 0x10000) k = 0x10000;
    e->ntri = e->nsel = 0;
    float a = (float)fx_mul(g_shadow_alpha, k) * (float)(1.0 / 65536.0);
    uint8_t full = (uint8_t)llrint(a);
    int nv = e->src.nv;
    uint16_t *map = malloc(((size_t)nv + 1) * sizeof *map);   /* 0x6e0354 */
    if (!map) return;
    memset(map, 0xff, ((size_t)nv + 1) * sizeof *map);
    const float *m = wm(e->obj);
    float t[3] = {m[12], m[13], m[14]};
    for (int i = 0; i < 4; i++) {   /* the sun (0x6e01b0), the axes (0x6e0220, 0x6e022c, 0x6e0238) */
        const int32_t *v = i == 0 ? h : h + 3 * i;
        float f[3] = {fx2f(v[0]), fx2f(v[1]), fx2f(v[2])};
        into(&e->axes[3 * i], f, m);
    }
    for (int i = 0; i < nv; i++)
        e->dot[i] = e->nrm[3 * i] * e->axes[0] + e->nrm[3 * i + 1] * e->axes[1] + e->nrm[3 * i + 2] * e->axes[2];
    uint32_t rgb = g_shadow_rgb;
    for (int p = 0; p < e->src.np; p++) {
        const uint8_t *q = e->src.polys + (size_t)p * C3D_POLY;
        uint16_t c[3] = {rd16(q + 0x40), rd16(q + 0x42), rd16(q + 0x44)};
        if (!(e->dot[c[0]] >= 0 || e->dot[c[1]] >= 0 || e->dot[c[2]] >= 0)) continue;
        for (int j = 0; j < 3; j++) {
            uint16_t v = c[j];
            if (map[v] != 0xffff) {
                e->tri[3 * e->ntri + j] = map[v];
                continue;
            }
            float *s = &e->sel[3 * e->nsel];
            memcpy(s, &e->pos[3 * v], 3 * sizeof *s);
            e->tri[3 * e->ntri + j] = e->nsel;
            map[v] = e->nsel;
            float d = e->dot[v];
            uint8_t al = full;
            if (d >= 0) {
                if (d < 0.1f) al = (uint8_t)llrint(d * 10.0f * a);
            } else
                al = 0;
            uint8_t *o = e->out + (size_t)e->nsel * C3D_VERTEX;
            uint32_t col = (uint32_t)al << 24 | (rgb & 0xff) << 16 | (rgb >> 8 & 0xff) << 8 | (rgb >> 16 & 0xff);
            memcpy(o + 0x18, &col, 4);
            float w[3];
            outof(w, s, m);
            fput(o, w[0] + t[0]), fput(o + 4, w[1] + t[1]), fput(o + 8, w[2] + t[2]);
            e->nsel++;
        }
        e->ntri++;
    }
    free(map);
}

/* 0x4b4490: after a change the positions (each 0.005 out along its normal) and normals again; on the
   first tree of a frame the triangles copied */
static void prepare(Listed *l, int32_t k)
{
    if (l->reset)
        for (int i = 0; i < l->n; i++) {
            Entry *e = &l->e[i];
            if (e->dirty)
                for (int v = 0; v < e->src.nv; v++) {
                    const uint8_t *s = e->src.verts + (size_t)v * C3D_VERTEX;
                    for (int q = 0; q < 3; q++) {
                        float n = fget(s + 0xc + 4 * q);
                        e->nrm[3 * v + q] = n;
                        e->pos[3 * v + q] = n * 0.005f + fget(s + 4 * q);
                    }
                }
            e->dirty = false;
        }
    l->reset = false;
    if (l->used)
        for (int i = 0; i < l->n; i++) shade(&l->e[i], k);
    l->used = false;
}

/* 0x4b5770: a tree's billboard texture along the sun onto the copies: each chosen corner's
   coordinates from where the sun's line through it meets the billboard's plane, and the triangles
   inside the texture's rectangle into the buffer (a new run when the texture or the passes change) */
static void project(const uint8_t *item, Listed *l, uint8_t passes)
{
    const Scenery *sc = Tre_ItemScenery(item);
    if (!sc || !sc->mesh || !sc->model) return;
    const uint8_t *sv = C3d_Ref(sc->model, sc->mesh + 0x0c, sc->model->verts);
    const uint8_t *sp = C3d_Ref(sc->model, sc->mesh + 0x24, sc->model->polys);
    if (!sv || !sp) return;
    int n = (int)rd32(sc->mesh + 0x10);
    float xmax = 0, xmin = 0, ymax = 0, ymin = 0, umin = 10.0f, umax = -10.0f, vmin = 10.0f, vmax = -10.0f;
    for (int i = 0; i < n; i++) {
        const uint8_t *v = sv + (size_t)i * C3D_VERTEX;
        float x = fx2f(sc->m.m[0]) * fget(v);
        if (xmax < x) xmax = x;
        else if (x < xmin) xmin = x;
        float y = fx2f(sc->m.m[5]) * fget(v + 4);
        if (ymax < y) ymax = y;
        else if (y < ymin) ymin = y;
        float u = fget(v + 0x20), w = fget(v + 0x24);
        if (u < umin) umin = u;
        if (umax < u) umax = u;
        if (w < vmin) vmin = w;
        if (vmax < w) vmax = w;
    }
    float f = fx2f((int32_t)rd32(item + 0x18)) + 1.0f;
    xmin = f * xmin;
    float kx = 1.0f / (f * xmax - xmin);
    /* (the original leaves the low y unscaled) */
    float ky = 1.0f / ((fx2f((int32_t)rd32(item + 0x1c)) + 1.0f) * ymax - ymin);
    float vy = ky * ymin + 1.0f;
    float su = kx * (umax - umin), sv2 = ky * (vmax - vmin);
    float at[3] = {fx2f(sc->pos[0]), fx2f(sc->pos[1]), fx2f(sc->pos[2])};
    uint32_t tex = rd32(sp + 4);
    if (last_tex == 0xffffffffu || last_pass == 0xffffffffu) {
        runs[run].passes = passes, runs[run].start = nverts, runs[run].tex = tex;
        nruns++;
    } else if (last_tex != tex || last_pass != passes) {
        runs[run].count = nverts - runs[run].start;
        nruns++;
        if (run < RUNS - 1) run++;   /* (the original has no limit) */
        runs[run].passes = passes, runs[run].start = nverts, runs[run].tex = tex;
    }
    last_tex = tex, last_pass = passes;
    for (int i = 0; i < l->n; i++) {
        Entry *e = &l->e[i];
        if (!e->obj->mask) continue;
        const float *m = wm(e->obj);
        float la[3], lb[3], d[3], dd[3];
        for (int q = 0; q < 3; q++) la[q] = sv2 * e->axes[6 + q], lb[q] = su * e->axes[9 + q];
        const float *L = &e->axes[0], *N = &e->axes[3];
        for (int q = 0; q < 3; q++) dd[q] = at[q] - m[12 + q];
        into(d, dd, m);
        float nl = N[0] * L[0] + N[1] * L[1] + N[2] * L[2];
        if (!(fabsf(nl) > 0.01f)) continue;
        nl = -1.0f / nl;
        float s[3] = {N[0] * nl, N[1] * nl, N[2] * nl};
        for (int v = 0; v < e->nsel; v++) {
            const float *p = &e->sel[3 * v];
            float qx = p[0] - d[0], qy = p[1] - d[1], qz = p[2] - d[2];
            float tq = qx * s[0] + qy * s[1] + qz * s[2];
            float rx = qx + tq * L[0], ry = qy + tq * L[1], rz = qz + tq * L[2];
            uint8_t *o = e->out + (size_t)v * C3D_VERTEX;
            fput(o + 0x20, (rx * lb[0] + ry * lb[1] + rz * lb[2] - kx * xmin * (umax - umin)) + umin);
            fput(o + 0x24, (vy * (vmax - vmin) - (rx * la[0] + ry * la[1] + rz * la[2])) + vmin);
        }
        for (int t = 0; t < e->ntri; t++) {
            const uint8_t *a = e->out + (size_t)e->tri[3 * t] * C3D_VERTEX, *b = e->out + (size_t)e->tri[3 * t + 1] * C3D_VERTEX,
                          *c = e->out + (size_t)e->tri[3 * t + 2] * C3D_VERTEX;
            float au = fget(a + 0x20), bu = fget(b + 0x20), cu = fget(c + 0x20);
            float av = fget(a + 0x24), bv = fget(b + 0x24), cv = fget(c + 0x24);
            if (!((au <= umax || bu <= umax || cu <= umax) && (umin <= au || umin <= bu || umin <= cu) &&
                  (av <= vmax || bv <= vmax || cv <= vmax) && (vmin <= av || vmin <= bv || vmin <= cv)))
                continue;
            if (!vbp || nverts + 3 > VB_VERTS) continue;   /* (the original writes past the buffer) */
            memcpy(vbp + (size_t)nverts * C3D_VERTEX, a, C3D_VERTEX);
            memcpy(vbp + (size_t)(nverts + 1) * C3D_VERTEX, b, C3D_VERTEX);
            memcpy(vbp + (size_t)(nverts + 2) * C3D_VERTEX, c, C3D_VERTEX);
            nverts += 3;
        }
    }
}

void CarShade_Shade(const Obj *o, int32_t strength, uint8_t passes)
{
    if (!o || !o->mask || !g_tre_have || !nitems) return;
    Listed *l = find(o);
    if (!l) return;
    l->used = true;
    for (int i = 0; i < nitems; i++) {
        prepare(l, strength);
        project(items[i], l, passes);
    }
}

void CarShade_Draw(uint32_t pass)
{
    if (!nverts || !vb) return;
    Render_ZWrite(0);
    Render_SetCull(1);
    Tex *bound = NULL;
    for (int i = 0; i < nruns && i < RUNS; i++) {
        const Run *r = &runs[i];
        if (!r->count || !(r->passes & 1u << (pass & 31))) continue;
        Tex *t = r->tex < TEX_MAX ? g_tex[r->tex] : NULL;
        if (!t) continue;
        if (t != bound) {
            uint32_t keep = t->blend;
            Tex_SetBlend(t, 10);
            Tex_Bind(0, t);
            Tex_SetBlend(t, keep);
            bound = t;
        }
        D3d_DrawPrimitiveVB(D3DPT_TRIANGLELIST, vb, r->start, r->count, 0);
    }
    Tex_Bind(0, NULL);
    Render_ZWrite(1);
    Render_SetCull(Render_DefaultCull());
    Tex_ForgetBlend();
    Render_ForgetBlend();
}
