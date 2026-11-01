/* The 2D queues (see twod.h). */
#include <math.h>
#include "render/twod.h"
#include "fixmath.h"
#include "render/d3d7.h"
#include "render/polyq.h"
#include "render/rstate.h"
#include <string.h>

enum { SPRITES = 1023, TRIS = 1024 };

typedef struct { float x, y, z, rhw; uint32_t diffuse, specular; float u, v; } TLVertex;   /* FVF 0x1c4 */

typedef struct {               /* the original's 0x38-byte record */
    Rect16 src, dst, uv2;      /* +0x00, +0x08, +0x10 */
    Tex *tex;                  /* +0x18 */
    uint8_t rgba[4];           /* +0x1c */
    int layer;                 /* +0x20 */
    int16_t angle;             /* +0x24: 4,096 units */
    int32_t centre[3];         /* +0x28 */
    uint32_t flip;             /* +0x34 */
} Sprite;

static Sprite sprite[4][SPRITES];              /* layers 1-4: 0x5f0858, 0x64c958, 0x5e2858, 0x63e958 */
static int nsprite[4];                         /* 0x65a95c-0x65a968 */
static TLVertex sprite_verts[4][SPRITES * 4];  /* kept between frames, as the original's arrays */
static uint32_t prims_2d;                      /* DAT_0059ce20 */

static bool tris_on;                           /* DAT_00816168 */
static TLVertex tri[4][TRIS * 3];              /* queue for layer "other", 2, 3, 4 */
static int ntri[4];

/* The original also sets DAT_0065a958, which Sprite_Add never checks. */
void Sprite_Init(void)
{
    memset(nsprite, 0, sizeof nsprite);
}

void Tri_Init(void)
{
    tris_on = true;
    memset(ntri, 0, sizeof ntri);
    Poly_Init();
}

static uint32_t argb(const uint8_t c[4]) { return (uint32_t)c[3] << 24 | (uint32_t)c[0] << 16 | (uint32_t)c[1] << 8 | c[2]; }

void Sprite_Add(const Rect16 *src, const Rect16 *dst, Tex *tex, int layer, int16_t angle_deg,
                const int32_t centre[3], const Rect16 *uv2, const uint8_t rgba[4], uint32_t flip)
{
    if (layer < 1 || layer > 4) return;
    int q = layer - 1;
    if (nsprite[q] > SPRITES - 1) return;
    Sprite *s = &sprite[q][nsprite[q]++];
    s->src = *src;
    s->dst = *dst;
    s->uv2 = uv2 ? *uv2 : (Rect16){0, 0, 0, 0};
    s->tex = tex;
    memcpy(s->rgba, rgba, 4);
    s->layer = layer;
    s->angle = (int16_t)((angle_deg * 0x1000) / 360);   /* the original's multiply-shift divide by 360 */
    if (centre) memcpy(s->centre, centre, sizeof s->centre);
    else memset(s->centre, 0, sizeof s->centre);
    s->flip = flip;
}

static void corner(TLVertex *v, int x, int y, uint32_t c, float u, float t, int16_t angle, const FxMat *m,
                   const FxVec *centre)
{
    v->x = (float)x;
    v->y = (float)y;
    v->z = 0.0f;
    v->rhw = 1.0f;
    v->diffuse = c;
    v->specular = 0xff000000u;
    v->u = u;
    v->v = t;
    if (angle) {   /* turned in whole pixels: the corner as integers through the 16.16 matrix */
        FxVec p = {x, y, 0}, o;
        Vec_RotateAbout(&o, &p, centre, m);
        v->x = (float)o.x;
        v->y = (float)o.y;
        v->z = (float)o.z;
    }
}

/* A sprite without a texture gets no vertices, but the draw loop still steps through every record: the
   sprites after it draw with the next sprite's vertices (and the last with what the array held). */
void Sprite_Flush(int layer)
{
    int q = layer >= 2 && layer <= 4 ? layer - 1 : 0;
    D3d_SetRenderState(D3DRS_CLIPPING, 0);
    int n = nsprite[q];
    nsprite[q] = 0;
    TLVertex *out = sprite_verts[q];
    for (int i = 0; i < n; i++) {
        const Sprite *s = &sprite[q][i];
        if (!s->tex) continue;
        float tw = (float)s->tex->width, th = (float)s->tex->height;
        float u0 = (float)s->src.x / tw, u1 = (float)(s->src.w + s->src.x) / tw;
        float v0 = (float)s->src.y / th, v1 = (float)(s->src.h + s->src.y) / th;
        /* the half-texel offset (the flag 0x663b1c that turns it off is never set) */
        u0 += 0.5f / tw;
        u1 += 0.5f / tw;
        v0 += 0.5f / th;
        v1 += 0.5f / th;
        if (s->flip & 5) {
            float t = u0;
            u0 = u1;
            u1 = t;
        }
        if (s->flip & 6) {
            float t = v0;
            v0 = v1;
            v1 = t;
        }
        uint32_t c = argb(s->rgba);
        FxVec centre = {s->centre[0], s->centre[1], s->centre[2]};
        if (centre.x == -1 && centre.y == -1 && centre.z == -1) {
            centre.x = s->dst.w / 2 + s->dst.x;
            centre.y = s->dst.h / 2 + s->dst.y;
        }
        FxMat m;
        if (s->angle) Mat_RotZ(&m, (uint32_t)(int32_t)s->angle);
        int x = s->dst.x, y = s->dst.y;
        corner(&out[0], x, y, c, u0, v0, s->angle, &m, &centre);
        corner(&out[1], x + s->dst.w, y, c, u1, v0, s->angle, &m, &centre);
        corner(&out[2], x, y + s->dst.h, c, u0, v1, s->angle, &m, &centre);
        corner(&out[3], x + s->dst.w, y + s->dst.h, c, u1, v1, s->angle, &m, &centre);
        out += 4;
    }
    D3d_SetTextureStageState(0, D3DTSS_MINFILTER, 1);
    D3d_SetTextureStageState(0, D3DTSS_MAGFILTER, 1);
    for (int i = 0; i < n; i++) {
        Tex_Bind(0, sprite[q][i].tex);
        D3d_DrawPrimitive(D3DPT_TRIANGLESTRIP, FVF_TL, &sprite_verts[q][i * 4], 4, 0);
        prims_2d += 2;
    }
    D3d_SetTextureStageState(0, D3DTSS_MINFILTER, 2);
    D3d_SetTextureStageState(0, D3DTSS_MAGFILTER, 2);
    D3d_SetRenderState(D3DRS_CLIPPING, 1);
    Tex_ForgetBlend();
    Render_ForgetBlend();
}

uint32_t Twod_Prims(void) { return prims_2d; }
void Twod_CountPrims(uint32_t n) { prims_2d += n; }
void Twod_ResetPrims(void) { prims_2d = 0; }

static int tri_queue(int layer) { return layer == 2 ? 1 : layer == 3 ? 2 : layer == 4 ? 3 : 0; }

/* Only x, y and the colour are written: z, rhw, specular and the coordinates keep what the arrays held
   (zero at start: rhw 0, and a specular alpha of 0, which is full fog if fog is on). The original's limit
   test leaves out the layer-3 queue's count, which can then run past its array; the port stops it at
   1,024 like the others. */
void Tri_Add(const int32_t a[2], const int32_t b[2], const int32_t c[2], const uint8_t rgba[4], int layer)
{
    if (!tris_on || ntri[3] >= TRIS || ntri[1] >= TRIS || ntri[0] >= TRIS) return;
    int q = tri_queue(layer);
    if (ntri[q] >= TRIS) return;   /* the port's bound for layer 3 */
    TLVertex *v = &tri[q][ntri[q] * 3];
    const int32_t *p[3] = {a, b, c};
    for (int k = 0; k < 3; k++) {
        v[k].x = (float)((double)p[k][0] * (1.0 / 65536.0));
        v[k].y = (float)((double)p[k][1] * (1.0 / 65536.0));
        v[k].diffuse = argb(rgba);
    }
    ntri[q]++;
}

void Tri_Flush(int layer)
{
    Render_SetBlend(5, 6);
    int q = tri_queue(layer);
    if (ntri[q]) {
        Tex_Bind(0, NULL);
        D3d_DrawPrimitive(D3DPT_TRIANGLELIST, FVF_TL, tri[q], (uint32_t)ntri[q] * 3, 0);
        prims_2d += (uint32_t)ntri[q];
        ntri[q] = 0;
    }
    Tex_ForgetBlend();
    Render_ForgetBlend();
}

void Rect_Fill(const int16_t r[4], const uint8_t rgba[4], int layer)
{
    int sw, sh;
    D3d_ScreenSize(&sw, &sh);
    int x = r[0], y = r[1], w = r[2], h = r[3];
    if (w <= 0 || h <= 0 || (x < 0 && w + x < 0) || (y < 0 && h + y < 0) || x >= sw || y >= sh) return;
    if (x < 0) w += x, x = 0;
    if (sw < w + x) w = sw - x - 1;
    if (y < 0) h += y, y = 0;
    if (sh < h + y) h = sw - y - 1;   /* (the width: the original's slip) */
    int32_t x0 = (int32_t)llrint(x * 65536.0), y0 = (int32_t)llrint(y * 65536.0);
    int32_t x1 = 0x8000 - (int32_t)llrint((w - 1 + x) * -65536.0), y1 = 0x8000 - (int32_t)llrint((h - 1 + y) * -65536.0);
    int32_t a[2] = {x0, y0}, b[2] = {x1, y0}, c[2] = {x0, y1}, d[2] = {x1, y1};
    Tri_Add(a, b, c, rgba, layer);
    Tri_Add(b, d, c, rgba, layer);
}

int Tri_Queued(void) { return ntri[0] + ntri[1] + ntri[2] + ntri[3]; }
