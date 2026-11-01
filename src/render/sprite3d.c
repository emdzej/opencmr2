/* The camera-facing sprites (see sprite3d.h). */
#include "render/sprite3d.h"
#include "render/d3d7.h"
#include "render/rstate.h"
#include "render/twod.h"
#include "world/relight.h"
#include "fixmath.h"
#include <string.h>

enum { MAX = 800 };

/* 0x6c9e00 + 0x58 n: a queued sprite, the extents and the centre already floats */
typedef struct {
    float c[4][2];        /* the corners: right, up */
    uint16_t tex;         /* +0x30: the texture's index */
    float pos[3];         /* +0x34 */
    uint32_t colour;      /* +0x40: D3D ARGB */
    uint32_t flip;        /* +0x50 */
    uint16_t angle;       /* +0x54 */
} Queued;

static Queued q[MAX];
static int nq, nruns;                    /* DAT_006dd784, DAT_006dd788 */
static bool ready;                       /* DAT_006dd780 */
static struct { uint32_t n, tex; } runs[MAX];   /* 0x6c8500: count and texture per run */
static uint16_t idx[MAX * 6];            /* 0x6db200 */
static float verts[MAX * 4][12];         /* 0x6a2cf8: FVF 0x2d2, 48 bytes */

int Sprite3d_Count(void) { return nq; }

/* 0x4b1150: the queue emptied, the index list made (per sprite 0 3 2, 0 1 3) */
void Sprite3d_Init(void)
{
    nq = nruns = 0;
    for (int k = 0; k < MAX; k++) {
        uint16_t b = (uint16_t)(4 * k);
        uint16_t *p = idx + 6 * k;
        p[0] = b, p[1] = (uint16_t)(b + 3), p[2] = (uint16_t)(b + 2);
        p[3] = b, p[4] = (uint16_t)(b + 1), p[5] = (uint16_t)(b + 3);
    }
    ready = true;
}

/* 0x4b1500 */
void Sprite3d_Free(void)
{
    memset(q, 0, sizeof q);
    nq = nruns = 0;
    ready = false;
}

static uint32_t argb(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    return (uint32_t)a << 24 | (uint32_t)r << 16 | (uint32_t)g << 8 | b;
}

void Sprite3d_Add(const Sprite3d *s, const Tex *tex)
{
    if (!ready) return;   /* the original also sets a flag nothing reads (0x6dd78c) */
    if (!tex || !s || nq >= MAX) return;
    const float k = (float)(1.0 / 65536.0);
    Queued *e = &q[nq];
    e->c[0][0] = -((float)s->e[1] * k), e->c[0][1] = (float)s->e[0] * k;
    e->c[1][0] = -((float)s->e[1] * k), e->c[1][1] = (float)s->e[2] * k;
    e->c[2][0] = -((float)s->e[3] * k), e->c[2][1] = (float)s->e[0] * k;
    e->c[3][0] = -((float)s->e[3] * k), e->c[3][1] = (float)s->e[2] * k;
    e->tex = tex->index;
    for (int i = 0; i < 3; i++) e->pos[i] = (float)s->pos[i] * k;
    if (!(s->flags & 2)) e->colour = argb(s->rgba[0], s->rgba[1], s->rgba[2], s->rgba[3]);
    else {
        /* the light ramp's dark and full ends (bytes R, G, B), each times the colour, then mixed by
           the shade: (full (256 - shade) + dark shade) / 256 */
        uint32_t lo = Light_ByteRamp(0), hi = Light_ByteRamp(0x10000);
        uint8_t m[3];
        uint32_t t = s->shade, u = 0x100 - t;
        for (int i = 0; i < 3; i++) {
            uint32_t a = ((lo >> 8 * i & 0xff) * s->rgba[i]) >> 8;
            uint32_t b = ((hi >> 8 * i & 0xff) * s->rgba[i]) >> 8;
            m[i] = (uint8_t)((b * u + a * t) >> 8);
        }
        e->colour = argb(m[0], m[1], m[2], s->rgba[3]);
    }
    e->flip = s->flags & 1;
    e->angle = s->angle;
    if (nq == 0) {
        nruns = 1;
        runs[0].n = 1, runs[0].tex = e->tex;
    } else if (e->tex != runs[nruns - 1].tex) {
        runs[nruns].n = 1, runs[nruns].tex = e->tex;
        nruns++;
    } else runs[nruns - 1].n++;
    nq++;
}

void Sprite3d_Draw(const Obj *camera)
{
    if (!ready || !nq) return;
    float m[16];
    Mat_ToFloat(m, &camera->world);
    Twod_CountPrims((uint32_t)nq * 2);
    static const float uv[2][4][2] = {{{0, 1}, {0, 0}, {1, 1}, {1, 0}}, {{1, 1}, {1, 0}, {0, 1}, {0, 0}}};
    const uint32_t spec = 0xff000000u;
    for (int n = 0; n < nq; n++) {
        const Queued *e = &q[n];
        float r[3], u[3];
        if (!e->angle) {
            memcpy(r, m, sizeof r);
            memcpy(u, m + 4, sizeof u);
        } else {
            float c = (float)g_sin[(e->angle + 0x400) & 0xfff] * (float)(1.0 / 65536.0);
            float s = (float)g_sin[e->angle & 0xfff] * (float)(1.0 / 65536.0);
            for (int i = 0; i < 3; i++) r[i] = m[i] * c + m[4 + i] * s, u[i] = m[4 + i] * c - m[i] * s;
        }
        for (int k = 0; k < 4; k++) {
            float *v = verts[4 * n + k];
            for (int i = 0; i < 3; i++) v[i] = e->c[k][0] * r[i] + e->c[k][1] * u[i] + e->pos[i];
            memcpy(&v[6], &e->colour, 4);
            memcpy(&v[7], &spec, 4);
            v[8] = uv[e->flip ? 1 : 0][k][0];
            v[9] = uv[e->flip ? 1 : 0][k][1];
        }
    }
    Render_ZWrite(0);
    int first = 0;
    for (int i = 0; i < nruns; i++) {
        uint32_t t = runs[i].tex;
        Tex_Bind(0, t < TEX_MAX ? g_tex[t] : NULL);
        D3d_DrawIndexedPrimitive(4, 0x2d2, verts[4 * first], runs[i].n * 4, idx, runs[i].n * 6, 0);
        first += (int)runs[i].n;
    }
    Render_ZWrite(1);
    nq = nruns = 0;
    Tex_ForgetBlend();
    Render_ForgetBlend();
}
