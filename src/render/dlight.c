/* The Direct3D lights (see dlight.h). */
#include "render/dlight.h"
#include "fixmath.h"
#include "render/light.h"
#include "world/relight.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    D3DLIGHT7 l;
    uint32_t index;   /* +0x68 */
} Rec;

static Rec *slot[DLIGHT_MAX];   /* 0x6dffa4 */
static int count;               /* 0x6e0b48 */

static float fl(int32_t v) { return (float)v * (float)(1.0 / 65536.0); }

Obj *Dlight_Add(int type, int32_t r, int32_t g, int32_t b, const int32_t pos[3], const uint16_t angles[3],
                Obj *parent)
{
    int i = 0;
    while (i < DLIGHT_MAX && slot[i]) i++;
    if (i == DLIGHT_MAX) return NULL;
    Rec *x = calloc(1, sizeof *x);
    if (!x) return NULL;
    slot[i] = x;
    D3DLIGHT7 *l = &x->l;
    x->index = (uint32_t)i;
    l->diffuse = (D3DCOLORVALUE){fl(r), fl(g), fl(b), fl(1)};
    l->position = (D3DVECTOR){fl(pos[0]), fl(pos[1]), fl(pos[2])};
    l->direction = (D3DVECTOR){0, 0, 1};
    l->falloff = 1, l->attenuation0 = 1, l->theta = 0.5f, l->phi = 1;
    switch (type) {
    case 0:
        l->type = D3DLIGHT_POINT;
        l->ambient = (D3DCOLORVALUE){fl(r), fl(g), fl(b), fl(1)};
        l->range = 60;
        l->attenuation0 = 0, l->attenuation1 = 0, l->attenuation2 = 0.1f;
        break;
    case 1:
        l->type = D3DLIGHT_SPOT;
        l->ambient = (D3DCOLORVALUE){fl(r), fl(g), fl(b), fl(1)};
        l->range = 100;
        l->falloff = 1, l->theta = 0.2f, l->phi = 2;
        l->attenuation0 = 0, l->attenuation1 = 0, l->attenuation2 = 0;
        break;
    case 2: {
        l->type = D3DLIGHT_DIRECTIONAL;
        int32_t d[3] = {-pos[0], -pos[1], -pos[2]};
        Vec_Unit(d);
        l->direction = (D3DVECTOR){fl(d[0]), fl(d[1]), fl(d[2])};
        break;
    }
    case 3:
        l->type = 4;   /* D3DLIGHT_PARALLELPOINT */
        break;
    }
    D3d_SetLight((uint32_t)i, l);
    D3d_LightEnable((uint32_t)i, true);
    Obj *o = NULL;
    if (parent) {
        o = Obj_New(parent);   /* 0x4ac6f0 */
        if (o) {
            o->kind = 1, o->light = l, o->light_index = x->index;   /* 0x4ac260 */
            Obj_Place(o, pos, angles);
        }
    }
    count++;
    return o;
}

/* With the device (re)made (0x4a78a0): the lights set and on again, a white material (diffuse and
   ambient 1, no specular or emissive) and ambient B from the low colour, set. */
void Dlight_Restore(void)
{
    for (int i = 0; i < DLIGHT_MAX; i++)
        if (slot[i]) {
            D3d_SetLight((uint32_t)i, &slot[i]->l);
            D3d_LightEnable((uint32_t)i, true);
        }
    D3DMATERIAL7 m = {{1, 1, 1, 1}, {1, 1, 1, 1}, {0, 0, 0, 0}, {0, 0, 0, 0}, 0};
    D3d_SetMaterial(&m);
    uint8_t c[4];
    Light_GetLow(c);
    g_ambient_b = (uint32_t)c[3] << 24 | (uint32_t)c[0] << 16 | (uint32_t)c[1] << 8 | c[2];
    D3d_SetRenderState(D3DRS_AMBIENT, g_ambient_b);
}

void Dlight_SetAtten(Obj *o, int32_t a)
{
    if (!o || !o->light) return;
    o->light->attenuation0 = fl(a);
    D3d_SetLight(o->light_index, o->light);
}

/* Not found, the count still drops; the light stays switched on in the device. */
void Dlight_Free(D3DLIGHT7 *l)
{
    for (int i = 0; i < DLIGHT_MAX; i++)
        if (slot[i] && &slot[i]->l == l) {
            free(slot[i]);
            slot[i] = NULL;
            break;
        }
    count--;
}

void Dlight_FreeAll(void)
{
    for (int i = 0; i < DLIGHT_MAX; i++)
        if (slot[i]) Dlight_Free(&slot[i]->l);
}

int Dlight_Count(void) { return count; }
D3DLIGHT7 *Dlight_Get(int i) { return i >= 0 && i < DLIGHT_MAX && slot[i] ? &slot[i]->l : NULL; }
