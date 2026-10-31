/* Lighting modes (see light.h). */
#include "render/light.h"
#include "render/d3d7.h"

uint32_t g_ambient_a, g_ambient_b, g_lights_keep;
static uint32_t mode;   /* DAT_005210c4 */

/* 0: lit, ambient A, vertex colour on, light 1 only; 1: the same with light 2 only; 2: lit, ambient B,
   material colours only, light 0; 3: as 2 with the vertex colour as the diffuse; 4 and 5: unlit (vertex
   colours), no lights. Other values change nothing but are still recorded. */
void Light_SetMode(uint32_t m)
{
    if (m <= 5) {
        bool lit = m <= 3;
        D3d_SetRenderState(D3DRS_LIGHTING, lit);
        D3d_SetRenderState(D3DRS_AMBIENT, m == 2 || m == 3 ? g_ambient_b : g_ambient_a);
        D3d_SetRenderState(D3DRS_COLORVERTEX, m != 2);
        D3d_SetRenderState(D3DRS_DIFFUSEMATERIALSOURCE, m == 3);
        D3d_LightEnable(0, m == 2 || m == 3);
        D3d_LightEnable(1, m == 0);
        D3d_LightEnable(2, m == 1);
    }
    if (!g_lights_keep) {
        D3d_LightEnable(1, false);
        D3d_LightEnable(2, false);
    }
    mode = m;
}

uint32_t Light_Mode(void) { return mode; }
