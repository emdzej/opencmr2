/* The world-space triangle queues (see polyq.h). */
#include "render/polyq.h"
#include "render/d3d7.h"
#include "render/rstate.h"
#include "render/twod.h"
#include <string.h>

enum { POLY_VERTS = 0x1800 };

typedef struct {
    float v[3][12];   /* +0x00: three FVF 0x2d2 vertices (x y z, normal, diffuse, specular, u v, u2 v2) */
    Tex *tex;         /* +0x90 */
    uint32_t flags;   /* +0x94 */
} Poly;               /* 0x98 bytes */

/* The buckets' sizes: 0x400, 0x200, 0x800 and, as the original's limit test reads, 1. */
static Poly q8[0x400], q10[0x200], q20[0x800], q40[1];   /* 0x7dd168, 0x803168, 0x730fd0, 0x7acfd0 */
static uint32_t n8, n10, n20, n40;                        /* DAT_0081616c-DAT_00816178 */
static uint8_t full_warned, full_warned_raw;              /* DAT_0081618c, DAT_0081618d (never read) */
static D3dVB *vb;                                         /* PTR_DAT_00520b78 + 0x338 */

void Poly_Init(void)
{
    n8 = n10 = 0;
}

void Poly_CreateVB(void)
{
    if (!vb) vb = D3dVB_Create(FVF_MODEL, POLY_VERTS);
}

void Poly_FreeVB(void)
{
    D3dVB_Free(vb);
    vb = NULL;
}

/* The record for the flags' bucket, NULL when it is full (the flag noted). Without a bucket bit the
   original writes through the flags value as an address; the port drops the triangle. */
static Poly *slot(uint32_t flags, uint8_t *warned)
{
    Poly *p = NULL;
    if (flags & 8) { if (n8 < 0x400) p = &q8[n8++]; }
    else if (flags & 0x10) { if (n10 < 0x200) p = &q10[n10++]; }
    else if (flags & 0x20) { if (n20 < 0x800) p = &q20[n20++]; }
    else if (flags & 0x40) { if (n40 < 1) p = &q40[n40++]; }
    else return NULL;
    if (!p) *warned = 1;
    return p;
}

static float fx(int32_t v) { return (float)((double)v * (1.0 / 65536.0)); }

void Poly_Add(const PolyVert *a, const PolyVert *b, const PolyVert *c, Tex *tex, uint32_t flags)
{
    Poly *p = slot(flags, &full_warned);
    if (!p) return;
    p->tex = tex;
    p->flags = flags;
    const PolyVert *s[3] = {a, b, c};
    for (int k = 0; k < 3; k++) {
        float *v = p->v[k];
        v[0] = fx(s[k]->x);
        v[1] = fx(s[k]->y);
        v[2] = fx(s[k]->z);
        uint32_t d = (uint32_t)s[k]->rgba[3] << 24 | (uint32_t)s[k]->rgba[0] << 16 |
                     (uint32_t)s[k]->rgba[1] << 8 | s[k]->rgba[2];
        uint32_t sp = 0xff000000u;
        memcpy(&v[6], &d, 4);
        memcpy(&v[7], &sp, 4);
        v[8] = fx(s[k]->u);
        v[9] = fx(s[k]->v);
    }
}

void Poly_AddRaw(const void *verts, Tex *tex, uint32_t flags)
{
    Poly *p = slot(flags, &full_warned_raw);
    if (!p) return;
    p->flags = flags;
    p->tex = tex;
    memcpy(p->v, verts, sizeof p->v);
}

static void set_state(const Poly *p)
{
    if (p->tex) Tex_SetBlend(p->tex, p->tex->blend);
    Tex_Bind(0, p->tex);
}

static void set_flags(uint32_t f)
{
    Render_ZEnable(!(f & POLY_NOZTEST));
    Render_ZWrite(!(f & POLY_NOZWRITE));
    Render_SetCull(f & POLY_NOCULL ? 1 : Render_DefaultCull());
}

/* The buffer is locked at the start and the runs written from its start; a change of texture or flags
   unlocks, draws the run so far, locks again (without discarding) and sets the new state. The state
   starts as texture "1" and flags -1, so the first triangle always sets it. The last run is drawn with
   the last triangle's state set again. */
void Poly_Flush(uint32_t bucket)
{
    Poly *q;
    uint32_t n;
    if (bucket & 8) { q = q8; n = n8; n8 = 0; }
    else if (bucket & 0x10) { q = q10; n = n10; n10 = 0; }
    else if (bucket & 0x20) { q = q20; n = n20; n20 = 0; }
    else { q = q40; n = n40; n40 = 0; }
    if (vb) {
        float *dst = D3dVB_Lock(vb);
        uint32_t nv = 0;
        const Tex *tex = (const Tex *)(uintptr_t)1;
        uint32_t flags = 0xffffffffu;
        for (uint32_t i = 0; i < n; i++) {
            const Poly *p = &q[i];
            if (p->tex != tex || p->flags != flags) {
                D3dVB_Unlock(vb);
                if (nv) {
                    D3d_DrawPrimitiveVB(D3DPT_TRIANGLELIST, vb, 0, nv, 0);
                    nv = 0;
                }
                dst = D3dVB_Lock(vb);
                if (p->tex != tex) set_state(p);
                if (p->flags != flags) set_flags(p->flags);
                tex = p->tex;
                flags = p->flags;
            }
            memcpy(dst + (size_t)nv * 12, p->v, sizeof p->v);
            nv += 3;
            Twod_CountPrims(1);
        }
        D3dVB_Unlock(vb);
        if (nv) {
            set_state(&q[n - 1]);
            set_flags(q[n - 1].flags);
            D3d_DrawPrimitiveVB(D3DPT_TRIANGLELIST, vb, 0, nv, 0);
        }
    }
    Tex_Bind(0, NULL);
    Render_ZEnable(1);
    Render_ZWrite(1);
    Render_SetCull(Render_DefaultCull());
    Tex_ForgetBlend();
    Render_ForgetBlend();
}
