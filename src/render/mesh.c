/* Model meshes on the device (see mesh.h). */
#include "render/mesh.h"
#include "render/polyq.h"
#include "exe.h"
#include "platform.h"
#include "render/light.h"
#include "render/rstate.h"
#include "render/tex.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

D3dVB *g_vb[VB_POOL];
uint32_t g_vb_fill[VB_POOL];
uint32_t g_polys_drawn;

static Batch *pool;          /* the batches (the original allocates each on its own) */
static uint32_t npool, cap;
static uint16_t *tris;       /* DAT_0059be74: the polygon-by-polygon index buffer */
static uint32_t tris_cap;

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static void wr32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8), p[2] = (uint8_t)(v >> 16), p[3] = (uint8_t)(v >> 24); }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

void VB_CreatePool(void)
{
    for (int i = 0; i < VB_POOL; i++) {
        if (!g_vb[i]) g_vb[i] = D3dVB_Create(FVF_MODEL, VB_VERTS);
        g_vb_fill[i] = 0;
    }
    Poly_CreateVB();
}

void VB_FreePool(void)
{
    for (int i = 0; i < VB_POOL; i++) {
        D3dVB_Free(g_vb[i]);
        g_vb[i] = NULL;
        g_vb_fill[i] = 0;
    }
    Poly_FreeVB();
    for (uint32_t i = 0; i < npool; i++) free(pool[i].idx);
    free(pool);
    pool = NULL;
    npool = cap = 0;
}

/* Into the first buffer with room (fill + count below 2,000); none: the message only, the part keeps
   its buffer fields. */
/* 0x4b2020: the part's vertices copied into its buffer again (lock flags 0x821) */
void Mesh_UploadVerts(const C3d *m, uint8_t *part)
{
    uint32_t b = rd32(part + 0x14), n = rd32(part + 0x10);
    const uint8_t *v = C3d_Ref(m, part + 0x0c, m->verts);
    if (b >= VB_POOL || !g_vb[b] || !v) return;
    uint8_t *dst = D3dVB_Lock(g_vb[b]);
    memcpy(dst + (size_t)rd32(part + 0x18) * C3D_VERTEX, v, (size_t)n * C3D_VERTEX);
    D3dVB_Unlock(g_vb[b]);
}

/* 0x4b2090: every vertex's colour (+0x18), then the upload */
void Mesh_SetColour(const C3d *m, uint8_t *part, uint32_t argb)
{
    uint8_t *v = (uint8_t *)C3d_Ref(m, part + 0x0c, m->verts);
    uint32_t n = rd32(part + 0x10);
    for (uint32_t k = 0; v && k < n; k++) wr32(v + (size_t)k * C3D_VERTEX + 0x18, argb);
    Mesh_UploadVerts(m, part);
}

void Mesh_RestoreColours(const C3d *m, uint8_t *part)
{
    uint8_t *v = (uint8_t *)C3d_Ref(m, part + 0x0c, m->verts);
    const uint8_t *p = C3d_Ref(m, part + 0x24, m->polys);
    uint32_t np = rd32(part + 0x28);
    for (uint32_t i = 0; v && p && i < np; i++) {
        const uint8_t *q = p + (size_t)i * C3D_POLY;
        for (uint32_t k = 0; k < 3; k++) {
            uint32_t c = rd32(q + 0x34 + 4 * k);
            c = (c & 0xff00ff00u) | (c & 0xff) << 16 | (c >> 16 & 0xff);
            wr32(v + (size_t)rd16(q + 0x40 + 2 * k) * C3D_VERTEX + 0x18, c);
        }
    }
    Mesh_UploadVerts(m, part);
}

static void set_frames(const C3d *m, uint8_t *part, uint32_t mask, uint32_t frame, uint32_t at)
{
    uint8_t *p = (uint8_t *)C3d_Ref(m, part + 0x24, m->polys);
    uint32_t np = rd32(part + 0x28);
    for (uint32_t i = 0; p && i < np; i++) {
        uint8_t *q = p + (size_t)i * C3D_POLY;
        uint32_t f = rd16(q);
        if ((f & mask & 0x7f) || (mask >> 7 & 0x7f & f >> 9)) wr32(q + at, frame);
    }
}

void Mesh_SetFrame(const C3d *m, uint8_t *part, uint32_t mask, uint32_t frame)
{
    if (part) set_frames(m, part, mask, frame, 0x2c);
}

/* (the original reads the part without the NULL check) */
void Mesh_SetFrame1(const C3d *m, uint8_t *part, uint32_t mask, uint32_t frame)
{
    if (part) set_frames(m, part, mask, frame, 0x30);
}

void Mesh_AddToVB(const C3d *m, uint8_t *part)
{
    uint32_t n = rd32(part + 0x10);
    const uint8_t *v = C3d_Ref(m, part + 0x0c, m->verts);
    for (int i = 0; i < VB_POOL; i++) {
        if (g_vb_fill[i] + n >= VB_VERTS || !g_vb[i]) continue;
        uint8_t *dst = D3dVB_Lock(g_vb[i]);
        if (v) memcpy(dst + (size_t)g_vb_fill[i] * C3D_VERTEX, v, (size_t)n * C3D_VERTEX);
        D3dVB_Unlock(g_vb[i]);
        wr32(part + 0x14, (uint32_t)i);
        wr32(part + 0x18, g_vb_fill[i]);
        g_vb_fill[i] += n;
        return;
    }
    char msg[96];
    snprintf(msg, sizeof msg, "Mesh_AddToVB: no vertex buffer with room for %u vertices", n);
    plat_log(msg);
}

static Batch *batch(const uint8_t *part, int k) { return &pool[rd32(part + 0x38 + 4 * k)]; }

/* Once per part (count at +0x100 still 0): runs of equal stage-0 texture index in polygon order, each
   with its indices and their range, the indices then made relative to the range's start. The range
   starts from 999, so a run whose indices are all 999 or more keeps a start of 0 (the record is
   zero-filled). The record has room for 50 batches; more would overwrite the count and beyond in the
   original, and the port stops at 50. */
void Mesh_Batches(const C3d *m, uint8_t *part)
{
    if ((int32_t)rd32(part + 0x100) >= 1) return;
    int32_t np = (int32_t)rd32(part + 0x28);
    const uint8_t *p = C3d_Ref(m, part + 0x24, m->polys);
    if (!p) np = 0;
    int nb = 0;
    int32_t last = -99;
    for (int32_t k = 0; k < np; k++) {
        int32_t t = (int32_t)rd32(p + (size_t)k * C3D_POLY + 4);
        if (t != last) nb++, last = t;
    }
    if (nb > MESH_BATCHES) nb = MESH_BATCHES;
    wr32(part + 0x100, (uint32_t)nb);
    if (npool + (uint32_t)nb > cap) {
        cap = (npool + (uint32_t)nb) * 2 + 64;
        pool = realloc(pool, cap * sizeof *pool);
    }
    for (int b = 0; b < nb; b++) {
        memset(&pool[npool], 0, sizeof *pool);
        wr32(part + 0x38 + 4 * b, npool++);
    }
    int b = -1;
    last = -99;
    for (int32_t k = 0; k < np; k++) {
        const uint8_t *q = p + (size_t)k * C3D_POLY;
        int32_t t = (int32_t)rd32(q + 4);
        if (t != last) {
            if (b + 1 >= nb) break;
            Batch *x = batch(part, ++b);
            x->count = 0;
            x->tex = t;
            x->tex1 = (int32_t)rd32(q + 8);
            last = t;
        }
        batch(part, b)->count += 3;
    }
    for (int k = 0; k < nb; k++) {
        Batch *x = batch(part, k);
        x->idx = calloc(x->count ? x->count : 1, sizeof *x->idx);
        x->count = 0;
    }
    b = -1;
    last = -99;
    for (int32_t k = 0; k < np; k++) {
        const uint8_t *q = p + (size_t)k * C3D_POLY;
        int32_t t = (int32_t)rd32(q + 4);
        if (t != last) {
            if (b + 1 >= nb) break;
            ++b;
            last = t;
        }
        Batch *x = batch(part, b);
        for (int j = 0; j < 3; j++) x->idx[x->count++] = rd16(q + 0x40 + 2 * j);
    }
    for (int k = 0; k < nb; k++) {
        Batch *x = batch(part, k);
        uint32_t lo = 999;
        int32_t hi = -1;
        for (uint32_t i = 0; i < x->count; i++) {
            if (x->idx[i] < lo) x->min = lo = x->idx[i];
            if (hi < (int32_t)x->idx[i]) x->max = (uint32_t)(hi = x->idx[i]);
        }
        for (uint32_t i = 0; i < x->count; i++) x->idx[i] = (uint16_t)(x->idx[i] - x->min);
    }
}

void C3d_Build(const C3d *m)
{
    for (int i = 0; i < m->nmesh1 + m->nmesh2; i++) {
        uint8_t *me = (uint8_t *)C3d_Mesh(m, i);
        for (uint32_t k = 0; k < me[0x113]; k++) {
            uint8_t *part = me + k * 0x108;
            Mesh_AddToVB(m, part);
            Mesh_Batches(m, part);
            wr32(part + 0x30, rd32(part + 0x30) & ~0x40000u);
        }
    }
}

static Tex *tex_at(int32_t i) { return i >= 0 && i < TEX_MAX ? g_tex[i] : NULL; }

static D3dVB *part_vb(const uint8_t *part)
{
    uint32_t i = rd32(part + 0x14);
    return i < VB_POOL ? g_vb[i] : NULL;
}

/* FUN_0049c880 / FUN_0049c7b0: batch by batch, each from its range in the part's buffer. */
static void draw_batches(uint8_t *part, bool tex10)
{
    int32_t nb = (int32_t)rd32(part + 0x100);
    for (int32_t k = 0; k < nb && k < MESH_BATCHES; k++) {
        Batch *x = batch(part, k);
        if (tex10) Tex_SetBlend(tex_at(x->tex), 10);
        Tex_Bind(0, tex_at(x->tex));
        D3d_DrawIndexedPrimitiveVB(D3DPT_TRIANGLELIST, part_vb(part), rd32(part + 0x18) + x->min,
                                   rd32(part + 0x10) - x->min, x->idx, x->count, 0);
        g_polys_drawn += x->count / 3;
    }
}

/* FUN_0049c680 / FUN_0049c510: polygon by polygon, each polygon's texture from its frame index (+0x2c
   picks one of the 10 indices), runs drawn over the part's whole range. FUN_0049c510 also sets stage 0
   wrapping for flags (low 7 bits) 0x70, 0x71, 0x73, 0x74 and clamping otherwise, after each polygon:
   a run is drawn with its last polygon's setting. The original's index buffer holds 2,002 indices
   (667 triangles a run) before running into the next variables; the port's grows. */
/* A polygon's current texture: index number +0x2c of its ten. The original reads any value; past the
   record the port takes -1 (no texture). */
static int32_t poly_tex(const uint8_t *q)
{
    uint32_t f = rd32(q + 0x2c);
    return f < (C3D_POLY - 4) / 4 ? (int32_t)rd32(q + 4 + 4 * f) : -1;
}

static void draw_polys(const C3d *m, uint8_t *part, bool clamp_by_flags)
{
    int32_t np = (int32_t)rd32(part + 0x28);
    const uint8_t *p = C3d_Ref(m, part + 0x24, m->polys);
    if (!p || np <= 0) return;
    if ((uint32_t)np * 3 > tris_cap) {
        tris_cap = (uint32_t)np * 3;
        tris = realloc(tris, tris_cap * sizeof *tris);
    }
    int32_t last = -1;
    uint32_t n = 0;
    D3dVB *vb = part_vb(part);
    for (int32_t k = 0; k < np; k++) {
        const uint8_t *q = p + (size_t)k * C3D_POLY;
        int32_t t = poly_tex(q);
        if (t != last) {
            if (n) D3d_DrawIndexedPrimitiveVB(D3DPT_TRIANGLELIST, vb, rd32(part + 0x18), rd32(part + 0x10), tris, n, 0);
            n = 0;
            Tex_Bind(0, tex_at(t));
            last = t;
        }
        for (int j = 0; j < 3; j++) tris[n++] = rd16(q + 0x40 + 2 * j);
        g_polys_drawn++;
        if (clamp_by_flags) {
            uint32_t f = rd16(q) & 0x7f;
            Render_ClampStage0(!(f == 0x70 || f == 0x71 || f == 0x73 || f == 0x74));
        }
    }
    if (n) {
        const uint8_t *q = p + (size_t)(np - 1) * C3D_POLY;
        Tex_Bind(0, tex_at(poly_tex(q)));
        D3d_DrawIndexedPrimitiveVB(D3DPT_TRIANGLELIST, vb, rd32(part + 0x18), rd32(part + 0x10), tris, n, 0);
    }
}

/* Part flags (+0x30): 1 two-sided, 8 blended (when 0x5207b4 is set), 0x20 keeps the lighting mode
   (else mode 2, or 3 with 0x40000), 0x200 reflective (a second pass with option 8: Render_EnvMapped
   0x4b2980 with option 0x80, else 0x4b2610; not ported yet, and off by default), 0x2000 drawn polygon by
   polygon unless asked otherwise. */
void Mesh_Draw(const C3d *m, uint8_t *mesh, uint32_t batched, uint32_t clamp_flags, uint32_t batched_tex10)
{
    uint8_t *part = mesh + (size_t)mesh[0x112] * 0x108;
    if (rd32(part + 0x24) == 0xffffffffu) return;   /* no polygons (a NULL pointer after the fixup) */
    uint32_t f = rd32(part + 0x30);
    if (!batched && !(f & 0x2000)) batched = 1;
    Render_SetCull(f & 1 ? 1 : Render_DefaultCull());
    uint32_t saved = 0;
    if (!(f & 0x20)) {
        saved = Light_Mode();
        Light_SetMode(f & 0x40000 ? 3 : 2);
    }
    Render_AlphaBlend(exe_u32(0x5207b4) ? f >> 3 & 1 : 0);
    if (!batched) draw_polys(m, part, clamp_flags != 0);
    else draw_batches(part, batched_tex10 != 0);
    if (!(f & 0x20)) Light_SetMode(saved);
}
