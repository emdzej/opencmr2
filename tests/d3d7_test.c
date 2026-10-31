/* The Direct3D 7 device's recorded state (no GPU): Direct3D 7's defaults, the game's start-up states
   (Render_InitStates 0x49df90) and its cached setters, vertex buffer layouts, draw counting. */
#include "exe.h"
#include "render/d3d7.h"
#include "font.h"
#include "world/obj.h"
#include "render/mesh.h"
#include "render/rstate.h"
#include "render/tex.h"
#include "render/twod.h"
#include "render/polyq.h"
#include "world/frame.h"
#include "crt.h"
#include "fixmath.h"
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

        /* its objects under a stand moved 10 along x: the wheels' world = their local + 10 */
        Obj stand = {0};
        stand.kind = 3;
        stand.mask = 0xff;
        Mat_Identity(&stand.world);
        stand.world.m[12] = 10 << 16;
        stand.dirty = 1;
        Obj *car = C3d_Objects(&m, &stand);
        CHECK(car && stand.child == car && car->parent == &stand && g_nobjs == m.nobj);
        Obj_Frame(&stand, NULL);
        Obj *wheel = car->child;   /* the first part under the root: a wheel */
        CHECK(wheel && wheel->world.m[12] == wheel->local.m[12] + (10 << 16) && wheel->dirty == 0);
        CHECK(wheel->worldf.m[3][0] == (float)wheel->world.m[12] / 65536.0f);
        Obj cam = {0};
        cam.kind = 3;
        Mat_Identity(&cam.world);
        cam.world.m[14] = -(5 << 16);
        cam.dirty = 1;
        View_Set(&cam);
        CHECK(D3d_State()->view.m[3][2] == 5.0f);   /* the inverse moves the world 5 forward */
        D3dStats bo = D3d_Stats();
        Obj_Draw(car, 0);
        CHECK(D3d_Stats().prims - bo.prims == m.npoly);
        Obj_FreeAll();
        Tex_FreeAll();
        VB_FreePool();
        Bfl_Unmount(&arch);
    }
    free(f);
    /* the 2D queues: a sprite without a texture shifts the later ones onto the next vertices */
    Math_InitTables();
    Sprite_Init();
    Tex t1 = {0}, t2 = {0};
    t1.width = t1.height = t2.width = t2.height = 64;
    Rect16 src = {0, 0, 64, 64}, d1 = {10, 10, 20, 20}, d2 = {100, 100, 30, 30};
    uint8_t white[4] = {255, 255, 255, 255};
    Sprite_Add(&src, &d1, NULL, 1, 0, NULL, NULL, white, 0);
    Sprite_Add(&src, &d2, &t2, 1, 0, NULL, NULL, white, 0);
    D3dStats b2 = D3d_Stats();
    Sprite_Flush(1);
    D3dStats a2 = D3d_Stats();
    CHECK(a2.draws - b2.draws == 2);   /* both records are drawn */
    for (int i = 0; i < 1100; i++) Sprite_Add(&src, &d1, &t1, 2, 0, NULL, NULL, white, 0);
    b2 = D3d_Stats();
    Sprite_Flush(2);
    CHECK(D3d_Stats().draws - b2.draws == 1023);   /* the layer's limit */
    Tri_Init();
    int32_t p0[2] = {0, 0}, p1[2] = {10 << 16, 0}, p2[2] = {0, 10 << 16};
    for (int i = 0; i < 1100; i++) Tri_Add(p0, p1, p2, white, 7);   /* any other layer: the first queue */
    b2 = D3d_Stats();
    Tri_Flush(1);
    CHECK(D3d_Stats().prims - b2.prims == 1024);
    /* the triangle queues: a run per texture and flags; the 0x40 bucket holds one triangle */
    PolyVert pv[3] = {{0, 0, 0, {255, 0, 0, 255}, 0, 0}, {1 << 16, 0, 0, {0}, 0, 0}, {0, 1 << 16, 0, {0}, 0, 0}};
    Poly_CreateVB();
    for (int i = 0; i < 5; i++) Poly_Add(&pv[0], &pv[1], &pv[2], i < 3 ? &t1 : &t2, 0x20);
    Poly_Add(&pv[0], &pv[1], &pv[2], &t2, 0x20 | POLY_NOZTEST);
    b2 = D3d_Stats();
    Poly_Flush(0x20);
    CHECK(D3d_Stats().draws - b2.draws == 3 && D3d_Stats().prims - b2.prims == 6);
    for (int i = 0; i < 3; i++) Poly_Add(&pv[0], &pv[1], &pv[2], NULL, 0x40);
    b2 = D3d_Stats();
    Poly_Flush(0x40);
    CHECK(D3d_Stats().prims - b2.prims == 1);
    Poly_Add(&pv[0], &pv[1], &pv[2], NULL, 0);   /* no bucket: dropped */
    Poly_FreeVB();
    /* the sorted pass compare: farther first, type 0x14 always after, type 5 after 0 within a unit */
    Obj oa = {0}, ob = {0};
    Obj *pa = &oa, *pb = &ob;
    oa.dist = 10 << 16, ob.dist = 5 << 16;
    CHECK(Obj_CompareFar(&pa, &pb) == -1 && Obj_CompareFar(&pb, &pa) == 1);
    oa.dist = ob.dist = 5 << 16, oa.type = 5;
    CHECK(Obj_CompareFar(&pa, &pb) == 1 && Obj_CompareFar(&pb, &pa) == -1);
    ob.type = 0x14;
    CHECK(Obj_CompareFar(&pa, &pb) == 1 && Obj_CompareFar(&pb, &pa) == 1);
    /* Font_Print draws three numbers from rand() even with no font */
    crt_srand(400);
    int r1 = crt_rand(), r2 = crt_rand(), r3 = crt_rand(), r4 = crt_rand();
    crt_srand(400);
    Font_InitSlots(1);
    Font_Print(0, "x", 0, 0, white, 0);
    CHECK(crt_rand() == r4 && r1 != r2 && r2 != r3);
    Font_FreeSlots();
    printf("d3d7_test: %s\n", fails ? "FAILED" : "ok");
    return fails != 0;
}
