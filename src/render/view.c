/* The projection and the fog (see view.h). */
#include "render/view.h"
#include "exe.h"
#include "render/d3d7.h"
#include "render/rstate.h"
#include <math.h>
#include <string.h>

static float xs_f, ys_f, far_f, near_f;   /* DAT_00520b8c, 0x520b90, 0x520b88, 0x520b84 (initialised data) */
static bool loaded;
static int32_t fixed[16];
static int32_t far_given;                 /* DAT_005207b0 */
static float fog_start, fog_end;   /* DAT_00816600, 0x8165f4 */

static float exe_f32(uint32_t va)
{
    uint32_t u = exe_u32(va);
    float f;
    memcpy(&f, &u, 4);
    return f;
}

static uint32_t f2u(float f)
{
    uint32_t u;
    memcpy(&u, &f, 4);
    return u;
}

static void once(void)
{
    if (loaded) return;
    xs_f = exe_f32(0x520b8c);
    ys_f = exe_f32(0x520b90);
    far_f = exe_f32(0x520b88);
    near_f = exe_f32(0x520b84);
    far_given = (int32_t)exe_u32(0x5207b0);
    loaded = true;
}

/* 16.16 to float: the game multiplies by the double 2^-16 (0x511310) */
static float fx(int32_t v) { return (float)v * (float)(1.0 / 65536.0); }

void Proj_Set(int32_t xs, int32_t ys, int32_t zfar, int32_t znear)
{
    once();
    if (xs) xs_f = fx(xs);
    if (ys) ys_f = fx(ys);
    if (zfar) far_f = fx(zfar);
    if (znear) near_f = fx(znear);
    float k = exe_f32(0x520b80);
    D3DMATRIX m;
    memset(&m, 0, sizeof m);
    m.m[0][0] = xs_f * k;
    m.m[1][1] = k * ys_f;
    m.m[2][2] = far_f / (far_f - near_f);
    m.m[2][3] = 1.0f;
    m.m[3][2] = -(near_f * far_f) / (far_f - near_f);
    /* the fixed-point copy: rounded times 65536 (0x5112e0), with 0x10000 in m23 */
    memset(fixed, 0, sizeof fixed);
    fixed[0] = (int32_t)lrintf(m.m[0][0] * 65536.0f);
    fixed[5] = (int32_t)lrintf(m.m[1][1] * 65536.0f);
    fixed[10] = (int32_t)lrintf(m.m[2][2] * 65536.0f);
    fixed[11] = 0x10000;
    fixed[14] = (int32_t)lrintf(m.m[3][2] * 65536.0f);
    D3d_SetTransform(D3DTS_PROJECTION, &m);
    far_given = zfar;
}

const int32_t *Proj_Fixed(void) { return fixed; }
int32_t Proj_Far(void) { return far_given; }

/* The colour bytes are R, G, B from the low byte up, made opaque. With vertex fog, the linear vertex
   mode is chosen at once (the distances are set by Fog_SetLinear). */
void Fog_Set(int32_t start, int32_t end, int32_t start2, int32_t end2, uint32_t rgb)
{
    fog_start = fx(start);
    fog_end = fx(end);
    /* start2 and end2 are stored too (0x8165fc, 0x8165f8), but nothing reads them */
    D3d_SetRenderState(D3DRS_FOGCOLOR, 0xff000000u | (rgb & 0xff) << 16 | (rgb >> 8 & 0xff) << 8 | (rgb >> 16 & 0xff));
    if (g_caps.fog_vertex) {
        D3d_SetRenderState(D3DRS_FOGTABLEMODE, 0);
        D3d_SetRenderState(D3DRS_FOGVERTEXMODE, 3);
    }
}

void Fog_SetLinear(void)
{
    if (!g_caps.fog_vertex) return;
    D3d_SetRenderState(D3DRS_FOGTABLEMODE, 0);
    D3d_SetRenderState(D3DRS_FOGVERTEXMODE, 3);
    D3d_SetRenderState(D3DRS_FOGSTART, f2u(fog_start));
    D3d_SetRenderState(D3DRS_FOGEND, f2u(fog_end));
}

/* Off when the card has table or vertex fog (the original leaves the states alone otherwise). */
void Fog_Off(void)
{
    if (!g_caps.fog_table && !g_caps.fog_vertex) return;
    D3d_SetRenderState(D3DRS_FOGTABLEMODE, 0);
    D3d_SetRenderState(D3DRS_FOGVERTEXMODE, 0);
}
