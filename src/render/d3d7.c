/* The Direct3D 7 device's state (see d3d7.h). Defaults are Direct3D 7's for a device with a z-buffer. */
#include "render/d3d7.h"
#include <stdlib.h>
#include <string.h>

static D3dState st;
static const D3dBackend *be;
static D3dStats stats;

void D3d_SetBackend(const D3dBackend *b) { be = b; }
const D3dState *D3d_State(void) { return &st; }
D3dStats D3d_Stats(void) { return stats; }

uint32_t D3d_FvfStride(uint32_t fvf)
{
    uint32_t n = 0;
    if (fvf & D3DFVF_XYZRHW) n += 16;
    else if (fvf & D3DFVF_XYZ) n += 12;
    if (fvf & D3DFVF_NORMAL) n += 12;
    if (fvf & D3DFVF_DIFFUSE) n += 4;
    if (fvf & D3DFVF_SPECULAR) n += 4;
    n += 8 * (fvf >> 8 & 15);
    return n;
}

static void identity(D3DMATRIX *m)
{
    memset(m, 0, sizeof *m);
    for (int i = 0; i < 4; i++) m->m[i][i] = 1.0f;
}

static uint32_t f2u(float f)
{
    uint32_t u;
    memcpy(&u, &f, 4);
    return u;
}

static void defaults(void)
{
    memset(st.rs, 0, sizeof st.rs);
    memset(st.tss, 0, sizeof st.tss);
    st.rs[D3DRS_TEXTUREPERSPECTIVE] = 1;
    st.rs[D3DRS_ZENABLE] = 1;
    st.rs[D3DRS_FILLMODE] = 3;            /* solid */
    st.rs[D3DRS_SHADEMODE] = 2;           /* Gouraud */
    st.rs[D3DRS_ZWRITEENABLE] = 1;
    st.rs[D3DRS_SRCBLEND] = 2;            /* one */
    st.rs[D3DRS_DESTBLEND] = 1;           /* zero */
    st.rs[D3DRS_CULLMODE] = 3;            /* counter-clockwise */
    st.rs[D3DRS_ZFUNC] = 4;               /* less or equal */
    st.rs[D3DRS_ALPHAFUNC] = 8;           /* always */
    st.rs[D3DRS_FOGEND] = f2u(1.0f);
    st.rs[D3DRS_FOGDENSITY] = f2u(1.0f);
    st.rs[D3DRS_STENCILFAIL] = st.rs[D3DRS_STENCILZFAIL] = st.rs[D3DRS_STENCILPASS] = 1;
    st.rs[D3DRS_STENCILFUNC] = 8;
    st.rs[D3DRS_STENCILMASK] = st.rs[D3DRS_STENCILWRITEMASK] = 0xffffffffu;
    st.rs[D3DRS_TEXTUREFACTOR] = 0xffffffffu;
    st.rs[D3DRS_CLIPPING] = 1;
    st.rs[D3DRS_LIGHTING] = 1;
    st.rs[D3DRS_COLORVERTEX] = 1;
    st.rs[D3DRS_LOCALVIEWER] = 1;
    st.rs[D3DRS_DIFFUSEMATERIALSOURCE] = 1;    /* colour 1 */
    st.rs[D3DRS_SPECULARMATERIALSOURCE] = 2;   /* colour 2 */
    for (int s = 0; s < D3D_STAGES; s++) {
        st.tss[s][D3DTSS_COLOROP] = s == 0 ? D3DTOP_MODULATE : D3DTOP_DISABLE;
        st.tss[s][D3DTSS_COLORARG1] = D3DTA_TEXTURE;
        st.tss[s][D3DTSS_COLORARG2] = D3DTA_CURRENT;
        st.tss[s][D3DTSS_ALPHAOP] = s == 0 ? D3DTOP_SELECTARG1 : D3DTOP_DISABLE;
        st.tss[s][D3DTSS_ALPHAARG1] = D3DTA_TEXTURE;
        st.tss[s][D3DTSS_ALPHAARG2] = D3DTA_CURRENT;
        st.tss[s][D3DTSS_TEXCOORDINDEX] = (uint32_t)s;
        st.tss[s][D3DTSS_ADDRESS] = st.tss[s][D3DTSS_ADDRESSU] = st.tss[s][D3DTSS_ADDRESSV] = 1;   /* wrap */
        st.tss[s][D3DTSS_MAGFILTER] = st.tss[s][D3DTSS_MINFILTER] = 1;   /* point */
        st.tss[s][D3DTSS_MIPFILTER] = 1;                                 /* none */
        st.tex[s] = NULL;
        identity(&st.texm[s]);
    }
    identity(&st.world);
    identity(&st.view);
    identity(&st.proj);
    memset(st.light, 0, sizeof st.light);
    memset(st.light_on, 0, sizeof st.light_on);
    memset(&st.material, 0, sizeof st.material);
    st.viewport = (D3DVIEWPORT7){0, 0, (uint32_t)st.width, (uint32_t)st.height, 0.0f, 1.0f};
}

void D3d_ScreenSize(int *width, int *height) { *width = st.width, *height = st.height; }

bool D3d_Open(int width, int height, int bpp)
{
    st.width = width;
    st.height = height;
    st.bpp = bpp;
    st.in_scene = false;
    memset(&stats, 0, sizeof stats);
    defaults();
    return !be || be->open(width, height, bpp);
}

void D3d_Close(void) { st.in_scene = false; }

void D3d_BeginScene(void)
{
    st.in_scene = true;
    stats.scenes++;
}

void D3d_EndScene(void) { st.in_scene = false; }

/* Each rectangle (or the viewport when there are none) is cleared, clipped to the viewport. */
void D3d_Clear(uint32_t n, const int32_t *rects, uint32_t flags, uint32_t colour, float z, uint32_t stencil)
{
    stats.clears++;
    if (!be) return;
    if (!n || !rects) {
        const D3DVIEWPORT7 *v = &st.viewport;
        be->clear(&st, (int)v->x, (int)v->y, (int)(v->x + v->width), (int)(v->y + v->height), flags, colour, z,
                  stencil);
        return;
    }
    for (uint32_t i = 0; i < n; i++)
        be->clear(&st, rects[4 * i], rects[4 * i + 1], rects[4 * i + 2], rects[4 * i + 3], flags, colour, z, stencil);
}

static D3DMATRIX *xform(uint32_t type)
{
    if (type == D3DTS_WORLD) return &st.world;
    if (type == D3DTS_VIEW) return &st.view;
    if (type == D3DTS_PROJECTION) return &st.proj;
    if (type >= D3DTS_TEXTURE0 && type <= D3DTS_TEXTURE7) return &st.texm[type - D3DTS_TEXTURE0];
    return NULL;
}

void D3d_SetTransform(uint32_t type, const D3DMATRIX *m)
{
    D3DMATRIX *d = xform(type);
    if (d && m) *d = *m;
}

void D3d_GetTransform(uint32_t type, D3DMATRIX *m)
{
    D3DMATRIX *d = xform(type);
    if (d && m) *m = *d;
}

void D3d_SetViewport(const D3DVIEWPORT7 *v)
{
    if (v) st.viewport = *v;
}

void D3d_SetMaterial(const D3DMATERIAL7 *m)
{
    if (m) st.material = *m;
}

void D3d_SetLight(uint32_t i, const D3DLIGHT7 *l)
{
    if (i < D3D_LIGHTS && l) st.light[i] = *l;
}

void D3d_LightEnable(uint32_t i, bool on)
{
    if (i < D3D_LIGHTS) st.light_on[i] = on;
}

void D3d_SetRenderState(uint32_t rs, uint32_t v)
{
    if (rs < D3DRS_COUNT) st.rs[rs] = v;
}

uint32_t D3d_GetRenderState(uint32_t rs) { return rs < D3DRS_COUNT ? st.rs[rs] : 0; }

void D3d_SetTexture(uint32_t stage, D3dTexture *t)
{
    if (stage < D3D_STAGES) st.tex[stage] = t;
}

void D3d_SetTextureStageState(uint32_t stage, uint32_t tss, uint32_t v)
{
    if (stage >= D3D_STAGES || tss >= D3DTSS_COUNT) return;
    st.tss[stage][tss] = v;
    if (tss == D3DTSS_ADDRESS) st.tss[stage][D3DTSS_ADDRESSU] = st.tss[stage][D3DTSS_ADDRESSV] = v;
}

static uint32_t prims(uint32_t prim, uint32_t n)
{
    switch (prim) {
    case D3DPT_POINTLIST: return n;
    case D3DPT_LINELIST: return n / 2;
    case D3DPT_LINESTRIP: return n ? n - 1 : 0;
    case D3DPT_TRIANGLELIST: return n / 3;
    default: return n >= 2 ? n - 2 : 0;
    }
}

static void draw(uint32_t prim, uint32_t fvf, const void *v, uint32_t nv, const uint16_t *idx, uint32_t ni,
                 D3dVB *vb, uint32_t first)
{
    if (prim < D3DPT_POINTLIST || prim > D3DPT_TRIANGLEFAN || !nv) return;
    stats.draws++;
    stats.prims += prims(prim, idx ? ni : nv);
    if (be) be->draw(&st, (int)prim, fvf, v, nv, idx, ni, vb, first);
}

void D3d_DrawPrimitive(uint32_t prim, uint32_t fvf, const void *verts, uint32_t n, uint32_t flags)
{
    draw(prim, fvf, verts, n, NULL, 0, NULL, 0);
}

void D3d_DrawIndexedPrimitive(uint32_t prim, uint32_t fvf, const void *verts, uint32_t nverts,
                              const uint16_t *indices, uint32_t nindices, uint32_t flags)
{
    draw(prim, fvf, verts, nverts, indices, nindices, NULL, 0);
}

void D3d_DrawPrimitiveVB(uint32_t prim, D3dVB *vb, uint32_t first, uint32_t n, uint32_t flags)
{
    if (vb && first + n <= vb->count) draw(prim, vb->fvf, vb->data, n, NULL, 0, vb, first);
}

/* The indices are relative to first (the start vertex), as in Direct3D 7. */
void D3d_DrawIndexedPrimitiveVB(uint32_t prim, D3dVB *vb, uint32_t first, uint32_t nverts,
                                const uint16_t *indices, uint32_t nindices, uint32_t flags)
{
    if (vb && first + nverts <= vb->count) draw(prim, vb->fvf, vb->data, nverts, indices, nindices, vb, first);
}

D3dVB *D3dVB_Create(uint32_t fvf, uint32_t count)
{
    D3dVB *vb = calloc(1, sizeof *vb);
    if (!vb) return NULL;
    vb->fvf = fvf;
    vb->stride = D3d_FvfStride(fvf);
    vb->count = count;
    vb->data = calloc(count ? count : 1, vb->stride);
    if (!vb->data) {
        free(vb);
        return NULL;
    }
    vb->dirty = true;
    vb->lo = 0, vb->hi = count;
    return vb;
}

void *D3dVB_Lock(D3dVB *vb) { return vb ? vb->data : NULL; }

static void mark(D3dVB *vb, uint32_t lo, uint32_t hi)
{
    if (!vb->dirty) vb->lo = lo, vb->hi = hi;
    else {
        if (lo < vb->lo) vb->lo = lo;
        if (hi > vb->hi) vb->hi = hi;
    }
    vb->dirty = true;
}

void D3dVB_Unlock(D3dVB *vb)
{
    if (vb) mark(vb, 0, vb->count);
}

void D3dVB_UnlockRange(D3dVB *vb, uint32_t first, uint32_t n)
{
    if (!vb) return;
    if (first > vb->count) first = vb->count;
    if (n > vb->count - first) n = vb->count - first;
    mark(vb, first, first + n);
}

void D3dVB_Free(D3dVB *vb)
{
    if (!vb) return;
    if (be && be->vb_free) be->vb_free(vb);
    free(vb->data);
    free(vb);
}

D3dTexture *D3dTex_Create(int width, int height, uint8_t *rgba)
{
    D3dTexture *t = calloc(1, sizeof *t);
    if (!t) return NULL;
    t->width = width;
    t->height = height;
    t->rgba = rgba;
    t->dirty = true;
    return t;
}

D3dTexture *D3dTex_CreateCube(int size)
{
    D3dTexture *t = D3dTex_Create(size, size, NULL);
    if (t) t->cube = true;
    return t;
}

void D3d_SetRenderTarget(D3dTexture *cube, int face)
{
    st.target = cube && cube->cube ? cube : NULL;
    st.target_face = st.target ? face % 6 : 0;
}

void D3dTex_Free(D3dTexture *t)
{
    if (!t) return;
    if (st.target == t) st.target = NULL;
    for (int s = 0; s < D3D_STAGES; s++)
        if (st.tex[s] == t) st.tex[s] = NULL;
    if (be && be->texture_free) be->texture_free(t);
    free(t->rgba);
    free(t);
}

void D3d_ReadBack(int x, int y, int w, int h, uint8_t *rgba)
{
    if (be && be->read) be->read(x, y, w, h, rgba);
    else memset(rgba, 0, (size_t)w * (size_t)h * 4);
}

void D3d_Present(void)
{
    if (be) be->present();
}
