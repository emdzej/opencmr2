/* The Direct3D 7 device's recorded state (no GPU): Direct3D 7's defaults, the game's start-up states
   (Render_InitStates 0x49df90) and its cached setters, vertex buffer layouts, draw counting. */
#include "exe.h"
#include "render/d3d7.h"
#include "render/mesh.h"
#include "render/rstate.h"
#include "render/tex.h"
#include "bfl.h"
#include "c3d.h"
#include <stdlib.h>
#include "vfs.h"
#include "vfs_host.h"
#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

int main(void)
{
    char err[200];
    bool have_exe = vfs_mount_default() && exe_init(err, sizeof err);
    CHECK(D3d_FvfStride(FVF_MODEL) == 0x30);   /* the PP_F vertex */
    CHECK(D3d_FvfStride(FVF_TL) == 32);
    CHECK(D3d_Open(640, 480, 16));
    const D3dState *s = D3d_State();
    CHECK(s->rs[D3DRS_CULLMODE] == 3 && s->rs[D3DRS_LIGHTING] == 1 && s->tss[0][D3DTSS_COLOROP] == D3DTOP_MODULATE);
    CHECK(s->tss[1][D3DTSS_COLOROP] == D3DTOP_DISABLE && s->viewport.width == 640 && s->viewport.maxz == 1.0f);

    Render_Reset();
    Render_InitStates();
    CHECK(s->rs[D3DRS_ALPHATESTENABLE] == 1 && s->rs[D3DRS_ALPHAFUNC] == 7 && s->rs[D3DRS_ALPHAREF] == 1);
    CHECK(s->rs[D3DRS_EMISSIVEMATERIALSOURCE] == 1 && s->rs[D3DRS_DIFFUSEMATERIALSOURCE] == 0);
    CHECK(s->tss[0][D3DTSS_ADDRESSU] == 3 && s->tss[2][D3DTSS_MAGFILTER] == 2);
    if (have_exe) CHECK(Render_DefaultCull() == 3 && s->rs[D3DRS_CULLMODE] == 3);

    /* the cached setters skip repeats: a state changed behind their back stays */
    Render_ZWrite(0);
    D3d_SetRenderState(D3DRS_ZWRITEENABLE, 1);
    Render_ZWrite(0);
    CHECK(s->rs[D3DRS_ZWRITEENABLE] == 1);
    /* Render_InitStates turns blending on directly while the setter's cache says off: the first
       Render_AlphaBlend(0) does nothing (the original's behaviour) */
    Render_AlphaBlend(0);
    CHECK(s->rs[D3DRS_ALPHABLENDENABLE] == 1 && s->rs[D3DRS_ALPHAREF] == 1);
    Render_AlphaBlend(1);
    Render_AlphaBlend(0);
    CHECK(s->rs[D3DRS_ALPHABLENDENABLE] == 0 && s->rs[D3DRS_ALPHAREF] == 0x80);
    Render_ClampStage0(0);
    CHECK(s->tss[0][D3DTSS_ADDRESSV] == 1);

    D3dVB *vb = D3dVB_Create(FVF_MODEL, 10);
    CHECK(vb && vb->stride == 0x30 && D3dVB_Lock(vb));
    uint16_t idx[3] = {0, 1, 2};
    D3d_DrawIndexedPrimitiveVB(D3DPT_TRIANGLELIST, vb, 0, 10, idx, 3, 0);
    D3d_DrawIndexedPrimitiveVB(D3DPT_TRIANGLELIST, vb, 8, 3, idx, 3, 0);   /* past the end: refused */
    D3d_DrawPrimitive(D3DPT_TRIANGLESTRIP, FVF_TL, vb->data, 4, 0);
    D3dStats st = D3d_Stats();
    CHECK(st.draws == 2 && st.prims == 3);
    D3dVB_Free(vb);

    /* a car through the ported path: its texture records, vertex buffers, batches and draws */
    BflSlot arch = {0};
    uint8_t *f = have_exe ? File_Load("Game\\Cars\\205c1.c3d", 1) : NULL;
    if (f && Bfl_Mount(&arch, "Game\\Cars\\205c1.bfl")) {
        C3d m;
        int base = g_tex_count;
        CHECK(C3d_Load(&m, f, File_LastSize(), base));
        VB_CreatePool();
        C3d_Textures(m.data + m.slots, m.data + m.paths, m.ntex, m.kind, &arch, "");
        CHECK(g_tex_count - base == m.ntex);
        int surfaces = 0;
        for (int i = base; i < g_tex_count; i++) surfaces += g_tex[i] && g_tex[i]->surface;
        CHECK(surfaces == m.ntex);
        CHECK(g_tex[base]->flags & 0x1000);   /* from memory */
        C3d_Build(&m);
        CHECK(g_vb_fill[0] == m.nvert);       /* every vertex in the first buffer (1,004 < 2,000) */
        uint32_t batches = 0;
        for (int i = 0; i < m.nmesh1 + m.nmesh2; i++) {
            const uint8_t *me = C3d_Mesh(&m, i);
            batches += (uint32_t)(me[0x100] | me[0x101] << 8);
        }
        D3dStats before = D3d_Stats();
        for (int i = 0; i < m.nmesh1 + m.nmesh2; i++) Mesh_Draw(&m, (uint8_t *)C3d_Mesh(&m, i), 1, 0, 0);
        D3dStats after = D3d_Stats();
        CHECK(after.draws - before.draws == batches && after.prims - before.prims == m.npoly);
        printf("205c1: %u textures, %u batches, %u triangles drawn\n", m.ntex, batches, after.prims - before.prims);
        Tex_FreeAll();
        VB_FreePool();
        Bfl_Unmount(&arch);
    }
    free(f);
    printf("d3d7_test: %s\n", fails ? "FAILED" : "ok");
    return fails != 0;
}
