/* The stage's light colours and the cells' vertex relighting (see relight.h). */
#include "world/relight.h"
#include "exe.h"
#include "fixmath.h"
#include "render/d3d7.h"
#include "render/dlight.h"
#include "render/light.h"
#include "render/mesh.h"
#include "world/section.h"
#include "world/tod.h"
#include "world/stage.h"
#include "world/tre.h"
#include <stdlib.h>
#include <string.h>

static uint8_t low[4];                     /* 0x6e01ec */
static int32_t base[3], slope[3], high[3]; /* 0x6e0118, 0x6e0348, 0x6e01a0 */
static uint8_t high_rgb[4];                /* 0x6dfdc4 */
static bool dirty;                         /* 0x6e0244 */
uint32_t g_shadow_rgb;
int32_t g_shadow_alpha;
static uint32_t ramp[50], shadow_ramp[50]; /* 0x6dfdc8, 0x6deacc (and the byte copy at 0x6e0b34) */
static uint32_t *cache_low, *cache_high;   /* 0x6e019c, 0x6dfd90 */

static int32_t iabs(int32_t v) { return v < 0 ? -v : v; }
static uint8_t hi8(int64_t v) { return (uint8_t)(v >> 32); }

/* 0x4b4910: the alpha from the contrast (the summed channel differences times 0x55, at most 1.0, times
   255; doubled with the flag, at most 255). The flag's colour is a sixth of the low colour (times 0x2aac,
   the product's high word); without it, each channel's excess over the smallest low channel. */
static void shadow_colour(bool from_low)
{
    int32_t d = iabs(base[0] - high[0]) + iabs(base[1] - high[1]) + iabs(base[2] - high[2]);
    int32_t a = fx_mul(d, 0x55);
    if (a > 0x10000) a = 0x10000;
    a = fx_mul(a, 0xff0000);
    uint8_t c[3];
    if (from_low) {
        a = fx_mul(a, 0x20000);
        if (a > 0xff0000) a = 0xff0000;
        for (int i = 0; i < 3; i++) c[i] = hi8((int64_t)base[i] * 0x2aac);
    } else {
        int32_t r = base[0], g = base[1], b = base[2];
        int m;
        if (r <= g && r <= b) m = 0;
        else if (r <= g && r < g) m = 2;   /* r > b */
        else if (g <= b) m = 1;
        else m = 2;
        for (int i = 0; i < 3; i++) c[i] = i == m ? 0 : (uint8_t)((uint32_t)(base[i] - base[m]) >> 16);
    }
    g_shadow_alpha = a;
    g_shadow_rgb = (g_shadow_rgb & 0xff000000u) | c[0] | (uint32_t)c[1] << 8 | (uint32_t)c[2] << 16;
}

void Light_SetLow(const uint8_t c[4], bool from_low)
{
    g_ambient_b = (uint32_t)c[3] << 24 | (uint32_t)c[0] << 16 | (uint32_t)c[1] << 8 | c[2];
    if (c[0] != low[0] || c[1] != low[1] || c[2] != low[2]) dirty = true;
    memcpy(low, c, 4);
    for (int i = 0; i < 3; i++) {
        base[i] = c[i] * 0x10000;
        slope[i] = high[i] - c[i] * 0x10000;
    }
    shadow_colour(from_low);
    D3d_SetRenderState(D3DRS_AMBIENT, g_ambient_b);
}

void Light_GetLow(uint8_t c[4]) { memcpy(c, low, 4); }
void Light_GetHigh(uint8_t c[4]) { memcpy(c, high_rgb, 4); }

void Light_SetHigh(const int32_t c[3], bool from_low)
{
    if (c[0] != high[0] || c[1] != high[1] || c[2] != high[2]) dirty = true;
    for (int i = 0; i < 3; i++) {
        high[i] = c[i];
        high_rgb[i] = (uint8_t)((uint32_t)c[i] >> 16);
        slope[i] = high[i] - base[i];
    }
    shadow_colour(from_low);
}

void Light_Defaults(void)
{
    static const uint8_t white[4] = {0xff, 0xff, 0xff, 0xff};
    static const int32_t full[3] = {0xff0000, 0xff0000, 0xff0000};
    /* the sun: white, from (50, 75, 75), under the world */
    static const int32_t sun[3] = {0x320000, 0x4b0000, 0x4b0000};
    static const uint16_t none[3];
    Dlight_Add(2, 0x10000, 0x10000, 0x10000, sun, none, &g_world);
    bool f = Tod_Rally() != 3;
    Light_SetLow(white, f);
    Light_SetHigh(full, f);
}

/* 0x4b3940: 50 steps of 0x539 (about 1/49) from the low colour towards the high one, each channel's
   whole part clamped to 0-255; the shadow ramp is the shadow colour with an alpha of the shadow alpha
   times the step (the product's high word). */
static void build_ramps(void)
{
    for (int i = 0; i < 50; i++) {
        int32_t t = i * 0x539;
        int v[3];
        for (int k = 0; k < 3; k++) {
            int32_t c = (fx_mul(slope[k], t) + base[k]) >> 16;
            v[k] = c < 0 ? 0 : c > 0xff ? 0xff : c;
        }
        ramp[i] = 0xff000000u | (uint32_t)v[0] << 16 | (uint32_t)v[1] << 8 | (uint32_t)v[2];
        uint8_t a = (uint8_t)((int64_t)g_shadow_alpha * t >> 32);
        shadow_ramp[i] = (uint32_t)a << 24 | (g_shadow_rgb & 0xff) << 16 | (g_shadow_rgb >> 8 & 0xff) << 8 | (g_shadow_rgb >> 16 & 0xff);
    }
}

static int ramp_index(int32_t v)
{
    int32_t i = (int32_t)((uint32_t)v * 0x31) >> 16;
    return i < 0 ? 0 : i > 0x31 ? 0x31 : i;
}

uint32_t Light_TerrainRamp(int32_t v)
{
    if (dirty) build_ramps(), dirty = false;
    return ramp[ramp_index(v)];
}

uint32_t Light_ByteRamp(int32_t v)
{
    if (dirty) build_ramps(), dirty = false;
    uint32_t c = ramp[ramp_index(v)];
    return 0xff000000u | (c & 0xff) << 16 | (c & 0xff00) | (c >> 16 & 0xff);
}

uint32_t Light_ShadowRamp(int32_t v)
{
    if (dirty) build_ramps(), dirty = false;
    return shadow_ramp[ramp_index(v)];
}

void Relight_Alloc(void)
{
    Relight_Free();
    cache_low = calloc(SEC_MAX, 4);
    cache_high = calloc(SEC_MAX, 4);
}

void Relight_Free(void)
{
    free(cache_low);
    free(cache_high);
    cache_low = cache_high = NULL;
}

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static void wr32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8), p[2] = (uint8_t)(v >> 16), p[3] = (uint8_t)(v >> 24); }
static uint32_t dw(const uint8_t c[4]) { return c[0] | (uint32_t)c[1] << 8 | (uint32_t)c[2] << 16 | (uint32_t)c[3] << 24; }

/* Each vertex's colour (+0x18) from the ramp for its intensity in the mesh's +0x34 array. */
static void per_vertex(const C3d *m, uint8_t *me, uint32_t (*f)(int32_t))
{
    uint8_t *v = (uint8_t *)C3d_Ref(m, me + 0x0c, m->verts);
    const uint8_t *c = C3d_Ref(m, me + 0x34, m->vx);
    uint32_t n = rd32(me + 0x10);
    for (uint32_t k = 0; v && c && k < n; k++) wr32(v + (size_t)k * C3D_VERTEX + 0x18, f((int32_t)rd32(c + 4 * k)));
}

/* Tre_LightFade 0x4b6ca0 needs the light object of 0x6dffa4[1] (a car's lights), not ported: none,
   so it reports no change. */
static bool light_fade(int cell) { (void)cell; return false; }

void Section_Relight(int cell)
{
    if (!cache_low) return;
    bool changed = false;
    uint8_t cl[4], ch[4];
    memcpy(cl, &cache_low[cell], 4);
    memcpy(ch, &cache_high[cell], 4);
    if (cl[0] != low[0] || cl[1] != low[1] || cl[2] != low[2] || ch[0] != high_rgb[0] || ch[1] != high_rgb[1] ||
        ch[2] != high_rgb[2]) {
        cache_low[cell] = dw(low);
        cache_high[cell] = dw(high_rgb);
        Section *s = &g_sec[cell];
        if (s->mesh) {
            per_vertex(&g_stage_mesh, s->mesh, Light_TerrainRamp);
            Mesh_UploadVerts(&g_stage_mesh, s->mesh);
        }
        /* scenery: one colour from the record's +0x9c, or per vertex with mesh flag 0x80 (a mesh shared by
           several records takes the last one's colour) */
        for (Scenery *sc = s->scenery; sc; sc = sc->next) {
            uint8_t *me = sc->mesh;
            if (!me) continue;
            if (!(rd32(me + 0x30) & 0x80)) Mesh_SetColour(sc->model, me, Light_TerrainRamp((int32_t)rd32(sc->rec + 0x9c)));
            else {
                per_vertex(sc->model, me, Light_TerrainRamp);
                Mesh_UploadVerts(sc->model, me);
            }
        }
        /* the cell's objects: FUN_004a3240, an empty function */
        if (!g_tre_have) return;
        if (g_tre_mesh[cell]) changed = true;
    }
    uint8_t *me = g_tre_mesh[cell];
    bool fade = g_tre_have && me && exe_u32(0x5210c0) && light_fade(cell);
    if (!(fade || changed) || !me) return;
    if (!exe_u32(0x5210c0)) {
        per_vertex(&g_tre, me, Light_ShadowRamp);
    } else {
        /* per tree: its vertices (item +0x28) scaled by its factor (+0x20) */
        uint8_t *nd = g_tre.data + 0x32 + (size_t)(uint16_t)g_tre_node[cell] * 0x14;
        uint8_t *v = g_tre.data + rd32(me + 0x0c);
        const uint8_t *c = g_tre.data + rd32(me + 0x34);
        uint32_t ni = (uint32_t)(nd[2] | nd[3] << 8), k = 0, nv = rd32(me + 0x10);
        for (uint32_t j = 0; j < ni; j++) {
            const uint8_t *item = g_tre.data + rd32(nd + 0x0c) + j * 0x30;
            int32_t f = (int32_t)rd32(item + 0x20);
            uint32_t n = rd32(item + 0x28);
            for (uint32_t q = 0; q < n && k < nv; q++, k++) {
                int32_t iv = (int32_t)rd32(c + 4 * k);
                wr32(v + (size_t)k * C3D_VERTEX + 0x18, Light_ShadowRamp(f == 0x10000 ? iv : fx_mul(f, iv)));
            }
        }
    }
    Mesh_UploadVerts(&g_tre, me);
}

/* Each cell's cached low red moved 10 away (so it differs), then relit. The original first passes the
   cell's objects to 0x4b50b0 (the shadows of sorted objects with mesh flag 0x800, not ported). */
void Light_RelightAll(void)
{
    if (!cache_low) return;
    for (int i = 0; i < g_nsec; i++) {
        uint8_t c[4];
        memcpy(c, &cache_low[i], 4);
        c[0] = (uint8_t)(low[0] < 0xf0 ? low[0] + 10 : low[0] - 10);
        memcpy(&cache_low[i], c, 4);
        Section_Relight(i);
    }
}
