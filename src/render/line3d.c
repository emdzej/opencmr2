/* The 3D line queue (see line3d.h). */
#include "render/line3d.h"
#include "render/d3d7.h"
#include "render/tex.h"
#include <string.h>

typedef struct {
    int32_t a[3], b[3];
    uint8_t ca[4], cb[4];
} Line;

static Line lines[200];   /* 0x72d680 */
static uint32_t nlines;   /* DAT_00730fc8 */

void Line3d_Add(const int32_t a[3], const int32_t b[3], const uint8_t ca[4], const uint8_t cb[4])
{
    if (nlines >= 200) return;
    Line *l = &lines[nlines++];
    memcpy(l->a, a, sizeof l->a), memcpy(l->b, b, sizeof l->b);
    memcpy(l->ca, ca, 4), memcpy(l->cb, cb, 4);
}

/* No texture, blending source alpha over its inverse; each line drawn on its own as a two-vertex line
   list of model vertices (FVF 0x2d2, 0x30 bytes): the position and the diffuse colour (A, R, G, B
   from the bytes) set, the normal, specular and texture coordinates left as the stack had them (the
   port: zero) */
void Line3d_Flush(void)
{
    if (!nlines) return;
    Tex_Bind(0, NULL);
    D3d_SetRenderState(D3DRS_SRCBLEND, 5);
    D3d_SetRenderState(D3DRS_DESTBLEND, 6);
    for (uint32_t i = 0; i < nlines; i++) {
        const Line *l = &lines[i];
        uint8_t v[2][0x30];
        memset(v, 0, sizeof v);
        for (int k = 0; k < 2; k++) {
            const int32_t *p = k ? l->b : l->a;
            const uint8_t *c = k ? l->cb : l->ca;
            float f[3] = {(float)(p[0] * (1.0 / 65536.0)), (float)(p[1] * (1.0 / 65536.0)), (float)(p[2] * (1.0 / 65536.0))};
            memcpy(v[k], f, sizeof f);
            uint32_t argb = (uint32_t)c[3] << 24 | (uint32_t)c[0] << 16 | (uint32_t)c[1] << 8 | c[2];
            memcpy(v[k] + 0x18, &argb, 4);
        }
        D3d_DrawPrimitive(D3DPT_LINELIST, FVF_MODEL, v, 2, 0);
    }
    nlines = 0;
}
