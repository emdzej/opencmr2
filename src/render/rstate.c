/* The original's render-state layer (see rstate.h). */
#include "render/rstate.h"
#include "exe.h"
#include "render/d3d7.h"

RenderCaps g_caps = {true, true};

static uint8_t clear_rgb[3];      /* DAT_0059ce24 */
static uint32_t cull;             /* DAT_00597cb8 */
static uint32_t default_cull;     /* DAT_005207f8 (initialised data: read from the exe) */
static uint32_t blend;            /* DAT_0059ce30 */
static uint32_t zenable;          /* DAT_0059ce34 */
static uint32_t clamp0;           /* DAT_0059ce38 */
static uint32_t zwrite;           /* DAT_0059ce3c */
static uint32_t tci[8];           /* DAT_0059bd04 */
static bool loaded;

void Render_Reset(void)
{
    cull = blend = zenable = clamp0 = zwrite = 0;
    default_cull = exe_u32(0x5207f8);
    loaded = true;
}

static void once(void)
{
    if (!loaded) Render_Reset();
}

void Render_SetClearColour(uint8_t r, uint8_t g, uint8_t b)
{
    clear_rgb[0] = r;
    clear_rgb[1] = g;
    clear_rgb[2] = b;
}

/* Render_ClearTarget 0x49d960: the whole mode, opaque, in the colour above. */
void Render_ClearTarget(void)
{
    const D3dState *s = D3d_State();
    int32_t r[4] = {0, 0, s->width, s->height};
    D3d_Clear(1, r, D3DCLEAR_TARGET, 0xff000000u | (uint32_t)clear_rgb[0] << 16 | (uint32_t)clear_rgb[1] << 8 | clear_rgb[2],
              1.0f, 0);
}

/* Render_ClearZ 0x49d9d0: the z-buffer (and the stencil when the mode has one), but only on a device
   without DRAWPRIMITIVES2EX. */
void Render_ClearZ(bool stencil)
{
    if (g_caps.dp2ex) return;
    const D3dState *s = D3d_State();
    int32_t r[4] = {0, 0, s->width, s->height};
    D3d_Clear(1, r, stencil ? D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL : D3DCLEAR_ZBUFFER, 0xff000000u, 1.0f, 0);
}

void Render_SetCull(uint32_t mode)
{
    once();
    if (mode != cull) {
        D3d_SetRenderState(D3DRS_CULLMODE, mode);
        cull = mode;
    }
}

void Render_SetDefaultCull(uint32_t mode)
{
    once();
    default_cull = mode;
}

uint32_t Render_DefaultCull(void)
{
    once();
    return default_cull;
}

/* With the caps flag the alpha reference follows the blending: 1 with it, 0x80 without (the alpha test
   then cuts out instead); without the flag blending is switched straight back on. Render_InitStates sets
   ALPHABLENDENABLE directly while this cache starts at 0, so the first call with 0 does nothing and
   blending stays on (as in the original). */
void Render_AlphaBlend(uint32_t on)
{
    once();
    if (on == blend) return;
    D3d_SetRenderState(D3DRS_ALPHABLENDENABLE, on);
    if (g_caps.alpha_ref_switch) D3d_SetRenderState(D3DRS_ALPHAREF, on ? 1 : 0x80);
    else D3d_SetRenderState(D3DRS_ALPHABLENDENABLE, 1);
    blend = on;
}

void Render_ZEnable(uint32_t on)
{
    once();
    if (on != zenable) {
        D3d_SetRenderState(D3DRS_ZENABLE, on);
        zenable = on;
    }
}

/* Stage 0 addressing: clamp (3) when on, wrap (1) when off. */
void Render_ClampStage0(uint32_t on)
{
    once();
    if (on != clamp0) {
        D3d_SetTextureStageState(0, D3DTSS_ADDRESS, on ? 3 : 1);
        clamp0 = on;
    }
}

void Render_ZWrite(uint32_t on)
{
    once();
    if (on != zwrite) {
        D3d_SetRenderState(D3DRS_ZWRITEENABLE, on);
        zwrite = on;
    }
}

/* Not cached: always set, with the texture transform off. */
void Render_TexCoordIndex(uint32_t stage, uint32_t index)
{
    D3d_SetTextureStageState(stage, D3DTSS_TEXCOORDINDEX, index);
    D3d_SetTextureStageState(stage, D3DTSS_TEXTURETRANSFORMFLAGS, 0);
    if (stage < 8) tci[stage] = index;
}

void Render_InitStates(void)
{
    once();
    static const uint32_t rs[][2] = {
        {D3DRS_FILLMODE, 3}, {D3DRS_SHADEMODE, 2}, {D3DRS_ZFUNC, 4}, {D3DRS_ALPHAREF, 1},
        {D3DRS_ALPHATESTENABLE, 1}, {D3DRS_ALPHAFUNC, 7}, {D3DRS_ALPHABLENDENABLE, 1}, {D3DRS_FOGENABLE, 0},
        {D3DRS_DITHERENABLE, 1}, {D3DRS_SPECULARENABLE, 0}, {D3DRS_ANTIALIAS, 0}, {D3DRS_TEXTUREPERSPECTIVE, 1},
        {D3DRS_STIPPLEDALPHA, 0}, {41, 0}, {D3DRS_COLORVERTEX, 1}, {D3DRS_DIFFUSEMATERIALSOURCE, 0},
        {D3DRS_SPECULARMATERIALSOURCE, 0}, {D3DRS_AMBIENTMATERIALSOURCE, 0}, {D3DRS_EMISSIVEMATERIALSOURCE, 1},
        {D3DRS_CLIPPING, 1}, {D3DRS_EXTENTS, 0},
    };
    for (unsigned i = 0; i < sizeof rs / sizeof *rs; i++) D3d_SetRenderState(rs[i][0], rs[i][1]);
    Render_ZEnable(1);
    Render_ZWrite(1);
    Render_SetCull(Render_DefaultCull());
    for (uint32_t s = 0; s < 3; s++) D3d_SetTextureStageState(s, D3DTSS_ADDRESS, 3);
    for (uint32_t s = 0; s < 3; s++) D3d_SetTextureStageState(s, D3DTSS_MAGFILTER, 2);
    Render_ClampStage0(1);
}

/* Render_SetBlend 0x4a3e40: the frame-buffer blend factors, not cached. */
void Render_SetBlend(uint32_t src, uint32_t dst)
{
    D3d_SetRenderState(D3DRS_SRCBLEND, src);
    D3d_SetRenderState(D3DRS_DESTBLEND, dst);
}
